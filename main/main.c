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


static const char *TAG = "uart_nb_iot";

#define BUF_SIZE (1024)
#define RD_BUF_SIZE (BUF_SIZE)
#define EX_UART_NUM UART_NUM_2

int64_t t_before_us = 0;
int64_t t_after_us = 0;
static gpio_num_t pwr_key = 14;
gpio_config_t io_conf;

static char url[]      = "AT+SMCONF=\"URL\",\"demo.thingsboard.io\",1883";
static char username[] = "AT+SMCONF=\"USERNAME\",\"VHT\"";
static char clientid[] = "AT+SMCONF=\"CLIENTID\",\"VHT\"";
static char password[] = "AT+SMCONF=\"PASSWORD\",\"123456\"";
static char topic_telemetry[]    = "AT+SMPUB=\"v1/devices/me/telemetry\"";
static char topic_attributes[]    = "AT+SMPUB=\"v1/devices/me/attributes\"";

static int pci = 0;
static int rsrp = 0;
static int rsrq = 0;
static int sinr = 0;
static int cellid = 0;

static float latitude = 0;
static float longitude = 0;

typedef enum
{   
    AT_ERROR,
    AT_NO_SERVICE,
    AT_CENG,
    AT_SEND_MQTT,
    AT_ACTIVE,
    AT_DEACTIVE,
    AT_CGNSINF,
    AT_CLBS,
    AT_OK,
    AT_POWER_OFF,
    AT_TIMEOUT,
} 
AT_flag;

static QueueHandle_t uart0_queue; // queue de luu su kien uart

typedef struct{
    char buffer_rec[BUF_SIZE];//buffer chua du lieu tra ve
    bool flag_have_rec;//flag kiem tra co du lieu tra ve thong qua uart khong
} connect_Simcom7090_t;
connect_Simcom7090_t Simcom7090;//Simcom7090 la 1 bien theo kieu du lieu tu dinh nghia, trong do co chua buffer luu thong tin tra ve


/*
Ham Read_response duoc cac phan hoi nhan duoc sau khi gui lenh AT command, tuy vao data nhan
duoc ma se tra ve cac flag tuong ung, vi du gui lenh AT bi loi thi se tra ve AT_ERROR
*/
AT_flag Read_response(uint16_t timewait, char *ATcommand){
    vTaskDelay(timewait/portTICK_PERIOD_MS);
    if(Simcom7090.flag_have_rec) {
        ESP_LOGI(TAG,"Data nhan duoc: %s\n",Simcom7090.buffer_rec);
        if(strstr((char *)Simcom7090.buffer_rec, "ERROR")){
            ESP_LOGI(TAG, "Fail from %s\n", ATcommand);
            vTaskDelay(100/portTICK_PERIOD_MS);
            return AT_ERROR;
        }
        else if(strstr((char *)Simcom7090.buffer_rec, "ACTIVE")){
            if(strstr((char *)Simcom7090.buffer_rec, "DEACTIVE")){
                ESP_LOGI(TAG, "Deactive from %s\n", ATcommand);
                vTaskDelay(50/portTICK_PERIOD_MS);
                return AT_DEACTIVE;
            }
            ESP_LOGI(TAG, "Active from %s\n", ATcommand);
            vTaskDelay(80/portTICK_PERIOD_MS);
            return AT_ACTIVE;
        }
        else if(strstr((char *)Simcom7090.buffer_rec, "OK")){
            ESP_LOGI(TAG, "OK from %s\n", ATcommand);
            vTaskDelay(100/portTICK_PERIOD_MS);
            return AT_OK;
        }
        else if(strstr((char *)Simcom7090.buffer_rec, ">")){
            vTaskDelay(100/portTICK_PERIOD_MS);
            return AT_SEND_MQTT;
        }
    }
    printf("Khong nhan duoc data tu %s\n",ATcommand);
    return AT_TIMEOUT;
}

