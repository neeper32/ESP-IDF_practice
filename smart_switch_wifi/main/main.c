#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"  // 서보모터 제어용 PWM 라이브러리
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h" // IP 주소 출력을 위해 필요

static const char *TAG = "SMART_SWITCH";

// ==========================================
// [사용자 설정] 와이파이 & 핀 설정
// ==========================================
#define MY_SSID      "neeper_wifi"  // 와이파이 이름
#define MY_PASS      "12241224"        // 비밀번호

#define SERVO_PIN    18                // 서보모터 신호선 (GPIO 18)
#define BUTTON_PIN   0                 // 물리 버튼 (GPIO 0 = BOOT 버튼)

// [서보모터 각도 설정] (설치 환경에 맞춰 조정 필요)
// 서보모터가 스위치를 '탁' 치고 다시 가운데로 와야 함
#define ANGLE_CENTER 90  // 중립 (대기 상태)
#define ANGLE_ON     120  // 켜는 방향으로 밀기
#define ANGLE_OFF    60 // 끄는 방향으로 밀기

// 현재 전등 상태 (0:꺼짐, 1:켜짐)
int light_state = 0;

// ==========================================
// [서보모터 제어 함수] LEDC PWM 사용
// ==========================================
void servo_init() {
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = LEDC_TIMER_0,
        .duty_resolution  = LEDC_TIMER_13_BIT, // 13비트 해상도
        .freq_hz          = 50,  // 서보모터는 50Hz 사용
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = LEDC_CHANNEL_0,
        .timer_sel      = LEDC_TIMER_0,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = SERVO_PIN,
        .duty           = 0, 
        .hpoint         = 0
    };
    ledc_channel_config(&ledc_channel);
}

// 각도(0~180)를 PWM 듀티비로 변환하여 이동시키는 함수
void servo_move(int angle) {
    // SG90 기준: 0도=약 2.5%듀티, 180도=약 12.5%듀티 (13비트 8192 기준 계산)
    // 펄스 폭: 500us(0도) ~ 2400us(180도)
    int duty = (int)(((angle / 180.0) * 1900.0 + 500.0) / 20000.0 * 8192.0);
    
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

// [기존 servo_move 밑에 이 함수를 추가하세요]
// 서보모터의 신호를 끊어서 떨림을 방지하는 함수
void servo_detach() {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

// [기존 action_light 함수를 이걸로 교체하세요]
void action_light(int turn_on) {
    if (turn_on) {
        ESP_LOGI(TAG, "전등 켜는 중...");
        servo_move(ANGLE_ON);     // 1. 스위치 밀기
        vTaskDelay(pdMS_TO_TICKS(500)); 
        
        servo_move(ANGLE_CENTER); // 2. 중립 복귀
        vTaskDelay(pdMS_TO_TICKS(500));
        
        servo_detach();           // 3. 힘 빼기 (진동 멈춤!)
        light_state = 1;
    } else {
        ESP_LOGI(TAG, "전등 끄는 중...");
        servo_move(ANGLE_OFF);    // 1. 반대로 밀기
        vTaskDelay(pdMS_TO_TICKS(500));
        
        servo_move(ANGLE_CENTER); // 2. 중립 복귀
        vTaskDelay(pdMS_TO_TICKS(500));
        
        servo_detach();           // 3. 힘 빼기 (진동 멈춤!)
        light_state = 0;
    }
}

// ==========================================
// [웹 서버] 핸드폰 제어
// ==========================================
static esp_err_t root_get_handler(httpd_req_t *req) {
    char html_buf[1024];
    // 현재 상태에 따라 버튼 색상 변경
    char *status_text = light_state ? "ON (켜짐)" : "OFF (꺼짐)";
    char *color = light_state ? "#f1c40f" : "#95a5a6"; // 노랑 vs 회색

    sprintf(html_buf, 
        "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<style>"
        "body { font-family: sans-serif; text-align: center; margin-top: 50px; background-color: #2c3e50; color: white; }"
        ".btn { padding: 20px 40px; font-size: 24px; border: none; border-radius: 10px; cursor: pointer; margin: 10px; width: 200px; }"
        ".btn-on { background-color: #f1c40f; color: #333; }"
        ".btn-off { background-color: #e74c3c; color: white; }"
        ".status { font-size: 30px; margin-bottom: 30px; color: %s; }"
        "</style></head>"
        "<body>"
        "<h1>💡 스마트 스위치</h1>"
        "<div class='status'>현재 상태: %s</div>"
        "<a href='/on'><button class='btn btn-on'>켜기 (ON)</button></a><br>"
        "<a href='/off'><button class='btn btn-off'>끄기 (OFF)</button></a>"
        "</body></html>", 
        color, status_text);

    httpd_resp_send(req, html_buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// "켜기" 버튼 눌렀을 때
static esp_err_t on_handler(httpd_req_t *req) {
    action_light(1); // 켜기 동작
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/"); // 메인 화면으로 복귀
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// "끄기" 버튼 눌렀을 때
static esp_err_t off_handler(httpd_req_t *req) {
    action_light(0); // 끄기 동작
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

httpd_handle_t start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root_uri = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler, .user_ctx = NULL };
        httpd_uri_t on_uri   = { .uri = "/on", .method = HTTP_GET, .handler = on_handler, .user_ctx = NULL };
        httpd_uri_t off_uri  = { .uri = "/off", .method = HTTP_GET, .handler = off_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &root_uri);
        httpd_register_uri_handler(server, &on_uri);
        httpd_register_uri_handler(server, &off_uri);
        return server;
    }
    return NULL;
}

// ==========================================
// [물리 버튼 감지 태스크]
// ==========================================
void button_task(void *arg) {
    // 버튼 핀 설정 (입력, 풀업)
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    int last_state = 1; // 1: 안 눌림 (풀업)

    while (1) {
        int current_state = gpio_get_level(BUTTON_PIN);

        // 버튼이 눌려짐 (Falling Edge: 1 -> 0)
        if (last_state == 1 && current_state == 0) {
            ESP_LOGI(TAG, "물리 버튼 감지됨!");
            
            // 현재 상태의 반대로 동작 (Toggle)
            if (light_state == 0) {
                action_light(1); // 켜기
            } else {
                action_light(0); // 끄기
            }
            
            vTaskDelay(pdMS_TO_TICKS(300)); // 디바운싱 (중복 눌림 방지)
        }
        last_state = current_state;
        vTaskDelay(pdMS_TO_TICKS(10)); // 0.01초마다 확인
    }
}

// ==========================================
// [Wi-Fi 연결]
// ==========================================
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "접속 주소: http://" IPSTR, IP2STR(&event->ip_info.ip));
    }
}

void wifi_init_sta(void) {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL);
    wifi_config_t wifi_config = {
        .sta = { .ssid = MY_SSID, .password = MY_PASS, .threshold.authmode = WIFI_AUTH_WPA2_PSK },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
}

void app_main(void) {
    // 1. 초기화
    nvs_flash_init();
    servo_init();
    servo_move(ANGLE_CENTER); // 시작 시 중립 위치

    // 2. Wi-Fi 및 웹서버 시작
    wifi_init_sta();
    start_webserver();

    // 3. 물리 버튼 감지 시작 (멀티태스킹)
    xTaskCreate(button_task, "button_task", 2048, NULL, 10, NULL);
}