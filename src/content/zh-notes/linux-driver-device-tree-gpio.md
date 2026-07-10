---
title: "Linux驱动开发：设备树与总线设备框架下的GPIO驱动"
description: "基于 RK3568 板载 LED 实验，把设备树、pinctrl、GPIO 子系统与 platform 驱动串起来，梳理从 DTS 描述硬件到 /dev/ledN 控制设备的完整流程。"
date: "2026-07-10"
draft: false
featured: false
tags: ["RK3568", "Linux Driver", "Device Tree", "Platform Bus", "GPIO", "pinctrl", "Kernel"]
readingTime: "30 分钟"
category: "驱动开发"
---

# 一、设备树引入带来的差异

上一篇《Linux驱动开发：总线设备驱动框架下的GPIO驱动》已经把字符设备、`platform_device` 和 `platform_driver` 串了起来。

当时为了把总线、设备、驱动三者的关系看清楚，我专门写了一个 `board_rk3568_led.c`。这个文件使用 `struct resource` 描述 `GPIO0_C0`，再手动注册一个 `platform_device`：

```c
static struct resource resources[] = {
    {
        .start = GROUP_PIN(0, RK_PC0),
        .flags = IORESOURCE_IRQ,
        .name = "led_pin",
    },
};

static struct platform_device board_rk3568_led_dev = {
    .name = "led",
    .num_resources = ARRAY_SIZE(resources),
    .resource = resources,
};
```

这套写法适合学习 platform 框架，因为设备端和驱动端都由自己编写，匹配过程比较直观。但它还有几个明显的问题：

- 板级硬件信息仍然写在 C 文件中
- 更换 GPIO 时需要修改代码并重新编译模块
- 为了描述 GPIO，例程借用了 `IORESOURCE_IRQ`，语义并不准确
- 驱动还要自己操作 IOMUX、方向和数据寄存器
- 换一块板子时，板级差异不容易独立维护

这一次，我把 LED 的硬件信息移入设备树，并使用内核的 pinctrl 与 GPIO 子系统管理引脚。驱动代码不再关心 GPIO 寄存器地址，也不再手动注册 `platform_device`。

改造前后的关系可以对比为：

| 对比项 | 上一版 | 这一版 |
| :-- | :-- | :-- |
| 设备来源 | `board_rk3568_led.c` 手动注册 | 内核解析 DTB 后自动创建 |
| 匹配依据 | 设备名和驱动名都是 `led` | `compatible = "sc,rk3568-led"` |
| GPIO 描述 | `struct resource` | `led-gpios` 属性 |
| 引脚复用 | 驱动直接操作 IOMUX 寄存器 | pinctrl 子系统根据设备树配置 |
| GPIO 读写 | `ioremap`、`readl`、`writel` | `gpio_get_value`、`gpio_set_value` |
| 模块数量 | 3 个：字符设备、platform 驱动、platform 设备 | 2 个：字符设备、platform 驱动 |

最大的变化不是代码少了几行，而是硬件描述和驱动逻辑真正分开了：

```text
设备树：板子上有什么、设备用了哪些引脚
内核子系统：怎样把引脚配置成 GPIO，并管理 GPIO 资源
驱动程序：设备匹配后怎样申请、使用和释放资源
字符设备：怎样向用户态提供 /dev/ledN 接口
```

以后 LED 换了引脚，通常只需要修改设备树。只要 `compatible` 和属性格式保持不变，驱动主体就可以继续复用。

# 二、先导知识点

## 1. Linux 设备树

### 为什么需要设备树

设备树用于描述不能由硬件自动枚举的设备。RK3568 上的 GPIO、I2C、SPI、UART 等片上外设，通常都需要由软件告诉内核：

- 设备是否存在
- 设备位于哪里
- 使用了哪些 GPIO、中断、时钟和寄存器
- 引脚应该复用成什么功能
- 哪个驱动可以与它匹配

设备树只负责描述硬件，不应该在里面编写“点亮 LED”之类的操作流程。

可以把它理解成一张硬件清单：设备树说清楚“有什么”，驱动负责“怎么用”。

### DTS、DTSI、DTB 与 DTC

