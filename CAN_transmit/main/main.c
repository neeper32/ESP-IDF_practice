#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/twai.h" // can.h 대신 twai.h 사용
#include "driver/i2c.h" //i2c 헤더
#include "rom/ets_sys.h" // 정밀 딜레이(ets_delay_us) 사용을 위해 필수

static const char *TAG = "CAN_Transmit";    // 로그 태그

// 핀 설정
#define TX_GPIO_NUM     GPIO_NUM_2  //CAN TX
#define RX_GPIO_NUM     GPIO_NUM_1  //CAN RX
#define BUTTON_GPIO     GPIO_NUM_3  //버튼 입력 핀
#define DHT11_PIN       GPIO_NUM_4  //DHT11 입력 핀
#define I2C_MASTER_SCL_IO   5  //가속도 센서 SCL 핀 번호
#define I2C_MASTER_SDA_IO   6  //가속도 센서 SDA 핀 번호

//[MPU6500-I2C설정
#define I2C_MASTER_NUM              0     // I2C 포트 0번 사용
#define I2C_MASTER_FREQ_HZ          400000 // 속도 400kHz
#define MPU6500_ADDR                0x68  // MPU6500 주소 (AD0핀이 GND일 때)
#define MPU6500_PWR_MGMT_1          0x6B  // 전원 관리 레지스터
#define MPU6500_ACCEL_XOUT_H        0x3B  // 가속도 데이터 시작 주소]

//함수 선언
int dht11_read(int *humidity, int *temperature);
void i2c_master_init();
void mpu6500_init();
void mpu6500_read_accel(int16_t *ax, int16_t *ay, int16_t *az);

void app_main(void)
{
    //버튼 GPIO 설정
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO),  // 버튼 핀 선택
        .mode = GPIO_MODE_INPUT,                // 입력 모드로 설정
        .pull_up_en = 1,                        // 내부 풀업 저항 켜기
        .intr_type = GPIO_INTR_DISABLE          // 인터럽트 사용 안 함
    };
    gpio_config(&io_conf);

    //I2C 및 MPU6500 초기화 (순서 중요)
    i2c_master_init();
    mpu6500_init();
    ESP_LOGI(TAG, "MPU6500 Initialized");

    //CAN 드라이버 설정 및 시작
    // 1. 설정 구조체 초기화 (TWAI 접두어 사용)
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(TX_GPIO_NUM, RX_GPIO_NUM, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS(); // 속도 500kbps
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL(); // 모든 ID 수신
    // 2. 드라이버 설치 및 시작
    if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
        ESP_LOGI(TAG, "CAN Driver installed");
    } else {
        ESP_LOGE(TAG, "CAN Driver install failed");
        return;
    }
    twai_start();

    // 변수 설정
    int last_button_state = 1;      // 버튼 상태 저장을 위한 변수    1: 안 눌림, 0: 눌림 (풀업기준)
    int hum = 0, temp = 0;          // DHT11 변수
    int16_t ax = 0, ay = 0, az = 0;  // 가속도 값 (16비트 정수
    TickType_t last_dht_tick = 0;   // 마지막으로 DHT센서를 읽은 시간 기록
    TickType_t last_accel_tick = 0;   // 마지막으로 DHT센서를 읽은 시간 기록

    

    while (1) {
        // ==========================================
        // 버튼 감지 및 CAN송신
        int current_button_state = gpio_get_level(BUTTON_GPIO);
        if (last_button_state == 1 && current_button_state == 0) {          // "이전에는 안 눌렸는데(1), 지금 눌렸다(0)" -> 막 눌린 순간 감지 (Rising Edge)
            twai_message_t tx_msg = {       // 보낼 데이터 준비
                .identifier = 0x100,        //ID
                .data_length_code = 5,      //데이터 길이
                .flags = TWAI_MSG_FLAG_NONE,
            };
            memcpy(tx_msg.data, "CLICK", 5);
            twai_transmit(&tx_msg, pdMS_TO_TICKS(100));
            ESP_LOGI(TAG, "Button Sent");
        }
        last_button_state = current_button_state;   // 현재 버튼 상태를 저장 (다음 루프 비교용)
        
        // DHT센서값 1초마다 전송
        TickType_t current_tick = xTaskGetTickCount();
        if (current_tick - last_dht_tick >= pdMS_TO_TICKS(1000)) {
            // 1. DHT11 읽기 및 전송 (ID: 0x200)
            if (dht11_read(&hum, &temp) == 0) {
                twai_message_t tx_msg;
                tx_msg.identifier = 0x200; //ID
                tx_msg.data_length_code = 2;            // 데이터 길이 2바이트
                tx_msg.data[0] = (uint8_t)temp; // 첫 번째 바이트: 온도
                tx_msg.data[1] = (uint8_t)hum;  // 두 번째 바이트: 습도
                twai_transmit(&tx_msg, pdMS_TO_TICKS(100));
                printf("[DHT] Temp:%d C, Hum:%d %%\n", temp, hum);
            } 
            last_dht_tick = current_tick;
        }

        //accel 센서값 1초 마다 전송
        if (current_tick - last_accel_tick >= pdMS_TO_TICKS(1000)) {
            // 2. MPU6500 가속도 읽기 & 전송
            mpu6500_read_accel(&ax, &ay, &az);
            
            twai_message_t acc_msg = {
                .identifier = 0x300,   // 가속도용 새 ID
                .data_length_code = 6  // X(2) + Y(2) + Z(2) = 6바이트
            };

            // 16비트 데이터를 8비트씩 쪼개서 넣기 (Big Endian 방식)
            acc_msg.data[0] = (uint8_t)(ax >> 8); // X 상위
            acc_msg.data[1] = (uint8_t)(ax);      // X 하위
            acc_msg.data[2] = (uint8_t)(ay >> 8); // Y 상위
            acc_msg.data[3] = (uint8_t)(ay);      // Y 하위
            acc_msg.data[4] = (uint8_t)(az >> 8); // Z 상위
            acc_msg.data[5] = (uint8_t)(az);      // Z 하위

            if (twai_transmit(&acc_msg, pdMS_TO_TICKS(100)) == ESP_OK) {
                printf("[MPU] Accel X:%d, Y:%d, Z:%d -> Sent\n", ax, ay, az);
            }
            last_accel_tick = current_tick;
        }
        // ==========================================
        // [수신] 메시지 받기 (큐에 있는 것 모두 처리)
        twai_message_t rx_msg;
        
        // while문을 써서 쌓여있는 메시지를 빠르게 다 읽어옵니다.
        while (twai_receive(&rx_msg, pdMS_TO_TICKS(100)) == ESP_OK) {
            printf("Recv ID[0x%lx] Len[%d]: ", rx_msg.identifier, rx_msg.data_length_code);
            
            // 데이터 출력
            for (int i = 0; i < rx_msg.data_length_code; i++) {
                printf("%c", rx_msg.data[i]);
            }
            printf("\n");
        }

        // 0.01초 대기
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}


