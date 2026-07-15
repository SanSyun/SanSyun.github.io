---
title: "Linux驱动开发：内核定时器消抖下的按键中断设计"
description: "基于 RK3568 按键驱动，梳理 Linux 内核定时器、中断上下半部、设备树中断描述，以及使用定时器完成按键消抖的完整设计。"
date: "2026-07-14"
draft: false
featured: false
tags: ["RK3568", "Linux Driver", "Interrupt", "Kernel Timer", "GPIO"]
readingTime: "35 分钟"
category: "驱动开发"
---

# 一、Linux 内核定时器

在前面的 GPIO 驱动中，应用程序调用 `read` 或 `write` 后，驱动再去读取或设置 GPIO。按键驱动和 LED 驱动不太一样：按键什么时候被按下并不由应用程序决定，因此更适合让硬件在电平发生变化时主动产生中断。

但机械按键在按下和松开的瞬间通常会发生抖动。一次真实的按下动作，GPIO 电平可能在很短的时间内来回变化多次。如果每次边沿都立即判断按键状态，就可能把一次按键误认为多次操作。

本文采用的处理方法是：

```text
GPIO 边沿触发中断
        ↓
中断处理函数不立即读取按键状态
        ↓
把定时器推迟到 15 ms 后执行
        ↓
定时器回调再次读取 GPIO
        ↓
根据稳定后的电平判断按下、松开或保持
```

这个过程把“快速响应中断”和“延迟确认按键状态”分开，既缩短了中断处理时间，也实现了软件消抖。

## 1. 内核定时器时间管理简介

### `HZ` 和 `jiffies`

Linux 内核需要有自己的时间刻度。内核中一个很重要的配置项是 `HZ`，它表示每秒包含多少个系统节拍。

例如，当：

```text
HZ = 100
```

一个节拍大约就是 10 ms；当 `HZ = 1000` 时，一个节拍大约是 1 ms。具体取值由内核配置决定，所以驱动中不应该直接假设一个节拍等于多少毫秒，在编译内核的时候可以直接通过图形化界面配置系统节拍率：

![78407293041](/images/notes/linux_irq/1784072930418.png)

高节拍率和低节拍率分别有什么优缺点呢？

* 高节拍率会提高系统时间精度，如果采用 100Hz 的节拍率，时间精度就是 10ms，采用1000Hz 的话时间精度就是 1ms，精度提高了 10 倍。高精度时钟的好处有很多，对于那些对时间要求严格的函数来说，能够以更高的精度运行，时间测量也更加准确。
* 高节拍率会导致中断的产生更加频繁，频繁的中断会加剧系统的负担，1000Hz 和 100Hz的系统节拍率相比，系统要花费 10 倍的“精力”去处理中断。中断服务函数占用处理器的时间增加，但是现在的处理器性能都很强大，所以采用 1000Hz 的系统节拍率并不会增加太大的负载压力。根据自己的实际情况，选择合适的系统节拍率。

`jiffies` 是内核维护的全局节拍计数。系统启动后，每经过一个 tick，`jiffies` 就会增加。可以把它理解为内核中的“当前时间刻度”。

因此，下面这段代码表示的不是“15 个 tick 后”，而是“把 15 ms 自动换算成对应的 tick 数，再计算到期时间”：

```c
jiffies + msecs_to_jiffies(15)
```

这里：

- `jiffies`：当前内核时间
- `msecs_to_jiffies(15)`：把 15 ms 换算为 jiffies
- 两者相加：定时器下一次到期的绝对时间

内核还提供了几组常见的时间转换 API：

| API | 作用 |
| :-- | :-- |
| `msecs_to_jiffies(ms)` | 毫秒转换为 jiffies |
| `usecs_to_jiffies(us)` | 微秒转换为 jiffies |
| `nsecs_to_jiffies(ns)` | 纳秒转换为 jiffies |
| `jiffies_to_msecs(j)` | jiffies 转换为毫秒 |
| `jiffies_to_usecs(j)` | jiffies 转换为微秒 |
| `jiffies_to_nsecs(j)` | jiffies 转换为纳秒 |

如果需要比较两个 jiffies 时间，推荐使用下表中的API，不要直接使用普通的大于号和小于号。原因是 jiffies 计数最终会发生回绕，而这些宏已经考虑了回绕问题。其中，uknown通常为jiffies，known通常为要对比的值。如果 unkown 超过 known 的话，time_after 函数返回真，否则返回假。如果 unkown 没有超过 known 的话 time_before 函数返回真，否则返回假。time_after_eq 函数和 time_after 函数类似，只是多了判断等于这个条件。同理，time_before_eq 函数和time_before 函数也类似。

