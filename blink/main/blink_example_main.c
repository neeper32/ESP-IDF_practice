/* Blink Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "led_strip.h"
#include "sdkconfig.h"

static const char *TAG = "example";     //포인터 문자열 변수 TAG에 example 대입

/* Use project configuration menu (idf.py menuconfig) to choose the GPIO to blink,
   or you can edit the following line and set a number here.
*/
#define BLINK_GPIO CONFIG_BLINK_GPIO    //CONFIG_BLINK_GPIO: menuconfig에서 GPIO핀을 지정할 수 있음(sdkconfig파일 내에 있는 핀을 수정하면 됨)

static uint8_t s_led_state = 0;     //LED의 현재상태를 저장하기 위한 변수(8비트 정수형)

#ifdef CONFIG_BLINK_LED_STRIP       //CONFIG_BLINK_LED_STRIP값이 참이면 #elif가 나오기 전까지 컴파일을 진행 거짓이면 컴파일을 진행하지 않고 무시함

static led_strip_handle_t led_strip;    //led_strip.h 에 정의된 데이터 타입     led 스트립을 제어하기 위한 핸들 변수, LED스트립 드라이버를 초기화 한 후 드라이버를 제어할 수 있는 고유ID가 핸들이다.

static void blink_led(void)     //LED를 켜고 끄는 함수 선언
{
    /* If the addressable LED is enabled */
    if (s_led_state) {                                                                          //LED가 1일때 즉, 켜져 있을 때
        /* Set the LED pixel using RGB from 0 (0%) to 255 (100%) for each color */
        led_strip_set_pixel(led_strip, 0, 16, 16, 16);             //어떤 LED를 어떤 색으로 켤지 지정, led_strip 핸들, LED번호(0), RGB(16,16,16) 0부터 255까지 지정 가능, 저장만 해놓는 상태
        /* Refresh the strip to send data */
        led_strip_refresh(led_strip);                           //저장되어 있는 LED 정보를 실제로 수행
    } else {                                                    //LED가 0일때 즉, 꺼져 있을 때
        /* Set all LED off to clear all pixels */
        led_strip_clear(led_strip);                         //LED 스트립의 모든 LED를 끄는 함수
    }
}

static void configure_led(void)     //LED를 제어하기 위한 초기 설정 함수 선언
{
    ESP_LOGI(TAG, "Example configured to blink addressable LED!");      //ESP_LOGI: Info(정보)등급의 로그를 출력하는 함수 TAG: 이전에 선언 해둔 변수, 이후 시리얼 모니터에 출력할 내용
    /* LED strip initialization with the GPIO and pixels number*/
    led_strip_config_t strip_config = {     //구조체 변수
        .strip_gpio_num = BLINK_GPIO,       //LED 제어 핀 넘버는 BLINK_GPIO다
        .max_leds = 1, // at least one LED on board     //최대 제어할 LED수는 1개다
    };
#if CONFIG_BLINK_LED_STRIP_BACKEND_RMT      //#if: 컴파일 중에 조건을 확인, sdkconfig에서 RMT가 참이라면 RMT 방식에 필요한 명령 실행
    led_strip_rmt_config_t rmt_config = {               //rmt_config 변수 선언과 동시에 RMT하드웨어 동작 정밀도 10MHz로 설정
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .flags.with_dma = false,
    };                                                                                  //ESP_ERROR_CHECK():드라이버 생성이 성공했는지 검사후 실패했다면 어떤 에러인지 상세한 정보를 출력하고 프로그램을 중단함.
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));      //led_strip_new_rmt_device(): 지정한 모든 설정 값을 바탕으로 RMT 드라이버를 생성하고 핸들을 만들어서 led_strip에 저장                                                                     
    #elif CONFIG_BLINK_LED_STRIP_BACKEND_SPI    //sdkconfig에서 RTM가 참이라면 SPI 방식에 필요한 명령 실행
    led_strip_spi_config_t spi_config = {
        .spi_bus = SPI2_HOST,
        .flags.with_dma = true,
    };
    ESP_ERROR_CHECK(led_strip_new_spi_device(&strip_config, &spi_config, &led_strip));
#else                                       //else에 해당하면 #error를 통해 에러메시지로 ""사이에 있는 문장을 출력
#error "unsupported LED strip backend"      
#endif                                          //#if를 종료할 때 사용, #if를 사용하면 꼭 endif로 닫아줘야 함
    /* Set all LED off to clear all pixels */
    led_strip_clear(led_strip);                 //설정 진행후 LED를 전부 끔, LED 스트립의 모든 LED를 끄는 함수
}


#elif CONFIG_BLINK_LED_GPIO     //CONFIG_BLINK_LED_GPIO가 참일 때 아래 내용 컴파일

static void blink_led(void)     //함수 정의
{
    /* Set the GPIO level according to the state (LOW or HIGH)*/
    gpio_set_level(BLINK_GPIO, s_led_state);        //s_led_state에 따라 gpio핀에 3.3V혹은 0V인가(GPIO핀 디지털 제어)
}

static void configure_led(void)
{
    ESP_LOGI(TAG, "Example configured to blink GPIO LED!");
    gpio_reset_pin(BLINK_GPIO);                             //BLINK_GPIO핀을 초기화(GPIO핀을 설정하기 전 항상 초기화)
    /* Set the GPIO as a push/pull output */
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);       //BLINK_GPIO핀을 아웃풋 모드로 지정
}

#else
#error "unsupported LED type"
#endif

void app_main(void)
{

    /* Configure the peripheral according to the LED type */
    configure_led();

    while (1) {     //무한 루프
        ESP_LOGI(TAG, "Turning the LED %s!",s_led_state == true ? "ON" : "OFF");
        blink_led();
        /* Toggle the LED state */
        s_led_state = !s_led_state;
        vTaskDelay(CONFIG_BLINK_PERIOD / portTICK_PERIOD_MS);   //portTICK_PERIOD_MS 1틱당 몇ms인지
    }
}
