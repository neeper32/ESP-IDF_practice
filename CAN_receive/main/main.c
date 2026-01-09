#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c.h" 
#include "driver/gpio.h"
#include "driver/twai.h" // 여기가 핵심: can.h 대신 twai.h 사용

// 로그 태그
static const char *TAG = "TWAI_CAN_RTC";

// CAN모듈 핀 설정
#define TX_GPIO_NUM     GPIO_NUM_2
#define RX_GPIO_NUM     GPIO_NUM_1

// --- [설정] 핀 및 I2C 주소 ---
#define I2C_MASTER_SDA_IO           18    // SDA 핀 (CAN과 겹치지 않게 주의!)
#define I2C_MASTER_SCL_IO           17    // SCL 핀
#define I2C_MASTER_NUM              0     // I2C 포트 번호 (0 또는 1)
#define I2C_MASTER_FREQ_HZ          100000 // 통신 속도 (100kHz)
#define DS3231_ADDR                 0x68  // DS3231의 I2C 주소

// --- [유틸리티] BCD 변환 함수 ---
// RTC는 데이터를 10진수가 아닌 BCD(Binary Coded Decimal) 포맷으로 저장합니다.
// 예: 45초 -> 0x45 (16진수처럼 보이지만 각 자리가 10진수 숫자)
uint8_t decToBcd(int val) {
    return (uint8_t)((val / 10 * 16) + (val % 10));
}

int bcdToDec(uint8_t val) {
    return (int)((val / 16 * 10) + (val % 16));
}

//함수 불러오기
int get_month_number(const char *m);
void set_time_to_compile();
unsigned long long convert_to_timestamp(int y, int m, int d, int h, int min, int s);
void set_time_smart();
void i2c_master_init();
void set_time(int year, int month, int day, int hour, int min, int sec);
void get_time(int *year, int *month, int *day, int *hour, int *min, int *sec);


