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

#include "led_resource.h"

static void led_dev_release(struct device* dev){

}

//定义设备资源，这里led连接到GPIO0_C0引脚，因此这里填写一个资源即可
static struct resource resources[] = {
    {
        .start = GROUP_PIN(0, RK_PC0),
        .flags = IORESOURCE_MEM, //IORESOURCE_IO通常只x86上的端口IO，因此这里不填写IORESOURCE_IO
        .name = "led_pin",
    },
};

//设备信息，该设备上可操作的资源有哪些
static struct platform_device board_rk3568_led_dev = {
    .name = "led",
    .num_resources = ARRAY_SIZE(resources),
    .resource = resources,
    .dev = {
        .release = led_dev_release,
    }
};

//注册设备
static int __init led_dev_init(void)
{
    int err;
    
    err = platform_device_register(&board_rk3568_led_dev);   
    
    return 0;
}

//卸载设备
static void __exit led_dev_exit(void)
{
    platform_device_unregister(&board_rk3568_led_dev);
}

module_init(led_dev_init);
module_exit(led_dev_exit);

MODULE_LICENSE("GPL");