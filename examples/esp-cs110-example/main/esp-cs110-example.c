#include <stdio.h>
#include "esp-cs110.h"

#define reset_device    16
#define uri_default     "mqtt://efm2com:efm2com@node02.myqtthub.com:1883"
#define unique_topic    "com2efm4"
#define clientID_x      "efm2com4"
#define num_modulo      4

void app_main(void)
{

    init_esp_cs110(uri_default, clientID_x, unique_topic, num_modulo, reset_device);

}
