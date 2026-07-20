---
title: "Linux驱动开发：基于TCP与多线程的GPIO、I²C、SPI综合测试"
description: "基于 RK3568 完成一次 Linux 驱动综合测试：板端以 TCP Server 对接三个客户端，通过多线程分别控制 LED、读取 AP3216C，并读写 W25Q64。"
date: "2026-07-20"
draft: false
featured: false
tags: ["RK3568", "Linux Driver", "TCP", "Pthread", "epoll", "GPIO", "I2C", "SPI", "AP3216C", "W25Q64"]
readingTime: "30 分钟"
category: "驱动开发"
---

前面的文章分别学习了字符设备、设备树 GPIO 驱动和 Linux 应用编程。这一次不再只验证某一个接口，而是把 GPIO、I²C、SPI、TCP 网络通信和多线程编程串起来，完成一个小型的板端综合测试程序。

整个工程分为驱动层和应用层。驱动层向用户空间提供 `/dev/led0`、`/dev/ap3216c0` 和 `/dev/w25q640` 三个设备节点；应用层在 RK3568 开发板上运行 TCP Server，接收 PC 端三个 Client 的请求，再把网络命令转换成对字符设备的 `read`、`write` 和 `lseek` 操作。

这篇文章主要记录本次测试的设计思路。重点不在逐行解释全部代码，而在于说明 I²C、SPI 驱动怎样接入内核子系统，以及板端应用怎样把网络线程、设备线程和自定义协议组织起来。

# 一、测试要求

本次测试要求如下：

![Linux 测试 1 的任务要求](/images/notes/linux_test/requirements.png)

1. 在 RK3568 开发板上运行一个 TCP Server，在 PC 虚拟机 Ubuntu 中运行三个 TCP Client
2. 三个 Client 分别对应 LED、I²C 和 SPI 功能
3. Client 1 读取或设置 GPIO LED 状态，同时使用前面编写的设备树 LED 驱动
4. Client 2 接收 AP3216C 采集的红外光、环境光和接近传感数据
5. Client 3 按指定地址和长度读写 W25Q64 Flash

本次通信使用一套简单的二进制帧协议。三类数据共用相同的固定头部，再根据设备类型组织各自的数据域。

![LED、I²C 与 SPI 的 TCP 通信协议](/images/notes/linux_test/tcp-protocol.png)

公共帧格式可以整理为：

| 字段 | 长度 | 含义 |
| :-- | :-- | :-- |
| 帧头 | 2 字节 | 固定为 `0xEB 0x9C` |
| 方向 | 1 字节 | `0x00` 表示发往 ARM，`0x01` 表示发往 PC |
| 数据长度 | 4 字节 | 数据域的有效字节数，按大端顺序传输 |
| 数据域 | 可变 | 不同设备对应不同内容 |

三类数据域分别为：

| 类型 | 数据域 |
| :-- | :-- |
| LED | 读写标志 1 字节 + LED 状态 1 字节 |
| AP3216C | IR 2 字节 + ALS 2 字节 + PS 2 字节 |
| W25Q64 | 读写标志 1 字节 + 地址 4 字节 + 长度 4 字节 + 读写数据 |

其中，读写标志 `0x00` 表示读，`0x01` 表示写。地址、长度以及 AP3216C 的三个测量值都按高字节在前的顺序组帧，避免 PC 和 ARM 对多字节整数的理解不一致。

从整体调用关系看，本次测试可以压缩成下面这条链路：

![Linux 综合测试的分层结构](/images/notes/linux_test/1784532063626.png)

# 二、驱动层设计思路

## 1. LED 驱动设计

LED 部分没有重新编写驱动，而是直接复用前面设备树 GPIO 驱动实验生成的 `.ko` 文件。

这样处理的好处是，本次综合测试只需要关心字符设备接口，不需要在 TCP 应用中直接操作 GPIO 寄存器，也不需要把 LED 的引脚编号写死在用户程序里。

LED 线程打开设备节点：

```c
int fd = open("/dev/led0", O_RDWR);
```

收到读命令时，通过 `read` 获取当前状态：

```c
ret = read(fd, &led.status, sizeof(led.status));
```