| API                                 |
| :---------------------------------- |
| ``` time_after(unkown, known)```    |
| ```time_before(unkown, known)```    |
| ```time_after_eq(unkown, known)```  |
| ```time_before_eq(unkown, known)``` |

### 内核定时器不是硬件定时器

本文使用的 `struct timer_list` 是 Linux 内核提供的软件定时器。它表达的是：

```text
到达指定 jiffies 后，内核尽快调用回调函数
```

它并不保证在某个纳秒或微秒位置绝对精确执行。系统负载、中断屏蔽和调度状态都可能让回调稍晚发生。按键消抖只需要毫秒级延时，因此普通内核定时器非常适合；如果需要更高精度，则要了解高精度定时器 `hrtimer`。

## 2. 内核定时器简介

内核定时器使用 `struct timer_list` 描述：

```c
struct timer_list timer;
```

在本文代码中，定时器没有单独定义成全局变量，而是嵌入了每个按键自己的设备结构体：

```c
struct btn_dev {
    int gpio;
    struct timer_list timer;
    int irq_num;
    char irq_name[20];
    spinlock_t spinlock;
    int status;
    int last_val;
};
```

这样每个按键都拥有自己的 GPIO、中断号、定时器和状态。第 0 个按键触发时，只会修改 `g_btn[0]` 中的定时器；以后扩展多个按键时，不需要让所有按键共用一个全局定时器。

定时器的生命周期可以记成：

![78407404625](/images/notes/linux_irq/1784074046250.png)

本文的定时器是单次触发的。回调执行结束后，如果没有再次调用 `mod_timer()`，它不会自动周期运行。

还要注意，普通内核定时器回调运行在软中断上下文中，因此回调应该尽量短，不能执行可能睡眠的操作。例如不要在回调中调用 `msleep()`，也不要在其中直接执行 `copy_to_user()`。

## 3. 内核定时器常用 API

### `timer_setup()`：初始化定时器

```c
timer_setup(&g_btn[i].timer, btn_timer_callback, 0);
```

它完成定时器对象与回调函数的绑定。三个参数分别是：

```c
timer_setup(timer, callback, flags);
```

- `timer`：需要初始化的 `struct timer_list`
- `callback`：到期后执行的函数
- `flags`：普通场景填写 `0`

初始化并不等于启动。执行 `timer_setup()` 后，定时器已经可以使用，但还没有设置到期时间。

### `add_timer()`：启动定时器

add_timer 函数用于向 Linux 内核注册定时器，使用 add_timer 函数向内核注册定时器以后，定时器就会开始运行。

### `mod_timer()`：启动或更新定时器

中断函数中的核心代码是：

```c
mod_timer(&btn->timer,
          jiffies + msecs_to_jiffies(15));
```

如果定时器还没有挂入系统，`mod_timer()` 会启动它；如果定时器已经等待执行，`mod_timer()` 会更新它的到期时间。

这个“更新”能力正好适合按键消抖。按键在抖动期间可能连续产生多次边沿中断，每次中断都会把定时器重新推迟到“当前时刻之后 15 ms”。只有电平安静下来 15 ms，回调才会真正执行。

### `del_timer_sync()`：同步删除定时器

驱动卸载时使用：

```c
del_timer_sync(&g_btn[i].timer);
```

它不仅删除尚未执行的定时器，还会等待正在其他 CPU 上运行的回调结束。相比只删除定时器，同步版本更适合模块退出路径，可以防止模块代码已经卸载后，回调仍然访问原来的函数或数据。

本文先调用 `free_irq()` 阻止新的按键中断继续修改定时器，再调用 `del_timer_sync()` 清理已经安排的回调：

```c
free_irq(g_btn[i].irq_num, &g_btn[i]);
del_timer_sync(&g_btn[i].timer);
```

## 4. 内核短延时函数

有时候我们需要在内核中实现短延时，尤其是在 Linux 驱动中。Linux 内核提供了毫秒、微秒和纳秒延时函数，这三个函数如下表所示：

| API                                 | 作用         |
| ----------------------------------- | ------------ |
| `void ndelay(unsigned long nsecs)`  | 纳秒延时函数 |
| `void udelay(unsigned long usecs)`  | 微秒延时函数 |
| `void mdelay(unsigned long mseces)` | 毫秒延时函数 |



# 二、Linux 中断

## 1. Linux 中断简介

如果使用轮询方式读取按键，应用程序或驱动就需要不断检查 GPIO：

```text
读取 GPIO → 没变化 → 再读取 → 没变化 → 再读取……
```

这会造成无意义的 CPU 消耗。中断方式则是平时不处理，只有 GPIO 边沿出现时才由硬件通知 CPU。