这几个名称容易混在一起，可以先记住下面的关系：

| 名称 | 含义 | 作用 |
| :-- | :-- | :-- |
| DTS | Device Tree Source | 某块具体开发板的设备树源码 |
| DTSI | Device Tree Source Include | 可被多个 DTS 复用的公共描述 |
| DTB | Device Tree Blob | 编译后的二进制设备树 |
| DTC | Device Tree Compiler | 在 DTS/DTSI 与 DTB 之间转换的编译器 |

编译方向是：

```text
DTS + DTSI
    ↓ DTC
DTB
    ↓ BootLoader 传给内核
Linux 内核解析设备树
    ↓
生成 platform_device，并与驱动匹配
```

在 RK3568 SDK 中修改板级 DTS 后，需要重新编译设备树，并确认新的 DTB 或包含它的 `boot.img` 已经真正烧写到开发板。只修改源码但没有更新启动介质，板子运行的仍然是旧设备树。

### DTS 常用语法

本文使用的设备节点可以写成：

![RK3568 LED 设备节点与 pinctrl 配置](/images/notes/linux_device_tree/device-tree-led-node.png)

```dts
/ {
    gpioled {
        compatible = "sc,rk3568-led";
        pinctrl-names = "default";
        pinctrl-0 = <&led_gpio>;
        led-gpios = <&gpio0 RK_PC0 GPIO_ACTIVE_HIGH>;
        status = "okay";
    };
};
```

先看几个最常用的语法单位。

#### 节点

```dts
gpioled {
    /* 属性写在这里 */
};
```

`gpioled` 是节点名。节点用一对大括号包围，结尾要有分号。

#### 字符串属性

```dts
compatible = "sc,rk3568-led";
status = "okay";
```

字符串使用双引号。`compatible` 用于匹配驱动，`status = "okay"` 表示启用节点。

#### cell 数组

```dts
led-gpios = <&gpio0 RK_PC0 GPIO_ACTIVE_HIGH>;
```

尖括号内是一组 cell。cell 之间使用空格分隔，不能像 C 函数参数那样加入逗号。

这里三个参数分别表示：

- `&gpio0`：使用 GPIO0 控制器
- `RK_PC0`：使用控制器中的 C0 引脚
- `GPIO_ACTIVE_HIGH`：高电平为有效状态

#### label 与引用

```dts
led_gpio: led-pin {
    /* ... */
};
```

冒号前的 `led_gpio` 是 label，其他位置可以通过 `&led_gpio` 引用这个节点。

```dts
pinctrl-0 = <&led_gpio>;
```

`&pinctrl` 也是引用，表示对已经存在的 pinctrl 节点进行追加或修改：

```dts
&pinctrl {
    led-gpios {
        led_gpio: led-pin {
            rockchip,pins =
                <0 RK_PC0 RK_FUNC_GPIO &pcfg_pull_none>;
        };
    };
};
```

### `compatible` 怎样触发驱动匹配

设备树中写的是：

```dts
compatible = "sc,rk3568-led";
```

驱动中提供对应的匹配表：

```c
static const struct of_device_id led_of_match[] = {
    { .compatible = "sc,rk3568-led" },
    { }
};

MODULE_DEVICE_TABLE(of, led_of_match);
```

再把匹配表交给 `platform_driver`：

```c
static struct platform_driver rk3568_gpio_driver = {
    .probe  = rk3568_gpio_probe,
    .remove = rk3568_gpio_remove,
    .driver = {
        .name = "led",
        .of_match_table = led_of_match,
    },
};
```

内核解析 DTB 后，会为这个设备树节点创建一个 `platform_device`。当它发现设备节点的 `compatible` 与 `led_of_match` 中的字符串完全相同，就会调用 `rk3568_gpio_probe()`。

这里需要记住：这一版的主要匹配依据是 `compatible`，不是 `led-gpios`，也不是字符设备名 `/dev/led0`。

## 2. pinctrl 子系统

一颗 SoC 的同一个物理引脚经常可以承担多种功能。例如某个引脚既可以作为普通 GPIO，也可以复用为 UART、SPI 或其他外设信号。

