#include <linux/module.h>

#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/tty.h>
#include <linux/kmod.h>
#include <linux/gfp.h>
#include <linux/platform_device.h>

#include "led_opr.h"
#include "led_drv.h"
#include "led_resource.h"

static int g_ledpins[100];
static int g_ledcnt = 0;

static volatile unsigned int* PMU_GRF_GPIO0C_IOMUX_L; //处理GPIO复用
static volatile unsigned int* PMU_GRF_GPIO0C_DS_0; //引脚驱动能力设置
static volatile unsigned int* GPIO0_SWPORT_DR_H; // 输入输出数据寄存器
static volatile unsigned int* GPIO0_SWPORT_DDR_H; //输入输出设置

static int board_rk3568_led_init(int io){
    uint32_t val;

    //寄存器地址映射
    PMU_GRF_GPIO0C_IOMUX_L = ioremap(PMU_GRF_BASE+0x0010, 4);
    PMU_GRF_GPIO0C_DS_0 = ioremap(PMU_GRF_BASE+0x0090, 4);
    GPIO0_SWPORT_DR_H = ioremap(GPIO0_BASE+0x0004, 4);
    GPIO0_SWPORT_DDR_H = ioremap(GPIO0_BASE+0x000c, 4);

    //引脚复用设置为普通GPIO口
    val = readl(PMU_GRF_GPIO0C_IOMUX_L);
    val &= ~(0x07 << 0);
    val |= ((0x07 << 16) | (0x00 << 0));
    writel(val, PMU_GRF_GPIO0C_IOMUX_L);

    //将GPIO0_C0的引脚驱动能力设置为5级
    val = readl(PMU_GRF_GPIO0C_DS_0);
    val &= ~(0x3f << 0);
    val |= ((0x3f << 16) | (0x3f << 0));
    writel(val, PMU_GRF_GPIO0C_DS_0);

    //设置GPIO0_C0为输出模式
    val = readl(GPIO0_SWPORT_DDR_H);
    val &= ~(0x01 << 0);
    val |= ((0x01 << 16) | (0x01 << 0));
    writel(val, GPIO0_SWPORT_DDR_H);

    return 0;
}

static int board_rk3568_led_write(int io, int status){
    uint32_t val;
    if(status == 1){
        val = readl(GPIO0_SWPORT_DR_H);
        val &= ~(0x01 << 0);
        val |= ((0x01 << 16) | (0x01 << 0));
        writel(val, GPIO0_SWPORT_DR_H);
        printk("led置1\n");
    }
    else if(status == 0){
        val = readl(GPIO0_SWPORT_DR_H);
        val &= ~(0x01 << 0);
        val |= ((0x01 << 16) | (0x00 << 0));
        writel(val, GPIO0_SWPORT_DR_H);
        printk("led置0\n");
    }
    else{
        //输入的status非法
        return -1;
    }

    return 0;
}

static int board_rk3568_led_read(int io){
    int status = 0; //问题

    *GPIO0_SWPORT_DR_H &= ~(0x00010001);
    *GPIO0_SWPORT_DR_H |= 0x00010000;

    if((*GPIO0_SWPORT_DR_H&0x00000001) == 0x00000001){
        status = 1;
    }
    else if((*GPIO0_SWPORT_DR_H&0x00000001) == 0x00000000){
        status = 0;
    }

    return status;
}

//注册板级led操作
static struct led_operations board_rk3568_led_opr = {
    .init = board_rk3568_led_init, 
    .write = board_rk3568_led_write,
    .read = board_rk3568_led_read
};

struct led_operations* get_board_led_opr(void){
    return &board_rk3568_led_opr;
}

//查找设备资源然后创建设备文件
static rk3568_gpio_probe(struct platform_device *pdev){
    struct resource * res;
    int i = 0;
    while (1){
        res = platform_get_resource(pdev, IORESOURCE_MEM, i++);
        if(!res)
            break;

        g_ledpins[g_ledcnt] = res->start;
        led_class_create_device(g_ledcnt);
        g_ledcnt++;
    }
    return 0;
}

//查找设备资源然后注销设备文件
static rk3568_gpio_remove(struct platform_device *pdev){
    struct resource * res;
    int i = 0;
    while (1){
        res = platform_get_resource(pdev, IORESOURCE_MEM, i);
        if(!res)
            break;

        led_class_destroy_device(i);
        i++;
        g_ledcnt--;
    }
    return 0;
}

static struct platform_driver rk3568_gpio_driver = {
    .probe      = rk3568_gpio_probe,
    .remove     = rk3568_gpio_remove,
    .driver     = {
        .name   = "led",
    },
};

static int __init rk3568_gpio_drv_init(void)
{
    int err;
    
    err = platform_driver_register(&rk3568_gpio_driver); 
    register_led_operations(&board_rk3568_led_opr);
    
    return 0;
}

static void __exit rk3568_gpio_drv_exit(void)
{
    platform_driver_unregister(&rk3568_gpio_driver);
}

module_init(rk3568_gpio_drv_init);
module_exit(rk3568_gpio_drv_exit);

MODULE_LICENSE("GPL");