---
title: "Linux驱动开发：总线设备驱动框架下的GPIO驱动"
description: "基于 RK3568 板载 LED 实验，把字符设备、platform 总线、platform_device、platform_driver 和 GPIO 寄存器控制串起来，理解总线设备驱动框架下的 GPIO 驱动编写思路。"
date: "2026-07-09"
draft: false
featured: false
tags: ["RK3568", "Linux Driver", "Platform Bus", "GPIO", "Kernel"]
readingTime: "25 分钟"
category: "驱动开发"
---

# 一、为什么要引入总线设备驱动框架

前面写 GPIO 驱动时，已经可以通过字符设备接口控制 RK3568 开发板上的 LED。应用层打开 `/dev/led`，再通过 `read` 或 `write` 进入内核驱动，最后由驱动程序去配置 GPIO 寄存器。

这条链路可以跑通，但还有一个问题：设备资源和驱动操作耦合得比较紧。

比如这次控制的是板载 LED，LED 接在 `GPIO0_C0`。如果以后换成另一个 LED，或者同一个驱动要支持多个 GPIO 设备，就不应该每次都去大改驱动主体。更合理的方式是：

- 设备部分只描述“我有哪些硬件资源”
- 驱动部分只描述“我应该怎样操作这些资源”
- 总线负责把合适的设备和合适的驱动匹配到一起

这就是 Linux 设备驱动模型里非常核心的一种思路：总线、设备、驱动分离。

对于 RK3568 这类 SoC 上的外设，很多设备并不像 USB、PCI 那样可以被硬件自动枚举出来，所以 Linux 提供了 `platform` 总线来管理这类“平台设备”。本文的 LED 驱动就是把原来的 GPIO 字符设备驱动，放到 `platform_device` 和 `platform_driver` 的框架下重新整理一遍。

# 二、先抓住三个核心对象

总线设备驱动框架刚开始看会比较绕，但先记住三个对象就够了：

| 对象 | 在本文中的文件 | 作用 |
| :-- | :-- | :-- |
| `platform_bus` | 内核已有 | 负责匹配设备和驱动 |
| `platform_device` | `board_rk3568_led.c` | 描述 LED 用到了哪些资源 |
| `platform_driver` | `led_dev.c` | 描述如何初始化、读写和释放这些资源 |

可以把这个框架想成一次“相亲”：

- `platform_device` 说：我这里有一个名字叫 `led` 的设备，资源是 `GPIO0_C0`
- `platform_driver` 说：我能驱动名字叫 `led` 的设备
- platform 总线发现两边名字匹配，就调用驱动里的 `probe`

在本文代码里，匹配条件主要来自 `.name = "led"`：

```c
static struct platform_device board_rk3568_led_dev = {
    .name = "led",
    .num_resources = ARRAY_SIZE(resources),
    .resource = resources,
    .dev = {
        .release = led_dev_release,
    }
};
```

```c
static struct platform_driver rk3568_gpio_driver = {
    .probe  = rk3568_gpio_probe,
    .remove = rk3568_gpio_remove,
    .driver = {
        .name = "led",
    },
};
```

只要设备名和驱动名匹配，`platform_driver` 里的 `probe` 函数就会被调用。

# 三、这次驱动的整体调用链

这次工程仍然保留了字符设备接口，因为应用程序最终还是习惯通过 `/dev/led` 来控制 LED。只是底层硬件资源不再直接写死在字符设备层，而是由 platform 设备和 platform 驱动来管理。

整体链路可以这样理解：

```text
应用程序
  |
  | open/read/write /dev/led
  v
字符设备层 led_drv.c
  |
  | p_led_opr->init/read/write(minor)
  v
GPIO 操作层 led_dev.c
  |
  | ioremap/readl/writel
  v
RK3568 GPIO0_C0 寄存器
```

![应用程序到 RK3568 GPIO 寄存器的调用链](/images/notes/linux_platform/1783560808373.png)

同时，设备创建流程是另一条线：

```text
board_rk3568_led.c 注册 platform_device
  |
  v
platform 总线匹配 .name = "led"
  |
  v
led_dev.c 的 probe 被调用
  |
  v
platform_get_resource 读取 GPIO 资源
  |
  v
led_class_create_device 创建设备节点
```

![platform 设备和驱动匹配后的设备节点创建流程](/images/notes/linux_platform/1783560997153.png)