Linux 中断处理的大致链路是：

```text
按键电平变化
    ↓
GPIO 控制器检测到边沿
    ↓
中断控制器向 CPU 报告中断
    ↓
内核找到对应 Linux IRQ
    ↓
调用 request_irq() 注册的处理函数
```

设备树中描述的是硬件中断资源，驱动通过 `irq_of_parse_and_map()` 把它解析并映射为 Linux IRQ 编号。后续的 `request_irq()` 和 `free_irq()` 都使用这个 Linux IRQ 编号。

## 2. 中断上半部与下半部的理解

中断处理最重要的原则之一是：中断来了要尽快响应，但不要长时间占着中断上下文。

因此，Linux 通常把中断工作分成两部分：

- 上半部：紧急、短小、必须立即完成的工作
- 下半部：可以稍后执行、耗时相对更长的工作

常见下半部机制包括**软中断**、**tasklet**、**工作队列**和**线程化中断**。本文没有单独创建工作队列，而是利用内核定时器完成“延后 15 ms 再处理”的工作。

本文的上半部就是 `btn_interrupt()`：

```c
static irqreturn_t btn_interrupt(int irq, void *dev_id)
{
    struct btn_dev *btn = dev_id;

    mod_timer(&btn->timer,
              jiffies + msecs_to_jiffies(15));

    return IRQ_HANDLED;
}
```

它只做三件事：

1. 从 `dev_id` 取回当前按键对象
2. 把这个按键的定时器更新到 15 ms 后
3. 返回 `IRQ_HANDLED`，表示中断已经处理

真正读取 GPIO 和判断按键状态的工作放在定时器回调中：

```c
static void btn_timer_callback(struct timer_list *arg)
{
    int curr_val;
    unsigned long flags;
    struct btn_dev *btn;

    btn = from_timer(btn, arg, timer);

    spin_lock_irqsave(&btn->spinlock, flags);

    curr_val = gpio_get_value(btn->gpio);
    if ((curr_val == 1) && (btn->last_val == 0)) {
        btn->status = BTN_PRESS;
    } else if ((curr_val == 0) && (btn->last_val == 1)) {
        btn->status = BTN_RELEASE;
    } else {
        btn->status = BTN_KEEP;
    }
    btn->last_val = curr_val;

    spin_unlock_irqrestore(&btn->spinlock, flags);
}
```

这就是本文对上下半部最直观的理解：

```text
上半部：收到中断，只安排后续工作
下半部：等待抖动结束，再读取并确认状态
```

严格来说，定时器回调本身运行在软中断上下文中，仍然不能睡眠。这里把它称为“延后处理部分”，重点是理解中断函数不承担消抖等待和完整状态判断。

## 3. 中断常用 API

### `irq_of_parse_and_map()`：解析设备树中断

```c
g_btn[i].irq_num = irq_of_parse_and_map(np, i);
```

参数含义：

- `np`：当前按键设备树节点
- `i`：`interrupts` 属性中的第几个中断
- 返回值：映射后的 Linux IRQ 编号，返回 `0` 表示失败

当一个节点同时描述多个按键时，代码默认 `key-gpios` 和 `interrupts` 中的条目顺序一一对应：

```text
key-gpios[0]  ↔ interrupts[0] ↔ /dev/btn0
key-gpios[1]  ↔ interrupts[1] ↔ /dev/btn1
```

### `irq_get_trigger_type()`：取得触发类型

```c
irq_flag = irq_get_trigger_type(g_btn[i].irq_num);
```

在文件include/linux/interrupt.h里面可以查看所有的中断标志，这里读取的是设备树映射后保存在 IRQ 描述中的触发类型，例如：

- `IRQ_TYPE_EDGE_RISING`：上升沿
- `IRQ_TYPE_EDGE_FALLING`：下降沿
- `IRQ_TYPE_EDGE_BOTH`：双边沿
- `IRQ_TYPE_LEVEL_HIGH`：高电平
- `IRQ_TYPE_LEVEL_LOW`：低电平

本文需要同时识别按下和松开，所以设备树使用双边沿触发。

### `request_irq()`：申请中断

本文调用如下：

```c
ret = request_irq(g_btn[i].irq_num,
                  btn_interrupt,
                  irq_flag,
                  g_btn[i].irq_name,
                  &g_btn[i]);
```

五个参数分别表示：

| 参数 | 本文取值 | 含义 |
| :-- | :-- | :-- |
| `irq` | `g_btn[i].irq_num` | Linux IRQ 编号 |
| `handler` | `btn_interrupt` | 中断处理函数 |
| `flags` | `irq_flag` | 触发方式等标志 |
| `name` | `g_btn[i].irq_name` | 在 `/proc/interrupts` 中显示的名称 |
| `dev_id` | `&g_btn[i]` | 当前按键的私有数据指针 |