pinctrl 子系统主要负责：

- 选择引脚复用功能
- 配置上拉、下拉或无上下拉
- 配置驱动能力等电气属性
- 在默认、休眠等不同状态之间切换引脚配置

本文的设备节点中有：

```dts
pinctrl-names = "default";
pinctrl-0 = <&led_gpio>;
```

它表示该设备有一个名为 `default` 的 pinctrl 状态，这个状态引用 `led_gpio` 配置。

具体配置如下：

```dts
&pinctrl {
    led-gpios {
        /omit-if-no-ref/
        led_gpio: led-pin {
            rockchip,pins =
                <0 RK_PC0 RK_FUNC_GPIO &pcfg_pull_none>;
        };
    };
};
```

`rockchip,pins` 中各参数的含义是：

| 参数 | 本文取值 | 含义 |
| :-- | :-- | :-- |
| bank | `0` | GPIO bank 0，即 GPIO0 |
| pin | `RK_PC0` | GPIO0 内部的 C0 引脚 |
| function | `RK_FUNC_GPIO` | 将引脚复用为普通 GPIO |
| config | `&pcfg_pull_none` | 不配置内部上拉或下拉 |

`/omit-if-no-ref/` 表示如果这个节点最终没有被任何地方引用，DTC 可以把它从生成结果中省略。本文通过 `pinctrl-0 = <&led_gpio>` 引用了它，所以配置会被保留。

## 3. GPIO 子系统

pinctrl 把引脚配置成 GPIO 功能后，驱动还需要通过 GPIO 子系统申请和操作它。

本文源码使用的是传统整数 GPIO API，主要函数如下：

| API | 作用 |
| :-- | :-- |
| `gpio_is_valid(gpio)` | 判断解析出的 GPIO 编号是否有效 |
| `gpio_request(gpio, label)` | 申请 GPIO，防止其他驱动重复占用 |
| `gpio_direction_output(gpio, value)` | 设置为输出并给出初始电平 |
| `gpio_set_value(gpio, value)` | 输出高低电平 |
| `gpio_get_value(gpio)` | 读取当前电平 |
| `gpio_free(gpio)` | 释放 GPIO |

使用顺序可以记为：

```text
获取编号 → 检查 → 申请 → 设置方向 → 读写 → 释放
```

资源申请与释放要成对出现。`gpio_request()` 成功后，卸载驱动时就要调用 `gpio_free()`。

### 与 GPIO 相关的 OF 函数

OF 是 Open Firmware 的缩写。在 Linux 驱动中，很多 `of_*` API 用于读取和解析设备树。

本文使用了三个关键点：

```c
struct device_node *np = pdev->dev.of_node;
```

`pdev->dev.of_node` 指向与当前 `platform_device` 对应的设备树节点。

```c
led_num = of_gpio_named_count(np, "led-gpios");
```

`of_gpio_named_count()` 统计指定属性中包含多少个 GPIO 描述项。

```c
gpio = of_get_named_gpio(np, "led-gpios", index);
```

`of_get_named_gpio()` 解析指定属性中的第 `index` 个 GPIO，并返回传统 GPIO API 可以使用的 Linux GPIO 编号。

设备树中写的是硬件描述：

```dts
<&gpio0 RK_PC0 GPIO_ACTIVE_HIGH>
```

驱动获取到的是一个整数 GPIO 编号。后续可以把它传给 `gpio_request()`、`gpio_direction_output()` 和 `gpio_set_value()`。

# 三、设备树引入改造流程

## 1. 删除手动注册的 platform_device

上一版工程会生成三个模块：

```makefile
obj-m := led_drv.o
obj-m += led_dev.o
obj-m += board_rk3568_led.o
```

引入设备树后，不再需要 `board_rk3568_led.c`，Makefile 只生成两个模块：

```makefile
obj-m := led_drv.o
obj-m += led_dev.o
```

新的职责划分是：

- `led_drv.ko`：注册字符设备和 class，提供 `/dev/ledN`
- `led_dev.ko`：注册 platform 驱动，解析设备树并操作 GPIO
- DTB：代替 `board_rk3568_led.ko` 描述硬件设备

