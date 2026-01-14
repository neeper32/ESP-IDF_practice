// [보드 A: 채널 헌팅 송신용 - 수정됨]
#include <stdio.h>
#include <string.h>
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_log.h"

#define ADC_UNIT        ADC_UNIT_1
#define ADC_CHANNEL     ADC_CHANNEL_0  // GPIO 1번
#define ADC_ATTEN       ADC_ATTEN_DB_12

adc_oneshot_unit_handle_t adc_handle;

typedef struct struct_message {
    int id;
    int value;
} struct_message;

struct_message myData;
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

long map(long x, long in_min, long in_max, long out_min, long out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void init_adc() {
    adc_oneshot_unit_init_cfg_t init_config = { .unit_id = ADC_UNIT };
    adc_oneshot_new_unit(&init_config, &adc_handle);
    adc_oneshot_chan_cfg_t config = { .bitwidth = ADC_BITWIDTH_DEFAULT, .atten = ADC_ATTEN };
    adc_oneshot_config_channel(adc_handle, ADC_CHANNEL, &config);
}

void app_main(void) {
    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();

    if (esp_now_init() != ESP_OK) {
        printf("ESP-NOW Init Failed\n");
        return;
    }

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = 0;  
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);

    init_adc();
    const int DRY_VAL = 3300; 
    const int WET_VAL = 1400;

    while (1) {
        // 1. 센서 읽기
        int adc_raw = 0;
        adc_oneshot_read(adc_handle, ADC_CHANNEL, &adc_raw);
        int percent = map(adc_raw, DRY_VAL, WET_VAL, 0, 100);
        if(percent < 0) percent = 0;
        if(percent > 100) percent = 100;

        myData.id = 1;
        myData.value = percent;

        // [핵심 수정] 1번부터 13번 채널까지 바꾸면서 전송!
        // 보드 B가 어느 채널에 있든 무조건 받게 됨
        for (int ch = 1; ch <= 13; ch++) {
            esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
            esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));
            vTaskDelay(pdMS_TO_TICKS(10)); // 채널 변경 안정화 시간
        }
        
        printf("모든 채널로 데이터 발사 완료: %d%%\n", percent);

        vTaskDelay(pdMS_TO_TICKS(1000)); // 1초 대기
    }
}