`name`中断名字设置好后可以在/proc/interrupts文件中看到对应的中断名字。

![78407534898](/images/notes/linux_irq/1784075348988.png)

`dev_id` 不只是为了向中断函数传参。共享中断场景下，内核也使用它区分同一 IRQ 上的不同设备。因此申请和释放中断时必须传入相同的地址。

### `free_irq()`：释放中断

```c
free_irq(g_btn[i].irq_num, &g_btn[i]);
```

这里的 `&g_btn[i]` 必须和 `request_irq()` 第五个参数完全一致，不能传 `NULL`，也不能换成另一个临时对象的地址。

### `irq_dispose_mapping()`：释放 IRQ 映射

```c
irq_dispose_mapping(g_btn[i].irq_num);
```

由于本文使用 `irq_of_parse_and_map()` 建立映射，卸载时在中断已经释放后调用 `irq_dispose_mapping()` 释放对应映射。

### `irqreturn_t (*irq_handler_t) (int, void *)`：中断处理函数

第一个参数是要中断处理函数要相应的中断号。第二个参数是一个指向 void 的指针，也就是个通用指针，需要与 request_irq 函数的 dev_id 参数保持一致。用于区分共享中断的不同设备，dev_id 也可以指向设备数据结构。中断处理函数的返回值为 irqreturn_t 类型，irqreturn_t 类型定义如下所示：

```c
enum irqreturn{
    IRQ_NONE = (0 << 0),
    IRQ_HANDLED = (1 << 0),
    IRQ_WAKE_THREAD = (1 << 1),
};

typedef enum irqreturn irqreturn_t;
```

### enable_irq()和disable_irq()：中断使能与禁止函数

enable_irq 和 disable_irq 用于使能和禁止指定的中断，irq 就是要禁止的中断号。disable_irq函数要等到当前正在执行的中断处理函数执行完才返回，因此使用者需要保证不会产生新的中断，并且确保所有已经开始执行的中断处理程序已经全部退出。在这种情况下，可以使用disable_irq_nosync()。disable_irq_nosync 函数调用以后立即返回，不会等待当前中断处理程序执行完毕。

### 中断相关头文件

本文用到的头文件对应关系如下：

```c
#include <linux/of_irq.h>      /* irq_of_parse_and_map */
#include <linux/interrupt.h>   /* request_irq、free_irq、irqreturn_t */
#include <linux/irq.h>         /* irq_get_trigger_type */
```

## 4. 设备树中断信息节点

### GIC中断控制器

GIC 全称为：Generic Interrupt Controller，关于 GIC 的详细内容可以查看文档《ARM Generic Interrupt Controller(ARM GIC 控制器)V2.0》。GIC 是 ARM 公司给 Cortex-A/R 内核提供的一个中断控制器，类似之前学习STM32时 Cortex-M 内核中的NVIC。当 GIC 接收到外部中断信号以后就会报给 ARM 内核，但是 ARM 内核只提供了四个信号给 GIC 来汇报中断情况：**VFIQ虚拟快速FIQ**、**VIRQ虚拟快速IRQ**、**FIQ快速中断IRQ** 和 **IRQ外部中断IRQ**，他们之间的关系如图：

![78407802817](/images/notes/linux_irq/1784078028170.png)

GIC将众多的中断源分为了三类：

* **SPI(Shared Peripheral Interrupt)**,共享中断，顾名思义，所有 Core 共享的中断，这个是最常见的，那些外部中断都属于 SPI 中断(注意！不是 SPI 总线那个中断) 。比如 GPIO 中断、串口中断等等，这些中断所有的 Core 都可以处理，不限定特定 Core。
* **PPI(Private Peripheral Interrupt)**，私有中断, 因为 GIC 是支持多核的，每个核肯定有自己独有的中断。这些独有的中断肯定是要指定的核心处理，因此这些中断就叫做私有中断。
* **SGI(Software-generated Interrupt)**，软件中断，由软件触发引起的中断，通过向寄存器GICD_SGIR 写入数据来触发，系统会使用 SGI 中断来完成多核之间的通信。

### 中断ID

中断源有很多，为了区分这些不同的中断源肯定要给他们分配一个唯一 ID，这些 ID 就是中断 ID。每一个 CPU 最多支持 1020 个中断 ID，中断 ID 号为 ID0~ID1019。这 1020 个 ID 包含了 PPI、SPI 和 SGI，这 1020 个 ID 分配如下:

* **ID0~ID15：这 16 个 ID 分配给 SGI**
* **ID16~ID31：这 16 个 ID 分配给 PPI**
* **ID32~ID1019：这 988 个 ID 分配给 SPI**

