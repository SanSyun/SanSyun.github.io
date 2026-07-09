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

#include "led_opr.h"


//1、确定主设备号
static int major = 0;
static struct class* led_class;
struct led_operations* p_led_opr;

//创建设备
void led_class_create_device(int minor){
    device_create(led_class, NULL, MKDEV(major, minor), NULL, "led%d", minor);
}
//卸载设备
void led_class_destroy_device(int minor){
    device_destroy(led_class, MKDEV(major, minor));
}
//注册led操作
void register_led_operations(struct led_operations *opr){
    p_led_opr = opr;
}

//为了把这三个符号到处到其他.c文件里面使用，因此需要使用EXPORT_SYMBOL把这 3 个函数从当前模块导出去
EXPORT_SYMBOL(led_class_create_device);
EXPORT_SYMBOL(led_class_destroy_device);
EXPORT_SYMBOL(register_led_operations);

//2、定义file_operation结构体与相关的操作函数
//打开设备
static int led_drv_open(struct inode *inode, struct file *file){
    int minor = iminor(inode);

    p_led_opr->init(minor);
    printk("led initial success!\n");
    return 0;
}

//关闭设备
static int led_drv_close(struct inode *inode, struct file *file){
    printk("led close success!\n");
    return 0;
}

//从内核读取数据
static ssize_t led_drv_read(struct file *file, char __user *buf, size_t len, loff_t *offset){
    int err;
    int status = 0;

    //获取子设备号
    struct inode *inode = file_inode(file);
    int minor = iminor(inode);

    //从内核读取led状态
    status = p_led_opr->read(minor);

    err = copy_to_user(buf, &status, sizeof(status));
    if (err != 0) {
        return -EFAULT;
    }
    return 0;
}

//向内核设备写入数据
static ssize_t led_drv_write(struct file *file, const char __user *buf, size_t len, loff_t *offset){
    int err;
    int status = 0;

    //获取子设备号
    struct inode *inode = file_inode(file);
    int minor = iminor(inode);
    
    err = copy_from_user(&status, buf, sizeof(status));
    if (err != 0) {
        return -EFAULT;
    }
    
    //根据子设备号控制对应的led
    p_led_opr->write(minor, status);
    printk("内核收到的status = %d\n", status);
    return 0;
}

static struct file_operations led_drv = {
    .owner = THIS_MODULE,
    .open = led_drv_open,
    .release = led_drv_close,
    .read = led_drv_read,
    .write = led_drv_write,
};

//3、定义入口函数注册驱动程序
static int __init led_init(void){
    int err;

    //注册驱动
    major = register_chrdev(0, "led", &led_drv); // /dev/led

    led_class = class_create(THIS_MODULE, "led_class");
    err = PTR_ERR(led_class);
    if(IS_ERR(led_class)){
        printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
        unregister_chrdev(major, "led");
        return -1;
    }

    return 0;
}

//4、定义出口函数卸载驱动程序
static void __exit led_exit(void){
    //卸载设备
    class_destroy(led_class);
    unregister_chrdev(major, "led");
}

module_init(led_init);
module_exit(led_exit);

MODULE_LICENSE("GPL");