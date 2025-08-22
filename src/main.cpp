# include "Arduino.h"
# include "qei.hpp"
# include "esp_intr_alloc.h" 

# define ENCDR_A D4
# define ENCDR_B D5
# define PI 3.1415926535897932384626433832795

int16_t count_total;
float rad = 0.0;
int16_t count_now = 0;

float prev_rad = 0.0;
float angular_velocity = 0.0; // rad/s
unsigned long prev_time = 0;



TaskHandle_t thp[1];

void Core0a(void *args);

void setup()
{
	Serial.begin(115200);
	qei_setup_x4(PCNT_UNIT_2, ENCDR_A, ENCDR_B); //LEFT
	xTaskCreatePinnedToCore(Core0a,"Core0a", 4096, NULL, 1, &thp[0], 0);
}

const int ppr = 512;  // AMT102-V の設定に応じて変更

void loop()
{	
	unsigned long now = millis();
	unsigned long dt = now - prev_time;  // 経過時間[ms]

	// 角度計算
	rad = (count_total * 2 * PI) / (ppr * 4);  // 4逓倍の場合
	float rps = angular_velocity / (2 * PI);  // 回転数/秒
	float rpm = rps * 60;                     // 回転数/分

	// 角速度の計算
	if (dt > 0) {
		angular_velocity = (rad - prev_rad) / (dt / 1000.0);  // rad/s
		prev_rad = rad;
		prev_time = now;
	}

	Serial.println(">count_total: " + String(count_total));
	Serial.println(">rad: " + String(rad));
	Serial.println(">ω(rad/s): " + String(angular_velocity));
	Serial.println(">RPS: " + String(rps));
	Serial.println(">RPM: " + String(rpm));
	Serial.println();

	delay(100);  // 測定間隔 100ms
}


/*
void loop()
{	
	rad = (count_total * 2 * PI) / (ppr * 4);  // 4逓倍なら ppr*4
	
	//rad = (count_total / 2.0) * 2 * PI;
	
	Serial.println(">count_total: " + String(count_total));
	Serial.println(">rad: " + String(rad));
	Serial.println(">count_now: " + String(count_now));
	Serial.println();

	delay(100);
}
*/

void Core0a(void *args) {
	while (1){
		pcnt_get_counter_value(PCNT_UNIT_2, &count_now);
		count_total += count_now;
		pcnt_counter_clear(PCNT_UNIT_2);
		delay(1);
	}
}