收到写命令时，通过 `write` 设置亮灭状态：

```c
ret = write(fd, &led.status, sizeof(led.status));
```

因此，LED 部分在本次测试中的重点不是驱动内部如何控制 GPIO，而是验证前面写好的驱动能否作为一个稳定接口，被更复杂的多线程网络应用复用。

## 2. AP3216C 驱动设计

### I²C 子系统的基本结构

I²C 是一种同步、串行、半双工通信总线。总线通常由两根信号线组成：

- `SCL`：时钟线，由主机产生时钟
- `SDA`：数据线，用于双向传输数据

同一条 I²C 总线上可以挂接多个从设备，每个从设备通过地址进行区分。对于 Linux 驱动来说，不需要在外设驱动中手动模拟起始信号、应答位和停止信号，而是通过 I²C 子系统提交消息，由具体控制器驱动完成底层时序。

Linux I²C 子系统中最需要先认识三个对象：

| 对象 | 作用 |
| :-- | :-- |
| `i2c_adapter` | 表示一个 I²C 控制器，也就是总线主机 |
| `i2c_client` | 表示挂接在总线上的一个 I²C 从设备，保存设备地址等信息 |
| `i2c_driver` | 表示从设备驱动，提供 `probe`、`remove` 和匹配表 |

设备树节点与驱动中的 `of_match_table` 匹配后，I²C 核心会调用驱动的 `probe`。使用的匹配字符串是：

```c
static const struct of_device_id ap3216c_of_match[] = {
    { .compatible = "sc,ap3216c" },
    { }
};
```

驱动入口通过 `i2c_add_driver()` 注册 `i2c_driver`，退出时通过 `i2c_del_driver()` 注销：

```c
static int __init ap3216c_init(void)
{
    return i2c_add_driver(&ap3216c_driver);
}

static void __exit ap3216c_exit(void)
{
    i2c_del_driver(&ap3216c_driver);
}
```

### AP3216C 简介

AP3216C 是一颗集成式光学传感器，可以同时测量三类数据：

- IR：红外光强度
- ALS：环境光强度
- PS：接近传感数据

本次驱动使用的主要寄存器如下：

| 寄存器 | 地址 | 作用 |
| :-- | :-- | :-- |
| `AP3216C_SYSCFG` | `0x00` | 系统配置、复位和工作模式选择 |
| `AP3216C_IR_DATAL/H` | `0x0A/0x0B` | IR 数据低字节和高字节 |
| `AP3216C_ALS_DATAL/H` | `0x0C/0x0D` | ALS 数据低字节和高字节 |
| `AP3216C_PS_DATAL/H` | `0x0E/0x0F` | PS 数据低字节和高字节 |

设备打开时，驱动先向系统配置寄存器写入 `0x04` 完成软件复位，延时 50 ms 后再写入 `0x03`，启用 ALS、PS 和 IR 三类数据输出。

```c
dat = 0x04;
ap3216c_write_reg(dev, AP3216C_SYSCFG, &dat, 1);
mdelay(50);

dat = 0x03;
ap3216c_write_reg(dev, AP3216C_SYSCFG, &dat, 1);
```

### AP3216C 的读写设计

AP3216C 读取寄存器时，需要先告诉设备要读取哪个寄存器，再接收寄存器数据。因此构造了两个 `i2c_msg`：

```c
struct i2c_msg msg[2];

msg[0].addr  = client->addr;
msg[0].flags = 0;
msg[0].buf   = &addr;
msg[0].len   = 1;

msg[1].addr  = client->addr;
msg[1].flags = I2C_M_RD;
msg[1].buf   = val;
msg[1].len   = len;

ret = i2c_transfer(client->adapter, msg, 2);
```

第一条消息是写操作，只发送寄存器地址；第二条消息设置 `I2C_M_RD`，读取指定长度的数据。I²C 控制器驱动会把这两条消息组织成一次完整的总线传输。

写寄存器时，我把寄存器地址和数据拼到同一个缓冲区中，再提交一条写消息：

```text
[寄存器地址] [数据 0] [数据 1] ...
```

读取传感数据时，驱动连续读取 `0x0A` 到 `0x0F` 六个寄存器，再根据 AP3216C 的数据格式组合出 IR、ALS 和 PS。IR 与 PS 还需要检查无效数据标志位：