对于RK3568，其各个外设的中断ID可以从参考手册**《Rockchip RK3568 TRM Part1 V1.1（RK3568 参考手册 1）》**找到详细的解释

![78407865041](/images/notes/linux_irq/1784078650417.png)

## 5. 中断设备树如何编写

一个按键节点可以按下面的形式描述：

```dts
/ {
	gpiokey{
		compatible = "sc,rk3568-key";
		pinctrl-names = "default";
		pinctrl-0 = <&key_gpio>;
		key-gpios =
			<&gpio3 RK_PC5 GPIO_ACTIVE_HIGH>;
		interrupt-parent = <&gpio3>;
		interrupts = <21 IRQ_TYPE_EDGE_BOTH>;
		status = "okay";
	};
};

&pinctrl {
	key-gpios{
		/omit-if-no-ref/
		key_gpio:key-pin{
			rockchip,pins=
				<3 RK_PC5 0 &pcfg_pull_none>;
		};
	};
};
```

其中：

| 属性 | 作用 |
| :-- | :-- |
| `compatible` | 和驱动中的 `of_match_table` 匹配 |
| `pinctrl-0` | 配置按键引脚的复用和上下拉 |
| `key-gpios` | 让驱动取得 GPIO 编号 |
| `interrupt-parent` | 指定中断由哪个控制器提供 |
| `interrupts` | 描述引脚号和中断触发类型 |
| `status` | 启用当前节点 |

这里最容易写错的是属性名。正确名称是：

```dts
interrupts = <RK_PC5 IRQ_TYPE_EDGE_BOTH>;
```

不是：

```dts
interrupt = <21 IRQ_TYPE_EDGE_BOTH>;
```

`irq_of_parse_and_map()` 解析的是标准 `interrupts` 属性。即使 `key-gpios` 可以正常解析，只要中断属性名写错，GPIO 编号仍然能获取成功，但 IRQ 映射会返回 `0`。

另外，`pcfg_pull_down` 和 `GPIO_ACTIVE_HIGH` 对应本文“低电平为未按下、高电平为按下”的判断。如果实际开发板按键是按下接地，就应结合原理图改为上拉和低电平有效，同时调整状态判断。设备树不能脱离真实电路直接照抄。

下面简单总结一下，中断有关的设备树属性信息：

* **#interrupt-cells**，指定中断源的信息 cells 个数。
* **interrupt-controller**，表示当前节点为中断控制器。
* **interrupts**，指定中断号，触发方式等。
* **interrupt-parent**，指定父中断，也就是中断控制器。

# 三、驱动编写

## 1. 驱动编写思路

这次工程由两个模块组成：

```makefile
obj-m := btn_drv.o
obj-m += btn_dev.o
```

两个文件的职责如下：

| 文件 | 主要职责 |
| :-- | :-- |
| `btn_drv.c` | 注册字符设备和 class，提供 `/dev/btnN` 与用户态读接口 |
| `btn_dev.c` | 匹配设备树，申请 GPIO 和中断，管理定时器、锁与按键状态 |
| `btn_opr.h` | 定义按键对象、状态枚举和上下层 operations |
| `btn_drv.h` | 声明字符设备层导出的注册和设备节点接口 |

可以把整体调用链分为初始化链和事件链。

初始化链：

![78407695621](/images/notes/linux_irq/1784076956218.png)

按键事件链：

![78407747322](/images/notes/linux_irq/1784077473220.png)

这套分层里，字符设备层不知道 RK3568 使用了哪个 GPIO，也不直接操作中断和自旋锁；板级设备层不负责把数据复制到用户空间。两层只通过 `struct btn_operations` 连接。

## 2. 程序设计

### 定义按键状态和私有结构体

按键状态定义为：

```c
enum btn_status {
    BTN_KEEP = 0,
    BTN_PRESS,
    BTN_RELEASE,
};
```

含义是：

- `BTN_KEEP`：没有新的按下或松开事件
- `BTN_PRESS`：检测到一次稳定的按下边沿
- `BTN_RELEASE`：检测到一次稳定的松开边沿

每个按键的信息集中保存在 `struct btn_dev` 中：

```c
struct btn_dev {
    int gpio;
    struct timer_list timer;
    int irq_num;
    char irq_name[20];
    spinlock_t spinlock;
    int status;
    int last_val;
};
```

其中 `last_val` 保存上一次消抖后的稳定电平，定时器回调将它与本次 `curr_val` 比较：

