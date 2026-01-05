#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/twai.h"  // 표준 드라이버 헤더

// 핀 설정 (ESP32-S3)
#define TX_GPIO_NUM     GPIO_NUM_41
#define RX_GPIO_NUM     GPIO_NUM_42

void app_main()
{
    // 1. 설정 구조체 초기화
    // TX: 4번, RX: 5번, 모드: Normal
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(TX_GPIO_NUM, RX_GPIO_NUM, TWAI_MODE_NORMAL);
    
    // 속도 설정: 250Kbps (필요에 따라 500KBITS 등으로 변경 가능)
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_250KBITS();
    
    // 필터 설정: 모든 메시지 수신
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    // 2. 드라이버 설치
    if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
        printf("Driver installed successfully\n");
    } else {
        printf("Failed to install driver\n");
        return;
    }

    // 3. 드라이버 시작
    if (twai_start() == ESP_OK) {
        printf("Driver started\n");
    } else {
        printf("Failed to start driver\n");
        return;
    }

    // 4. 메시지 전송 루프
    while (1) {
        // 메시지 구조체 생성
        twai_message_t message;
        message.identifier = 0x1;          // ID: 0x1
        message.extd = 1;                  // Extended ID 사용 (29bit)
        message.rtr = 0;                   // 데이터 프레임
        message.data_length_code = 8;      // 데이터 길이: 8바이트
        
        // 보낼 데이터 채우기 (예: 0, 1, 2...)
        for (int i = 0; i < 8; i++) {
            message.data[i] = i; 
        }

        // 전송 (대기 시간 1000ms)
        esp_err_t res = twai_transmit(&message, pdMS_TO_TICKS(1000));
        
        if (res == ESP_OK) {
            printf("Message queued for transmission\n");
        } else {
            printf("Failed to queue message: %s\n", esp_err_to_name(res));
        }

        vTaskDelay(pdMS_TO_TICKS(1000)); // 1초 대기
    }
}