## 2. 在 DTS 中添加 LED 设备节点

普通设备节点应放在根节点内部：

```dts
/ {
    gpioled {
        compatible = "sc,rk3568-led";
        pinctrl-names = "default";
        pinctrl-0 = <&led_gpio>;
        led-gpios = <&gpio0 RK_PC0 GPIO_ACTIVE_HIGH>;
        status = "okay";
    };
};
```

这一段完成了四件事：

1. 用 `compatible` 声明可以匹配哪个驱动
2. 用 `pinctrl-0` 关联默认引脚配置
3. 用 `led-gpios` 描述 LED 使用的 GPIO
4. 用 `status = "okay"` 启用设备

## 3. 添加 pinctrl 配置

`&pinctrl` 是对已有节点的引用，应放在根节点外部：

```dts
&pinctrl {
    led-gpios {
        /omit-if-no-ref/
        led_gpio: led-pin {
            rockchip,pins =
                <0 RK_PC0 RK_FUNC_GPIO &pcfg_pull_none>;
        };
    };
};
```

这一步把 `GPIO0_C0` 复用为 GPIO，并配置为无内部上下拉。驱动不再需要自己访问 `PMU_GRF_GPIO0C_IOMUX_L`。

## 4. 在驱动中添加设备树匹配表

```c
static const struct of_device_id led_of_match[] = {
    { .compatible = "sc,rk3568-led" },
    { }
};

MODULE_DEVICE_TABLE(of, led_of_match);
```

匹配表最后的空项不能省略，因为内核需要它作为数组结束标志。

然后在 `platform_driver` 中加入：

```c
.of_match_table = led_of_match,
```

到这里，链路就从上一版的“设备名匹配”变成了“设备树 compatible 匹配”。

## 5. 在 probe 中获取并申请 GPIO

新源码中的 `probe` 主要分为四步。

### 第一步：取得设备树节点

```c
struct device_node *np;

np = pdev->dev.of_node;
if (!np) {
    printk(KERN_ERR "failed to get of node\n");
    return -1;
}
```

如果驱动通过设备树匹配，`pdev->dev.of_node` 就指向对应的 `gpioled` 节点。

### 第二步：统计 LED 数量

```c
led_num = of_gpio_named_count(np, "led-gpios");
if (led_num <= 0) {
    printk(KERN_ERR "invalid led-gpios count: %d\n", led_num);
    return led_num < 0 ? led_num : -ENODEV;
}
```

当前设备树只有一个 GPIO，因此 `led_num` 为 1。后面把 `led-gpios` 扩展成多个条目时，这段代码仍然可以继续使用。

### 第三步：逐个解析、申请和初始化 GPIO

```c
for (i = 0; i < led_num; i++) {
    g_ledgpios[i] = of_get_named_gpio(np, "led-gpios", i);
    if (!gpio_is_valid(g_ledgpios[i])) {
        printk(KERN_ERR "dev_id %d is not valid\n", i);
        return -1;
    }

    ret = gpio_request(g_ledgpios[i], "rk3568-led");
    if (ret) {
        printk(KERN_ERR
               "leddev: failed to request led-gpio %d, ret=%d\n",
               i, ret);
        return ret;
    }

    gpio_direction_output(g_ledgpios[i], 0);
    led_class_create_device(i);
}

g_lednum = led_num;
```

这里的 `i` 同时作为两种索引：

- `led-gpios` 属性中第几个 GPIO
- `/dev/ledN` 的次设备号 N

因此第 0 个 GPIO 会创建 `/dev/led0`，第 1 个 GPIO会创建 `/dev/led1`。

`gpio_direction_output(gpio, 0)` 同时完成两件事：把 GPIO 设置为输出，并把初始电平设置为低电平。

### 第四步：probe 成功后保存数量

```c
g_lednum = led_num;
```

卸载驱动时，`remove` 会根据这个数量逐个释放 GPIO 和设备节点。

## 6. open、read 和 write 只使用已经准备好的资源

GPIO 的申请和方向配置已经在 `probe` 中完成，所以 `open` 不再重复初始化硬件：