/*
Ham gpio_init_pwr cau hinh chan pwr 
*/
void gpio_init_pwr(gpio_config_t *io_conf, gpio_num_t gpio_num){
    io_conf->mode = GPIO_MODE_OUTPUT;
    io_conf->pin_bit_mask = 1 << gpio_num;
    io_conf->pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf->pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf->intr_type = GPIO_INTR_DISABLE;
    gpio_config(io_conf);
    gpio_set_level(gpio_num,1);
}

/*
Ham start_nbiot dung de kich hoat module nb_iot bang cach dat chan pwr xuong muc thap
theo 1 khoang thoi gian nhat dinh
*/
void start_nbiot(uint16_t timewait_active){
    gpio_set_level(pwr_key,0);
    vTaskDelay(timewait_active/portTICK_PERIOD_MS);
    gpio_set_level(pwr_key,1);
    vTaskDelay(100/portTICK_PERIOD_MS);
}

/*
Ham uart_event_task duoc dung de xu ly cac su kien UART, lay du lieu ra ben ngoai de xu ly.
Simcom7090.buffer_rec se la buffer duoc dua ra ben ngoai de xu ly du lieu
*/

static void uart_event_task(void *pvParameters)
{
    uart_event_t event;
    uint8_t* dtmp = (uint8_t*) malloc(RD_BUF_SIZE);
    char* buffer_sub = (char*) malloc(RD_BUF_SIZE);
    for(;;) {
        //Waiting for UART event.
        if(xQueueReceive(uart0_queue, (void * )&event, (portTickType)portMAX_DELAY)) {
            bzero(dtmp, RD_BUF_SIZE);
            bzero(buffer_sub,RD_BUF_SIZE);
            switch(event.type) {
                case UART_DATA:
                    uart_read_bytes(EX_UART_NUM, dtmp, event.size, portMAX_DELAY);
                    Simcom7090.flag_have_rec = true;
                    memcpy(buffer_sub,dtmp,strlen((char*)dtmp));
                    strcat(Simcom7090.buffer_rec,buffer_sub);
                    break;
                default:
                    ESP_LOGI(TAG, "uart event type: %d", event.type);
                    break;
            }
        }
    }
    free(dtmp);
    dtmp = NULL;
    vTaskDelete(NULL);
}
/*
Ham Send_AT dung de gui AT command va duoc su dung trong ham send_and_read_response
*/
void Send_AT(char *ATcommand){
    ESP_LOGI(TAG,"Data send: %s",ATcommand);
    Simcom7090.flag_have_rec = false;
    memset(Simcom7090.buffer_rec, 0, BUF_SIZE);
    uart_write_bytes(EX_UART_NUM, ATcommand, strlen(ATcommand));
    uart_write_bytes(EX_UART_NUM, "\r\n", strlen("\r\n"));
    vTaskDelay(100/portTICK_PERIOD_MS);
}

/*
Ham kiem tra service cua module sim. Ngoai ra ham nay con tien hanh lay cac du lieu ve 
pci, rsrp, rsrq, sinr, cellid
*/
AT_flag check_serive(uint16_t time_wait_res, uint8_t retry){
    while(retry--){
        Send_AT("AT+CENG?");
        vTaskDelay(time_wait_res/portTICK_PERIOD_MS);
        if(Simcom7090.flag_have_rec) {
            ESP_LOGI(TAG,"Data nhan duoc: %s\n",Simcom7090.buffer_rec);
            if (strstr((char *)Simcom7090.buffer_rec, "NO SERVICE")) {
                ESP_LOGI(TAG, "Response NO SERVICE from: %s\n", "AT+CENG?");
            }
            else if(strstr((char *)Simcom7090.buffer_rec, "LTE NB-IOT")){
                ESP_LOGI(TAG, "Read CENG response: %s\n", Simcom7090.buffer_rec);
                vTaskDelay(100/portTICK_PERIOD_MS);
                char extract[128];
                int dontcare;
                sscanf(Simcom7090.buffer_rec, "%[^:]:%[^:]: %s", extract, extract, extract);
                sscanf(extract, "%d,\"%d,%d,%d,%d,%d,%d,%d,%d",&dontcare,&dontcare,&pci,&rsrp,&dontcare,&rsrq,&sinr,&dontcare,&cellid);
                ESP_LOGI(TAG, "pci: %d", pci);
                ESP_LOGI(TAG, "rsrp: %d", rsrp);
                ESP_LOGI(TAG, "rsrq: %d", rsrq);
                ESP_LOGI(TAG, "sinr: %d", sinr);
                ESP_LOGI(TAG, "cellid: %d", cellid);
                return AT_CENG;
            }
        }
    }
    return AT_NO_SERVICE;
}

