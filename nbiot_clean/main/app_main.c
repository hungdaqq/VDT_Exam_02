/* Light sleep example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "module_7090g.h"

int64_t t_before_us = 0;
int64_t t_after_us = 0;

gpio_config_t io_conf;

char topic_telemetry[]    = "AT+SMPUB=\"v1/devices/me/telemetry\"";
char topic_attributes[]    = "AT+SMPUB=\"v1/devices/me/attributes\"";

int pci = 0;
int rsrp = 0;
int rsrq = 0;
int sinr = 0;
int cellid = 0;

float latitude = 0;
float longitude = 0;
connect_Simcom7090_t Simcom7090;

void app_main(){
    gpio_init_pwr(&io_conf,14);
    uart_start();
     while(1){
        t_before_us = esp_timer_get_time();
        start_init(2,1500);
        //Sau khi khoi tao nb_iot thi se delay khoang 2s de doi module on dinh
        vTaskDelay(2000/portTICK_PERIOD_MS);
        //tat mang di dong di
        turn_off_phone(3);
        vTaskDelay(2000/portTICK_PERIOD_MS);
        //Open GNSS boi lenh AT+CGNSPWR=1
        start_init_GNSS(2000,3);
        vTaskDelay(500/portTICK_PERIOD_MS);
        get_GNSS(2500,2);
        //sau khi lay du lieu toa do thi tien hanh tat gnss di
        turn_off_gnss(2);
        //Doi khoang 3s sau thi tien hanh bat mang di dong len
        vTaskDelay(3000/portTICK_PERIOD_MS);
        turn_on_phone(2);
        //Sau khi bat mang di dong len thi se doi khoang 5s de kiem tra co service hay khong
        vTaskDelay(5000/portTICK_PERIOD_MS);
        if(check_serive(2500,3)== AT_CENG){
            // lenh "AT+CNACT=0,1" dung de Active the network 
            if(send_and_read_response("AT+CNACT=0,1",2000,3) == true){
                if(start_connect_mqtt(3) == true){
                    //neu co data ve pci thi se gui ban tin len broker
                    if (pci != 0 ) {
                        char sys_message[128];
                        sprintf(sys_message, "{\"pci\": %d,\"rsrp\": %d,\"rsrq\": %d,\"sinr\": %d,\"cellid\": %d}",pci,rsrp,rsrq,sinr,cellid);
                        int content_length = strlen(sys_message);
                        mqtt_send_message(sys_message, content_length, 0, 1, topic_telemetry);
                    }
                    //neu co data ve toa do se gui ban tin ve broker
                    if (latitude != 0) {
                        char coo_message[128];
                        sprintf(coo_message, "{\"latitude\":%f,\"longitude\":%f}",latitude,longitude);
                        int content_length = strlen(coo_message);
                        mqtt_send_message(coo_message, content_length, 0, 1, topic_attributes);
                    }
                    else{
                        //neu den day ma chua co thong tin ve toa do thi tien hanh su dung LBS
                        get_CLBS(3000,2);
                        //neu lay duoc data thi gui thong tin toa do ve broker
                        if(latitude != 0){
                            char coo_message[128];
                            sprintf(coo_message, "{\"latitude\":%f,\"longitude\":%f}",latitude,longitude);
                            int content_length = strlen(coo_message);
                            mqtt_send_message(coo_message, content_length, 0, 1, topic_attributes);
                        }
                    }
                }
            }
        }         
        turn_off_nbiot(2);
        vTaskDelay(100/portTICK_PERIOD_MS);
        t_after_us = esp_timer_get_time();
        esp_sleep_enable_timer_wakeup(300000000 - (t_after_us - t_before_us));
        esp_light_sleep_start();
    }
}