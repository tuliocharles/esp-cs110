/*
Copyright (c) 2025 Tulio Charles de Oliveira Carvalho
Licensed under the MIT License. See LICENSE file for details.
*/

#include "esp-cs110.h"
#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE 
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_system.h"  
#include "driver/uart.h" 
#include "string.h"
#include <stdint.h>
#include <stddef.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "mqtt_client.h"

/*************************************************/
/************* Tags *************/
/*************************************************/
static const char* TAGSystem = "Sistema";
static const char* TAGGpio = "GPIO";
static const char *TAGUART = "RX_TASK";
static const char *TAGMQTT = "MQTT_EXAMPLE";

/*************************************************/
/************** DEFINES **************************/
/*************************************************/

#define GPIO_OUTPUT_IO_0    2
#define ESP_INTR_FLAG_DEFAULT 0 

/********** Define UART **************************/
#define TXD_PIN (GPIO_NUM_5) 
#define RXD_PIN (GPIO_NUM_4)

#define qos_ex 0
static char *clientID_x; 
static char *uri_default;
static char *unique_topic;  
static int num_modulo = 0;
static int reset_device = 0;   

int baud_default = 115200;

/**********************************************/
/************* types **************************/
/**********************************************/

typedef struct {
    bool conected;
    bool config_mode;
    uint8_t time_disconected ;
}  mqtt_status_t ;


/* UART */
static const int RX_BUF_SIZE = 1024;

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);

/**********************************************/
/*************** Queue ************************/
/**********************************************/

static QueueHandle_t status_mqtt_queue = NULL;
static QueueHandle_t reset_cr1000 = NULL;
static QueueHandle_t queue_serial = NULL;

/****************************************************
 **************** LIB ***********************
*****************************************************/

bool strcmpr_tc(char *str1, char *str2, int str_len)
{
    uint8_t i = 0;
    uint8_t c = 0;
    bool result = false;
    while (i < str_len)
    {
        if ((*str1) == (*str2))
        {
            c++;
        }

        str1++;
        str2++;
        i++;
    }

    if (c == str_len)
        result = true;

    return result;
}

/***********************************************/
/************* INITs ***************************/
/*************************************************/

void init_uart(void) {


    nvs_handle_t serial_nvs;
    
    char* baud_from_nvs = malloc(10);

    if (nvs_open("serial", NVS_READONLY, &serial_nvs) == ESP_OK){
        size_t required_size;
        nvs_get_str(serial_nvs, "baud", NULL, &required_size);
        //char* uri_from_nvs = malloc(required_size);
        nvs_get_str(serial_nvs, "baud", baud_from_nvs, &required_size);
        
        ESP_LOGI(TAGUART, "Lido da memoria BAUD: %s", baud_from_nvs);
        
        baud_default = atoi(baud_from_nvs);

        ESP_LOGI(TAGUART, "Transformado em inteiro o BAUD: %d", baud_default);

    }   else{
        // Em caso de erro reiniciar a memória do ponto certo
        
        nvs_open("serial", NVS_READWRITE, &serial_nvs);
        
        ESP_LOGI(TAGUART, "Gravando novo baud: %d", baud_default);

        sprintf(baud_from_nvs, "%d", baud_default);
        
        nvs_set_str(serial_nvs, "baud", baud_from_nvs);
        
        ESP_LOGI(TAGUART, "Gravado novo baud: %s", baud_from_nvs);
       
    }

    nvs_close(serial_nvs);

    free(baud_from_nvs);


    const uart_config_t uart_config = {
        .baud_rate = baud_default,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    // We won't use a buffer for sending data.
    uart_driver_install(UART_NUM_1, RX_BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_NUM_1, &uart_config);
    uart_set_pin(UART_NUM_1, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAGMQTT, "Last error %s: 0x%x", message, error_code);
    }
}

static void mqtt_app_start(char *uri)