```c
static int board_rk3568_led_init(int io)
{
    printk("/dev/led%d opened\n", io);
    return 0;
}
```

写操作根据次设备号找到对应 GPIO：

```c
static int board_rk3568_led_write(int io, int status)
{
    if (status == 1) {
        gpio_set_value(g_ledgpios[io], 1);
    } else if (status == 0) {
        gpio_set_value(g_ledgpios[io], 0);
    } else {
        return -1;
    }

    return 0;
}
```

读操作也不再读取 RK3568 寄存器，而是调用 GPIO API：

```c
static int board_rk3568_led_read(int io)
{
    return gpio_get_value(g_ledgpios[io]);
}
```

这一层的调用关系是：

```text
/dev/led0
    ↓ minor = 0
led_drv.c 的 read/write
    ↓ p_led_opr->read/write(0)
g_ledgpios[0]
    ↓ GPIO 子系统
GPIO0_C0
```

## 7. 在 remove 中释放资源

新源码的 `remove` 是 `probe` 的反向过程：

```c
static int rk3568_gpio_remove(struct platform_device *pdev)
{
    int i;

    for (i = g_lednum - 1; i >= 0; i--) {
        gpio_set_value(g_ledgpios[i], 0);
        gpio_free(g_ledgpios[i]);
        led_class_destroy_device(i);
    }

    g_lednum = 0;
    return 0;
}
```

卸载时先把 LED 输出恢复为低电平，再释放 GPIO，最后销毁 `/dev/ledN`。

更容易回忆的生命周期是：

```text
probe：获取资源 → 申请资源 → 初始化资源 → 创建设备节点
read/write：使用已经准备好的资源
remove：销毁设备节点 → 释放资源
```

## 8. 编译、烧写和加载

修改 DTS 后需要重新编译设备树或内核镜像，并把新的 DTB/`boot.img` 更新到开发板。

驱动模块编译后只剩：

```text
led_drv.ko
led_dev.ko
```

加载顺序可以写成：

```bash
insmod led_drv.ko
insmod led_dev.ko
```

原因是 `led_dev.ko` 会调用 `led_drv.ko` 导出的：

```c
led_class_create_device()
led_class_destroy_device()
register_led_operations()
```

卸载时按相反顺序：

```bash
rmmod led_dev
rmmod led_drv
```

如果设备树已经生效，并且 `compatible` 匹配成功，加载 `led_dev.ko` 后就会进入 `probe`，最终创建 `/dev/led0`。

# 四、需要注意的问题

下面这些问题是我在改造过程中实际容易混淆或写错的地方。以后再回看设备树 GPIO 驱动时，方便体系自己。

## 1. pinctrl 配置参数从哪里来

我一开始看到下面这行时，不清楚四个参数应该去哪里查：

```dts
<0 RK_PC0 RK_FUNC_GPIO &pcfg_pull_none>
```

现在可以把它拆开理解：

- `0`：GPIO bank 编号，即 GPIO0
- `RK_PC0`：GPIO0 内部的 C0 引脚
- `RK_FUNC_GPIO`：将引脚复用成普通 GPIO
- `&pcfg_pull_none`：不配置内部上拉和下拉

这些信息不是凭空写出来的，查找顺序可以固定下来：

1. 看开发板原理图，确认 LED 接在哪个 GPIO
2. 看 RK3568 数据手册或 pinmux 表，确认这个引脚支持什么复用功能
3. 看 `include/dt-bindings/pinctrl/rockchip.h`，查 `RK_PC0`、`RK_FUNC_GPIO` 等宏
4. 看 `rockchip-pinconf.dtsi`，查 `pcfg_pull_none`、`pcfg_pull_up` 等配置
5. 搜索 SDK 中已有 DTS/DTSI，参考官方节点的标准写法

推荐写：

```dts
<0 RK_PC0 RK_FUNC_GPIO &pcfg_pull_none>
```

不推荐直接写：

```dts
<0 RK_PC0 0 &pcfg_pull_none>
```

GPIO 功能的数值即使正好是 0，使用宏也更容易看懂，后期回看时不用重新猜这个 0 的含义。

