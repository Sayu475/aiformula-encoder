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

// 右(またはエンコーダ2)
volatile int32_t count_total_2 = 0;
int16_t count_now_2 = 0;
float rad_2 = 0.0;
float prev_rad_2 = 0.0;
float angular_velocity_2 = 0.0; // rad/s

unsigned long prev_time = 0;

TaskHandle_t thp[1];

void Core0a(void *args);

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

    // 角度計算（それぞれ）
    rad_1 = (count_total_1 * 2 * PI) / (ppr * 4);  // 4逓倍の場合
    rad_2 = (count_total_2 * 2 * PI) / (ppr * 4);

    float rps_1 = angular_velocity_1 / (2 * PI);  // 回転数/秒
    float rpm_1 = rps_1 * 60;                     // 回転数/分

    float rps_2 = angular_velocity_2 / (2 * PI);
    float rpm_2 = rps_2 * 60;

    // 角速度の計算(変更点)
    if (dt > 0) {
        float dt_s = dt / 1000.0; // 秒
        // 1パルス当たりの角度 (rad)。ppr と 4逓倍に基づく。
        float tick_rad = (2 * PI) / (ppr * 4);
        // 測定間隔 dt_s における最小検出角速度 (rad/s)
        float min_omega = tick_rad / dt_s;

        angular_velocity_1 = (rad_1 - prev_rad_1) / dt_s;  // rad/s
        // エンコーダの分解能より小さい変化はノイズとみなしてゼロにする
        if (angular_velocity_1 > -min_omega && angular_velocity_1 < min_omega) {
            angular_velocity_1 = 0.0;
        }
        prev_rad_1 = rad_1;

        angular_velocity_2 = (rad_2 - prev_rad_2) / dt_s;
        if (angular_velocity_2 > -min_omega && angular_velocity_2 < min_omega) {
            angular_velocity_2 = 0.0;
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