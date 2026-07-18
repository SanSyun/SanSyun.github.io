---
title: "Linux驱动开发：内核定时器消抖下的按键中断设计"
description: "梳理 Linux 驱动中的阻塞与非阻塞 IO、等待队列、poll 和异步通知，并总结按键中断程序相对上一版的改进。"
date: "2026-07-16"
draft: false
featured: false
tags: ["RK3568", "Linux Driver", "Wait Queue", "poll", "SIGIO"]
readingTime: "30 分钟"
category: "驱动开发"
---

# 一、阻塞与非阻塞 IO

## 1. 阻塞和非阻塞简介

### 什么是阻塞 IO

阻塞 IO 是指：应用程序调用 IO 函数后，如果当前条件不满足，调用者就暂时停止运行，直到条件满足后再继续执行。

例如，应用程序调用 `read()` 读取设备：

```c
ret = read(fd, buf, sizeof(buf));
```

如果驱动中还没有数据，阻塞式读取会让当前进程进入睡眠。等设备产生数据后，驱动唤醒进程，`read()` 再返回读取结果。

![阻塞读取的基本流程](/images/notes/linux_poll/1784343149097.png)

进程睡眠期间不会持续占用 CPU，调度器可以安排其他进程运行。因此，对于数据到达时间不确定的设备，阻塞 IO 通常比应用程序反复查询更节省资源。

下图中，测试程序的状态为 `S`，表示它正在睡眠等待事件，此时不会持续占用 CPU。

![进程进入睡眠等待数据](/images/notes/linux_poll/waitquene.png)

### 什么是非阻塞 IO

非阻塞 IO 是指：无论当前是否有数据，IO 函数都要尽快返回，不能让调用进程一直等待。

应用程序一般在打开文件时加入 `O_NONBLOCK`：

```c
fd = open("/dev/xxx", O_RDONLY | O_NONBLOCK);
```

此后调用 `read()`：

- 有数据：正常返回读取长度
- 没有数据：立即返回 `-1`，并将 `errno` 设置为 `EAGAIN` 或 `EWOULDBLOCK`

`EAGAIN` 表示“当前暂时没有数据，请稍后再试”，并不表示设备永久出错。

驱动中通常通过 `file->f_flags` 判断文件是否以非阻塞方式打开：

```c
if (file->f_flags & O_NONBLOCK) {
    if (!data_ready)
        return -EAGAIN;
}
```

### 阻塞和非阻塞的区别

| 对比项 | 阻塞 IO | 非阻塞 IO |
| :-- | :-- | :-- |
| 没有数据时 | 进程睡眠 | 立即返回 |
| 典型返回 | 等到有数据后返回 | 返回 `-EAGAIN` |
| CPU 占用 | 等待期间较低 | 取决于应用程序写法 |
| 程序结构 | 简单，适合等待单个设备 | 适合事件循环或配合 IO 多路复用 |

非阻塞 IO 本身不会造成高 CPU 占用。真正的问题通常是应用程序不停地重复查询：

```c
while (1) {
    read(fd, buf, sizeof(buf));
}
```

如果每次 `read()` 都立即返回，这个循环就会一直占用 CPU，这种方式叫作**忙轮询**或**忙等**。

![忙轮询造成 CPU 占用升高](/images/notes/linux_poll/轮询.png)

## 2. 等待队列

### 等待队列的作用

等待队列是 Linux 内核中实现阻塞 IO 的常用机制。它主要解决两个问题：

1. 条件不满足时，让进程进入睡眠
2. 条件满足后，唤醒正在等待的进程

可以把等待队列理解成一个“等待名单”：

![阻塞 IO 与非阻塞 IO 对比](/images/notes/linux_poll/1784343090021.png)

等待队列本身不保存设备数据，它只负责组织等待者。设备数据是否已经准备好，仍然需要通过状态变量、缓冲区长度或 FIFO 是否为空来判断。

### 等待队列头

等待队列通常由 `wait_queue_head_t` 表示：