| `last_val` | `curr_val` | 判断结果 |
| :--: | :--: | :-- |
| 0 | 1 | 按下 `BTN_PRESS` |
| 1 | 0 | 松开 `BTN_RELEASE` |
| 0 | 0 | 保持 `BTN_KEEP` |
| 1 | 1 | 保持 `BTN_KEEP` |

### probe：集中申请设备资源

驱动通过下面的匹配表与设备树建立联系：

```c
static const struct of_device_id btn_of_match[] = {
    { .compatible = "sc,rk3568-key" },
    { }
};

MODULE_DEVICE_TABLE(of, btn_of_match);
```

匹配成功后进入 `rk3568_gpio_probe()`。主循环可以整理成：

```c
for (i = 0; i < btn_num; i++) {
    spin_lock_init(&g_btn[i].spinlock);
    timer_setup(&g_btn[i].timer, btn_timer_callback, 0);

    ret = gpio_prase_init(np, i);
    if (ret)
        return ret;

    ret = irq_prase_init(np, i);
    if (ret)
        return ret;

    btn_class_create_device(i);
}
```

顺序上先初始化软件对象，再申请硬件资源，最后创建设备节点：

![78407763148](/images/notes/linux_irq/1784077631488.png)

`of_gpio_named_count(np, "key-gpios")` 用于统计设备树中有多少个 GPIO 描述项，`of_get_named_gpio(np, "key-gpios", i)` 用于取得第 `i` 个 GPIO。这里的 `i` 是属性索引，不是硬件 GPIO 编号。

GPIO 初始化的关键调用是：

```c
g_btn[i].gpio = of_get_named_gpio(np, "key-gpios", i);
gpio_request(g_btn[i].gpio, "rk3568-btn");
gpio_direction_input(g_btn[i].gpio);
g_btn[i].last_val = gpio_get_value(g_btn[i].gpio);
g_btn[i].status = BTN_KEEP;
```

把当前 GPIO 电平保存到 `last_val` 非常重要。否则模块加载后第一次定时器回调没有可靠的比较基准，可能凭空报告一次按下或松开。

### 中断函数：只更新定时器

申请中断时把当前按键地址作为 `dev_id`：

```c
request_irq(g_btn[i].irq_num,
            btn_interrupt,
            irq_flag,
            g_btn[i].irq_name,
            &g_btn[i]);
```

中断发生后通过 `dev_id` 直接找到对应按键：

```c
static irqreturn_t btn_interrupt(int irq, void *dev_id)
{
    struct btn_dev *btn = dev_id;

    mod_timer(&btn->timer,
              jiffies + msecs_to_jiffies(15));

    return IRQ_HANDLED;
}
```

这里没有根据 `irq` 再去遍历 `g_btn[]`，因为 `dev_id` 已经带回了准确的设备上下文。这是驱动中常见的私有数据传递方式。

### 定时器回调：读取稳定电平

回调收到的是当前 `struct timer_list *`：

```c
static void btn_timer_callback(struct timer_list *arg)
```

通过 `from_timer()` 找回包含它的 `struct btn_dev`：

```c
struct btn_dev *btn;

btn = from_timer(btn, arg, timer);
```

拿到 `btn` 后，就可以访问当前按键的：

```c
btn->gpio
btn->status
btn->last_val
btn->spinlock
```

这比使用一个全局“当前按键编号”更安全，因为多个按键可能在很接近的时间内触发。

### read：读取并消费一次按键事件

字符设备层先根据次设备号找到按键：

```c
struct inode *inode = file_inode(file);
int minor = iminor(inode);

status = p_btn_opr->read_event(minor);
```

`board_rk3568_btn_read_event()` 在持锁期间读取并清除状态：

```c
static int board_rk3568_btn_read_event(int io)
{
    unsigned long flags;
    int status;

    spin_lock_irqsave(&g_btn[io].spinlock, flags);

    status = g_btn[io].status;
    g_btn[io].status = BTN_KEEP;

    spin_unlock_irqrestore(&g_btn[io].spinlock, flags);

    return status;
}
```

这里采用的是“读取即清除”语义。假设状态为 `BTN_PRESS`，第一次 `read()` 会读到按下，随后状态恢复为 `BTN_KEEP`；如果没有新事件，下一次读取就是保持。

字符设备层拿到普通局部变量 `status` 后，再复制到用户空间：

```c
err = copy_to_user(buf, &status, sizeof(status));
if (err != 0)
    return -EFAULT;

return sizeof(status);
```

这里不能把 `copy_to_user()` 放进自旋锁保护区，因为用户空间拷贝可能发生缺页并导致睡眠，而持有自旋锁时不能睡眠。

### remove：按照相反顺序释放资源

卸载路径如下：