/*
Ham send_and_read_response thuc hien viec gui cac lenh AT_command. Sau do doc cac phan hoi tra ve, neu phan hoi tra ve dung mong muon se return true
. Nguoc lai ham nay se return false.
*/
bool send_and_read_response(char *ATcommand, uint16_t time_wait_res, uint8_t retry){
    AT_flag res;
    while(retry--){
        Send_AT(ATcommand);
        res = Read_response(time_wait_res,ATcommand);
        if(res == AT_OK || res == AT_SEND_MQTT || res == AT_ACTIVE){
            return true;
        }
    }
    return false;
}

/*
Ham connect_mqtt khoi tao chu trinh ket noi toi broker bang cach
cau hinh cac tham so quan trong nhu url, username, password, clientid
*/
bool connect_mqtt(){
    if(send_and_read_response(url,1500,3) == true){
        if(send_and_read_response(username,1500,3) == true){
            if(send_and_read_response(password,1500,3) == true){
                if(send_and_read_response(clientid,1500,3) == true){
                    if(send_and_read_response("AT+SMCONN",5000,3) == true){
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

/*
Ham start_connect_mqtt de thuc hien ket noi mqtt toi thingsboard
*/
bool start_connect_mqtt(uint8_t retry){
    bool check;
    while(retry--){
        check = connect_mqtt();
        if(check == true){
            return true;
        }
    }
    return false;
}

/*
Ham start_init_GNSS duoc dung de khoi tao GNSS
*/
void start_init_GNSS(uint16_t time_wait_res, uint8_t retry){
    send_and_read_response("AT+CGNSPWR=1",time_wait_res,retry);
}

/*
Ham get_GNSS se lay toa do thong qua gnss
*/
AT_flag get_GNSS(uint16_t time_wait_res, uint8_t retry){
    //Delay khoang 15s thi moi tien hanh lay du lieu tu gnss
    vTaskDelay(15000/portTICK_PERIOD_MS);
    while(retry--){
        Send_AT("AT+CGNSINF");
        vTaskDelay(time_wait_res/portTICK_PERIOD_MS);
        if (strstr((char *)Simcom7090.buffer_rec, "CGNSINF")) {
            ESP_LOGI(TAG, "Read CGNSINF response: %s\n", Simcom7090.buffer_rec);
            vTaskDelay(100/portTICK_PERIOD_MS);
            char extract[128];
            int dontcare;
            float dont_care;
            sscanf(Simcom7090.buffer_rec, "%[^:]: %s", extract, extract);
            sscanf(extract, "%d,%d,%f,%f,%f",&dontcare,&dontcare,&dont_care,&latitude,&longitude);
            ESP_LOGI(TAG, "latitude: %f", latitude);
            ESP_LOGI(TAG, "longitude: %f", longitude);
            return AT_CGNSINF;
        }
    }
    return AT_TIMEOUT;
}

/*
Ham lay toa do bang cach dung LBS
*/
AT_flag get_CLBS(uint16_t time_wait_res, uint8_t retry){
    while(retry--){
        Send_AT("AT+CLBS=1,0");
        vTaskDelay(time_wait_res/portTICK_PERIOD_MS);
        if (strstr((char *)Simcom7090.buffer_rec, "CLBS")) {
            ESP_LOGI(TAG, "Read CLBS response: %s\n", Simcom7090.buffer_rec);
            vTaskDelay(100/portTICK_PERIOD_MS);
            char extract[128];
            int dontcare;
            sscanf(Simcom7090.buffer_rec, "%[^:]: %d,%f,%f", extract,&dontcare,&longitude,&latitude);
            ESP_LOGI(TAG, "latitude: %f", latitude);
            ESP_LOGI(TAG, "longitude: %f", longitude);
            return AT_CLBS;
        }
    }
    return AT_TIMEOUT;
}

/*
Ham power off module nb_iot
*/
void turn_off_nbiot(uint8_t retry){
    while(retry--){
        Send_AT("AT+CPOWD=1");
        vTaskDelay(2000/portTICK_PERIOD_MS);
        if(Simcom7090.flag_have_rec) {
            if(strstr((char *)Simcom7090.buffer_rec, "NORMAL POWER DOWN")){
                ESP_LOGI(TAG, "POWER OFF SUCCESS");
                vTaskDelay(100/portTICK_PERIOD_MS);
                retry = 0;
            }
        }
        else{
            ESP_LOGI(TAG, "POWER OFF FAIL");
            if(retry == 1){
                ESP_LOGI(TAG, "POWER OFF URGENTLY");
                Send_AT("AT+CPOWD=0");
            }
        }
    } 
}

/*
Ham turn_off_phone dung de tat mang di dong
*/
void turn_off_phone(uint8_t retry){
    send_and_read_response("AT+CFUN=4",1500,retry);
}
/*
Ham turn_on_phone dung de tat mang di dong
*/
void turn_on_phone(uint8_t retry){
    while(retry--){
        if(send_and_read_response("AT+CFUN=0",1500,2) == true){
            if(send_and_read_response("AT+CFUN=1,1",2500,2) == true){
                vTaskDelay(2500/portTICK_PERIOD_MS);
                if(send_and_read_response("AT",1500,5) == true){
                    send_and_read_response("ATE0",1500,2);
                }
                retry = 0;
            }
        }
    }
}

/*
Ham tat gnss
*/
void turn_off_gnss(uint8_t retry){
    while(retry--){
        Send_AT("AT+CGNSPWR=0");
        vTaskDelay(2000/portTICK_PERIOD_MS);
        if(Simcom7090.flag_have_rec) {
            if(strstr((char *)Simcom7090.buffer_rec, "OK")){
                ESP_LOGI(TAG, "TURN OFF GNSS SUCCESS");
                vTaskDelay(100/portTICK_PERIOD_MS);
                retry = 0;
            }
        }
        else{
            ESP_LOGI(TAG, "TURN OFF GNSS FAIL");
        }
    }
}

/* 
Ham mqtt_send_message dung de gui ban tin toi broker
*/
void mqtt_send_message(char *message, int length, int qos, int retain, char *topic_need_send)
{   
    char topic_set[256];
    uint8_t retry = 3;
    sprintf(topic_set, "%s,%d,%d,%d", topic_need_send, length, qos, retain);
    while(retry--){
        if(send_and_read_response(topic_set,2000,1)==true){
            ESP_LOGI(TAG,"Start send data\n");
            if(send_and_read_response(message,1500,1) == true){
                retry = 0;
            }
        }
    }
    vTaskDelay(100/portTICK_PERIOD_MS);
}
/*
Ham start_init tao bat module simcom len bang cach dat chan pwr ve muc low. Gui cac lenh AT va ATE0
de kiem tra viec ket noi co thanh cong hay khong
*/
bool start_init(int retry, uint16_t timewait_active){
    while(retry--){
        start_nbiot(timewait_active);
        vTaskDelay(1500/portTICK_PERIOD_MS);
        if(send_and_read_response("AT",1000,3) == true){
            send_and_read_response("ATE0",1000,2);
            return true;
        }
    }
    return false;
}

/*
Ham uart_start dung de khoi tao UART
*/
void uart_start(){
    /* Configure parameters of an UART driver,
     * communication pins and install the driver */
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    //Install UART driver, and get the queue.
    uart_driver_install(EX_UART_NUM, BUF_SIZE * 2, 0, 20, &uart0_queue, 0);
    uart_param_config(EX_UART_NUM, &uart_config);
    //Set UART log level
    esp_log_level_set(TAG, ESP_LOG_INFO);
    //Set UART pins
    uart_set_pin(EX_UART_NUM, 17, 16, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    //Create a task to handler UART event
    xTaskCreate(uart_event_task, "uart_event_task", 2048, NULL, 10, NULL);
}

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