```c
wait_queue_head_t read_wait;
```

动态初始化：

```c
init_waitqueue_head(&read_wait);
```

也可以在定义时静态初始化：

```c
DECLARE_WAIT_QUEUE_HEAD(read_wait);
```

### 进入等待状态

驱动中常用 `wait_event_interruptible()` 等待条件成立：

```c
ret = wait_event_interruptible(read_wait, data_ready);
if (ret)
    return ret;
```

它的含义是：

- `data_ready` 为真：直接继续执行
- `data_ready` 为假：当前进程进入可中断睡眠
- 睡眠期间收到信号：函数提前返回非 0 值
- 被唤醒后：重新判断 `data_ready`

这里的条件不是只判断一次。进程每次被唤醒后都会重新检查条件，只有条件真正成立时才继续执行。

因此，等待队列的核心不是“有没有调用唤醒函数”，而是“唤醒后条件是否成立”。

### 唤醒等待进程

数据准备完成后，可以调用：

```c
wake_up_interruptible(&read_wait);
```

正确顺序一般是：

```c
data_ready = 1;
wake_up_interruptible(&read_wait);
```

也就是先修改条件，再执行唤醒。

如果先唤醒再修改条件，被唤醒的进程可能仍然看到旧状态，然后再次睡眠，从而错过本次事件。

### 可中断睡眠和不可中断睡眠

Linux 中常见的两类等待是：

| 等待方式 | 进程状态 | 能否被信号打断 |
| :-- | :-- | :-- |
| `wait_event()` | 不可中断睡眠 | 不能 |
| `wait_event_interruptible()` | 可中断睡眠 | 可以 |

字符设备的阻塞读取通常优先使用 `wait_event_interruptible()`。这样用户发送终止信号时，应用程序可以退出等待，而不会一直卡在驱动中。

### 等待队列的通用读取模板

```c
static ssize_t xxx_read(struct file *file,
                        char __user *buf,
                        size_t len,
                        loff_t *offset)
{
    int ret;

    if (file->f_flags & O_NONBLOCK) { //非阻塞：没有数据就返回 -EAGAIN
        if (!data_ready)
            return -EAGAIN;
    } else { //阻塞：没有数据就等待
        ret = wait_event_interruptible(read_wait, data_ready);
        if (ret)
            return ret;
    }

    /* 读取已经准备好的数据 */
    /* 将数据复制到用户空间 */
    /* 更新 data_ready */

    return data_len;
}
```

## 3. 轮询

### 忙轮询

最简单的轮询方式是应用程序不断查询设备：

```c
while (1) {
    if (read(fd, buf, sizeof(buf)) > 0)
        handle_data(buf);
}
```

这种方式实现简单，但没有数据时仍然不断执行，容易造成 CPU 浪费。

在循环中加入 `sleep()` 可以降低 CPU 占用，但也会增加事件响应延迟：

```c
while (1) {
    read(fd, buf, sizeof(buf));
    usleep(100000);
}
```

休眠时间太短，CPU 查询仍然频繁；休眠时间太长，设备产生数据后又不能及时处理。因此，单纯依靠固定延时并不是理想的事件等待方式。

### IO 多路复用

Linux 提供了 `select()`、`poll()` 和 `epoll` 等 IO 多路复用接口。

它们可以让一个进程同时等待多个文件描述符：

![等待队列工作流程](/images/notes/linux_poll/1784343551236.png)

这里虽然也使用了“轮询”这个词，但它与用户程序不停调用 `read()` 的忙轮询不同。等待期间，进程可以在内核中睡眠，并不会持续占用 CPU。

### `select`、`poll` 和 `epoll` 的简单区别

| 接口 | 特点 | 常见使用场景 |
| :-- | :-- | :-- |
| `select()` | 使用位图管理描述符，接口较早 | 描述符数量少、兼容性要求高 |
| `poll()` | 使用 `pollfd` 数组，没有位图大小限制 | 同时监听少量或中等数量描述符 |
| `epoll` | 使用内核维护的就绪集合 | 大量描述符和长期事件循环，网络编程常用 |