```c
dev->ir  = ((uint16_t)buf[1] << 2) | (buf[0] & 0x03);
dev->als = ((uint16_t)buf[3] << 8) | buf[2];
dev->ps  = ((uint16_t)buf[5] << 4) | (buf[4] & 0x0f);
```

驱动最后把三个 `uint16_t` 按 `IR、ALS、PS` 的顺序复制到用户空间。应用程序只需要读取 `/dev/ap3216c0`，不再关心每个传感数据分布在哪些寄存器中。

### 字符设备封装

`ap3216c_probe()` 中主要完成以下工作：

1. 使用 `devm_kzalloc()` 分配并清零设备结构体
2. 使用 `alloc_chrdev_region()` 动态申请设备号
3. 使用 `cdev_init()` 和 `cdev_add()` 注册字符设备
4. 使用 `class_create()` 与 `device_create()` 创建 `/dev/ap3216c0`
5. 保存 `i2c_client`，并通过 `i2c_set_clientdata()` 绑定私有数据

这里通过 `container_of()` 从当前文件对应的 `cdev` 找回完整的 `ap3216c_dev`：

```c
struct cdev *cdev = filp->f_path.dentry->d_inode->i_cdev;
ap3216c_dev *dev = container_of(cdev, ap3216c_dev, cdev);
```

这样每个字符设备实例都能找到自己对应的 I²C 客户端和传感数据。

### I²C 驱动常用 API 总结

| API | 作用 |
| :-- | :-- |
| `i2c_add_driver()` | 向 I²C 核心注册从设备驱动 |
| `i2c_del_driver()` | 注销 I²C 驱动 |
| `i2c_transfer()` | 向适配器提交一组 I²C 消息 |
| `i2c_set_clientdata()` | 在 `i2c_client` 中保存驱动私有数据 |
| `i2c_get_clientdata()` | 在 `remove` 等位置取回私有数据 |
| `devm_kzalloc()` | 分配由设备生命周期管理的内存 |
| `copy_to_user()` | 把内核缓冲区数据复制到用户空间 |

如果设备只进行简单的单字节或 SMBus 兼容访问，也可以使用 `i2c_smbus_read_byte_data()`、`i2c_smbus_write_byte_data()` 等封装接口。本次实验直接使用 `i2c_transfer()`，能够更直观地看到“先写寄存器地址，再读取数据”的消息组合过程。

## 3. W25Q64 驱动设计

### SPI 子系统的基本结构

SPI 同样是同步串行总线，但它通常使用四根信号线：

- `SCLK`：串行时钟
- `MOSI`：主机发送、从机接收
- `MISO`：主机接收、从机发送
- `CS`：片选信号

SPI 没有像 I²C 那样统一的从设备地址阶段，而是通过片选信号选择设备。通信模式由时钟极性 CPOL 和时钟相位 CPHA 共同决定，常见模式为 Mode 0 到 Mode 3。

Linux SPI 子系统中的几个核心对象如下：

| 对象 | 作用 |
| :-- | :-- |
| `spi_controller` | 表示 SPI 控制器，旧内核中也常称为 `spi_master` |
| `spi_device` | 表示挂接在控制器上的一个 SPI 从设备 |
| `spi_driver` | 表示 SPI 从设备驱动，提供 `probe`、`remove` 和匹配表 |
| `spi_transfer` | 描述一次发送或接收的数据段 |
| `spi_message` | 把一个或多个 `spi_transfer` 组织成完整事务 |

本次驱动通过 `spi_register_driver()` 注册，并使用设备树匹配字符串 `sc,w25q64`。匹配后进入 `w25q64_probe()`，设置 SPI Mode 0 和 8 位字长：

```c
spi->mode = SPI_MODE_0;
spi->bits_per_word = 8;
ret = spi_setup(spi);
```

### W25Q64 简介

W25Q64 是一颗 64 Mbit SPI NOR Flash，换算后总容量为 8 MiB。它的存储组织和本次用到的指令如下：

