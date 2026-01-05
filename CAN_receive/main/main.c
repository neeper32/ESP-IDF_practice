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

void app_main(void)
{
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
        // ==========================================
        // [송신] 메시지 보내기 (두 번에 나누어 전송)
        // ==========================================
        /*
        // 첫 번째 패킷: "Hello_wo" (8 bytes)
        twai_message_t tx_msg1 = {
            .identifier = 0x100,           // ID 설정
            .data_length_code = 8,         // 데이터 길이
            .flags = TWAI_MSG_FLAG_NONE,   // 표준 프레임
        };
        memcpy(tx_msg1.data, "Hello_wo", 8);
        twai_transmit(&tx_msg1, pdMS_TO_TICKS(100)); // 전송

        // 두 번째 패킷: "rld" (3 bytes)
        twai_message_t tx_msg2 = {
            .identifier = 0x100,           // 같은 ID 사용 (혹은 구분하려면 다르게)
            .data_length_code = 3,
            .flags = TWAI_MSG_FLAG_NONE,
        };
        memcpy(tx_msg2.data, "rld", 3);
        twai_transmit(&tx_msg2, pdMS_TO_TICKS(100)); // 전송

        ESP_LOGI(TAG, "Sent: Hello_world (Split into 2 frames)");
        */

        // ==========================================
        // [수신] 메시지 받기 (큐에 있는 것 모두 처리)
        // ==========================================
        twai_message_t rx_msg;
        
        // while문을 써서 쌓여있는 메시지를 빠르게 다 읽어옵니다.
        while (twai_receive(&rx_msg, pdMS_TO_TICKS(10)) == ESP_OK) {
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