### 用户空间的 `poll()` 基本写法

```c
struct pollfd pfd;

pfd.fd = fd;
pfd.events = POLLIN;
pfd.revents = 0;

ret = poll(&pfd, 1, -1);
if (ret < 0) {
    /* 发生错误或被信号中断 */
} else if (ret == 0) {
    /* 等待超时 */
} else if (pfd.revents & POLLIN) {
    /* 文件可读，可以调用 read */
}
```

`poll()` 的最后一个参数是超时时间，单位为毫秒：

| timeout | 含义 |
| :-- | :-- |
| `< 0` | 一直等待，直到有事件或被信号中断 |
| `= 0` | 立即检查，不进入等待 |
| `> 0` | 最多等待指定毫秒数 |

`poll()` 返回值含义如下：

- 小于 0：调用失败或被信号中断
- 等于 0：等待超时
- 大于 0：至少有一个文件描述符产生事件

## 4. Linux 驱动下的 `poll` 操作函数

### `.poll` 的作用

应用程序调用 `select()`、`poll()` 或 `epoll` 时，VFS 会调用驱动提供的 `.poll` 操作函数。

驱动的 `.poll` 主要完成两件事：

1. 告诉内核应该在哪个等待队列上等待
2. 返回设备当前的就绪状态

通用模板如下：

```c
static __poll_t xxx_poll(struct file *file, poll_table *wait)
{
    __poll_t mask = 0;

    poll_wait(file, &read_wait, wait);

    if (data_ready)
        mask |= POLLIN | POLLRDNORM;

    return mask;
}
```

然后注册到 `file_operations`：

```c
static const struct file_operations xxx_fops = {
    .owner = THIS_MODULE,
    .read  = xxx_read,
    .poll  = xxx_poll,
};
```

### `poll_wait()` 的作用

`poll_wait()` 的名字中虽然有 `wait`，但它不会直接让驱动回调睡眠。

它的作用是把当前文件与等待队列建立联系，可以理解为：**如果应用程序需要等待，请让它等待在这个队列上**

是否真正进入睡眠、等待多久，由 VFS 中的 `select()`、`poll()` 或 `epoll` 代码决定。

因此，不要在 `.poll` 回调中自己调用 `wait_event_interruptible()`。`.poll` 应该快速登记等待队列、检查状态并返回。

### 为什么登记后还要检查状态

`.poll` 不能只调用 `poll_wait()`，还必须检查设备当前是否已经就绪。

原因是数据可能在应用调用 `poll()` 之前就已经到达。如果设备当前已有数据，`.poll` 应立即返回可读掩码，不能让应用再次进入睡眠。

典型顺序是：

```c
poll_wait(file, &read_wait, wait);

if (data_ready)
    return POLLIN | POLLRDNORM;

return 0;
```

这种“先登记等待队列，再检查状态”的写法，也可以避免检查状态与登记等待之间出现事件而造成漏唤醒。

### `.poll` 返回的是事件掩码

`.poll` 返回值不是数据内容，也不是普通的负错误码，而是事件掩码。

常见返回值包括：

| 掩码 | 含义 |
| :-- | :-- |
| `POLLIN` | 有数据可读 |
| `POLLRDNORM` | 有普通数据可读 |
| `POLLOUT` | 可以写入数据 |
| `POLLERR` | 设备发生错误 |
| `POLLHUP` | 设备挂断 |

字符设备有普通数据可读时，通常返回：

```c
POLLIN | POLLRDNORM
```

如果设备发生错误，应返回 `POLLERR`，不应直接返回 `-EINVAL` 或 `-ENODEV`。因为负数会被当作一组位全部置位的掩码，可能让用户空间误以为多种事件同时发生。

### 从驱动到应用的唤醒过程

![poll 操作的调用关系](/images/notes/linux_poll/1784344635818.png)