| 项目 | 数值 |
| :-- | :-- |
| 总容量 | 8 MiB |
| 页大小 | 256 字节 |
| 扇区大小 | 4 KiB |
| 地址宽度 | 24 位 |

| 指令 | 操作码 | 作用 |
| :-- | :-- | :-- |
| JEDEC ID | `0x9F` | 读取厂商和器件标识 |
| Read Data | `0x03` | 从指定地址读取数据 |
| Read Status Register 1 | `0x05` | 读取状态寄存器 1 |
| Write Enable | `0x06` | 置位写使能锁存器 |
| Page Program | `0x02` | 页编程 |
| Sector Erase | `0x20` | 擦除 4 KiB 扇区 |

**SPI NOR Flash 不能像普通 RAM 一样直接覆盖任意字节。编程只能把位从 1 改为 0；若要把 0 恢复为 1，必须先擦除所在扇区。**页编程又不能跨越 256 字节页边界。因此，Flash 写驱动的主要难点不是发出 `0x02` 指令，而是正确处理擦除、保留原数据、分页编程和忙状态等待。

### W25Q64 的读取设计

读数据时，驱动先发送 1 字节读命令和 3 字节地址，再接收指定长度的数据：

```c
dev->txbuf[0] = W25Q64_CMD_READ_DATA;
dev->txbuf[1] = (addr >> 16) & 0xff;
dev->txbuf[2] = (addr >> 8) & 0xff;
dev->txbuf[3] = addr & 0xff;

xfer[0].tx_buf = dev->txbuf;
xfer[0].len = 4;
xfer[1].rx_buf = buf;
xfer[1].len = len;

spi_sync_transfer(dev->spi, xfer, 2);
```

这两个 `spi_transfer` 属于同一个同步事务。控制器会在保持片选有效的情况下，先发送命令和地址，再读取数据。

字符设备的 `read` 还会检查文件偏移和 Flash 容量边界，并把较大的读取拆成若干块。读取完成后更新 `*offset`，因此用户空间可以像访问普通文件一样连续读取，也可以先用 `lseek` 跳到指定地址。

### W25Q64 的写入设计

本次源码使用“读出整个扇区、修改缓存、擦除扇区、分页写回”的策略支持任意地址写入：

![W25Q64 扇区修改与分页写回流程](/images/notes/linux_test/1784532455218.png)

这种写法牺牲了一部分写入速度和擦写寿命，但逻辑清楚，而且可以保留同一扇区内没有被本次 `write` 覆盖的数据。

每次擦除或页编程前都需要发送写使能指令：

```c
dev->txbuf[0] = W25Q64_CMD_WRITE_ENABLE;
spi_write(dev->spi, dev->txbuf, 1);
```

命令发出后，驱动轮询状态寄存器 1 的 BUSY 位，并使用 jiffies 设置超时，防止硬件异常时永久卡在循环中：

```c
if (!(status & 0x01))
    return 0;

if (time_after_eq(jiffies, deadline))
    return -ETIMEDOUT;
```

驱动还使用互斥锁保护读写过程：

```c
ret = mutex_lock_interruptible(&dev->lock);
/* 完成一组 Flash 读写操作 */
mutex_unlock(&dev->lock);
```

这样可以避免多个进程或线程同时访问共享的命令缓冲区，并防止一个写操作的“读扇区—擦除—写回”过程被另一个访问打断。

### 文件偏移与 `llseek`

应用层协议中的 SPI 地址最终通过 `lseek` 映射为字符设备文件偏移：

```c
lseek(fd, spi.addr, SEEK_SET);
read(fd, spi.data, spi.len);
```

驱动实现了 `.llseek`，支持 `SEEK_SET`、`SEEK_CUR` 和 `SEEK_END`，同时把合法位置限制在 `0` 到 8 MiB 之间。这样，用户程序不需要设计额外的 `ioctl` 来传递 Flash 地址，标准文件接口就足以表达“从哪里开始读写”。

### SPI 驱动常用 API 总结