## 2. pinctrl 与 GPIO 属性的职责不同

我最初容易把下面两项当成同一件事：

```dts
pinctrl-0 = <&led_gpio>;
led-gpios = <&gpio0 RK_PC0 GPIO_ACTIVE_HIGH>;
```

实际上它们分工不同。

`pinctrl-0` 负责：

- 把引脚复用成 GPIO
- 配置上拉和下拉
- 配置驱动能力等电气属性

它不负责让 LED 亮或灭。

`led-gpios` 负责描述：

- 使用哪个 GPIO 控制器
- 使用控制器中的哪个引脚
- 高电平有效还是低电平有效

驱动负责：

- 获取并申请 GPIO
- 设置输入输出方向
- 输出或读取高低电平
- 卸载时释放 GPIO

一句话来说：

```text
pinctrl：这个引脚是什么功能
GPIO 属性：这个设备使用哪个 GPIO
驱动：实际申请并操作这个 GPIO
```

## 3. compatible 的格式和两边必须完全一致

我最初写过：

```dts
compatible = "rk3568_sc, led";
```

这里逗号后多了空格，命名形式也不规范。后来改成：

```dts
compatible = "sc,rk3568-led";
```

驱动中的匹配表必须使用完全相同的字符串：

```c
{ .compatible = "sc,rk3568-led" }
```

只要有一个字符、空格或连字符不一致，就不会匹配，也不会进入 `probe`。

## 4. pinctrl-names 不是 compatible

我最初写过：

```dts
pinctrl-names = "rk3568_sc, led";
```

这是把 pinctrl 状态名和驱动兼容字符串混在了一起。这里应该写：

```dts
pinctrl-names = "default";
```

因为 `pinctrl-names` 描述的是引脚状态名称。`pinctrl-0` 对应列表中的第 0 个状态，也就是 `default`。

## 5. GPIO cell 中不能加入 C 语言式逗号

错误写法：

```dts
led-gpio = <&gpio0, RK_PC0, GPIO_ACTIVE_HIGH>;
```

正确写法：

```dts
led-gpios = <&gpio0 RK_PC0 GPIO_ACTIVE_HIGH>;
```

需要同时注意两点：

- cell 之间使用空格，不使用逗号
- 属性名使用 `led-gpios`，驱动读取时也必须写同一个名称

`of_get_named_gpio(np, "led-gpios", i)` 读取的是 `led-gpios` 属性，不是 `compatible`。`compatible` 只负责匹配驱动。

## 6. 普通节点和 `&pinctrl` 的位置不同

我曾经因为节点层级放错而导致设备树编译失败。

普通设备节点应该放在根节点内部：

```dts
/ {
    gpioled {
        /* ... */
    };
};
```

`&pinctrl` 是对已有节点的追加，放在根节点外部：

```dts
&pinctrl {
    /* ... */
};
```

完整结构是：

```dts
/ {
    gpioled {
        compatible = "sc,rk3568-led";
        pinctrl-names = "default";
        pinctrl-0 = <&led_gpio>;
        led-gpios = <&gpio0 RK_PC0 GPIO_ACTIVE_HIGH>;
        status = "okay";
    };
};

&pinctrl {
    led-gpios {
        led_gpio: led-pin {
            rockchip,pins =
                <0 RK_PC0 RK_FUNC_GPIO &pcfg_pull_none>;
        };
    };
};
```

如果仍然编译失败，还要继续排查：

- 是否已经存在同名 `gpioled` 节点
- 是否已经存在同名 `led_gpio` label
- 是否缺少定义 `GPIO_ACTIVE_HIGH`、`RK_PC0` 的头文件
- 是否在一个已有的 `&pinctrl` 节点里又嵌套了 `&pinctrl`
- 大括号和分号是否配对

## 7. probe 中应该完成什么

这次我把驱动生命周期重新理了一遍。

`probe` 中完成：

- 获取设备树节点
- 统计并获取 GPIO
- 检查和申请 GPIO
- 配置输出方向和初始电平
- 创建 `/dev/ledN`
- 保存 GPIO 编号和 LED 数量

`open` 中完成：