这样拆开之后，`led_drv.c` 更像一个通用的字符设备壳，`board_rk3568_led.c` 负责声明板级资源，`led_dev.c` 负责真正操作 RK3568 的 GPIO。

# 四、用 `led_resource.h` 描述 GPIO 资源

本文代码里新增了一个 `led_resource.h`，它主要做两件事：

1. 定义 RK3568 GPIO 引脚编号
2. 定义寄存器基地址和引脚编码宏

```c
#define RK_PA0  0
#define RK_PA1  1
/* ... */
#define RK_PC0  16
#define RK_PC1  17
/* ... */
#define RK_PD7  31

#define GROUP(x)        (((x) >> 16) & 0xFFFF)
#define PIN(x)          ((x) & 0xFFFF)
#define GROUP_PIN(g, p) (((g) << 16) | (p))

#define PMU_GRF_BASE    0xFDC20000
#define GPIO0_BASE      0xFDD60000
```

这里的 `GROUP_PIN(g, p)` 很关键。它把 GPIO 组号和组内引脚编号打包到一个整数里。

例如：

```c
GROUP_PIN(0, RK_PC0)
```

表示 `GPIO0_C0`。

这样写的好处是，设备资源里不再直接塞一堆零散信息，而是用一个统一格式描述“第几组 GPIO、哪一个引脚”。后续如果要扩展 `GPIO3_B2`，就可以写成：

```c
GROUP_PIN(3, RK_PB2)
```

从回看角度来说，这一层的价值是：先把硬件资源抽象成统一编号，再让驱动去解析这个编号。

# 五、`board_rk3568_led.c`：设备只负责描述资源

`board_rk3568_led.c` 对应的是 `platform_device`。它不应该关心寄存器怎么配置，也不应该关心 LED 怎么点亮。它只需要告诉内核：我这里有一个 LED 设备，这个 LED 用到了哪些资源。

本文中 LED 接在 `GPIO0_C0`，所以资源数组里只放了一个资源：

```c
static struct resource resources[] = {
    {
        .start = GROUP_PIN(0, RK_PC0),
        .flags = IORESOURCE_IRQ,
        .name = "led_pin",
    },
};
```

这里虽然使用了 `IORESOURCE_IRQ`，但它在这个学习例程里更像是借用了 `struct resource` 的标准格式来保存 GPIO 引脚编号。也就是说，重点不是这个资源真的表示“中断”，而是设备端和驱动端使用同一种资源类型，驱动就能通过 `platform_get_resource` 把它取出来。

真实项目中，GPIO、寄存器、中断、时钟等资源通常会通过设备树描述，再由内核解析出来。这里先用手动注册 `platform_device` 的方式学习框架，更容易看清匹配流程。

接着定义 `platform_device`：

```c
static struct platform_device board_rk3568_led_dev = {
    .name = "led",
    .num_resources = ARRAY_SIZE(resources),
    .resource = resources,
    .dev = {
        .release = led_dev_release,
    }
};
```

这里最重要的是两点：

- `.name = "led"` 用来和 `platform_driver` 匹配
- `.resource = resources` 用来把设备资源交给驱动

模块加载时注册设备：

```c
static int __init led_dev_init(void)
{
    int err;

    err = platform_device_register(&board_rk3568_led_dev);

    return 0;
}
```

注意这里函数名叫 `led_dev_init`，但从当前源码文件来看，它是在 `board_rk3568_led.c` 中注册 `platform_device`。回看时不要只根据函数名判断职责，还是要看它最终调用的是 `platform_device_register` 还是 `platform_driver_register`。

模块卸载时注销设备：

```c
static void __exit led_dev_exit(void)
{
    platform_device_unregister(&board_rk3568_led_dev);
}
```

这一部分可以用一句话总结：`board_rk3568_led.c` 只回答“板子上有什么设备、设备占用了什么资源”。

# 六、`led_dev.c`：驱动负责匹配和操作资源

`led_dev.c` 做的事情更多，它既注册了 `platform_driver`，也实现了具体的 GPIO 初始化和读写。

## 1. 注册 platform 驱动

驱动结构体如下：

```c
static struct platform_driver rk3568_gpio_driver = {
    .probe  = rk3568_gpio_probe,
    .remove = rk3568_gpio_remove,
    .driver = {
        .name = "led",
    },
};
```

模块入口中注册这个驱动：

```c
static int __init rk3568_gpio_drv_init(void)
{
    int err;

    err = platform_driver_register(&rk3568_gpio_driver);
    register_led_operations(&board_rk3568_led_opr);

    return 0;
}
```

