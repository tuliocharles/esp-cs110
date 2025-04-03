#include <stdio.h>
#include "esp-cs110.h"

#define reset_device    16
#define uri_default     "mqtt://user:password@broker.com:1883"
#define unique_topic    "topic0"
#define clientID_x      "cliente_1"
#define num_modulo      4

void app_main(void)
{

    init_esp_cs110(uri_default, clientID_x, unique_topic, num_modulo, reset_device);

}