| API | 作用 |
| :-- | :-- |
| `spi_register_driver()` | 注册 SPI 从设备驱动 |
| `spi_unregister_driver()` | 注销 SPI 驱动 |
| `spi_setup()` | 应用模式、字长、最大频率等配置 |
| `spi_write()` | 完成一次只发送事务 |
| `spi_write_then_read()` | 先发送命令，再接收少量返回数据 |
| `spi_sync_transfer()` | 同步提交一组 `spi_transfer` |
| `spi_set_drvdata()` | 保存驱动私有数据 |
| `spi_get_drvdata()` | 取回驱动私有数据 |
| `mutex_lock_interruptible()` | 可被信号中断地获取互斥锁 |
| `msecs_to_jiffies()` | 把毫秒转换为内核节拍，用于超时控制 |

在 `probe` 中，驱动还通过 `0x9F` 指令读取 JEDEC ID。只有 SPI 模式、片选和接线正确时，才能得到合理的器件标识，因此这一步也可以作为驱动加载阶段最直接的硬件连通性检查。

# 三、应用层设计思路

## 1. 整体线程框架

板端应用运行在 RK3568 上，监听 TCP 端口 `8888`。主线程依次接受三个客户端连接，并按照连接顺序建立固定映射：

| 客户端 | 对应功能 | 设备节点 |
| :-- | :-- | :-- |
| Client 0 | LED 控制 | `/dev/led0` |
| Client 1 | AP3216C 数据 | `/dev/ap3216c0` |
| Client 2 | W25Q64 读写 | `/dev/w25q640` |

三个客户端全部连接后，程序创建五个工作线程：

| 线程 | 作用 |
| :-- | :-- |
| 接收线程 | 使用 `epoll` 监听三个客户端，查找帧头并解析命令 |
| 发送线程 | 根据当前设备类型组帧，并把结果发回对应客户端 |
| LED 线程 | 响应 LED 读写命令 |
| AP3216C 线程 | 每 500 ms 读取一次传感器数据并请求发送 |
| W25Q64 线程 | 响应 Flash 指定地址的读写命令 |

## 2. `epoll` 监听多个客户端

三个客户端套接字在 `accept` 后被设置为非阻塞模式。接收线程创建一个 epoll 实例，把三个文件描述符都以 `EPOLLIN` 事件加入监听集合：

```c
event.events = EPOLLIN;
event.data.fd = connfd[i];
epoll_ctl(epoll_fd, EPOLL_CTL_ADD, connfd[i], &event);
```

之后通过 `epoll_wait()` 阻塞等待。任意客户端有数据时，接收线程只处理已经就绪的文件描述符，不需要依次轮询三个连接。

```c
ready_count = epoll_wait(epoll_fd,
                         ready_events,
                         MAX_CONNECTIONS,
                         -1);
```

对于 LED 和 SPI 客户端，接收线程在缓冲区中查找连续的 `0xEB 0x9C`，找到帧头后按固定偏移解析数据域，再通知对应设备线程。AP3216C 当前采用周期主动上报方式，所以 Client 1 不需要先发送读取命令。

## 3. 条件变量完成线程间通知

接收线程不直接操作设备，而是把命令保存到全局控制结构体，再通过条件变量唤醒业务线程。

以 LED 为例：

![LED 命令在线程之间的处理流程](/images/notes/linux_test/1784532755339.png)

条件变量等待使用 `while` 而不是 `if`：

```c
pthread_mutex_lock(&mutex);
while (!ready_led) {
    pthread_cond_wait(&cond_led, &mutex);
}
ready_led = 0;
pthread_mutex_unlock(&mutex);
```

这样即使线程发生虚假唤醒，也会重新检查条件。`pthread_cond_wait()` 在进入等待时会自动释放互斥锁，被唤醒后再重新加锁，因此其他线程可以安全修改 `ready_led`。

## 4. 三个业务线程的职责

### LED 线程

LED 线程等待 `cond_led`。当 `rw == 0x00` 时读取 LED 状态并唤醒发送线程；当 `rw == 0x01` 时直接把新状态写入设备。

### AP3216C 线程

AP3216C 线程打开 `/dev/ap3216c0` 后，每隔 500 ms 读取三个 `uint16_t` 数据，并通知发送线程把 6 字节传感数据发给 Client 1。

```c
ret = read(fd, databuf, sizeof(databuf));
iic.IR  = databuf[0];
iic.ALS = databuf[1];
iic.PS  = databuf[2];
usleep(500000);
```

