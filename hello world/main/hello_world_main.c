/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>      //Standard Input Output의 약자로 printf같은 함수를 가지고 있음
#include <inttypes.h>   //uint32_t와 같은 정수형 변수를 다룰 떄 필요한 도구
#include "sdkconfig.h"  //idf.py menuconfig로 설정한 프로젝트의 모든 설정값이 들어있는 중요 파일
#include "freertos/FreeRTOS.h"  //FreeROTS와 관련 기능을 가져옴. 멀티태스킹을 가능하게 해줌, vTaskDelay 같은 함수가 포함되어 있음
#include "freertos/task.h"      //FreeROTS와 관련 기능을 가져옴. 멀티태스킹을 가능하게 해줌, vTaskDelay 같은 함수가 포함되어 있음
#include "esp_chip_info.h"      //ESP32 칩 자체의 정보(CPU 코어 수 등), 플래시 메모리 정보, 그리고 esp_restart 같은 시스템 기능을 사용하기 위한 헤더파일
#include "esp_flash.h"          //ESP32 칩 자체의 정보(CPU 코어 수 등), 플래시 메모리 정보, 그리고 esp_restart 같은 시스템 기능을 사용하기 위한 헤더파일
#include "esp_system.h"         //ESP32 칩 자체의 정보(CPU 코어 수 등), 플래시 메모리 정보, 그리고 esp_restart 같은 시스템 기능을 사용하기 위한 헤더파일

void app_main(void)     //아두이노에서는 void setup(), void loop()를 하는 반면 ESP-IDF에서는 void app_main(void)가 main()함수를 대신한다.
{
    printf("Hello world!\n");   //시리얼 모니터에 Hello world를 출력함 '\n'은 줄바꿈

    /* Print chip information */
    esp_chip_info_t chip_info;      //esp_chip_info_t: Espressif가 미리 만들어 놓은 양식(구조체), chip_info라는 변수명
    uint32_t flash_size;            //u: 부호가 없다(0과 양수만 저장), int: 정수, 32(32비트 정수까지 저장 가능), _t: C언어에서 type(자료형)임을 나타냄
    esp_chip_info(&chip_info);      //chip의 상태를 스캔하는 함수, &주소값을 나타냄(포인터) chip info의 주소값에다가 저장을 해야되기 때문에 주소값을 사용
    printf("This is %s chip with %d CPU core(s), %s%s%s%s, ",       //%s: string(문자열), %d(정수형)
           CONFIG_IDF_TARGET,                                                                   //menuconfig(ESP-IDF에서 지정한 보드명), 칩의 이름이 들어가 있음, 빌드하면 변수가 생성됨
           chip_info.cores,                                                                     //chip_info 구조체에서 CPU코어 수를 읽어옴
           //&: AND(&전후 값이 1일 때 1 반환), ?: ? 앞의 값이 1이면 : 앞의 값 반환 0이면 뒤의 값 반환   chip_info.features: chip_info 구조체에서 지원기능 항목을 읽어옴(ex 0101)
           (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi/" : "",                         //CHIP_FEATURE_WIFI_BGN: Wifi기능에 해당하는 비트만 1로 켜져 있는 기준값(ex:0001)
           (chip_info.features & CHIP_FEATURE_BT) ? "BT" : "",                                  //CHIP_FEATURE_BT: 블루투스 클래식을 의미함 (BT는 스트리밍에 강함, BLE는 상태 업데이트에 강함)
           (chip_info.features & CHIP_FEATURE_BLE) ? "BLE" : "",                                //CHIP_FEATURE_BLE: 저전력 블루투스를 의미함(아주 적은 양의 데이터를 가끔식 보내야 할 때 사용)
           (chip_info.features & CHIP_FEATURE_IEEE802154) ? ", 802.15.4 (Zigbee/Thread)" : ""); //CHIP_FEATURE_IEEE802154: 저속 무선 개인 통신망을 의미함(IOT 기기들이 통신할 때 사용 MESH형태)

    unsigned major_rev = chip_info.revision / 100;      //칩의 세부 버전 정보를 가져와서 x.x 형태를 xx형태로 출력(ex: 1.1 -> 101)
    unsigned minor_rev = chip_info.revision % 100;
    printf("silicon revision v%d.%d, ", major_rev, minor_rev);  //저장된 버전 정보 변수를 출력함
    if(esp_flash_get_size(NULL, &flash_size) != ESP_OK) {       //esp_flash_get_size: 플래시 메모리 크기 출력, 첫번째 인수(NULL)은 어떤 플래시 메모리 크기를 잴 것인지(NULL이면 메인 플래시 메모리), 두번째 인수는 어디에 저장할 것인지(주소값형태를 사용해야함)
        printf("Get flash size failed");                        //esp_flash_get_size함수는 실행 종료 후 esp_err_t 변수를 반환함 함수를 성공적으로 실행했다면 ESP_OK를 반환하고 실패했다면 ESP_FAIL을 반환함
        return;
    }

    printf("%" PRIu32 "MB %s flash\n", flash_size / (uint32_t)(1024 * 1024),            //PRIu32: uint32_t타입의 변수를 출력함, {flash_size / (uint32_t)(1024 * 1024)}: falsh_size는 바이트 단위임 그래서 MB형태로 바꾸기 위해 1024*1024(1MB)단위로 나눠줌
           (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");    //내장형 플래시 메모리인지 외장형 메모리인지 출력

    printf("Minimum free heap size: %" PRIu32 " bytes\n", esp_get_minimum_free_heap_size()/ (uint32_t)(1024 * 1024));    //힙(Heap)메모리: 프로그램이 실행되는 동안 할당할 수 있는 자유 메모리 공간
                                                                                                //esp_get_minimum_free_heap_size(): 프로그램이 부팅된 이후 지금까지 남아있던 힙 메모리의 양이 가장 적었을 때의 메모리크기(크키가 너무 작다면 메모리가 부족하여 오작동할 수도 있음)
    for (int i = 60; i >= 0; i--) {                     //재시작 카운트다운
        if(i%10==0)
        {
        printf("Restarting in %d seconds...\n", i);
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);          //vTaskDelay(): CPU를 독점하지 않고 효율적으로 기다리는 FreeRTOS 방식, 인수로 틱의 개수를 인수로 받음
                                                        //계산식: (기다리고 싶은 총 시간 ms) / (1틱당 시간 ms) = (기다려야 할 총 틱의 개수)      portTICK_PERIOD_MS를 사용하면, 나중에 menuconfig에서 틱 주기를 변경하더라도 코드를 수정할 필요 없음
    }                                                   
    printf("Restarting now.\n");    //재시작 직전에 메시지 출력
    fflush(stdout);                 //pritf로 출력할 내용이 버퍼(임시 저장 공간)에 남이있을 수 있으므로 재시작 전 강제로 비워서 메시지가 출력되게 함, fflush: 강제로 비운다, stdout: 표준 출력(시리얼 모니터)
    esp_restart();                  //esp보드 재시작        컴퓨터는 한글자씩 출력하지 않고 버퍼에 담아뒀다가 특정조건(\n 등)이 되면 출력함, 따라서 버퍼를 비워야 정상적으로 출력할 수 있음
}