```c
for (i = g_btnnum - 1; i >= 0; i--) {
    btn_class_destroy_device(i);
    free_irq(g_btn[i].irq_num, &g_btn[i]);
    del_timer_sync(&g_btn[i].timer);
    irq_dispose_mapping(g_btn[i].irq_num);
    gpio_free(g_btn[i].gpio);
}
```

资源申请与释放成对，是驱动退出路径最需要养成的习惯。

### 编译、加载与验证

编译后会生成：

```text
btn_drv.ko
btn_dev.ko
```

因为 `btn_dev.ko` 使用了 `btn_drv.ko` 导出的符号，所以加载顺序是：

```bash
insmod btn_drv.ko
insmod btn_dev.ko
```

卸载时反过来：

```bash
rmmod btn_dev
rmmod btn_drv
```

可以通过下面几项检查是否工作：

```bash
ls -l /dev/btn*
cat /proc/interrupts | grep btn
dmesg | tail
```

如果使用简单测试程序循环读取 `/dev/btn0`，返回值含义为：

```text
0：BTN_KEEP，没有新事件
1：BTN_PRESS，按键按下
2：BTN_RELEASE，按键松开
```

当前接口是非阻塞式的“读取当前事件状态”。如果希望应用程序在没有按键事件时睡眠，后续可以加入等待队列，并实现 `poll`，把它扩展成更接近正式输入设备的事件模型。

# 四、值得关注的点

## 1. 内核定时器回调如何传递参数

定时器回调定义是：

```c
static void btn_timer_callback(struct timer_list *arg)
```

这里的 `arg` 不需要驱动手动传入。定时器到期时，内核自动把“当前到期的 `struct timer_list` 地址”传给回调。

问题在于，仅有定时器地址还不够，我们真正需要的是按键设备信息。解决方法是把定时器嵌入 `struct btn_dev`：

```c
struct btn_dev {
    int gpio;
    struct timer_list timer;
    /* ... */
};
```

然后在回调中使用：

```c
struct btn_dev *btn;

btn = from_timer(btn, arg, timer);
```

`from_timer()` 根据成员 `timer` 的地址，反推出包含它的 `struct btn_dev` 地址。它的思路和内核中常见的 `container_of()` 相同。

这样就能直接访问：

```c
btn->gpio;
btn->status;
btn->last_val;
```

可以把这个关系理解成：

```text
arg 指向 btn->timer
        ↓ from_timer()
找回 timer 所属的 btn
        ↓
访问整个按键对象
```

## 2. `btn_drv.c` 和 `btn_dev.c` 分层后如何使用自旋锁

按键状态在定时器回调中修改，在字符设备 `read()` 路径中读取。这两个执行路径可能并发，因此需要保护共享的 `status`。

锁应该遵循一个简单原则：

```text
谁管理数据，谁负责加锁
```

`g_btn[i].status` 和 `g_btn[i].spinlock` 都属于 `btn_dev.c` 管理。因此：

- 定时器回调在 `btn_dev.c` 内加锁更新状态
- `board_rk3568_btn_read_event()` 在 `btn_dev.c` 内加锁读取并清除状态
- `btn_drv.c` 只调用 operations，不直接接触锁

```c
spin_lock_irqsave(&g_btn[io].spinlock, flags);

status = g_btn[io].status;
g_btn[io].status = BTN_KEEP;

spin_unlock_irqrestore(&g_btn[io].spinlock, flags);
```

使用 `spin_lock_irqsave()` 不仅取得锁，还会保存并关闭本地 CPU 中断，避免普通进程上下文持锁时被相关中断路径打断。解锁时 `spin_unlock_irqrestore()` 恢复原来的中断状态。

一定不要在持有自旋锁时调用：

```c
copy_to_user()
```

正确流程是先在锁内把共享状态复制到局部变量并清除，再解锁，最后由 `btn_drv.c` 执行用户空间拷贝。

## 3. 中断相关函数需要哪些头文件

对应关系如下：

```c
#include <linux/of_irq.h>      /* irq_of_parse_and_map */
#include <linux/interrupt.h>   /* request_irq、free_irq、irqreturn_t */
#include <linux/irq.h>         /* irq_get_trigger_type */
```

另外，本文定时器和自旋锁结构体使用：

```c
#include <linux/timer.h>
#include <linux/spinlock.h>
```

GPIO 与设备树解析使用：

```c
#include <linux/of.h>
#include <linux/of_gpio.h>
```

## 4. `request_irq()` 参数类型不匹配

中断函数不能写成：

```c
irqreturn_t btn_interrupt(int irq, int i);
```

内核要求的处理函数类型是：

```c
irqreturn_t btn_interrupt(int irq, void *dev_id);
```

