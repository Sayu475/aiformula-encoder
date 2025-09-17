# include "Arduino.h"
# include "qei.hpp"
# include "esp_intr_alloc.h" 

// 1つ目のエンコーダ (既存)
# define ENCDR_A D4
# define ENCDR_B D5
// 2つ目のエンコーダを D2/D3 に接続
# define ENCDR2_A D2
# define ENCDR2_B D3

# define PI 3.1415926535897932384626433832795

// 左(またはエンコーダ1)
volatile int32_t count_total_1 = 0;
int16_t count_now_1 = 0;
float rad_1 = 0.0;
float prev_rad_1 = 0.0;
float angular_velocity_1 = 0.0; // rad/s

// LPF 用の状態（エンコーダ1）
float lpf_omega_1 = 0.0;
bool lpf_init_1 = false;

// 右(またはエンコーダ2)
volatile int32_t count_total_2 = 0;
int16_t count_now_2 = 0;
float rad_2 = 0.0;
float prev_rad_2 = 0.0;
float angular_velocity_2 = 0.0; // rad/s

// LPF 用の状態（エンコーダ2）
float lpf_omega_2 = 0.0;
bool lpf_init_2 = false;

// LPF カットオフ周波数(Hz) — ランタイムで変更可
float lpf_cutoff_hz = 15.0;

unsigned long prev_time = 0;

// スパイク検出／クランプ用パラメータ
float spike_threshold_mult = 3.0; // min_omega の何倍をスパイクとみなすか

// 停止判定（ゼロロック）
int zero_count_threshold = 2; // 閾値カウント数（サンプル数）で強制ゼロ化
int zero_count_1 = 0;
int zero_count_2 = 0;

// メディアン用バッファ（窓幅3）
float raw_buf_1[3] = {0.0, 0.0, 0.0};
int raw_idx_1 = 0;
float raw_buf_2[3] = {0.0, 0.0, 0.0};
int raw_idx_2 = 0;

TaskHandle_t thp[1];

void Core0a(void *args);

// ヘルパ: 3要素のメディアン
static float median3(float a, float b, float c){
    // 簡易な比較ベースの実装
    if (a > b) { float t = a; a = b; b = t; }
    if (b > c) { float t = b; b = c; c = t; }
    if (a > b) { float t = a; a = b; b = t; }
    return b;
}

// シリアルコマンドの簡易パーサ（例: "SET CUTOFF 10" / "SET SPIKE 2.5" / "SET ZCNT 3"）
static void handleSerialCommands(){
    if (!Serial.available()) return;
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) return;
    line.toUpperCase();
    // トークン分割
    int sp1 = line.indexOf(' ');
    if (sp1 < 0) return;
    String cmd = line.substring(0, sp1);
    String rest = line.substring(sp1 + 1);

    if (cmd == "SET"){
        int sp2 = rest.indexOf(' ');
        if (sp2 < 0) return;
        String key = rest.substring(0, sp2);
        String val = rest.substring(sp2 + 1);
        if (key == "CUTOFF"){
            float v = val.toFloat();
            if (v > 0.1) {
                lpf_cutoff_hz = v;
                Serial.println("CUTOFF=" + String(lpf_cutoff_hz));
            }
        } else if (key == "SPIKE"){
            float v = val.toFloat();
            if (v > 0.0) {
                spike_threshold_mult = v;
                Serial.println("SPIKE_MULT=" + String(spike_threshold_mult));
            }
        } else if (key == "ZCNT"){
            int v = val.toInt();
            if (v >= 1 && v <= 20){
                zero_count_threshold = v;
                Serial.println("ZCNT=" + String(zero_count_threshold));
            }
        }
    }
}

void setup()
{
    Serial.begin(115200);

    // PCNT ユニットをそれぞれ設定（qei_setup_x4 の引数はライブラリに依存）
    qei_setup_x4(PCNT_UNIT_2, ENCDR_A, ENCDR_B); // エンコーダ1 (D4/D5)
    qei_setup_x4(PCNT_UNIT_3, ENCDR2_A, ENCDR2_B); // エンコーダ2 (D2/D3)

    prev_time = millis();
    xTaskCreatePinnedToCore(Core0a,"Core0a", 4096, NULL, 1, &thp[0], 0);
}

const int ppr = 512;  // AMT102-V の設定に応じて変更