- 检查或记录当前打开的是哪个次设备
- 不重复申请 GPIO
- 不重复配置 pinctrl 或输出方向

`read/write` 中完成：

- 根据 minor 找到对应 GPIO
- 通过 GPIO API 获取或设置电平

`remove` 中完成：

- 恢复安全电平
- 销毁 `/dev/ledN`
- 释放 GPIO
- 清理数量或私有数据

核心原则是：

```text
probe 获取资源
read/write 使用资源
remove 释放资源
```

## 8. 为什么不应该在 open 中申请 GPIO

如果每次打开设备都执行：

```text
open
  ↓
gpio_request
```

就会出现这些问题：

- 第一次打开成功，第二次打开会重复申请同一个 GPIO
- `gpio_request()` 可能返回 `-EBUSY`
- 多进程同时打开设备时行为异常
- 资源问题要等到用户调用 `open` 才暴露

GPIO 属于设备资源，更适合在 `probe` 中申请一次，在 `remove` 中释放一次。`open` 只需要使用已经准备好的设备。

## 9. 怎样验证设备树是否真的生效

修改、编译并烧写新的 `boot.img` 后，可以从上到下检查整条链路。

先看设备树节点：

```bash
ls /proc/device-tree/gpioled
```

查看 `compatible` 时要去掉设备树字符串结尾的 `\0`：

```bash
tr -d '\0' < /proc/device-tree/gpioled/compatible
```

期望看到：

```text
sc,rk3568-led
```

然后继续检查：

```bash
ls /sys/bus/platform/devices/
ls /sys/bus/platform/drivers/
dmesg
ls -l /dev/led*
cat /sys/kernel/debug/gpio
```

如果最终可以控制 LED，说明下面这条链路已经打通：

```text
DTS
  ↓ DTC
DTB
  ↓ 内核解析
platform_device
  ↓ compatible 匹配
probe
  ↓ 获取并申请 GPIO
GPIO 子系统
  ↓
/dev/led0
  ↓
应用程序
```

排查时不要一上来只盯着驱动代码。先确认板子运行的是新 DTB，再确认设备存在、驱动匹配、`probe` 被调用，最后再查字符设备和 GPIO。

## 10. 多个 LED 的两种设备树设计

后面扩展多个 LED 时，有两种常见组织方式。

### 方案一：一个 LED 一个节点

```dts
led0 {
    compatible = "sc,rk3568-led";
    led-gpios = <&gpio0 RK_PC0 GPIO_ACTIVE_HIGH>;
};

led1 {
    compatible = "sc,rk3568-led";
    led-gpios = <&gpio0 RK_PC1 GPIO_ACTIVE_HIGH>;
};
```

这种方式的特点是：

- 多个设备树节点
- 内核创建多个 `platform_device`
- 同一个 `probe` 会被调用多次
- 每次 `probe` 管理一个 LED

它适合继续练习 platform 多设备实例、`platform_set_drvdata()` 和每设备独立私有数据。

### 方案二：一个节点保存多个 GPIO

```dts
gpioled {
    compatible = "sc,rk3568-led";

    led-gpios =
        <&gpio0 RK_PC0 GPIO_ACTIVE_HIGH>,
        <&gpio0 RK_PC1 GPIO_ACTIVE_HIGH>,
        <&gpio1 RK_PA2 GPIO_ACTIVE_LOW>;
};
```

这种方式的特点是：

- 只有一个设备树节点和一个 `platform_device`
- `probe` 只执行一次
- `probe` 内部循环获取多个 GPIO
- 使用次设备号区分 `/dev/led0`、`/dev/led1`、`/dev/led2`

为了在原有字符设备框架上做最小改动，我当前采用的是第二种方案。

## 11. `of_get_named_gpio()` 的 index 不是 GPIO 编号

下面调用中的 `index`：

```c
gpio = of_get_named_gpio(np, "led-gpios", index);
```

表示的是 `led-gpios` 属性中的第几个 GPIO 描述项。

例如：

```dts
led-gpios =
    <GPIO_A>,
    <GPIO_B>,
    <GPIO_C>;
```

对应关系是：

