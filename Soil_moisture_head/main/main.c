#include <stdio.h>
#include <string.h>
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "esp_now.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/ip_addr.h"

// 핫스팟 설정
#define MY_SSID      "phyco01"
#define MY_PASS      "#jiho1224"

// ====================================================
// [사용자 설정] 고정 IP 설정 (여기를 수정하세요!)
// ====================================================
// 예: 아이폰 핫스팟이면 -> 172, 20, 10, 50 (게이트웨이는 172, 20, 10, 1)
// 예: 일반 공유기면    -> 192, 168, 0, 50 (게이트웨이는 192, 168, 0, 1)

// 1. 내가 쓰고 싶은 고정 IP (마지막 숫자는 50~200 사이 추천)
#define FIXED_IP_ADDR   192, 168, 219, 150

// 2. 공유기(핫스팟)의 대문 주소 (보통 마지막 자리가 1)
#define FIXED_GATEWAY   172, 20, 10, 1

// 3. 서브넷 마스크 (건드리지 마세요)
#define FIXED_NETMASK   255, 255, 255, 0

#define ADC_UNIT        ADC_UNIT_1
#define ADC_CHANNEL     ADC_CHANNEL_0
#define ADC_ATTEN       ADC_ATTEN_DB_12

// 데이터 저장용 변수
volatile int val_board1 = 0; // A보드 (외부)
volatile int val_board2 = 0; // B보드 (나 자신)

// ESP-NOW로 받을 데이터 구조체 (A보드랑 똑같이 생겨야 함)
typedef struct struct_message {
    int id;
    int value;
} struct_message;
struct_message incomingData;

adc_oneshot_unit_handle_t adc_handle;

// --------------------------------------------------
// [ESP-NOW 수신 콜백 함수] (A보드가 보내면 여기가 실행됨)
// --------------------------------------------------
void OnDataRecv(const esp_now_recv_info_t * esp_now_info, const uint8_t *incomingData, int len) {
    struct_message *myRecv = (struct_message *) incomingData;
    // ID가 1번이면 변수에 저장
    if (myRecv->id == 1) {
        val_board1 = myRecv->value;
    }
}

// --------------------------------------------------
// [웹 서버] 화면에 두 개 다 보여주기
// --------------------------------------------------
static esp_err_t root_get_handler(httpd_req_t *req) {
    char html_buf[1024];
    sprintf(html_buf, 
        "<!DOCTYPE html><html>"
        "<head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<meta http-equiv='refresh' content='1'>" 
        "<style>"
        "body { font-family: Arial; text-align: center; margin-top: 30px; background-color: #f4f4f4; }"
        ".container { display: flex; justify-content: center; gap: 20px; flex-wrap: wrap; }"
        ".box { background: white; padding: 20px; border-radius: 15px; width: 300px; box-shadow: 0 4px 10px rgba(0,0,0,0.1); }"
        ".val { font-size: 50px; font-weight: bold; color: #27ae60; }"
        "h2 { color: #333; }"
        "</style></head>"
        "<body>"
        "<h1>🌱 우리집 화분 관리</h1>"
        "<div class='container'>"
        
        // 화분 1 (외부 보드)
        "<div class='box'>"
        "<h2>🪴 화분 1 (저쪽)</h2>"
        "<div class='val'>%d %%</div>"
        "<div>무선 수신됨</div>"
        "</div>"
        
        // 화분 2 (이 보드)
        "<div class='box'>"
        "<h2>🪴 화분 2 (이쪽)</h2>"
        "<div class='val'>%d %%</div>"
        "<div>직접 연결됨</div>"
        "</div>"

        "</div></body></html>", 
        val_board1, val_board2);

    httpd_resp_send(req, html_buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static const httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler, .user_ctx = NULL };

httpd_handle_t start_webserver(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &root);
        return server;
    }
    return NULL;
}

// --------------------------------------------------
// [Wi-Fi 설정] (핫스팟 접속)
// --------------------------------------------------
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        printf("===========================================\n");
        printf("주소: http://" IPSTR "\n", IP2STR(&event->ip_info.ip));
        printf("===========================================\n");
        start_webserver();
    }
}

void wifi_init_sta(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    
    // Wi-Fi 인터페이스 생성
    esp_netif_t *my_netif = esp_netif_create_default_wifi_sta();

    // 1. DHCP(자동 할당) 끄기
    esp_netif_dhcpc_stop(my_netif);

    // 2. 고정 IP 정보 입력 (여기에 숫자를 직접 적으세요!)
    esp_netif_ip_info_t ip_info;
    
    // ▼ [여기 수정됨] 아이폰 핫스팟 예시 (172.20.10.50)
    IP4_ADDR(&ip_info.ip, 192, 168, 219, 50);      // 내가 쓸 고정 IP
    IP4_ADDR(&ip_info.gw, 192, 168, 219, 1);       // 게이트웨이 (핫스팟 주소)
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0); // 서브넷 마스크 (고정)

    // 만약 공유기(192.168.0.x)를 쓴다면 위 3줄을 지우고 아래를 쓰세요
    // IP4_ADDR(&ip_info.ip, 192, 168, 0, 50);
    // IP4_ADDR(&ip_info.gw, 192, 168, 0, 1);
    // IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);

    // 3. 설정 적용
    esp_netif_set_ip_info(my_netif, &ip_info);

    // ... (이 아래 코드는 기존과 동일) ...
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL);
    
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = MY_SSID,
            .password = MY_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
}

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
    
    // 1. Wi-Fi 연결
    wifi_init_sta();

    // 2. ESP-NOW 초기화 (Wi-Fi 켜진 뒤에 해야 함)
    if (esp_now_init() != ESP_OK) { printf("Error initializing ESP-NOW\n"); return; }
    esp_now_register_recv_cb(OnDataRecv);

    // 3. 내 센서 초기화
    init_adc();
    const int DRY_VAL = 3300; 
    const int WET_VAL = 1400;

    while (1) {
        // 내 센서(화분 2) 읽기
        int adc_raw = 0;
        adc_oneshot_read(adc_handle, ADC_CHANNEL, &adc_raw);
        int percent = map(adc_raw, DRY_VAL, WET_VAL, 0, 100);
        if(percent < 0) percent = 0;
        if(percent > 100) percent = 100;
        
        val_board2 = percent; // 내 값 업데이트

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}