{

         
  
    esp_mqtt_client_config_t mqtt_cfg = {
        //.broker.address.uri = "mqtt://efm2com:efm2com@node02.myqtthub.com:1883",
        .credentials.client_id = clientID_x,
        .broker.address.uri = uri, //"mqtt://0.tcp.sa.ngrok.io:15947",
       //.broker.address.uri = "mqtt://192.168.0.7:1883", 
    };

    //free(uri_from_nvs_temp);

   
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

/****************************************************/
/************* Call Back *************************/
/****************************************************/

esp_mqtt_client_handle_t client = NULL;

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAGMQTT, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    client = event->client;

    int msg_id;

    static mqtt_status_t status = {
            .conected = false,
            .config_mode = false,
            .time_disconected = 0  
            };


    switch ((esp_mqtt_event_id_t)event_id) {

    case MQTT_EVENT_CONNECTED:
        
        ESP_LOGI(TAGMQTT, "MQTT_EVENT_CONNECTED");
        //msg_id = esp_mqtt_client_publish(client, "/topic/qos1", "data_3", 0, 1, 0);
        //ESP_LOGI(TAGMQTT, "sent publish successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "com2efm", qos_ex);
        ESP_LOGI(TAGMQTT, "sent subscribe successful, msg_id=%d", msg_id);


        msg_id = esp_mqtt_client_subscribe(client, unique_topic, qos_ex);
        ESP_LOGI(TAGMQTT, "sent subscribe successful, msg_id=%d", msg_id);

/*        msg_id = esp_mqtt_client_subscribe(client, "/cor/2", 0);
        ESP_LOGI(TAGMQTT, "sent subscribe successful, msg_id=%d", msg_id);

        
*/

        //bloco adicionado
        msg_id = esp_mqtt_client_subscribe(client, "red_button", qos_ex);  
        ESP_LOGI(TAGMQTT, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "config", qos_ex);  
        ESP_LOGI(TAGMQTT, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "serial", qos_ex);  
        ESP_LOGI(TAGMQTT, "sent subscribe successful, msg_id=%d", msg_id);


        
        //gpio_set_level(GPIO_OUTPUT_IO_0, 1);

        status.conected = true;
        status.time_disconected = 0;  
        
        xQueueSendFromISR(status_mqtt_queue,&status, 0);
        
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAGMQTT, "MQTT_EVENT_DISCONNECTED");

        status.conected = false;
        status.time_disconected = 0;  
        xQueueSendFromISR(status_mqtt_queue,&status, 0);

        //gpio_set_level(GPIO_OUTPUT_IO_0, 0);
        
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAGMQTT, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        //msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
        //ESP_LOGI(TAGMQTT, "sent publish successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAGMQTT, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAGMQTT, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAGMQTT, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);

        ESP_LOG_BUFFER_HEXDUMP(TAGMQTT,  event->data, event->data_len, ESP_LOG_INFO);


        if (strcmpr_tc ( event->topic, "red_button", strlen("red_button") ) ) {
            
            nvs_handle_t my_handle_nvs;
            char *uri_from_nvs = malloc(100);
            sprintf(uri_from_nvs, "%.*s", event->data_len, event->data);

            nvs_open("MQTT", NVS_READWRITE, &my_handle_nvs);
            
            nvs_set_str(my_handle_nvs, "uri", uri_from_nvs);
            free(uri_from_nvs);

            nvs_close(my_handle_nvs);
 
            printf("Restarting now.\n");
            fflush(stdout);
            esp_restart();

                
            
            } else if (strcmpr_tc ( event->topic, "config", strlen("config") ) ) {

                // passa por fila o valor recebido convertido para inteiro para main. 
                uint32_t num_reset = 0;
                char *x = malloc(5);
                snprintf(x, 5, "%.*s", event->data_len, event->data);
                num_reset = atoi(x);
                xQueueSendFromISR(reset_cr1000,&num_reset, NULL);
                free(x);

            } else if (strcmpr_tc ( event->topic, "serial", strlen("serial") ) ) {
                int new_baud = 0;
                int modulo;//num_modulo
                char *new_baud_temp = malloc(15);
                sprintf(new_baud_temp, "%.*s", event->data_len, event->data);
                
                char *token;

                token = strtok(new_baud_temp, ",");

                modulo = atoi(token);
                printf( " %d\n", modulo );

                if (modulo == num_modulo){
                                         
                    token = strtok(NULL, "-");

                    new_baud = atoi(token);

                    xQueueSendFromISR(queue_serial,&new_baud, NULL);
                
                }
                
                
                free(new_baud_temp);
                



            }else {

                uart_write_bytes(UART_NUM_1, event->data, event->data_len);

            }

        

/*        pwmqtt_element_t mqtt_pwm = {
            .num = 0,
            .basic_duty = 0,
        };

        char *x = malloc(5);
        snprintf(x, 5, "%.*s", event->data_len, event->data);
        mqtt_pwm.basic_duty = atoi(x);

        if (strcmpr_tc ( event->topic, "/cor/1", strlen("/cor/1") ) ) {
                mqtt_pwm.num = 1;
                
            }

            if (strcmpr_tc ( event->topic, "/cor/2", strlen("/cor/2") ) ) {
                mqtt_pwm.num = 2;
            }

        xQueueSendFromISR(mqtt_queue,&mqtt_pwm, 0);    
*/

        
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAGMQTT, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAGMQTT, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));

        }
        break;
    default:
        ESP_LOGI(TAGMQTT, "Other event id:%d", event->event_id);
        break;
    }
}

/****************************************************/
/******************** TASKS *************************/
/****************************************************/

