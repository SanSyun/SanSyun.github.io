#ifndef _LED_RESOURCE_H
#define _LED_RESOURCE_H

/*
 * RK3568 GPIO naming:
 *
 * GPIOx_A0 ~ GPIOx_A7  -> offset 0  ~ 7
 * GPIOx_B0 ~ GPIOx_B7  -> offset 8  ~ 15
 * GPIOx_C0 ~ GPIOx_C7  -> offset 16 ~ 23
 * GPIOx_D0 ~ GPIOx_D7  -> offset 24 ~ 31
 *
 * Example:
 * GPIO0_C0 -> bank = 0, offset = 16
 * GPIO3_B2 -> bank = 3, offset = 10
 * GPIO4_D1 -> bank = 4, offset = 25
 */

#define RK_PA0  0
#define RK_PA1  1
#define RK_PA2  2
#define RK_PA3  3
#define RK_PA4  4
#define RK_PA5  5
#define RK_PA6  6
#define RK_PA7  7

#define RK_PB0  8
#define RK_PB1  9
#define RK_PB2  10
#define RK_PB3  11
#define RK_PB4  12
#define RK_PB5  13
#define RK_PB6  14
#define RK_PB7  15

#define RK_PC0  16
#define RK_PC1  17
#define RK_PC2  18
#define RK_PC3  19
#define RK_PC4  20
#define RK_PC5  21
#define RK_PC6  22
#define RK_PC7  23

#define RK_PD0  24
#define RK_PD1  25
#define RK_PD2  26
#define RK_PD3  27
#define RK_PD4  28
#define RK_PD5  29
#define RK_PD6  30
#define RK_PD7  31

#define GROUP(x)        (((x) >> 16) & 0xFFFF)
#define PIN(x)          ((x) & 0xFFFF)
#define GROUP_PIN(g, p) (((g) << 16) | (p))

#define PMU_GRF_BASE             0xFDC20000
#define GPIO0_BASE               0xFDD60000


#endif