`.poll` 只负责报告“能不能读”，真正的数据仍然由 `.read` 返回。

## 5. 常用 API

### 阻塞与非阻塞相关 API

| API / 标志 | 所在位置 | 作用 |
| :-- | :-- | :-- |
| `O_NONBLOCK` | 应用和驱动 | 以非阻塞方式访问文件 |
| `file->f_flags` | 驱动 | 查看文件打开标志 |
| `-EAGAIN` | 驱动返回值 | 表示当前暂时没有数据 |

### 等待队列相关 API

| API | 作用 |
| :-- | :-- |
| `DECLARE_WAIT_QUEUE_HEAD(name)` | 定义并静态初始化等待队列头 |
| `init_waitqueue_head(&wq)` | 动态初始化等待队列头 |
| `wait_event(wq, condition)` | 条件不成立时进入不可中断睡眠 |
| `wait_event_interruptible(wq, condition)` | 条件不成立时进入可中断睡眠 |
| `wake_up(&wq)` | 唤醒等待队列中的进程 |
| `wake_up_interruptible(&wq)` | 唤醒处于可中断睡眠的进程 |

常用头文件：

```c
#include <linux/wait.h>
```

### 驱动 `poll` 相关 API

| API / 掩码 | 作用 |
| :-- | :-- |
| `poll_wait(file, &wq, wait)` | 登记当前文件使用的等待队列 |
| `POLLIN` | 普通数据可读 |
| `POLLRDNORM` | 普通优先级数据可读 |
| `POLLOUT` | 普通数据可写 |
| `POLLERR` | 设备发生错误 |
| `POLLHUP` | 设备挂断 |

常用头文件：

```c
#include <linux/poll.h>
```

不同内核版本中 `.poll` 的函数声明可能分别使用 `unsigned int` 或 `__poll_t`。编写驱动时，应以当前内核中 `struct file_operations` 的定义为准。

# 二、异步通知

## 1. 异步通知简介

异步通知是一种由驱动主动通知应用程序的机制。

阻塞读取和 `poll()` 都是应用程序先发起等待：

```text
应用程序：我准备好等待设备事件了
```

异步通知则是应用先完成登记，然后继续执行其他任务。设备产生事件时，内核向应用发送信号：

![poll 等待与唤醒流程](/images/notes/linux_poll/1784344773109.png)

Linux 字符驱动通常使用 `SIGIO` 实现异步 IO 通知。

需要注意，`SIGIO` 只是通知应用“设备状态发生了变化”或“设备现在可以读取”，并不直接携带完整的设备数据。应用收到信号后，通常还要调用 `read()` 读取数据。另外，**应用处理函数应当只负责短平快的任务，不得处理过于复杂的逻辑**。

### 异步通知与 `poll` 的区别

| 对比项 | `poll` / `select` | 异步通知 |
| :-- | :-- | :-- |
| 通知方向 | 应用主动等待 | 驱动主动发送信号 |
| 应用主要接口 | `poll()`、`select()` | `signal()`、`sigaction()`、`fcntl()` |
| 驱动主要接口 | `.poll`、`poll_wait()` | `.fasync`、`kill_fasync()` |
| 数据读取 | 就绪后调用 `read()` | 收到信号后调用 `read()` |
| 适用场景 | 统一监听多个描述符 | 设备事件较少、希望信号驱动处理 |

两种机制并不冲突。同一个驱动可以同时支持阻塞读取、`poll` 和异步通知，由应用程序选择合适的使用方式。

## 2. 驱动中的信号处理

### 保存异步通知队列

驱动需要使用一个 `struct fasync_struct` 指针保存异步通知关系：

```c
struct fasync_struct *async_queue;
```

这个队列记录哪些文件启用了异步通知，以及信号应该发送给哪些进程或进程组。

驱动不需要自己操作 `struct fasync_struct` 的内部成员，内核已经提供了辅助函数。

### 实现 `.fasync` 操作函数

通用写法如下：