void loop()
{    
    unsigned long now = millis();
    unsigned long dt = now - prev_time;  // 経過時間[ms]

    // シリアルコマンド処理（ランタイムでパラメータ調整）
    handleSerialCommands();

    // 角度計算（それぞれ）
    rad_1 = (count_total_1 * 2 * PI) / (ppr * 4);  // 4逓倍の場合
    rad_2 = (count_total_2 * 2 * PI) / (ppr * 4);

    float rps_1 = angular_velocity_1 / (2 * PI);  // 回転数/秒
    float rpm_1 = rps_1 * 60;                     // 回転数/分

    float rps_2 = angular_velocity_2 / (2 * PI);
    float rpm_2 = rps_2 * 60;

    // 角速度の計算(改良: スパイククランプ + メディアン(3) + ゼロロック)
    if (dt > 0) {
        float dt_s = dt / 1000.0; // 秒
        // 1パルス当たりの角度 (rad)。ppr と 4逓倍に基づく。
        float tick_rad = (2 * PI) / (ppr * 4);
        // 測定間隔 dt_s における最小検出角速度 (rad/s)
        float min_omega = tick_rad / dt_s;
        // スパイク閾値
        float spike_thresh = spike_threshold_mult * min_omega;

        // --- エンコーダ1 ---
        float raw_omega_1 = (rad_1 - prev_rad_1) / dt_s;  // rad/s

        // デッドバンド: 1パルス未満はゼロ候補
        if (raw_omega_1 > -min_omega && raw_omega_1 < min_omega) {
            raw_omega_1 = 0.0;
        }

        // メディアン3に入れる
        raw_buf_1[raw_idx_1 % 3] = raw_omega_1;
        raw_idx_1++;
        float med_1 = median3(raw_buf_1[0], raw_buf_1[1], raw_buf_1[2]);

        // スパイククランプ: 前回のフィルタ出力との差が大きければクランプ
        float prev_f_1 = lpf_omega_1; // 前回フィルタ値
        if (!lpf_init_1) prev_f_1 = med_1; // 初期化時は差を見ない
        float diff1 = med_1 - prev_f_1;
        if (fabs(diff1) > spike_thresh) {
            // クランプして突発値を抑える（ほとんど遅延を生まない）
            if (diff1 > 0) med_1 = prev_f_1 + spike_thresh;
            else med_1 = prev_f_1 - spike_thresh;
        }

        // ゼロロック: 小さな値が連続したら即座に0に
        if (fabs(med_1) < min_omega) {
            zero_count_1++;
            if (zero_count_1 >= zero_count_threshold) {
                lpf_omega_1 = 0.0;
                angular_velocity_1 = 0.0;
                lpf_init_1 = true;
            }
        } else {
            zero_count_1 = 0;
            // LPF (1次 IIR)
            float tau = 1.0 / (2.0 * PI * lpf_cutoff_hz);
            float alpha = dt_s / (tau + dt_s); // 0..1
            if (!lpf_init_1) {
                lpf_omega_1 = med_1;
                lpf_init_1 = true;
            } else {
                lpf_omega_1 += alpha * (med_1 - lpf_omega_1);
            }
            angular_velocity_1 = lpf_omega_1;
        }
        prev_rad_1 = rad_1;

        // --- エンコーダ2 ---
        float raw_omega_2 = (rad_2 - prev_rad_2) / dt_s;
        if (raw_omega_2 > -min_omega && raw_omega_2 < min_omega) {
            raw_omega_2 = 0.0;
        }
        raw_buf_2[raw_idx_2 % 3] = raw_omega_2;
        raw_idx_2++;
        float med_2 = median3(raw_buf_2[0], raw_buf_2[1], raw_buf_2[2]);

        float prev_f_2 = lpf_omega_2;
        if (!lpf_init_2) prev_f_2 = med_2;
        float diff2 = med_2 - prev_f_2;
        if (fabs(diff2) > spike_thresh) {
            if (diff2 > 0) med_2 = prev_f_2 + spike_thresh;
            else med_2 = prev_f_2 - spike_thresh;
        }

        if (fabs(med_2) < min_omega) {
            zero_count_2++;
            if (zero_count_2 >= zero_count_threshold) {
                lpf_omega_2 = 0.0;
                angular_velocity_2 = 0.0;
                lpf_init_2 = true;
            }
        } else {
            zero_count_2 = 0;
            float tau = 1.0 / (2.0 * PI * lpf_cutoff_hz);
            float alpha = dt_s / (tau + dt_s);
            if (!lpf_init_2) {
                lpf_omega_2 = med_2;
                lpf_init_2 = true;
            } else {
                lpf_omega_2 += alpha * (med_2 - lpf_omega_2);
            }
            angular_velocity_2 = lpf_omega_2;
        }
        prev_rad_2 = rad_2;

        prev_time = now;
    }
    //

    Serial.println(">E1_count_total: " + String(count_total_1));
    Serial.println(">E1_rad: " + String(rad_1));
    Serial.println(">E1_ω(rad/s): " + String(angular_velocity_1));
    Serial.println(">E1_RPS: " + String(rps_1));
    Serial.println(">E1_RPM: " + String(rpm_1));
    Serial.println();

    Serial.println(">E2_count_total: " + String(count_total_2));
    Serial.println(">E2_rad: " + String(rad_2));
    Serial.println(">E2_ω(rad/s): " + String(angular_velocity_2));
    Serial.println(">E2_RPS: " + String(rps_2));
    Serial.println(">E2_RPM: " + String(rpm_2));
    Serial.println();

    delay(100);  // 測定間隔 100ms
}

void Core0a(void *args) 
{
    while (1){
        // 一時変数に取得してから volatile 変数へ代入する
        int16_t tmp1 = 0;
        int16_t tmp2 = 0;

        // エンコーダ1 を取得
        pcnt_get_counter_value(PCNT_UNIT_2, (int16_t*)&tmp1);
        count_now_1 = tmp1;
        count_total_1 += count_now_1;
        pcnt_counter_clear(PCNT_UNIT_2);

        // エンコーダ2 を取得
        pcnt_get_counter_value(PCNT_UNIT_3, (int16_t*)&tmp2);
        count_now_2 = tmp2;
        count_total_2 += count_now_2;
        pcnt_counter_clear(PCNT_UNIT_3);

        delay(1);
    }
}