这里有两个动作：

1. `platform_driver_register` 把驱动挂到 platform 总线上
2. `register_led_operations` 把板级 GPIO 操作注册给字符设备层

第二个动作很重要。字符设备层并不直接知道 RK3568 的寄存器怎么配，它只保存一个 `struct led_operations *p_led_opr`。当应用层写 `/dev/led` 时，字符设备层通过这个函数指针调用底层 GPIO 操作。

## 2. `probe`：设备和驱动匹配后的入口

一旦 `platform_device` 和 `platform_driver` 匹配成功，内核就会调用 `probe`。

```c
static int rk3568_gpio_probe(struct platform_device *pdev)
{
    struct resource *res;
    int i = 0;

    while (1) {
        res = platform_get_resource(pdev, IORESOURCE_IRQ, i++);
        if (!res)
            break;

        g_ledpins[g_ledcnt] = res->start;
        led_class_create_device(g_ledcnt);
        g_ledcnt++;
    }

    return 0;
}
```

这段代码的核心是 `platform_get_resource`。

它的作用是从 `platform_device` 里取出第 `i` 个资源。本文中只有一个 LED 资源，所以最终会取到：

```c
res->start = GROUP_PIN(0, RK_PC0)
```

然后驱动把这个资源保存到 `g_ledpins` 数组，并调用：

```c
led_class_create_device(g_ledcnt);
```

创建设备节点。

也就是说，`probe` 不是普通的初始化函数，它是“设备已经匹配到了驱动，现在可以开始绑定资源并创建对应软件对象”的入口。

## 3. `remove`：设备或驱动卸载时释放资源

对应的释放函数是 `remove`：

```c
static int rk3568_gpio_remove(struct platform_device *pdev)
{
    struct resource *res;
    int i = 0;

    while (1) {
        res = platform_get_resource(pdev, IORESOURCE_IRQ, i);
        if (!res)
            break;

        led_class_destroy_device(i);
        i++;
        g_ledcnt--;
    }

    return 0;
}
```

`remove` 可以理解为 `probe` 的反向过程：

- `probe` 里创建设备节点
- `remove` 里销毁设备节点

学习驱动时可以养成一个习惯：每写一个申请动作，都要想清楚对应的释放动作在哪里。

# 七、GPIO 初始化和读写逻辑

真正操作 RK3568 GPIO 的部分，仍然和上一篇 GPIO 驱动文章里的思路一致：先通过 `ioremap` 映射寄存器，再用 `readl` 和 `writel` 修改寄存器位。

本文涉及的寄存器仍然是：

| 寄存器 | 作用 |
| :-- | :-- |
| `PMU_GRF_GPIO0C_IOMUX_L` | 设置 `GPIO0_C0` 的引脚复用 |
| `PMU_GRF_GPIO0C_DS_0` | 设置 `GPIO0_C0` 的驱动能力 |
| `GPIO0_SWPORT_DDR_H` | 设置 `GPIO0_C0` 为输入或输出 |
| `GPIO0_SWPORT_DR_H` | 设置或读取 `GPIO0_C0` 的电平 |

初始化函数如下：

```c
static int board_rk3568_led_init(int io)
{
    uint32_t val;

    PMU_GRF_GPIO0C_IOMUX_L = ioremap(PMU_GRF_BASE + 0x0010, 4);
    PMU_GRF_GPIO0C_DS_0    = ioremap(PMU_GRF_BASE + 0x0090, 4);
    GPIO0_SWPORT_DR_H      = ioremap(GPIO0_BASE + 0x0004, 4);
    GPIO0_SWPORT_DDR_H     = ioremap(GPIO0_BASE + 0x000c, 4);

    val = readl(PMU_GRF_GPIO0C_IOMUX_L);
    val &= ~(0x07 << 0);
    val |= ((0x07 << 16) | (0x00 << 0));
    writel(val, PMU_GRF_GPIO0C_IOMUX_L);

    val = readl(PMU_GRF_GPIO0C_DS_0);
    val &= ~(0x3f << 0);
    val |= ((0x3f << 16) | (0x3f << 0));
    writel(val, PMU_GRF_GPIO0C_DS_0);

    val = readl(GPIO0_SWPORT_DDR_H);
    val &= ~(0x01 << 0);
    val |= ((0x01 << 16) | (0x01 << 0));
    writel(val, GPIO0_SWPORT_DDR_H);

    return 0;
}
```