同时，`request_irq()` 的第五个参数是指针，不能直接传整数 `i`。正确写法是把当前按键结构体地址传进去：

```c
request_irq(g_btn[i].irq_num,
            btn_interrupt,
            irq_flag,
            g_btn[i].irq_name,
            &g_btn[i]);
```

中断函数中再还原：

```c
static irqreturn_t btn_interrupt(int irq, void *dev_id)
{
    struct btn_dev *btn = dev_id;

    mod_timer(&btn->timer,
              jiffies + msecs_to_jiffies(15));

    return IRQ_HANDLED;
}
```

释放中断时也必须使用相同地址：

```c
free_irq(g_btn[i].irq_num, &g_btn[i]);
```

## 5. 加载模块出现 `Unknown symbol`

曾经出现过：

```text
Unknown symbol register_btn_operations
Unknown symbol btn_class_create_device
Unknown symbol btn_class_destroy_device
```

这些函数由 `btn_drv.ko` 提供，`btn_dev.ko` 使用，因此首先要确认提供方使用了：

```c
EXPORT_SYMBOL(register_btn_operations);
EXPORT_SYMBOL(btn_class_create_device);
EXPORT_SYMBOL(btn_class_destroy_device);
```

并且按依赖顺序加载：

```bash
insmod btn_drv.ko
insmod btn_dev.ko
```

常见原因包括：

- 提供函数的模块没有使用 `EXPORT_SYMBOL()`
- 模块加载顺序错误
- 两个模块之间形成循环依赖
- 修改后的模块没有重新编译
- 开发板上的 `.ko` 不是电脑上最新生成的版本

这次问题最终在重新推送模块后恢复，实际原因是开发板上的 `.ko` 没有同步到最新版本。以后可以先对比：

```bash
md5sum btn_drv.ko btn_dev.ko
ls -l btn_drv.ko btn_dev.ko
modinfo btn_dev.ko
```

如果开发板已经 `insmod` 了旧模块，不要直接覆盖正在使用的 `.ko`。更稳妥的顺序是：

```text
rmmod 旧模块
    ↓
adb push 新模块
    ↓
insmod 新模块
```

这样可以避免“文件已经更新，但内核中仍运行旧代码”或者模块文件版本混淆。

## 6. GPIO 可以获取，但中断号获取失败

GPIO 解析正常：

```c
of_get_named_gpio(np, "key-gpios", i);
```

但下面的函数返回 `0`：

```c
irq_of_parse_and_map(np, i);
```

这种现象说明 `key-gpios` 属性存在且格式基本正确，但标准中断属性没有被正确解析。

错误写法是：

```dts
interrupt = <21 IRQ_TYPE_EDGE_BOTH>;
```

正确写法是：

```dts
interrupts = <RK_PC5 IRQ_TYPE_EDGE_BOTH>;
```

需要同时确认：

- 属性名是复数 `interrupts`
- `interrupt-parent` 指向正确的 GPIO 控制器
- 引脚宏与原理图一致
- 设备树已经重新编译并真正更新到开发板
- 修改后的节点 `status = "okay"`

可以在开发板上检查运行中的设备树，而不是只看电脑上的 DTS 源文件：

```bash
ls /proc/device-tree/gpio_key
hexdump -C /proc/device-tree/gpio_key/interrupts
tr -d '\0' < /proc/device-tree/gpio_key/compatible
```

## 7. 当前还可以继续完善的地方

正式产品中的按键更常接入 Linux input 子系统，通过 `input_report_key()` 上报标准键值，而不是自定义 `/dev/btnN`。本文保留字符设备方式，是为了把 GPIO、中断、定时器、自旋锁和上下层 operations 的调用关系看得更清楚。

# 五、小结

这篇按键驱动把前面学过的字符设备、platform 驱动、设备树和 GPIO 串到了一起，又加入了中断、内核定时器和自旋锁。

1. 机械按键会抖动，中断发生后延迟一小段时间再读取 GPIO，可以得到更稳定的状态
2. 中断上半部只通过 `mod_timer()` 安排后续工作，真正的状态判断放到定时器回调中
3. `timer_setup()` 负责初始化，`mod_timer()` 负责启动或更新时间，`del_timer_sync()` 负责退出时同步清理
4. `request_irq()` 的 `dev_id` 用来传递当前按键对象，申请和释放时必须使用相同指针
5. 定时器回调收到的是 `struct timer_list *`，通过 `from_timer()` 可以找回它所属的 `struct btn_dev`
6. 共享状态由 `btn_dev.c` 自己加锁管理，`btn_drv.c` 只通过 operations 读取结果
7. 设备树的中断属性必须写成 `interrupts`，GPIO 能解析成功不代表中断属性也一定正确
