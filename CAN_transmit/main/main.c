#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/twai.h" // 여기가 핵심: can.h 대신 twai.h 사용

// 로그 태그
static const char *TAG = "TWAI_CAN";

// 핀 설정
#define TX_GPIO_NUM     GPIO_NUM_2
#define RX_GPIO_NUM     GPIO_NUM_1

#define BUTTON_GPIO     GPIO_NUM_3

void app_main(void)
{
    //버튼 GPIO 설정
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;      // 인터럽트 사용 안 함
    io_conf.mode = GPIO_MODE_INPUT;             // 입력 모드로 설정
    io_conf.pin_bit_mask = (1ULL << BUTTON_GPIO); // 버튼 핀 선택
    io_conf.pull_up_en = 1;                     // 내부 풀업 저항 켜기 (중요!)
    io_conf.pull_down_en = 0;                   // 풀다운 끄기
    gpio_config(&io_conf);

    //CAN 드라이버 설정 및 시작
    //설정 구조체 초기화 (TWAI 접두어 사용)
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

    //버튼 상태 저장을 위한 변수
    int last_button_state = 1;  //1: 안 눌림, 0: 눌림 (풀업기준)
    while (1) {
        // ==========================================
        // 버튼 감지 및 송신
        // ==========================================
        // 첫 번째 패킷: "Hello_wo" (8 bytes)
        int current_button_state = gpio_get_level(BUTTON_GPIO);

        // "이전에는 안 눌렸는데(1), 지금 눌렸다(0)" -> 막 눌린 순간 감지 (Rising Edge)
        if (last_button_state == 1 && current_button_state == 0) {
            
            // 보낼 데이터 준비
            twai_message_t tx_msg = {
                .identifier = 0xAA,            // ID 변경해봄
                .data_length_code = 5,
                .flags = TWAI_MSG_FLAG_NONE,
            };
            memcpy(tx_msg.data, "CLICK", 5);

            // 전송
            if (twai_transmit(&tx_msg, pdMS_TO_TICKS(100)) == ESP_OK) {
                ESP_LOGI(TAG, "Button Pressed! Message Sent: CLICK");
            } else {
                ESP_LOGE(TAG, "Failed to send");
            }
        }

        // 현재 버튼 상태를 저장 (다음 루프 비교용)
        last_button_state = current_button_state;

        // ==========================================
        // [수신] 메시지 받기 (큐에 있는 것 모두 처리)
        // ==========================================
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

        // 2초 대기
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}