```c
static int xxx_fasync(int fd, struct file *file, int on)
{
    return fasync_helper(fd, file, on, &async_queue);
}
```

然后注册到 `file_operations`：

```c
static const struct file_operations xxx_fops = {
    .owner  = THIS_MODULE,
    .read   = xxx_read,
    .fasync = xxx_fasync,
};
```

参数 `on` 表示开启还是关闭异步通知：

- `on != 0`：把当前文件加入异步通知队列
- `on == 0`：把当前文件从异步通知队列移除

应用修改文件的 `FASYNC` 标志时，VFS 会调用驱动的 `.fasync` 操作函数。

### 发送异步信号

设备数据准备完成后，驱动调用：

```c
kill_fasync(&async_queue, SIGIO, POLL_IN);
```

三个参数分别表示：

| 参数 | 含义 |
| :-- | :-- |
| `&async_queue` | 异步通知队列 |
| `SIGIO` | 发送的信号 |
| `POLL_IN` | 通知原因是有输入数据可读 |

通常可以先判断队列是否为空：

```c
if (async_queue)
    kill_fasync(&async_queue, SIGIO, POLL_IN);
```

这里的 `POLL_IN` 用于 `kill_fasync()` 的 band 参数。它与驱动 `.poll` 返回的 `POLLIN` 名字相似，但使用位置不同：

```text
kill_fasync() 使用 POLL_IN
.poll 返回值使用 POLLIN | POLLRDNORM
```

### 关闭文件时清理异步通知关系

文件关闭时，应把当前文件从异步通知队列中移除：

```c
static int xxx_release(struct inode *inode, struct file *file)
{
    return xxx_fasync(-1, file, 0);
}
```

如果不清理，驱动可能继续保留已经关闭的文件关系。

### 驱动侧的完整流程

![异步通知的基本流程](/images/notes/linux_poll/1784345109015.png)

## 3. 应用程序对异步通知的处理

在已经发表的[《Linux应用开发》](/notes/linux-app)中，输入系统应用编程部分也介绍过 `poll` 和 `SIGIO` 的应用层使用方法。那篇文章主要关注应用程序怎样使用已有输入设备，本文这里更侧重梳理应用与自定义字符驱动之间的异步通知关系。

### 注册 `SIGIO` 信号处理函数

简单程序可以使用 `signal()`：

```c
signal(SIGIO, sigio_handler);
```

更推荐使用 `sigaction()`：

```c
struct sigaction action = {0};

action.sa_handler = sigio_handler;
sigemptyset(&action.sa_mask);
action.sa_flags = 0;

sigaction(SIGIO, &action, NULL);
```

`sigaction()` 可以明确设置信号屏蔽字和处理标志，控制能力比 `signal()` 更完整。

### 设置异步通知接收者

应用程序需要告诉内核：这个文件产生异步事件时，信号应该发送给谁。

```c
fcntl(fd, F_SETOWN, getpid());
```

这里的 `getpid()` 表示把当前进程设置为信号接收者。

### 开启 `FASYNC`

先读取文件原有标志：

```c
flags = fcntl(fd, F_GETFL);
```

然后在保留原标志的基础上加入 `FASYNC`：

```c
fcntl(fd, F_SETFL, flags | FASYNC);
```

不能简单写成：

```c
fcntl(fd, F_SETFL, FASYNC);
```

否则可能覆盖文件原来的 `O_NONBLOCK` 等标志。

### 信号处理函数的注意事项

信号可能在程序执行到任意位置时到来，因此信号处理函数应尽量短小。

一般建议：

- 不在 handler 中执行复杂业务
- 不在 handler 中调用 `printf()` 等非异步信号安全函数
- 只设置 `sig_atomic_t` 标志，或使用其他异步信号安全操作
- 在主循环或专门线程中完成真正的数据读取和处理

普通信号也不能用来精确统计事件次数。同一种信号连续到来时可能合并，所以不能简单认为“一个 `SIGIO` 一定对应一条数据”。