static void uart_task(void *arg)
{
    init_uart();
    uint8_t* data = (uint8_t*) malloc(RX_BUF_SIZE+1);

    //rtc_element_t rtc_serial;
    while (1) {
        const int rxBytes = uart_read_bytes(UART_NUM_1, data, RX_BUF_SIZE, 10 / portTICK_PERIOD_MS);
        if (rxBytes > 0) {
            data[rxBytes] = 0;
            ESP_LOGI(TAGUART, "Read %d bytes: '%s'", rxBytes, data);
            ESP_LOG_BUFFER_HEXDUMP(TAGUART, data, rxBytes, ESP_LOG_INFO);

//          rtc_serial.hours = (data[0]-0x30)*10+ (data[1]-0x30);
//            rtc_serial.minutes = (data[2]-0x30)*10+ (data[3]-0x30);  
//            rtc_serial.seconds = (data[4]-0x30)*10+ (data[5]-0x30);  


//            xQueueSendToBack(rtc_queue,&rtc_serial,0);


            //uart_write_bytes(UART_NUM_1, data, rxBytes);
           // printf("ok\n");

           
           esp_mqtt_client_publish(client, "efm2com", (char *)data, rxBytes, qos_ex, 0);
                

           
        }
       // printf("fora\n");
    }
    free(data);
}

