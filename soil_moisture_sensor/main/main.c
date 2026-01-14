#include <stdio.h>
#include <string.h>
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "lwip/err.h"
#include "lwip/sys.h"

static const char *TAG = "SOIL_WIFI";

// ====================================================
// [사용자 설정] 핫스팟 이름과 비번을 여기 적으세요!
// ====================================================
#define MY_SSID      "Jiho's iPhone"   // 핫스팟 이름 (대소문자 구분)
#define MY_PASS      "jiho1224"         // 핫스팟 비밀번호

// [핀 설정]
#define ADC_UNIT        ADC_UNIT_1
#define ADC_CHANNEL     ADC_CHANNEL_0   // GPIO 1번
#define ADC_ATTEN       ADC_ATTEN_DB_12

// 전역 변수
volatile int global_adc_raw = 0;
volatile int global_soil_percent = 0;
adc_oneshot_unit_handle_t adc_handle;

// 함수 선언
long map(long x, long in_min, long in_max, long out_min, long out_max);
void init_adc();
void wifi_init_sta(void);
httpd_handle_t start_webserver(void);

// ====================================================
// [웹 서버 핸들러]
// ====================================================
static esp_err_t root_get_handler(httpd_req_t *req)
{
    char html_buf[1024];
    sprintf(html_buf, 
        "<!DOCTYPE html><html>"
        "<head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<meta http-equiv='refresh' content='1'>" 
        "<style>"
        "body { font-family: Arial; text-align: center; margin-top: 50px; background-color: #e8f5e9; }"
        ".box { background: white; padding: 20px; border-radius: 15px; display: inline-block; box-shadow: 0 4px 15px rgba(0,0,0,0.1); }"
        ".val { font-size: 70px; font-weight: bold; color: #2e7d32; }"
        ".raw { color: #888; font-size: 16px; margin-top: 10px; }"
        "</style></head>"
        "<body>"
        "<div class='box'>"
        "<h1>🌿 화분 모니터</h1>"
        "<div class='val'>%d %%</div>"
        "<div class='raw'>센서값(Raw): %d</div>"
        "</div>"
        "</body></html>", 
        global_soil_percent, global_adc_raw);

    httpd_resp_send(req, html_buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static const httpd_uri_t root = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = root_get_handler,
    .user_ctx  = NULL
};

httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &root);
        return server;
    }
    return NULL;
}

// ====================================================
// [Wi-Fi 스테이션 모드 설정] (핫스팟 접속용)
// ====================================================
static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "핫스팟 연결 실패... 재시도 중...");
        esp_wifi_connect(); // 끊어지면 무한 재접속
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        
        // [중요] 여기서 IP 주소를 출력해줍니다!!
        ESP_LOGI(TAG, "------------------------------------------------");
        ESP_LOGI(TAG, "★ 접속 성공! 핸드폰 주소창에 아래 IP를 입력하세요 ★");
        ESP_LOGI(TAG, "http://" IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "------------------------------------------------");
        
        start_webserver(); // 연결되면 웹서버 시작
    }
}

void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = MY_SSID,
            .password = MY_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "핫스팟 연결 시도 중... (SSID:%s)", MY_SSID);
}

// ====================================================
// [메인 함수]
// ====================================================
void app_main(void)
{
    // 1. NVS 초기화
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Wi-Fi 연결 시작
    wifi_init_sta();
    
    // 3. ADC 초기화
    init_adc();

    const int DRY_VAL = 3300;
    const int WET_VAL = 1400;

    while (1) {
        int adc_raw = 0;
        adc_oneshot_read(adc_handle, ADC_CHANNEL, &adc_raw);
        
        int soil_percent = map(adc_raw, DRY_VAL, WET_VAL, 0, 100);
        if (soil_percent < 0) soil_percent = 0;
        if (soil_percent > 100) soil_percent = 100;

        global_adc_raw = adc_raw;
        global_soil_percent = soil_percent;

        // 시리얼 모니터로 값 확인
        // ESP_LOGI(TAG, "Soil: %d %% (Raw: %d)", soil_percent, adc_raw);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// 유틸리티 함수들
long map(long x, long in_min, long in_max, long out_min, long out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}
void init_adc() {
    adc_oneshot_unit_init_cfg_t init_config = { .unit_id = ADC_UNIT };
    adc_oneshot_new_unit(&init_config, &adc_handle);
    adc_oneshot_chan_cfg_t config = { .bitwidth = ADC_BITWIDTH_DEFAULT, .atten = ADC_ATTEN };
    adc_oneshot_config_channel(adc_handle, ADC_CHANNEL, &config);
}