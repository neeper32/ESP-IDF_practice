#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "GPS_STATUS";

// --- [핀 설정] ---
#define TXD_PIN (GPIO_NUM_17)
#define RXD_PIN (GPIO_NUM_16)

// --- [UART 설정] ---
#define UART_PORT_NUM      UART_NUM_1
#define UART_BAUD_RATE     9600
#define RX_BUF_SIZE        2048 // 버퍼를 조금 늘렸습니다

// --- [데이터 저장용 구조체] ---
typedef struct {
    double latitude;    // 위도
    double longitude;   // 경도
    int hour, minute, second; // 시간
    int sat_count;      // 연결된 위성 개수 (중요!)
    int max_snr;        // 가장 강한 신호 세기 (감도)
    int fix_quality;    // 0=없음, 1=GPS, 2=DGPS
} GPS_Data;

GPS_Data my_gps;

// --- [도분 -> 도 변환 함수] ---
double convert_nmea_to_decimal(double nmea_val) {
    int degrees = (int)(nmea_val / 100);
    double minutes = nmea_val - (degrees * 100);
    return degrees + (minutes / 60.0);
}

// --- [토큰 추출 함수] ---
// 콤마(,) 사이의 빈 값(Empty Field)도 정확히 처리하기 위한 함수
char* get_token(char *source, int token_index, char *dest, int dest_size) {
    int comma_count = 0;
    int i = 0, j = 0;
    
    // 해당 인덱스의 콤마 위치 찾기
    while (comma_count < token_index && source[i] != '\0') {
        if (source[i] == ',') comma_count++;
        i++;
    }

    // 데이터 복사
    while (source[i] != ',' && source[i] != '*' && source[i] != '\0' && j < dest_size - 1) {
        dest[j++] = source[i++];
    }
    dest[j] = '\0';
    return dest;
}

// --- [파싱 함수] ---
void parse_nmea(char *nmea_sentence) {
    char buffer[32]; // 임시 저장 공간

    // 1. $GPRMC: 시간, 위도, 경도
    if (strstr(nmea_sentence, "$GPRMC")) {
        // 시간 (Index 1)
        get_token(nmea_sentence, 1, buffer, sizeof(buffer));
        if (strlen(buffer) > 0) {
            float time_raw = atof(buffer);
            my_gps.hour = (int)(time_raw / 10000);
            my_gps.minute = (int)((time_raw - (my_gps.hour * 10000)) / 100);
            my_gps.second = (int)(time_raw - (my_gps.hour * 10000) - (my_gps.minute * 100));
            my_gps.hour = (my_gps.hour + 9) % 24; // 한국 시간
        }

        // 위도 (Index 3)
        get_token(nmea_sentence, 3, buffer, sizeof(buffer));
        if (strlen(buffer) > 0) my_gps.latitude = convert_nmea_to_decimal(atof(buffer));

        // 경도 (Index 5)
        get_token(nmea_sentence, 5, buffer, sizeof(buffer));
        if (strlen(buffer) > 0) my_gps.longitude = convert_nmea_to_decimal(atof(buffer));
    }
    
    // 2. $GPGGA: 위성 개수, 고정 품질
    else if (strstr(nmea_sentence, "$GPGGA")) {
        // Fix Quality (Index 6): 0=Invalid, 1=GPS fix, 2=DGPS fix
        get_token(nmea_sentence, 6, buffer, sizeof(buffer));
        my_gps.fix_quality = atoi(buffer);

        // 위성 개수 (Index 7) - 이게 중요!
        get_token(nmea_sentence, 7, buffer, sizeof(buffer));
        my_gps.sat_count = atoi(buffer);
    }

    // 3. $GPGSV: 신호 세기 (SNR)
    else if (strstr(nmea_sentence, "$GPGSV")) {
        // GSV 문장은 위성 4개씩 정보를 담고 있음.
        // SNR 위치: 7, 11, 15, 19 번째 인덱스
        int snr_indices[] = {7, 11, 15, 19};
        
        // GSV 메시지가 여러 줄로 오므로, 매번 0으로 초기화하지 않고 최댓값을 갱신함
        // (단, 1번 메시지가 올 때만 초기화하는 로직을 추가하면 더 정확하지만, 여기선 간단히 유지)
        
        for (int k = 0; k < 4; k++) {
            get_token(nmea_sentence, snr_indices[k], buffer, sizeof(buffer));
            int snr = atoi(buffer);
            if (snr > my_gps.max_snr) {
                my_gps.max_snr = snr; // 가장 센 신호 기록
            }
        }
    }
}

// --- [출력 태스크] ---
// 너무 자주 출력되면 정신없으니 2초마다 종합해서 보여줌
void display_task(void *pvParameters) {
    while (1) {
        if (my_gps.sat_count > 0) {
            ESP_LOGI(TAG, "========================================");
            ESP_LOGI(TAG, " 🛰️  위성 상태 모니터링");
            ESP_LOGI(TAG, "========================================");
            ESP_LOGI(TAG, " [시 간] %02d시 %02d분 %02d초 (KST)", my_gps.hour, my_gps.minute, my_gps.second);
            ESP_LOGI(TAG, " [위 치] 위도: %.6f / 경도: %.6f", my_gps.latitude, my_gps.longitude);
            ESP_LOGI(TAG, " [개 수] 연결된 위성: %d개", my_gps.sat_count);
            
            // 신호 품질 평가
            char *quality = "나쁨 🔴";
            if (my_gps.max_snr >= 40) quality = "최상 🟢";
            else if (my_gps.max_snr >= 30) quality = "좋음 🟡";
            else if (my_gps.max_snr >= 20) quality = "보통 🟠";

            ESP_LOGI(TAG, " [감 도] 최고 신호 세기: %d dB (%s)", my_gps.max_snr, quality);
            ESP_LOGI(TAG, "========================================\n");
            
            // 다음 측정을 위해 SNR 리셋 (순간적인 값 변화를 보기 위해)
            my_gps.max_snr = 0; 
        } else {
            ESP_LOGW(TAG, "위성 찾는 중... (하늘을 보여주세요!)");
        }
        vTaskDelay(pdMS_TO_TICKS(2000)); // 2초마다 갱신
    }
}

void init_uart() {
    const uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(UART_PORT_NUM, RX_BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_PORT_NUM, &uart_config);
    uart_set_pin(UART_PORT_NUM, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

void app_main(void) {
    init_uart();
    uint8_t *data = (uint8_t *) malloc(RX_BUF_SIZE + 1);
    char line_buffer[256];
    int line_pos = 0;

    // 출력용 태스크 별도 실행
    xTaskCreate(display_task, "display_task", 4096, NULL, 5, NULL);

    while (1) {
        int rxBytes = uart_read_bytes(UART_PORT_NUM, data, 1, 20 / portTICK_PERIOD_MS);
        if (rxBytes > 0) {
            if (data[0] == '\n' || data[0] == '\r') {
                if (line_pos > 0) {
                    line_buffer[line_pos] = '\0';
                    parse_nmea(line_buffer);
                    line_pos = 0;
                }
            } else {
                if (line_pos < sizeof(line_buffer) - 1) {
                    line_buffer[line_pos++] = (char)data[0];
                }
            }
        }
    }
    free(data);
}