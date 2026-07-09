#ifndef _LED_DRV_H
#define _LED_DRV_H

#include "led_opr.h"

void led_class_create_device(int minor);
void led_class_destroy_device(int minor);
void register_led_operations(struct led_operations *opr);

#endif