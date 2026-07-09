#ifndef _LED_OPR_H
#define _LED_OPR_H

struct led_operations{
    int num;
    int (*init)(int io);
    int (*write)(int io, int status);
    int (*read)(int io);
};

struct led_operations* get_board_led_opr(void);

#endif