更可靠的处理方式是：

1. 驱动使用 FIFO 保存事件
2. 信号只表示“FIFO 中可能有数据”
3. 应用收到通知后循环读取
4. 一直读到非阻塞 `read()` 返回 `EAGAIN`

## 4. 常用 API

### 驱动侧 API

| API / 类型 | 作用 |
| :-- | :-- |
| `struct fasync_struct *` | 保存异步通知关系 |
| `fasync_helper(fd, file, on, &queue)` | 添加或删除异步通知关系 |
| `kill_fasync(&queue, SIGIO, POLL_IN)` | 向登记的进程发送异步 IO 信号 |
| `.fasync` | 文件异步通知操作函数 |

常用头文件：

```c
#include <linux/fs.h>
#include <linux/fcntl.h>
#include <linux/signal.h>
```

### 应用侧 API

| API / 标志 | 作用 |
| :-- | :-- |
| `signal(SIGIO, handler)` | 简单注册 `SIGIO` 处理函数 |
| `sigaction(SIGIO, &action, NULL)` | 注册并配置 `SIGIO` 处理方式 |
| `fcntl(fd, F_SETOWN, pid)` | 指定异步信号接收者 |
| `fcntl(fd, F_GETFL)` | 取得文件当前状态标志 |
| `fcntl(fd, F_SETFL, flags | FASYNC)` | 开启异步通知 |
| `getpid()` | 取得当前进程 ID |
| `pause()` | 睡眠等待信号 |

常用头文件：

```c
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
```

# 三、改进思路

上一篇按键驱动已经实现了下面这条处理链：

![驱动异步通知流程](/images/notes/linux_poll/1784345294494.png)

当时的应用程序需要反复调用 `read()` 查询状态。即使没有按键事件，程序也会一直运行。

当前程序主要增加了三种事件通知能力。

## 1. 增加阻塞与非阻塞读取

当前驱动会检查 `O_NONBLOCK`：

- 阻塞方式打开：没有事件时进入等待队列
- 非阻塞方式打开：没有事件时返回 `-EAGAIN`

这样，应用程序可以根据自身结构选择读取方式，不再只能不断查询当前状态。

## 2. 增加等待队列

每个按键增加了自己的等待队列头。

定时器完成消抖并确认按键事件后，程序先更新按键状态，再调用 `wake_up_interruptible()` 唤醒等待者。

![应用程序处理异步通知的流程](/images/notes/linux_poll/1784345349520.png)

没有按键事件时，应用程序可以进入睡眠，CPU 占用明显低于忙轮询版本。

## 3. 增加 `poll` 支持

字符设备层新增 `.poll` 操作，底层通过 `poll_wait()` 登记按键等待队列。

当按键状态不是 `BTN_KEEP` 时，驱动返回：

```c
POLLIN | POLLRDNORM
```

因此，应用程序可以使用 `select()` 或 `poll()` 等待按键设备可读，再调用 `read()` 读取具体状态。

## 4. 增加异步通知

每个按键增加了异步通知队列，驱动新增 `.fasync` 操作。

定时器确认有效按键事件后，程序调用：

```c
kill_fasync(&async_queue, SIGIO, POLL_IN);
```

应用程序启用 `FASYNC` 后，就可以在按键事件到来时收到 `SIGIO`。

等待队列和异步通知分别服务不同的使用方式：

```text
wake_up_interruptible() //阻塞 read、select、poll

kill_fasync() //SIGIO 异步通知
```

## 5. 状态变量改为原子类型

上一篇使用普通整型和自旋锁保护按键状态，当前程序将状态改为 `atomic_t`，并使用：

```c
atomic_read()
atomic_set()
```

这样可以避免定时器回调与进程上下文同时访问状态时出现普通读写竞争。

如果要进一步减少“读取状态”和“清除状态”之间的竞态窗口，可以使用：

```c
status = atomic_xchg(&status, BTN_KEEP);
```

它可以在一次原子操作中完成读取和清除。