static void main_task(void *arg)
{

    status_mqtt_queue = xQueueCreate(2, sizeof(mqtt_status_t)); 
    reset_cr1000      = xQueueCreate(1, sizeof(uint32_t)); 
    queue_serial      = xQueueCreate(1, sizeof(int));   

    /**************************************************/
    /******************** I/O *************************/
    /**************************************************/
    
    
    int GPIO_OUTPUT_PIN_SEL = (1ULL<<GPIO_OUTPUT_IO_0) | (1ULL<<reset_device);
    //zero-initialize the config structure.
    gpio_config_t io_conf = {};
    //disable interrupt
    io_conf.intr_type = GPIO_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    //bit mask of the pins that you want to set,e.g.GPIO18/19
    io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL; // seleção do pino 2. 
    //disable pull-down mode
    io_conf.pull_down_en = 0;
    //disable pull-up mode
    io_conf.pull_up_en = 0;
    //configure GPIO with the given settings
    gpio_config(&io_conf);

    gpio_set_level(GPIO_OUTPUT_IO_0, 0);

    gpio_set_level(reset_device, 1);

    for(unsigned int i_reset=0;i_reset<30;i_reset++){
        gpio_set_level(GPIO_OUTPUT_IO_0, 1);
        vTaskDelay(500 / portTICK_PERIOD_MS);
        gpio_set_level(GPIO_OUTPUT_IO_0, 0);
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }

    gpio_set_level(reset_device, 0);
    
    for(unsigned int i_reset=0;i_reset<30;i_reset++){
        gpio_set_level(GPIO_OUTPUT_IO_0, 1);
        vTaskDelay(500 / portTICK_PERIOD_MS);
        gpio_set_level(GPIO_OUTPUT_IO_0, 0);
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }

    /*****************************************************
     * MQttt 
     *  
    ****************************************************/ 

    ESP_LOGI(TAGMQTT, "[APP] Startup..");
    ESP_LOGI(TAGMQTT, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAGMQTT, "[APP] IDF version: %s", esp_get_idf_version());

    //esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("mqtt_client", ESP_LOG_VERBOSE);
    esp_log_level_set("MQTT_EXAMPLE", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_BASE", ESP_LOG_VERBOSE);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    esp_log_level_set("outbox", ESP_LOG_VERBOSE);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

   /*  This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());
    
    mqtt_status_t status_mqtt ={
        .conected = false,
        .config_mode = false,
        .time_disconected = 0,
    };

    nvs_handle_t my_handle_nvs;
    
    char* uri_from_nvs = malloc(100);

    bool config_mode = false;    
    if (nvs_open("MQTT", NVS_READONLY, &my_handle_nvs) == ESP_OK){
        size_t required_size;
        nvs_get_str(my_handle_nvs, "uri", NULL, &required_size);
        nvs_get_str(my_handle_nvs, "uri", uri_from_nvs, &required_size);
        ESP_LOGI(TAGMQTT, "Lido da memoria Uri: %s", uri_from_nvs);
    } 
    else{
        nvs_open("MQTT", NVS_READWRITE, &my_handle_nvs);
        sprintf(uri_from_nvs, uri_default);
        nvs_set_str(my_handle_nvs, "uri", uri_from_nvs);
        ESP_LOGI(TAGMQTT, "Gravado novo Uri: %s", uri_from_nvs);
    }
    nvs_close(my_handle_nvs);
    
    
    mqtt_app_start(uri_from_nvs); 
    //create UART task
    xTaskCreate(uart_task, "uart_task", 8192, NULL, 20, NULL);

    /******************************************************/
    /*************LOG **************/
    /******************************************************/
    esp_log_level_set(TAGSystem, ESP_LOG_INFO);
    esp_log_level_set(TAGGpio, ESP_LOG_ERROR);
    esp_log_level_set(TAGUART, ESP_LOG_INFO);

    bool piscaled = false;
    uint32_t status_reset = 0;
    int status_serial = 9600;
    unsigned int count = 0;
    unsigned int count_ligado = 0;
    char *espstatus = malloc(200);
    
while(1)
    {
        xQueueReceive(status_mqtt_queue, &status_mqtt, 1000 / portTICK_PERIOD_MS);

        if (status_mqtt.conected == false){
            count ++;
            
            printf("%d\n",count);
            if (count >= 100) {
                
                sprintf(uri_from_nvs, uri_default);
                nvs_open("MQTT", NVS_READWRITE, &my_handle_nvs);
                nvs_set_str(my_handle_nvs, "uri", uri_from_nvs);
                free(uri_from_nvs);

                nvs_close(my_handle_nvs);
 
                printf("TIME OUT - Restarting now.\n");
                fflush(stdout);
                esp_restart();

            }
            if (count%4>0) gpio_set_level(GPIO_OUTPUT_IO_0, 0); else gpio_set_level(GPIO_OUTPUT_IO_0, 1);
        }
        if (status_mqtt.conected == true){
            count = 0;
            count_ligado = count_ligado+1;
            if (count_ligado%60 == 0){            
                sprintf(espstatus, "{\"modulo\" : \"%d\",\"count\" : \"%u\",\"uri\" : \"%s\",\"baud\" : \"%d\", \"name\" : \"TCOC\"}", num_modulo, count_ligado, uri_from_nvs, baud_default);
                printf(espstatus);
                printf("\n");
                esp_mqtt_client_publish(client, "espstatus", espstatus, strlen(espstatus), qos_ex, 0);
            }

            if (config_mode == true){
                piscaled = !piscaled;
                gpio_set_level(GPIO_OUTPUT_IO_0, piscaled);

            } else{
                 gpio_set_level(GPIO_OUTPUT_IO_0, 1);
            }
        }

        if(xQueueReceive(reset_cr1000, &status_reset, 10 / portTICK_PERIOD_MS)){
            
            printf("%"PRIu32"\n",status_reset);
            if (status_reset == num_modulo){
                printf("liga\n");
                gpio_set_level(reset_device, 1);
                vTaskDelay(10000 / portTICK_PERIOD_MS);
                gpio_set_level(reset_device, 0);
                printf("desliga\n");
            }
        }      // Aguarda 10 segundo desligado recebe da fila. 

        if(xQueueReceive(queue_serial, &status_serial, 10 / portTICK_PERIOD_MS)){// atualiza baudrate recebe da fila. 

            printf("NEW BAUD RATE: %d\n", status_serial);
            uart_set_baudrate(UART_NUM_1, (uint32_t) status_serial);
            baud_default = status_serial;
            nvs_handle_t serial_status_nvs;
            char* baud_status_to_nvs = malloc(10);
            nvs_open("serial", NVS_READWRITE, &serial_status_nvs);
            ESP_LOGI(TAGUART, "Gravando após receber dado: %d", status_serial);
            sprintf(baud_status_to_nvs, "%d", status_serial);
            nvs_set_str(serial_status_nvs, "baud", baud_status_to_nvs);
            ESP_LOGI(TAGUART, "Gravado após receber dado: %s", baud_status_to_nvs);
            nvs_close(serial_status_nvs);
            free(baud_status_to_nvs);
        }
    }
}

void init_esp_cs110(char *uri, char *clientid, char *unique, int mod, int reset_modem){ //

    num_modulo = mod;
    reset_device = reset_modem;

    if (clientID_x != NULL) {
        free(clientID_x);
    }
    if (uri_default != NULL) {
        free(uri_default);
    }
    if (unique_topic != NULL) {
        free(unique_topic);
    }
    
    uri_default = malloc(strlen(uri) + 1);
    clientID_x = malloc(strlen(clientid) + 1);
    unique_topic = malloc(strlen(unique) + 1);
    
    if (uri_default != NULL) {
        strcpy(uri_default, uri);  
    } else {
        printf("Error.\n");
    }
    if (clientID_x != NULL) {
        strcpy(clientID_x, clientid);  
    } else {
        printf("Error.\n");
    }
    if (unique_topic != NULL) {
        strcpy(unique_topic, unique);  
    } else {
        printf("Error.\n");
    }

    xTaskCreate(main_task, "main_task", 6144, NULL, 2, NULL);
}