//DHT11(온습도) 센서 읽기
int dht11_read(int *humidity, int *temperature) {
    uint8_t data[5] = {0, 0, 0, 0, 0};
    int counter = 0;

    // 1. 시작 신호 보내기 (Start Signal)
    gpio_set_direction(DHT11_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(DHT11_PIN, 0);       // 라인을 Low로 내림
    vTaskDelay(pdMS_TO_TICKS(20));      // 최소 18ms 이상 대기
    gpio_set_level(DHT11_PIN, 1);       // High로 올림
    ets_delay_us(30);                   // 20~40us 대기
    gpio_set_direction(DHT11_PIN, GPIO_MODE_INPUT); // 입력 모드로 전환

    // 2. 센서 응답 확인
    // 센서가 Low로 응답할 때까지 대기
    while (gpio_get_level(DHT11_PIN) == 1) {
        if (counter++ > 85) return -1; // 타임아웃
        ets_delay_us(1);
    }
    counter = 0;
    // 센서가 다시 High로 갈 때까지 대기 (80us Low 신호)
    while (gpio_get_level(DHT11_PIN) == 0) {
        if (counter++ > 85) return -1; 
        ets_delay_us(1);
    }
    counter = 0;
    // 센서가 데이터 전송 시작(Low)할 때까지 대기 (80us High 신호)
    while (gpio_get_level(DHT11_PIN) == 1) {
        if (counter++ > 85) return -1; 
        ets_delay_us(1);
    }

    // 3. 40비트 데이터 읽기 (습도16bit + 온도16bit + 체크섬8bit)
    for (int i = 0; i < 40; i++) {
        // 비트 시작 전 50us Low 구간 대기
        while (gpio_get_level(DHT11_PIN) == 0) {}

        // High 구간 길이 측정 (이 길이로 0과 1을 판별)
        ets_delay_us(28); // 28us 후에도 High라면 '1', 아니면 '0'
        
        if (gpio_get_level(DHT11_PIN)) {
            // High 상태라면 비트 '1' 기록
            data[i / 8] |= (1 << (7 - (i % 8)));
            
            // 나머지 High 구간이 끝날 때까지 대기
            while (gpio_get_level(DHT11_PIN) == 1) {}
        }
    }

    // 4. 체크섬 검증 및 데이터 저장
    // DHT11 데이터 구조: [습도정수].[습도소수].[온도정수].[온도소수].[체크섬]
    if (data[4] == (data[0] + data[1] + data[2] + data[3])) {
        *humidity = data[0];     // DHT11은 정수 부분만 유효함
        *temperature = data[2];  // DHT22라면 data[0]<<8 | data[1] 등 계산 필요
        return 0; // 성공
    } else {
        return -2; // 체크섬 에러
    }
}

// --- [I2C 초기화 함수] ---
void i2c_master_init() {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    i2c_param_config(I2C_MASTER_NUM, &conf);
    i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
}

// --- [MPU6500 초기화: 잠깨우기] ---
void mpu6500_init() {
    // 0x6B번 레지스터(PWR_MGMT_1)에 0을 쓰면 잠에서 깨어남
    uint8_t data[2] = {MPU6500_PWR_MGMT_1, 0x00};
    i2c_master_write_to_device(I2C_MASTER_NUM, MPU6500_ADDR, data, 2, pdMS_TO_TICKS(100));
}

// --- [MPU6500 가속도 읽기] ---
void mpu6500_read_accel(int16_t *ax, int16_t *ay, int16_t *az) {
    uint8_t raw_data[6];
    uint8_t reg_addr = MPU6500_ACCEL_XOUT_H;

    // 0x3B번지부터 6바이트 연속으로 읽기 (X상, X하, Y상, Y하, Z상, Z하)
    i2c_master_write_read_device(I2C_MASTER_NUM, MPU6500_ADDR, &reg_addr, 1, raw_data, 6, pdMS_TO_TICKS(100));

    // 상위 바이트와 하위 바이트 합치기
    *ax = (int16_t)((raw_data[0] << 8) | raw_data[1]);
    *ay = (int16_t)((raw_data[2] << 8) | raw_data[3]);
    *az = (int16_t)((raw_data[4] << 8) | raw_data[5]);
}