void app_main(void)
{
    //I2C 초기화
    i2c_master_init();
    ESP_LOGI(TAG, "I2C Initialized");

    // 2. 시간 설정 (컴파일 시간을 받아서 저장/ 기존시간이 더 최신이면 건너뜀)
    set_time_smart();

    //3. 시간 저장을 위한 변수 선언
    int year, month, day, hour, min, sec;

    // 1. 설정 구조체 초기화 (TWAI 접두어 사용)
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(TX_GPIO_NUM, RX_GPIO_NUM, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS(); // 속도 500kbps
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL(); // 모든 ID 수신

    // 2. 드라이버 설치
    if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
        ESP_LOGI(TAG, "Driver installed");
    } else {
        ESP_LOGE(TAG, "Failed to install driver");
        return;
    }

    // 3. 드라이버 시작
    if (twai_start() == ESP_OK) {
        ESP_LOGI(TAG, "Driver started");
    } else {
        ESP_LOGE(TAG, "Failed to start driver");
        return;
    }

    while (1) {
        //===========================================
        // [수신] 메시지 받기 (큐에 있는 것 모두 처리)
        // ==========================================
        twai_message_t rx_msg;
        
        // while문을 써서 쌓여있는 메시지를 빠르게 다 읽어옵니다.
        while (twai_receive(&rx_msg, pdMS_TO_TICKS(10)) == ESP_OK) {
            get_time(&year, &month, &day, &hour, &min, &sec); //시간 갱신

            // 2. ID에 따라 데이터 해석 및 출력
            switch (rx_msg.identifier) {
                
                // [CASE A] 버튼 (0x100)
                case 0x100:
                    printf("--------------------------------------------------\n");
                    printf("[%02d:%02d:%02d] 🔘 EVENT: Button Clicked!\n", hour, min, sec);
                    printf("--------------------------------------------------\n");
                    break;

                // [CASE B] 온습도 (0x200)
                case 0x200:
                    if (rx_msg.data_length_code >= 2) {
                        int temp = rx_msg.data[0];
                        int hum = rx_msg.data[1];
                        // 한 줄로 깔끔하게 출력
                        printf("[%02d:%02d:%02d] 🌡️ DHT11 | Temp: %2d°C  Hum: %2d%%\n", hour, min, sec, temp, hum);
                    }
                    break;

                // [CASE C] MPU6500 가속도 (0x300)
                case 0x300:
                    if (rx_msg.data_length_code == 6) {
                        // 1. 데이터 합치기
                        int16_t raw_ax = (int16_t)((rx_msg.data[0] << 8) | rx_msg.data[1]);
                        int16_t raw_ay = (int16_t)((rx_msg.data[2] << 8) | rx_msg.data[3]);
                        int16_t raw_az = (int16_t)((rx_msg.data[4] << 8) | rx_msg.data[5]);

                        // 2. 사람이 보기 편하게 변환 (나누기 16384)
                        float ax_g = raw_ax / 16384.0;
                        float ay_g = raw_ay / 16384.0;
                        float az_g = raw_az / 16384.0;

                        // 3. 소수점 2자리까지 출력
                        printf("[%02d:%02d:%02d] 🚀 Accel | X: %.2f g  Y: %.2f g  Z: %.2f g\n", 
                            hour, min, sec, ax_g, ay_g, az_g);
                    }
                    break;

                default:
                    // 알 수 없는 ID가 들어왔을 때
                    printf("[%02d:%02d:%02d] UNKNOWN ID: 0x%lx Len: %d\n", hour, min, sec, rx_msg.identifier, rx_msg.data_length_code);
                    break;
            }
        }

        // 0.1초 대기
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// 월(Month) 문자열을 숫자로 변환하는 도우미 함수
int get_month_number(const char *m) {
    if (strncmp(m, "Jan", 3) == 0) return 1;
    if (strncmp(m, "Feb", 3) == 0) return 2;
    if (strncmp(m, "Mar", 3) == 0) return 3;
    if (strncmp(m, "Apr", 3) == 0) return 4;
    if (strncmp(m, "May", 3) == 0) return 5;
    if (strncmp(m, "Jun", 3) == 0) return 6;
    if (strncmp(m, "Jul", 3) == 0) return 7;
    if (strncmp(m, "Aug", 3) == 0) return 8;
    if (strncmp(m, "Sep", 3) == 0) return 9;
    if (strncmp(m, "Oct", 3) == 0) return 10;
    if (strncmp(m, "Nov", 3) == 0) return 11;
    if (strncmp(m, "Dec", 3) == 0) return 12;
    return 0;
}

void set_time_to_compile() {
    char s_month[5];
    int year, month, day, hour, min, sec;

    // 컴파일 시점의 날짜와 시간 가져오기 (__DATE__, __TIME__은 C언어 표준 매크로)
    // __DATE__ 예: "Jan 06 2026"
    // __TIME__ 예: "22:10:00"
    sscanf(__DATE__, "%s %d %d", s_month, &day, &year);
    sscanf(__TIME__, "%d:%d:%d", &hour, &min, &sec);

    month = get_month_number(s_month);

    ESP_LOGI(TAG, "Setting time to Compile Time: %04d-%02d-%02d %02d:%02d:%02d", year, month, day, hour, min, sec);
    
    // 기존에 만드신 set_time 함수 호출
    set_time(year, month, day, hour, min, sec);
}
// [도우미 함수] 날짜/시간을 YYYYMMDDHHMMSS 형태의 큰 숫자로 변환 (비교용)
// 예: 2026년 1월 6일 22시 30분 00초 -> 20260106223000
unsigned long long convert_to_timestamp(int y, int m, int d, int h, int min, int s) {
    unsigned long long timestamp = 0;
    timestamp += (unsigned long long)y * 10000000000ULL;
    timestamp += (unsigned long long)m * 100000000ULL;
    timestamp += (unsigned long long)d * 1000000ULL;
    timestamp += (unsigned long long)h * 10000ULL;
    timestamp += (unsigned long long)min * 100ULL;
    timestamp += (unsigned long long)s;
    return timestamp;
}

// [핵심 함수] 스마트 시간 설정
void set_time_smart() {
    int r_year, r_month, r_day, r_hour, r_min, r_sec;
    int c_year, c_month, c_day, c_hour, c_min, c_sec;
    char s_month[5];

    // 1. 현재 RTC 시간 읽기
    get_time(&r_year, &r_month, &r_day, &r_hour, &r_min, &r_sec);

    // 2. 컴파일 시간 파싱 (__DATE__, __TIME__)
    sscanf(__DATE__, "%s %d %d", s_month, &c_day, &c_year);
    sscanf(__TIME__, "%d:%d:%d", &c_hour, &c_min, &c_sec);
    c_month = get_month_number(s_month);

    // 3. 두 시간을 비교 가능한 숫자로 변환
    unsigned long long rtc_time_val = convert_to_timestamp(r_year, r_month, r_day, r_hour, r_min, r_sec);
    unsigned long long compile_time_val = convert_to_timestamp(c_year, c_month, c_day, c_hour, c_min, c_sec);

    ESP_LOGI(TAG, "RTC Time: %llu / Compile Time: %llu", rtc_time_val, compile_time_val);

    // 4. 판단: 컴파일 시간이 RTC 시간보다 미래라면 (새로 굽거나, RTC가 초기화된 경우) -> 업데이트
    //    약간의 오차(컴파일 후 업로드까지 걸리는 시간, 약 1~2분)를 고려해도 컴파일 시간이 더 큽니다.
    //    또는 RTC 연도가 2000년(배터리 방전 등)이면 무조건 업데이트
    if (compile_time_val > rtc_time_val || r_year < 2024) {
        ESP_LOGW(TAG, "Updating RTC time to Compile Time...");
        set_time(c_year, c_month, c_day, c_hour, c_min, c_sec);
    } else {
        ESP_LOGI(TAG, "RTC time is up-to-date. Skipping update.");
    }
}

// --- [I2C 초기화] ---
void i2c_master_init() {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE, // 내부 풀업 저항 사용
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    i2c_param_config(I2C_MASTER_NUM, &conf);
    i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
}

// --- [기능] 시간 설정하기 ---
void set_time(int year, int month, int day, int hour, int min, int sec) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS3231_ADDR << 1) | I2C_MASTER_WRITE, true);
    
    i2c_master_write_byte(cmd, 0x00, true); // 레지스터 시작 주소 (0x00: 초)
    
    // 순서대로 쓰기: 초 -> 분 -> 시 -> 요일(무시) -> 일 -> 월 -> 년
    i2c_master_write_byte(cmd, decToBcd(sec), true);
    i2c_master_write_byte(cmd, decToBcd(min), true);
    i2c_master_write_byte(cmd, decToBcd(hour), true);
    i2c_master_write_byte(cmd, 1, true);            // 요일 (1~7, 여기선 임의로 1)
    i2c_master_write_byte(cmd, decToBcd(day), true);
    i2c_master_write_byte(cmd, decToBcd(month), true);
    i2c_master_write_byte(cmd, decToBcd(year - 2000), true); // 2024년 -> 24만 저장

    i2c_master_stop(cmd);
    i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    
    ESP_LOGI(TAG, "Time Set Completed!");
}

// --- [기능] 시간 읽어오기 ---
void get_time(int *year, int *month, int *day, int *hour, int *min, int *sec) {
    uint8_t data[7];

    // 1. 읽기 위치 설정
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS3231_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, 0x00, true);
    i2c_master_stop(cmd);
    i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);

    // 2. 데이터 읽기
    cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (DS3231_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, data, 7, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);

    // 3. 값 변환하여 포인터에 저장 (여기가 핵심!)
    *sec = bcdToDec(data[0]);
    *min = bcdToDec(data[1]);
    *hour = bcdToDec(data[2] & 0x3F);
    // data[3]은 요일이므로 패스
    *day = bcdToDec(data[4]);
    *month = bcdToDec(data[5]);
    *year = bcdToDec(data[6]) + 2000;
}