```text
index 0 → GPIO_A
index 1 → GPIO_B
index 2 → GPIO_C
```

它不是 GPIO bank 编号，不是硬件引脚编号，也不是 Linux GPIO 编号。

函数的返回值才是旧式 GPIO API 可以使用的 Linux GPIO 编号，可以继续传给：

```c
gpio_request()
gpio_direction_output()
gpio_set_value()
gpio_get_value()
gpio_free()
```

## 12. `of_gpio_count()` 与 `of_gpio_named_count()` 的区别

`of_gpio_count(np)` 固定统计名为 `gpios` 的属性，大致相当于：

```c
of_gpio_named_count(np, "gpios");
```

`of_gpio_named_count(np, propname)` 可以统计调用者指定的 GPIO 属性。

本文属性名是：

```dts
led-gpios = <...>, <...>;
```

因此应该调用：

```c
led_num = of_gpio_named_count(np, "led-gpios");
```

然后通过索引逐个获取：

```c
for (i = 0; i < led_num; i++)
    gpio[i] = of_get_named_gpio(np, "led-gpios", i);
```

可以概括为：先统计 `led-gpios` 的数量，再通过 index 逐个解析。

## 13. 当前源码还需要完善错误回滚

当前 `probe` 在循环中途失败时会直接返回。例如第 0 个 GPIO 已经申请成功，第 1 个 GPIO 申请失败，前面已经申请的资源不会自动释放。

更完整的写法应该在失败路径中反向回滚：

```c
for (i = 0; i < led_num; i++) {
    /* 获取、申请、配置 GPIO，创建设备节点 */
    if (ret)
        goto err_free_leds;
}

return 0;

err_free_leds:
while (--i >= 0) {
    led_class_destroy_device(i);
    gpio_free(g_ledgpios[i]);
}
return ret;
```

此外，`gpio_direction_output()` 和 `device_create()` 的返回值也应该检查。学习例程可以先把主流程跑通，但以后写正式驱动时，要保证每一个资源申请动作都有对应的失败回滚。

## 14. `GPIO_ACTIVE_LOW` 不能只改设备树就结束

当前代码使用 `of_get_named_gpio()` 获取整数 GPIO，再直接调用 `gpio_set_value()`。这条路径没有在驱动中保存并处理 GPIO flag。

目前板载 LED 配置为：

```dts
GPIO_ACTIVE_HIGH
```

所以写 1 点亮、写 0 熄灭与硬件逻辑一致。如果以后加入 `GPIO_ACTIVE_LOW` 的 LED，需要确认驱动是否正确处理有效电平。更现代的做法是使用 GPIO descriptor API，例如 `devm_gpiod_get()`、`gpiod_set_value()`，让有效电平和资源释放更自然地交给 GPIO 子系统处理。

这一版先保留旧式 GPIO API，方便看清从设备树解析出 GPIO 编号的过程；后续可以再单独改造成 `gpiod` 版本。

# 五、小结

这一版驱动完成了从“手动描述 platform 设备”到“使用设备树描述硬件”的过渡。

最值得注意的是下面五点：

1. 设备树通过 `compatible` 与 `of_match_table` 匹配驱动，匹配后进入 `probe`
2. pinctrl 负责引脚复用和电气属性，`led-gpios` 负责描述设备使用哪个 GPIO
3. `probe` 中获取、申请并初始化资源，`read/write` 使用资源，`remove` 释放资源
4. `of_gpio_named_count()` 统计 GPIO 数量，`of_get_named_gpio()` 根据 index 逐个解析 GPIO
5. 引入设备树后不再需要 `board_rk3568_led.ko`，也不需要在驱动中直接操作 GPIO 寄存器

完整链路可以压缩成一句话：

```text
DTS 描述 LED → DTB 交给内核 → compatible 匹配 platform_driver
→ probe 获取 GPIO → 创建 /dev/ledN → 应用程序通过 read/write 控制 LED
```

上一篇解决的是“怎样把设备和驱动放到 platform 总线框架中”，这一篇继续解决“怎样把板级硬件信息从 C 文件移到设备树，并交给内核子系统统一管理”。