这里要特别注意 RK3568 部分寄存器的写法：高 16 位通常是写使能位，低 16 位才是实际数据位。

例如设置方向寄存器时：

```c
val |= ((0x01 << 16) | (0x01 << 0));
```

含义是：

- `0x01 << 16`：允许修改低 16 位中的 bit0
- `0x01 << 0`：把 bit0 设置为 1，也就是输出模式

控制 LED 亮灭时也是类似思路：

```c
static int board_rk3568_led_write(int io, int status)
{
    uint32_t val;

    if (status == 1) {
        val = readl(GPIO0_SWPORT_DR_H);
        val &= ~(0x01 << 0);
        val |= ((0x01 << 16) | (0x01 << 0));
        writel(val, GPIO0_SWPORT_DR_H);
        printk("led置1\n");
    } else if (status == 0) {
        val = readl(GPIO0_SWPORT_DR_H);
        val &= ~(0x01 << 0);
        val |= ((0x01 << 16) | (0x00 << 0));
        writel(val, GPIO0_SWPORT_DR_H);
        printk("led置0\n");
    } else {
        return -1;
    }

    return 0;
}
```

从现象上看，`status = 1` 输出高电平，LED 点亮；`status = 0` 输出低电平，LED 熄灭。

# 八、`led_drv.c`：字符设备层保持对应用层友好

总线设备驱动框架解决的是内核里设备和驱动的组织问题，但用户态并不需要直接感知这些变化。

对应用层来说，它仍然只需要操作 `/dev/led`：

```text
open("/dev/led")
write(fd, &status, sizeof(status))
read(fd, &status, sizeof(status))
close(fd)
```

所以 `led_drv.c` 继续负责注册字符设备：

```c
major = register_chrdev(0, "led", &led_drv);

led_class = class_create(THIS_MODULE, "led_class");
```

为了让 platform 驱动在 `probe` 里创建设备节点，字符设备层导出了几个函数：

```c
void led_class_create_device(int minor)
{
    device_create(led_class, NULL, MKDEV(major, minor), NULL, "led%d", minor);
}

void led_class_destroy_device(int minor)
{
    device_destroy(led_class, MKDEV(major, minor));
}

void register_led_operations(struct led_operations *opr)
{
    p_led_opr = opr;
}
```

然后通过 `EXPORT_SYMBOL` 导出：

```c
EXPORT_SYMBOL(led_class_create_device);
EXPORT_SYMBOL(led_class_destroy_device);
EXPORT_SYMBOL(register_led_operations);
```

这样 `led_dev.c` 就能调用这些函数。

字符设备的 `open` 中，通过次设备号区分当前打开的是哪个 LED：

```c
static int led_drv_open(struct inode *inode, struct file *file)
{
    int minor = iminor(inode);

    p_led_opr->init(minor);
    printk("led initial success!\n");
    return 0;
}
```

`write` 中也是先取出次设备号，再调用底层操作：

```c
static ssize_t led_drv_write(struct file *file,
                             const char __user *buf,
                             size_t len,
                             loff_t *offset)
{
    int err;
    int status = 0;
    struct inode *inode = file_inode(file);
    int minor = iminor(inode);

    err = copy_from_user(&status, buf, sizeof(status));
    if (err != 0) {
        return -EFAULT;
    }

    p_led_opr->write(minor, status);
    printk("内核收到的status = %d\n", status);

    return 0;
}
```

这里的 `minor` 很重要。当前例程只有一个 LED，所以最终会创建 `/dev/led0`；但如果后面扩展多个 LED，就可以用不同的次设备号对应不同的资源。

# 九、Makefile 如何生成三个独立模块

这次工程不是把所有源码链接成一个 `.ko`，而是生成三个独立的内核模块：

```makefile
obj-m := led_drv.o
obj-m += led_dev.o
obj-m += board_rk3568_led.o
```

这几行的意思是：

- `led_drv.c` 编译成 `led_drv.ko`
- `led_dev.c` 编译成 `led_dev.ko`
- `board_rk3568_led.c` 编译成 `board_rk3568_led.ko`

完整编译规则如下：