传感器一次转换需要一定时间，周期读取还能避免在没有新数据时过于频繁地访问 I²C 总线。

### W25Q64 线程

W25Q64 线程等待 `cond_spi`。读写前先使用 `lseek` 设置地址，再根据 `rw` 调用 `read` 或 `write`。如果是读命令，读取完成后再唤醒发送线程，把地址、长度和数据一并回复给 Client 2。

## 5. 本次应用涉及的技术栈

| 技术 | 在本项目中的用途 |
| :-- | :-- |
| TCP Socket | 建立板端 Server 与三个 PC Client 的可靠字节流连接 |
| 非阻塞 I/O | 避免单个客户端读操作长期阻塞网络线程 |
| `epoll` | 一个线程同时监听三个客户端连接 |
| POSIX 线程 | 把网络收发和三类设备业务拆分执行 |
| 互斥锁 | 保护线程之间共享的状态标志和控制数据 |
| 条件变量 | 在接收、设备和发送线程之间传递处理事件 |
| 字符设备接口 | 用统一的 `open/read/write/lseek` 访问不同硬件 |
| 自定义二进制协议 | 在 PC 与 ARM 之间传递方向、长度、地址和业务数据 |

应用程序通过 Makefile 自动收集 `src` 目录中的源文件，生成依赖文件，最后链接 `pthread`：

```makefile
CC := aarch64-buildroot-linux-gnu-cc
CFLAGS := -g -Wall -Iinc
LDLIBS := -lpthread
```

# 四、实现效果

完成驱动加载和应用启动后，预期能够看到三个客户端分别完成下面的功能。

## 1. LED 客户端

### LED 熄灭

上位机显示 LED 状态为“灭”，开发板上对应 LED 同步熄灭：

![上位机控制开发板 LED 熄灭](/images/notes/linux_test/1784533314884.png)

### LED 点亮

发送点亮命令后，上位机状态切换为“亮”，开发板上的 LED 同步点亮：

![上位机控制开发板 LED 点亮](/images/notes/linux_test/1784533348689.png)

## 2. AP3216C 客户端

正常环境下，客户端可以周期接收红外数据、环境光数据和接近数据：

![AP3216C 正常环境数据](/images/notes/linux_test/1784533452078.png)

遮挡传感器或把物体靠近后，环境光与接近数据发生明显变化，说明 AP3216C 采集、驱动读取和 TCP 上传链路均已跑通：

![AP3216C 遮挡与接近测试数据](/images/notes/linux_test/1784533417907.png)

## 3. W25Q64 客户端

测试地址设置为 `0x3000`，读取长度设置为 9 字节。写入前读取该区域，内容均为擦除态 `0xFF`：

![W25Q64 写入前读取结果](/images/notes/linux_test/1784533561582.png)

随后写入 `01 02 03 04 05 06 07 08 09`，再从相同地址读取 9 字节，回读数据与写入数据完全一致：

![W25Q64 写入后回读结果](/images/notes/linux_test/1784533610549.png)

这说明从客户端命令、TCP 传输、板端 `lseek/write/read`，到驱动内部扇区擦除和分页写回的整条链路工作正常。

## 4. 板端程序运行状态

板端 `APP` 进程运行时可以在系统进程列表中正常观察到，综合测试期间保持运行：

![RK3568 板端 APP 进程运行状态](/images/notes/linux_test/1784533716624.png)

这次实验把前面相对独立的知识点真正连成了一条完整链路：设备树负责描述硬件，I²C、SPI 和 GPIO 子系统负责管理总线与资源，字符设备向用户空间提供统一入口，板端应用再通过多线程和 TCP 协议把硬件能力提供给 PC 客户端。

最终最值得记住的不是某一个函数，而是这种分层方式：

```text
硬件设备
    ↓
Linux 内核子系统与设备驱动
    ↓
字符设备文件
    ↓
板端多线程业务程序
    ↓
TCP 协议
    ↓
PC 客户端
```

当每一层只处理自己的职责后，后续无论是更换传感器、增加新的 SPI 设备，还是把 PC 客户端改成图形界面，都不需要推翻整个工程，只需要调整对应层的实现。
