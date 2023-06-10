#ifndef _MODULE_7090G_H_
#define _MODULE_7090G_H_

#include "driver/gpio.h"

#define BUF_SIZE (1024)
#define RD_BUF_SIZE (BUF_SIZE)

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

typedef struct{
    char buffer_rec[BUF_SIZE];//buffer chua du lieu tra ve
    bool flag_have_rec;//flag kiem tra co du lieu tra ve thong qua uart khong
} connect_Simcom7090_t;

void uart_start();
void start_init_GNSS(uint16_t time_wait_res, uint8_t retry);
bool start_init(int retry, uint16_t timewait_active);
bool start_connect_mqtt(uint8_t retry);
void gpio_init_pwr(gpio_config_t *io_conf, gpio_num_t gpio_num);
AT_flag check_serive(uint16_t time_wait_res, uint8_t retry);
AT_flag get_GNSS(uint16_t time_wait_res, uint8_t retry);
AT_flag get_CLBS(uint16_t time_wait_res, uint8_t retry);
bool send_and_read_response(char *ATcommand, uint16_t time_wait_res, uint8_t retry);
void turn_off_phone(uint8_t retry);
void turn_on_phone(uint8_t retry);
void turn_off_gnss(uint8_t retry);
void turn_off_nbiot(uint8_t retry);
bool start_connect_mqtt(uint8_t retry);
void mqtt_send_message(char *message, int length, int qos, int retain, char *topic_need_send);

#endif