```makefile
KERNELDIR := /root/rk3568/rk3568_linux_sdk/kernel
PWD := $(shell pwd)

ARCH ?= arm64
CROSS_COMPILE ?= aarch64-buildroot-linux-gnu-
TOOLCHAIN_DIR ?= /opt/atk-dlrk3568-5_10_sdk-toolchain/bin

obj-m := led_drv.o
obj-m += led_dev.o
obj-m += board_rk3568_led.o

all:
	PATH=$(TOOLCHAIN_DIR):$$PATH $(MAKE) -C $(KERNELDIR) \
		ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) \
		M=$(PWD) modules

clean:
	PATH=$(TOOLCHAIN_DIR):$$PATH $(MAKE) -C $(KERNELDIR) \
		ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) \
		M=$(PWD) clean
```

因为 `led_dev.ko` 需要调用 `led_drv.ko` 导出的函数，所以加载顺序也要注意：

```text
insmod led_drv.ko
insmod led_dev.ko
insmod board_rk3568_led.ko
```

对应关系是：

- `led_drv.ko` 先注册字符设备和 class，并导出 `led_class_create_device` 等函数
- `led_dev.ko` 再注册 `platform_driver`，并把 GPIO 操作注册给字符设备层
- `board_rk3568_led.ko` 最后注册 `platform_device`，触发 platform 总线匹配并进入 `probe`

卸载时按相反顺序更清晰：

```text
rmmod board_rk3568_led
rmmod led_dev
rmmod led_drv
```

这也是这版代码比上一版更适合观察 platform 框架的地方：三个模块分别代表“字符设备接口”“platform 驱动”“platform 设备”，职责边界更明显。

# 十、这版代码最值得记住的地方

这篇笔记的重点不只是“点亮一个 LED”，而是理解驱动框架的组织方式。

## 1. 设备和驱动分离

原来的写法更像是：

```text
驱动代码里直接知道 LED 在 GPIO0_C0
```

总线设备驱动框架下更像是：

```text
board_rk3568_led.c 描述 LED 在 GPIO0_C0
led_dev.c 负责操作 RK3568 GPIO
platform 总线负责匹配两者
```

这就是从“能跑”走向“可扩展”的关键一步。

## 2. `probe` 是理解 platform 驱动的入口

看到 `platform_driver` 时，不要先被结构体吓住，先盯住 `probe`。

`probe` 里一般会做这些事情：

- 读取设备资源
- 初始化硬件或软件对象
- 注册字符设备、输入设备、网络设备等上层接口
- 保存私有数据，方便后续操作使用

本文中 `probe` 做的事情比较简单：读取资源并创建设备节点。

## 3. 字符设备层和 platform 驱动层解决的问题不同

字符设备层解决的是用户态接口问题：

```text
应用程序怎样通过文件接口访问设备？
```

platform 驱动层解决的是内核设备模型问题：

```text
内核怎样描述设备资源，并把设备和驱动匹配起来？
```

两者不是互相替代，而是上下配合。

## 4. 当前例程仍然是学习版

这份代码适合理解框架，但还有几个后续可以继续完善的点：

- `read` / `write` 成功后更规范的返回值是实际读写的字节数，当前源码仍然返回 `0`
- `IORESOURCE_IRQ` 在这里是借用 `struct resource` 来传 GPIO 编号，语义上并不是真正的中断资源
- `led_dev.c` 中的 `board_rk3568_led_init(int io)` 当前还没有真正根据 `io` 解析不同 GPIO
- `board_rk3568_led_init` 每次 `open` 都会执行 `ioremap`，后续可以考虑在设备初始化阶段集中映射，并在退出时 `iounmap`
- 实际项目中更推荐用设备树描述板级资源，而不是手动注册 `platform_device`

这些问题并不影响用它学习 platform 框架，但后面写更完整驱动时要逐步补上。

# 十一、小结

本文把 RK3568 板载 LED 驱动放到了 platform 总线设备驱动框架下。回看时可以抓住这几句话：

1. `platform_device` 描述设备资源，本文中就是 `GPIO0_C0`
2. `platform_driver` 描述驱动能力，匹配成功后从 `probe` 开始工作
3. 字符设备层继续提供 `/dev/led`，让应用层用文件接口控制 LED
4. GPIO 控制本质上仍然是 `ioremap`、`readl`、`writel` 操作寄存器
5. 总线设备驱动框架的价值，是把资源描述、驱动操作和用户接口拆开，让代码更容易扩展

如果把前面几篇串起来看，学习路径大概是：

```text
字符设备驱动
  -> GPIO 寄存器控制
  -> 分层与资源分离
  -> platform 总线设备驱动框架
  -> 设备树驱动
```

这一篇就是从“手写 GPIO 字符设备驱动”过渡到“理解 Linux 设备驱动模型”的中间台阶。
