---
title: "Linux 应用开发入门：编译、输入系统与多线程编程"
description: "从 GCC 编译流程与 Makefile 入手，继续整理输入子系统应用编程、常见编译排错方法，以及 Linux 多线程同步中的互斥锁、信号量和条件变量。"
date: "2026-04-27"
draft: false
featured: false
tags: ["Linux", "GCC", "Makefile", "C", "Build"]
readingTime: "10 分钟"
category: "Linux 应用开发"
---
# 第一部分 GCC编译
## 1. GCC 编译过程

一个 C/C++ 源文件从代码到可执行文件，通常会经过 4 个阶段：

1. 预处理
2. 编译
3. 汇编
4. 链接

日常交流里经常直接把这整套过程统称为“编译”，但拆开理解后，更容易在出错时快速判断问题处在哪一层。

![GCC 编译整体流程示意](/images/notes/gcc-build-flow.png)

### 预处理

```bash
xxx-gcc -E -o hello.i hello.c
```

预处理主要负责这些事情：

- 展开 `#define` 宏定义
- 处理 `#include` 头文件包含
- 处理条件编译，例如 `#if`、`#ifdef`
- 生成预处理后的中间文件 `.i`

如果只是想快速确认当前编译环境里有哪些宏，也可以用：

```bash
xxx-gcc -E -dM hello.c
```

### 编译

```bash
xxx-gcc -S -o hello.s hello.i
```

这一步会把预处理后的 C/C++ 代码翻译成汇编代码，输出 `.s` 文件。

可以把它理解成：从高级语言转换为汇编语言。

### 汇编

```bash
xxx-gcc -c -o hello.o hello.s
```

汇编阶段会把 `.s` 汇编文件转换成目标文件 `.o`。

在 Linux 环境里，这类目标文件通常符合 `ELF` 格式。它已经包含机器代码，但还不能单独运行，需要继续参与链接。

### 链接

```bash
xxx-gcc -o hello hello.o other.o
```

链接阶段会把多个目标文件以及需要的库文件组合起来，最终生成可执行程序。

这一阶段的核心作用是把“函数定义在哪里”“库从哪里找”这些问题全部补齐。

## 2. 常用编译选项

开发时最常用的一组选项如下：

- `-E`：只做预处理
- `-c`：执行预处理、编译、汇编，但不链接
- `-o`：指定输出文件名
- `-I`：指定头文件搜索目录
- `-L`：指定库文件搜索目录
- `-l`：指定要链接的库

其中 `-I`、`-L`、`-l` 很容易一起出现，可以直接记成一组：

- `-I` 找头文件
- `-L` 找库目录
- `-l` 指定库名

## 3. 静态库的生成与使用

先分别生成目标文件：

```bash
gcc -c -o main.o main.c
gcc -c -o sub.o sub.c
```

然后使用 `ar` 把多个目标文件打包成静态库：

```bash
ar crs libsub.a sub.o sub2.o sub3.o
```

最后把静态库链接进可执行文件：

```bash
gcc -o test main.o libsub.a
```

如果静态库不在当前目录下，需要写出它的相对路径或绝对路径。

静态库的特点是：链接时所需代码会被拷贝到最终程序里，因此运行时通常不需要再额外携带 `libsub.a`。

## 4. 动态库的生成与使用

如果希望把库单独发布，或者让多个程序共享同一份实现，可以生成动态库：

```bash
gcc -fPIC -c -o sub.o sub.c
gcc -shared -o libsub.so sub.o sub2.o sub3.o
```

这里的 `-fPIC` 用来生成位置无关代码，通常是在制作动态库时一起使用。

链接动态库时常见写法如下：

```bash
gcc -o test main.o -lsub -L /path/to/lib
```

这里需要注意两点：

- `-L` 后面跟的是库所在目录，不是库文件本身
- `-lsub` 只写库名主体，不写前缀 `lib`，也不写后缀 `.so`

## 5. 优化选项

编译优化最常见的是：

```bash
-O2
```

一般来说：

- 优化等级越高，编译器做的优化越多
- 优化越彻底，编译时间通常也会更长

日常开发里 `-O2` 是比较常见的平衡选择。

## 6. 容易混淆的点

回头看 GCC 相关内容时，最值得优先记住的是下面这些：

- `.i` 是预处理结果
- `.s` 是汇编代码
- `.o` 是目标文件
- `-c` 的含义是“生成目标文件，但不链接”
- `-I`、`-L`、`-l` 分别对应头文件目录、库目录、库名
- 静态库是 `.a`，动态库是 `.so`

# 第二部分 Makefile的使用
## 1. 通用 Makefile 的整理

在 C 项目稍微变大以后，手动敲 `gcc` 命令就会开始变得低效，尤其是下面几件事会变得麻烦：

- 源文件一多，命令容易写漏
- 头文件修改后，哪些目标文件要重新编译不容易管
- 目标文件、依赖文件和最终程序混在一起，目录会越来越乱

下面这份 Makefile 比较适合作为小型 Linux 应用项目的通用模板：

```make
CC := gcc
CFLAGS := -g -Wall -Iinc
LDLIBS := -lpthread

TARGET := main

SRC_DIR := src
OBJ_DIR := obj
DEP_DIR := dep
INC_DIR := inc

MAIN_SRC := main.c
SRCS := $(wildcard $(SRC_DIR)/*.c)
ALL_SRCS := $(MAIN_SRC) $(SRCS)

OBJS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(notdir $(ALL_SRCS)))
DEPS := $(patsubst %.o,$(DEP_DIR)/%.d,$(notdir $(OBJS)))

.PHONY: all dirs run clean

all: dirs $(TARGET)

dirs:
	mkdir -p $(OBJ_DIR) $(DEP_DIR)

$(TARGET): $(OBJS)
	$(CC) $^ -o $@ $(LDLIBS)

$(OBJ_DIR)/%.o: %.c
	$(CC) $(CFLAGS) -MMD -MP -MF $(DEP_DIR)/$*.d -c $< -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -MMD -MP -MF $(DEP_DIR)/$*.d -c $< -o $@

-include $(DEPS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(TARGET) $(OBJ_DIR) $(DEP_DIR)
```

## 2. 这份 Makefile 解决了什么问题

这份脚本的核心目标其实很明确：

- 自动收集源文件
- 把目标文件和依赖文件分目录存放
- 自动生成头文件依赖
- 提供统一的构建、运行和清理入口

如果只写一个最短 Makefile，当然也能编译通过；但一旦项目里出现多个 `.c` 文件、多个头文件，或者需要频繁增删源码，这种结构化写法会省很多事。

## 3. 变量区的作用

最上面这部分是在集中定义“编译时的公共配置”：

```make
CC := gcc
CFLAGS := -g -Wall -Iinc
LDLIBS := -lpthread
```

可以直接这样理解：

- `CC`：使用哪个编译器
- `CFLAGS`：编译阶段要附带哪些选项
- `LDLIBS`：链接阶段需要补哪些库

这里的：

- `-g` 用来保留调试信息，便于配合 `gdb`
- `-Wall` 用来打开常见告警
- `-Iinc` 表示头文件目录在 `inc`
- `-lpthread` 表示最终链接时带上线程库

把这些内容单独抽成变量的好处是，后面需要换编译器、加优化选项、或者替换库时，只改一处就够了。

## 4. 目录变量和目标变量

```make
TARGET := main

SRC_DIR := src
OBJ_DIR := obj
DEP_DIR := dep
INC_DIR := inc
```

这一段是在定义项目结构：

- `TARGET`：最终生成的可执行文件名
- `SRC_DIR`：源文件目录
- `OBJ_DIR`：目标文件目录
- `DEP_DIR`：依赖文件目录
- `INC_DIR`：头文件目录

这里虽然 `INC_DIR` 还没有继续展开使用，但保留它是有意义的。后面如果想把 `-Iinc` 改成 `-I$(INC_DIR)`，或者引入多个头文件目录，就能很自然地扩展。

## 5. 自动收集源文件

```make
MAIN_SRC := main.c
SRCS := $(wildcard $(SRC_DIR)/*.c)
ALL_SRCS := $(MAIN_SRC) $(SRCS)
```

这三行做的事情是：

- 把根目录下的 `main.c` 单独列出来
- 自动收集 `src/` 目录下的所有 `.c` 文件
- 最后合并成完整的源码列表

这样写比较适合下面这种项目布局：

```text
.
├── main.c
├── inc/
├── src/
│   ├── sub1.c
│   ├── sub2.c
│   └── ...
```

它的好处是新增 `src/*.c` 文件时，不需要手动再改 Makefile。

## 6. 从源文件推导目标文件和依赖文件

```make
OBJS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(notdir $(ALL_SRCS)))
DEPS := $(patsubst %.o,$(DEP_DIR)/%.d,$(notdir $(OBJS)))
```

这里是整份脚本里最值得看懂的一段。

### `OBJS` 是怎么来的

假设：

```make
ALL_SRCS = main.c src/sub.c
```

那么：

- `$(notdir $(ALL_SRCS))` 会变成 `main.c sub.c`
- `$(patsubst %.c,$(OBJ_DIR)/%.o,...)` 会继续变成 `obj/main.o obj/sub.o`

这样就完成了“从源码文件名推导出目标文件名”。

### `DEPS` 是怎么来的

如果：

```make
OBJS = obj/main.o obj/sub.o
```

那么：

- `$(notdir $(OBJS))` 会得到 `main.o sub.o`
- 再经过 `patsubst` 就能得到 `dep/main.d dep/sub.d`

`.d` 文件用来记录头文件依赖关系，后面配合 `-include $(DEPS)` 使用。

## 7. `all` 和 `dirs`

```make
.PHONY: all dirs run clean

all: dirs $(TARGET)

dirs:
	mkdir -p $(OBJ_DIR) $(DEP_DIR)
```

这里有两个关键点：

- `all` 是默认目标，直接执行 `make` 时就会走这里
- `dirs` 会先创建 `obj/` 和 `dep/`，避免后面写目标文件或依赖文件时目录不存在

`.PHONY` 的作用是把这些名字声明成伪目标，避免当前目录下如果刚好有同名文件时，`make` 误判成“已经完成”。

## 8. 最终链接规则

```make
$(TARGET): $(OBJS)
	$(CC) $^ -o $@ $(LDLIBS)
```

这条规则的含义是：当所有 `.o` 文件准备好之后，链接生成最终程序。

这里两个自动变量需要记住：

- `$^`：当前规则的所有依赖，这里就是全部目标文件
- `$@`：当前规则的目标，这里就是 `main`

展开后大致可以理解成：

```bash
gcc obj/main.o obj/sub1.o obj/sub2.o -o main -lpthread
```

## 9. 两条编译规则为什么都要有

```make
$(OBJ_DIR)/%.o: %.c
	$(CC) $(CFLAGS) -MMD -MP -MF $(DEP_DIR)/$*.d -c $< -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -MMD -MP -MF $(DEP_DIR)/$*.d -c $< -o $@
```

这两条规则分别对应两类源文件：

- 根目录下的 `main.c`
- `src/` 目录下的其他 `.c`

之所以拆成两条，是因为你的项目源文件本来就来自两个不同位置。如果只保留其中一条，另一部分文件就匹配不到规则。

其中几个自动变量也值得记住：

- `$<`：当前规则的第一个依赖，也就是正在编译的源文件
- `$@`：当前规则生成的目标文件
- `$*`：模式匹配中 `%` 对应的主干名字

例如当编译 `src/sub.c` 时：

- `$<` 是 `src/sub.c`
- `$@` 是 `obj/sub.o`
- `$*` 是 `sub`

所以依赖文件会生成到：

```text
dep/sub.d
```

## 10. `-MMD -MP -MF` 这一组参数的意义

```make
-MMD -MP -MF $(DEP_DIR)/$*.d
```

这一组参数是这份 Makefile 里很实用的一部分。

- `-MMD`：生成依赖文件，但通常不把系统头文件算进去
- `-MP`：为依赖中的头文件补一个伪目标，减少头文件删除后带来的报错
- `-MF`：指定依赖文件输出到哪里

它们配合下面这句：

```make
-include $(DEPS)
```

就能让 `make` 在下次构建时自动知道：

- 哪个 `.o` 依赖了哪个 `.h`
- 某个头文件改动后，应该触发哪些文件重新编译

这也是 Makefile 从“能用”走向“好用”的关键一步。

## 11. `run` 和 `clean`

```make
run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(TARGET) $(OBJ_DIR) $(DEP_DIR)
```

这两个目标分别提供：

- `make run`：编译完成后直接运行程序
- `make clean`：清掉可执行文件、目标文件和依赖文件

开发时会很顺手，因为常见动作基本就剩三类：

- `make`
- `make run`
- `make clean`

## 12. 这份脚本后续可以怎么扩展

这份 Makefile 已经足够适合小型项目使用，后面如果项目继续变大，比较自然的扩展方向有这些：

- 把 `CFLAGS := -g -Wall -Iinc` 改成 `CFLAGS := -g -Wall -I$(INC_DIR)`
- 增加 `LDFLAGS`，把链接器选项和库拆开管理
- 区分调试构建和发布构建，例如 `DEBUG=1`
- 支持递归收集多级子目录源码
- 增加交叉编译支持，例如 `CC := arm-linux-gnueabihf-gcc`

# 第三部分 编译常见问题及其解决方法
## 1. 头文件找不到怎么办

编译时如果提示找不到头文件，最先要看的是源码里的包含方式，例如：

```c
#include <xxx.h>
```

对于这种尖括号写法，编译器通常会去系统头文件目录里查找。

这些“系统目录”在交叉编译场景下，往往就是交叉编译工具链自带的某些 `include` 目录；除此之外，也可以在编译时手动补充头文件搜索路径：

```bash
-I dir
```

也就是说，如果头文件不在默认搜索路径里，就需要通过 `-I` 显式告诉编译器去哪里找。

### 怎么查看编译器默认会搜索哪些头文件目录

可以执行下面这条命令：

```bash
echo 'main(){}' | arm-buildroot-linux-gnueabihf-gcc -E -v -
```

这条命令的作用是：

- 调用预处理流程
- 打印编译器默认搜索的头文件目录
- 同时也会输出一些和库路径相关的信息，例如 `LIBRARY_PATH`

排查时可以按下面这个顺序看：

1. 先确认默认头文件目录里到底有没有 `xxx.h`
2. 如果没有，再确认这个头文件实际位于哪个目录
3. 最后通过 `-I /path/to/include` 把对应目录补进去

很多“明明文件存在但还是报找不到头文件”的问题，本质上都是“文件在，但不在编译器的搜索路径里”。

## 2. 链接时报 `undefined reference` 怎么办

如果在链接阶段看到类似报错：

```text
undefined reference to `xxx'
```

它通常表示：链接器找不到符号 `xxx` 对应的函数实现。

这类问题一般可以从两个方向排查：

1. 这个函数本来就应该由你自己实现，但还没有写，或者虽然写了却没有参与编译链接
2. 这个函数来自某个库，但链接时没有把对应库带上

### 如果是库函数，怎么指定库

假设你要链接的是：

```text
libabc.so
```

那么链接时通常写成：

```bash
-labc
```

这里要注意一个常见规则：

- 写 `-l` 时只写库名主体
- 不写前缀 `lib`
- 不写后缀 `.so` 或 `.a`

### 库文件去哪里找

库的来源一般也有两类：

1. 编译器或交叉工具链自带的系统库目录
2. 你自己指定的库目录

如果库不在默认目录里，就要在链接时额外指定：

```bash
-L dir
```

所以排查 `undefined reference` 时，比较实用的检查顺序是：

1. 确认函数实现是否真的存在
2. 确认对应源码是否被编译成了 `.o`
3. 如果来自库，确认链接命令里是否写了 `-lxxx`
4. 如果库不在默认目录，确认是否写了 `-L/path/to/lib`

## 3. 运行时找不到动态库怎么办

有时候程序在编译和链接阶段都能通过，但运行时却报错：

```text
error while loading shared libraries: libxxx.so:
cannot open shared object file: No such file or directory
```

这说明问题已经不在“编译器找不找得到”，而是在“程序运行时，动态加载器找不找得到这个 `.so` 文件”。

### 运行时默认会去哪里找库

在目标板或 Linux 系统上，常见的默认动态库目录通常包括：

- `/lib`
- `/usr/lib`

如果目标库不在这些默认目录中，程序运行时就可能找不到它。

### 这类问题怎么处理

常见思路有这些：

1. 确认 `libxxx.so` 是否真的已经放到了板子上
2. 确认它所在的位置是不是系统默认动态库目录
3. 如果不在默认目录，就需要额外配置运行时库搜索路径

最核心的判断可以先记成一句话：

- `-I` 解决“编译时找头文件”
- `-L` 和 `-l` 解决“链接时找库”
- 运行时动态库报错，排查的是板子上的库文件位置和运行时搜索路径

# 第四部分 输入系统应用编程
## 1. 内核如何表示输入设备

在 Linux 输入子系统里，内核通常使用 `input_dev` 结构体来描述一个输入设备。它位于：

```c
// kernel/include/linux/input.h
struct input_dev {
	const char *name;
	const char *phys;
	const char *uniq;
	struct input_id id;

	unsigned long propbit[BITS_TO_LONGS(INPUT_PROP_CNT)];

	unsigned long evbit[BITS_TO_LONGS(EV_CNT)];
	unsigned long keybit[BITS_TO_LONGS(KEY_CNT)];
	unsigned long relbit[BITS_TO_LONGS(REL_CNT)];
	unsigned long absbit[BITS_TO_LONGS(ABS_CNT)];
	unsigned long mscbit[BITS_TO_LONGS(MSC_CNT)];
	unsigned long ledbit[BITS_TO_LONGS(LED_CNT)];
	unsigned long sndbit[BITS_TO_LONGS(SND_CNT)];
	unsigned long ffbit[BITS_TO_LONGS(FF_CNT)];
	unsigned long swbit[BITS_TO_LONGS(SW_CNT)];
};
```

这类字段可以先按“设备身份”和“设备能力”两组来理解。

### 设备身份信息

- `name`：设备名字
- `phys`：设备物理路径
- `uniq`：设备唯一标识
- `id`：设备 ID 信息

### 设备能力位图

后面这一串 `evbit`、`keybit`、`relbit`、`absbit` 本质上都是位图，用来表示这个输入设备支持什么事件。

例如：

- `evbit`：支持哪些事件大类
- `keybit`：支持哪些按键
- `relbit`：支持哪些相对位移事件
- `absbit`：支持哪些绝对坐标事件

所以从应用开发的角度看，一个输入设备不只是“有个设备节点”，它背后还带着一整套“它能上报什么类型的数据”的描述。

## 2. APP 可以读取到什么数据

用户态程序从 `/dev/input/eventX` 里读取到的数据，核心就是 `input_event` 结构体：

```c
// kernel/uapi/linux/input.h
struct input_event {
#if (__BITS_PER_LONG != 32 || !defined(__USE_TIME_BITS64)) && !defined(__KERNEL__)
	struct timeval time;
#define input_event_sec time.tv_sec
#define input_event_usec time.tv_usec
#else
	__kernel_ulong_t __sec;
#if defined(__sparc__) && defined(__arch64__)
	unsigned int __usec;
	unsigned int __pad;
#else
	__kernel_ulong_t __usec;
#endif
#define input_event_sec  __sec
#define input_event_usec __usec
#endif
	__u16 type;
	__u16 code;
	__s32 value;
};
```

可以把它理解成：每发生一次输入事件，驱动就会往输入设备节点里写入一份这样的数据。

### `time`

`timeval` 表示事件发生时间，里面通常包含：

- `tv_sec`：秒
- `tv_usec`：微秒

这部分常用来表示“自系统启动以来经过了多久”，方便在应用层分析事件发生顺序和时间间隔。

### `type`

`type` 表示事件类别，常见的有：

- `EV_KEY`：按键事件
- `EV_REL`：相对位移事件，比如鼠标
- `EV_ABS`：绝对坐标事件，比如触摸屏

### `code`

`code` 表示当前类别下的具体事件编号。

比如对于 `EV_KEY` 来说，按键并不只有一个，所以还需要再用 `code` 区分到底是：

- `KEY_1`
- `KEY_A`
- `KEY_ESC`
- `KEY_VOLUMEUP`

也就是说，`type` 决定“是哪一类输入”，`code` 决定“这一类里的哪一个具体成员”。

### `value`

`value` 表示事件值。

对于按键事件 `EV_KEY`，最常见的取值是：

- `1`：按下
- `0`：松开
- `2`：长按或自动重复

这个顺序很容易记反，实际写程序时最好直接按这个定义来判断。

### 同步事件

驱动在上报完一组相关数据后，通常还会再上报一个同步事件 `EV_SYN`，告诉应用层：

- 这一批输入数据已经发送完毕
- 当前这一轮事件可以认为是完整的

所以 APP 在读取事件流时，看到同步事件后，就知道当前这一组数据已经读完了。

## 3. 怎么查看板子上有哪些输入设备

排查输入设备时，最常看的有两个位置：

1. `/dev/input/`：看有哪些设备节点
2. `/proc/bus/input/devices`：看这些设备节点分别对应什么硬件

### 先看设备节点

下面这张图展示了板子上的输入设备节点，以及 `by-path` 目录下的符号链接关系：

![输入设备节点与 by-path 映射](/images/notes/linux_app/01.png)

这类输出有两个用途：

- 看系统当前生成了哪些 `/dev/input/eventX`
- 借助 `/dev/input/by-path/` 下的链接名，快速判断某个 `eventX` 大致对应哪类硬件

例如图里：

- `platform-adc-keys-event -> ../event6`

就能帮助我们判断 `event6` 对应的是 ADC 按键设备。

### 再看设备信息和能力

如果想进一步确认 `eventX` 到底是哪块硬件、支持哪些事件，可以看：

```bash
cat /proc/bus/input/devices
```

示例如下：

![输入设备详细信息](/images/notes/linux_app/02.png)

这个文件里每个设备通常会包含下面几类信息：

- `I`：设备 ID，由 `struct input_id` 描述
- `N`：设备名称
- `P`：设备物理路径
- `S`：位于 `sysfs` 中的路径
- `U`：设备唯一标识
- `H`：和该设备关联的输入句柄
- `B`：设备能力位图

其中 `H` 和 `B` 尤其常用。

### `H` 字段怎么看

`H: Handlers=...` 这一行经常能直接看出设备节点，例如：

```text
H: Handlers=kbd event0 cpufreq dmcfreq
```

这里最值得关注的是 `event0`，它说明这个设备最终对应的输入节点就是：

```text
/dev/input/event0
```

### `B` 字段怎么看

`B` 表示位图能力，常见的有：

- `PROP`：设备属性
- `EV`：支持的事件大类
- `KEY`：支持哪些按键
- `ABS`：支持哪些绝对坐标事件

例如图里：

- `EV=3` 往往意味着这个设备支持按键类事件
- `KEY=...` 则进一步表示它到底支持哪些按键编码

实际开发里通常不需要手工把每一位都背下来，但至少要知道：这些位图就是设备能力表。

### 用 `hexdump` 看原始输入数据

如果想直接看设备在上报什么，也可以用：

```bash
hexdump /dev/input/eventX
```

例如：

![hexdump 查看输入事件](/images/notes/linux_app/03.png)

这张图本质上是在显示 `/dev/input/event6` 中连续读出来的原始二进制数据。

可以先这样理解它：

- 前面一段通常对应时间戳
- 后面会依次对应 `type`、`code`、`value`
- 一次按键动作往往不只产生一条数据，最后通常还会跟着同步事件

`hexdump` 的优点是简单直接，适合快速确认“设备有没有在上报数据”；缺点是可读性一般，真正写应用程序时，还是更适合直接按 `struct input_event` 来解析。

## 4. 使用 `poll` 检测输入设备

如果应用程序不想一直阻塞在 `read` 上，可以使用 `poll` 先等待设备变为可读，再去读取输入事件。

下面是一段典型示例：

```c
#include <stdio.h>
#include <poll.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    struct pollfd kbd_fd;
    int fd = open("/dev/input/event6", O_RDONLY);
    if (fd < 0) {
        printf("open /dev/input/event6 failed!\n");
        return -1;
    }

    kbd_fd.fd = fd;
    kbd_fd.events = POLLIN;
    kbd_fd.revents = 0;

    while (1) {
        printf("POLL版本按键查询\n");

        int n = poll(&kbd_fd, 1, 5000);
        if (n < 0) {
            printf("poll 被信号中断\n");
            break;
        } else if (n == 0) {
            printf("等待超时\n");
            continue;
        } else {
            if ((n == 1) && (kbd_fd.revents & POLLIN)) {
                char buf[1024];
                memset(buf, 0, sizeof(buf));

                int ret = read(fd, buf, sizeof(buf));
                if (ret <= 0) {
                    continue;
                }

                switch (buf[18]) {
                case 0x72: // V-
                    if (buf[20] == 0x01)
                        printf("V-键被按下\n");
                    else
                        printf("V-键被松开\n");
                    break;
                case 0x8b: // MENU
                    if (buf[20] == 0x01)
                        printf("MENU键被按下\n");
                    else
                        printf("MENU键被松开\n");
                    break;
                case 0x73: // V+
                    if (buf[20] == 0x01)
                        printf("V+键被按下\n");
                    else
                        printf("V+键被松开\n");
                    break;
                case 0x01: // ESC
                    if (buf[20] == 0x01)
                        printf("ESC键被按下\n");
                    else
                        printf("ESC键被松开\n");
                    break;
                }
            }
        }
    }

    close(fd);
    return 0;
}
```

### 这段程序的工作流程

它的逻辑可以概括为：

1. 打开 `/dev/input/event6`
2. 把这个文件描述符交给 `poll`
3. 最多等待 5000 毫秒
4. 如果设备可读，再调用 `read`
5. 从读到的原始数据里取出按键编码和值并打印

这里 `poll` 的好处是：在没有按键时，程序不会一直忙等，而是进入等待状态。

### 这段示例的核心点

- `POLLIN` 表示关注“是否可读”
- `poll(&kbd_fd, 1, 5000)` 表示只监听 1 个文件描述符，超时时间是 5 秒
- 返回值小于 0 表示出错
- 返回值等于 0 表示超时
- 返回值大于 0 表示至少有一个描述符就绪

### 这个示例要注意什么

这段代码适合作为“快速验证设备上报内容”的实验程序，但它有一个明显特点：直接按字节偏移去取 `buf[18]`、`buf[20]`。

这种写法能工作，是因为它默认了当前平台上 `input_event` 的布局和字节顺序；但从可移植性和可维护性来说，更推荐的方式是：

- 直接把读到的数据按 `struct input_event` 解析
- 使用 `type`、`code`、`value` 来判断事件
- 遇到 `EV_SYN` 时把它视作一组事件结束

也就是说，`hexdump` 和按字节取值更像“观察底层格式”的方法，而真正写 APP 时，最好还是围绕 `input_event` 结构体来处理。

## 5. 使用异步通知检测输入设备

在 `poll` 版本里，程序虽然不用一直阻塞在 `read` 上，但本质上还是在循环里主动等待设备就绪。  
如果想让驱动在“有输入事件时主动通知应用程序”，可以使用异步通知机制，也就是 `SIGIO`。

下面是在前面按键读取示例基础上扩展出来的一版：

```c
#include <stdio.h>
#include <poll.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>

int fd;
unsigned int kbd_flag = 0;
unsigned char buf[1024];

static void kbd_sigHander(int sig)
{
    if (sig == SIGIO) { // 接收到 IO 信号
        memset(buf, 0, sizeof(buf));
        int ret = read(fd, buf, sizeof(buf));
        if (ret > 0) {
            kbd_flag = 0;
            kbd_flag |= (buf[18] << 1);   // 按键标识
            kbd_flag |= (buf[20] & 0x01); // 按键状态
        }
    }
}

int main(int argc, char *argv[])
{
    fd = open("/dev/input/event6", O_RDONLY);
    if (fd < 0) {
        printf("open /dev/input/event6 failed!\n");
        return -1;
    }

    if (argc < 2) {
        printf("请输入测试模式：1-signal注册方式 2-sigaction注册方式\n");
        return 0;
    }

    if (atoi(argv[1]) == 1) {
        printf("异步通知版本按键查询--signal注册方式\n");
        signal(SIGIO, kbd_sigHander);
    } else {
        printf("异步通知版本按键查询--sigaction注册方式\n");
        struct sigaction act;
        act.sa_handler = kbd_sigHander;
        sigemptyset(&act.sa_mask);
        act.sa_flags = 0;
        sigaction(SIGIO, &act, NULL);
    }

    fcntl(fd, F_SETOWN, getpid());
    int flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags | FASYNC);

    while (1) {
        if (kbd_flag) {
            switch ((kbd_flag & 0x1fe) >> 1) {
            case 0x72: // V-
                if ((kbd_flag & 0x01) == 0x01)
                    printf("V-键被按下\n");
                else
                    printf("V-键被松开\n");
                break;
            case 0x8b: // MENU
                if ((kbd_flag & 0x01) == 0x01)
                    printf("MENU键被按下\n");
                else
                    printf("MENU键被松开\n");
                break;
            case 0x73: // V+
                if ((kbd_flag & 0x01) == 0x01)
                    printf("V+键被按下\n");
                else
                    printf("V+键被松开\n");
                break;
            case 0x01: // ESC
                if ((kbd_flag & 0x01) == 0x01)
                    printf("ESC键被按下\n");
                else
                    printf("ESC键被松开\n");
                break;
            }
            kbd_flag = 0;
        }
    }

    return 0;
}
```

### 这段程序的核心思路

它和 `poll` 版本最大的区别在于：

- `poll` 版本是应用程序主动去问“现在有没有数据”
- 异步通知版本是设备有数据时，内核通过信号主动通知应用程序

可以把它理解成一种“事件到了再叫你”的机制。

### `SIGIO` 在这里做了什么

这段程序注册了 `SIGIO` 的信号处理函数：

```c
signal(SIGIO, kbd_sigHander);
```

或者：

```c
sigaction(SIGIO, &act, NULL);
```

当输入设备产生数据并触发异步通知后，进程就会收到 `SIGIO`，然后进入 `kbd_sigHander()` 去读取按键数据。

这里示例里给了两种注册方式：

- `signal`：写法简单，适合快速测试
- `sigaction`：功能更完整，实际开发里更常用

如果只是学习流程，两种方式都能帮助理解“信号驱动的输入处理”。

### `fcntl` 相关设置是什么意思

这几行是异步通知能工作的关键：

```c
fcntl(fd, F_SETOWN, getpid());
int flags = fcntl(fd, F_GETFL);
fcntl(fd, F_SETFL, flags | FASYNC);
```

它们可以这样理解：

- `F_SETOWN`：把当前进程设置成这个文件描述符的拥有者
- `F_GETFL`：读取当前文件状态标志
- `F_SETFL | FASYNC`：在原有标志基础上打开异步通知功能

只有把这些关系建立起来后，驱动在设备可读时，才知道应该把 `SIGIO` 发给哪个进程。

### 为什么还要用 `kbd_flag`

在信号处理函数里，示例没有直接打印，而是把结果整理到 `kbd_flag` 中：

- 高位部分保存按键编码
- 最低位保存按键状态

主循环里再根据 `kbd_flag` 去判断并打印具体按键信息。

这样写的好处是：把“收到信号时的快速处理”和“主循环里的业务判断”稍微分开了，逻辑更容易看清。

这里还有一个很重要的实践原则：无论是中断处理函数，还是这类异步信号处理函数，通常都不适合放复杂逻辑。

更常见的做法是：

- 在 handler 里只做很轻量的事情
- 例如读取必要状态、保存关键数据、置标志位
- 把真正复杂的处理放到外层线程、主循环或其他上下文里完成

这样做的原因是，这类处理函数往往要求尽快返回。如果在里面做太多计算、打印、阻塞等待或者复杂流程控制，代码会更难维护，也更容易引入时序问题。

从这个角度看，示例里在 `kbd_sigHander()` 中只更新 `kbd_flag`，再由外层循环完成按键解析和输出，就是一种比较典型的“handler 里做轻处理，外层做重处理”的思路。

### 这段代码的运行流程

整个流程可以概括成：

1. 打开 `/dev/input/event6`
2. 注册 `SIGIO` 信号处理函数
3. 用 `fcntl` 把当前进程声明为异步通知接收者
4. 设备有数据时，内核向当前进程发送 `SIGIO`
5. 信号处理函数里调用 `read`
6. 主循环根据 `kbd_flag` 输出按键结果

### 这一版和 `poll` 版的差别

从学习路径上看，这两种方式刚好适合放在一起比较：

- `poll`：适合同时监听多个文件描述符，模型直观
- `SIGIO` 异步通知：更接近“事件到达后自动唤醒处理”

但也要注意，信号驱动模型虽然很有代表性，代码处理起来通常会比 `poll` 更绕一些，因为你需要额外考虑：

- 信号处理函数何时触发
- 信号处理函数里适合做多少事情
- 主循环和信号处理逻辑之间如何共享状态

### 这个示例也有同样的局限

和前面的 `poll` 示例一样，这段程序也是直接通过 `buf[18]`、`buf[20]` 去取字段。  
它适合帮助理解底层事件格式，但如果要把程序写得更稳，仍然建议：

- 把数据解析为 `struct input_event`
- 使用 `type`、`code`、`value` 来判断事件
- 把同步事件 `EV_SYN` 当作一组上报结束的标记

这样代码会更清晰，也更不容易受平台结构体布局差异影响。

# 第五部分 多线程编程
## 1. 多线程里常见的互斥锁函数

多线程程序里最容易遇到的问题之一，就是多个线程同时访问同一份共享数据。  
如果没有同步机制，就可能出现数据错乱、覆盖、竞争条件等问题。

互斥锁 `mutex` 的作用很直接：同一时刻只允许一个线程进入临界区。

下面这几个函数是最常见的一组：

- `pthread_mutex_init`
- `pthread_mutex_lock`
- `pthread_mutex_unlock`
- `pthread_mutex_trylock`
- `pthread_mutex_destroy`

### `pthread_mutex_init`

这个函数用来初始化互斥锁。

常见写法如下：

```c
pthread_mutex_t mutex;
pthread_mutex_init(&mutex, NULL);
```

第二个参数通常传 `NULL`，表示使用默认属性。

可以把它理解成：先把这把锁创建好，后面线程才能去加锁和解锁。

### `pthread_mutex_lock`

这个函数用于加锁：

```c
pthread_mutex_lock(&mutex);
```

如果当前锁没有被占用，线程会立刻获得锁并继续往下执行。  
如果锁已经被别的线程拿走了，当前线程通常会阻塞，直到锁被释放。

它最常见的使用方式是：

```c
pthread_mutex_lock(&mutex);
// 访问共享资源
pthread_mutex_unlock(&mutex);
```

### `pthread_mutex_unlock`

这个函数用于解锁：

```c
pthread_mutex_unlock(&mutex);
```

它的作用是把当前线程占有的锁释放掉，让其他等待该锁的线程有机会继续执行。

互斥锁最重要的使用习惯之一，就是“谁加锁，谁解锁”，并且尽量保证每条逻辑路径都能走到 `unlock`。

### `pthread_mutex_trylock`

`trylock` 也是加锁，但它和 `lock` 的区别在于：拿不到锁时不会阻塞等待，而是立即返回。

```c
if (pthread_mutex_trylock(&mutex) == 0) {
    // 拿到锁
    pthread_mutex_unlock(&mutex);
} else {
    // 没拿到锁，立即走别的逻辑
}
```

这种方式适合下面这类场景：

- 不想阻塞当前线程
- 只是尝试获取资源
- 拿不到锁就先做别的事情

### `pthread_mutex_destroy`

这个函数用于销毁互斥锁：

```c
pthread_mutex_destroy(&mutex);
```

它一般在锁不再使用时调用，用来回收相关资源。

要注意的是：销毁之前应该确保没有线程还在使用这把锁，否则会带来未定义行为。

### 一段最小互斥锁用法

```c
pthread_mutex_t mutex;
pthread_mutex_init(&mutex, NULL);

pthread_mutex_lock(&mutex);
// 临界区
pthread_mutex_unlock(&mutex);

pthread_mutex_destroy(&mutex);
```

如果只先记一个最核心原则，可以记这句：

- 互斥锁解决的是“同一时刻谁能访问共享资源”的问题

## 2. 用信号量控制线程执行顺序

除了互斥锁之外，多线程里另一类常见需求是：  
不是为了保护共享资源，而是为了控制“谁先执行、谁后执行”。

这时信号量就很常用。

下面这段例程展示了一个很典型的用法：通过 3 个信号量，让 3 个线程按指定顺序运行。

```c
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <semaphore.h>

sem_t sem[3];

void* fun1(void* arg){
    sem_wait(&sem[0]);
    printf("%s: Pthread Come!\n", __FUNCTION__);
    sem_post(&sem[2]);
    pthread_exit(NULL);
}

void* fun2(void* arg){
    sem_wait(&sem[1]);
    printf("%s: Pthread Come!\n", __FUNCTION__);
    sem_post(&sem[0]);
    pthread_exit(NULL);
}

void* fun3(void* arg){
    sem_wait(&sem[2]);
    printf("%s: Pthread Come!\n", __FUNCTION__);
    sem_post(&sem[1]);
    pthread_exit(NULL);
}

int main(){
    int ret;

    // 初始化信号量
    for(int i=0; i<3; i++){
        if(i==0){
            ret = sem_init(&sem[i], 0, 1);
        }
        else{
            ret = sem_init(&sem[i], 0, 0);
        }
        if(ret != 0){
            printf("sem %d initial failed\n", i+1);
            return -1;
        }
    }

    // 初始化线程
    pthread_t tid[3];
    for(int i=0; i<3; i++){
        if(i == 0){
            ret = pthread_create(&tid[i], NULL, fun1, (void*)&i);
        }
        else if(i == 1){
            ret = pthread_create(&tid[i], NULL, fun2, (void*)&i);
        }
        else if(i == 2){
            ret = pthread_create(&tid[i], NULL, fun3, (void*)&i);
        }
        if(ret != 0){
            printf("pthread %d create failed\n", i+1);
            return -1;
        }
    }

    for(int i=0; i<3; i++){
        pthread_join(tid[i], NULL);
    }
    for(int i=0; i<3; i++){
        sem_destroy(&sem[i]);
    }
    return 0;
}
```

### 这段程序想实现什么

虽然线程创建顺序是 `fun1`、`fun2`、`fun3`，但真正的执行顺序并不由 `pthread_create` 保证。  
这个例子想做的是：通过信号量明确规定线程之间的先后关系。

从代码逻辑来看，最终打印顺序会是：

1. `fun1`
2. `fun3`
3. `fun2`

### 为什么会是这个顺序

关键在于这三个信号量的初始值：

- `sem[0] = 1`
- `sem[1] = 0`
- `sem[2] = 0`

这意味着：

- `fun1` 一开始就可以通过 `sem_wait(&sem[0])`
- `fun2` 会阻塞在 `sem_wait(&sem[1])`
- `fun3` 会阻塞在 `sem_wait(&sem[2])`

所以第一步一定是 `fun1` 先执行。

`fun1` 打印后执行：

```c
sem_post(&sem[2]);
```

这会把 `sem[2]` 加 1，于是 `fun3` 被唤醒，可以继续执行。

`fun3` 执行完以后再调用：

```c
sem_post(&sem[1]);
```

这样 `fun2` 才能继续执行。

于是最终形成了这条链：

```text
fun1 -> fun3 -> fun2
```

实际运行结果如下，可以看到线程输出顺序稳定为 `1 -> 3 -> 2`：

![信号量控制线程初始化顺序的运行结果](/images/notes/linux_app/04.png)

这也说明：虽然线程的创建动作是在一个循环里依次发起的，但真正的执行顺序已经被信号量控制住了。

### `sem_wait` 和 `sem_post` 怎么理解

可以把它们先理解成最朴素的两个动作：

- `sem_wait`：申请一个“可执行令牌”，如果没有就等待
- `sem_post`：释放一个“可执行令牌”，让别人可以继续

在这段程序里，信号量并不是拿来统计资源个数，而是拿来当“线程之间的接力棒”。

### 初始化部分为什么这样写

这段初始化代码最关键的是：

```c
ret = sem_init(&sem[0], 0, 1);
ret = sem_init(&sem[i], 0, 0);
```

可以这样理解：

- `sem[0]` 初始值为 1，表示第一棒已经准备好，允许 `fun1` 先跑
- 其他两个信号量初始值为 0，表示 `fun2` 和 `fun3` 先别动，等前一个线程通知

所以“初值设成多少”，本质上就是在定义谁先开始。

### 这段代码适合拿来理解什么

这段例子很适合拿来理解：

- 线程创建顺序不等于线程执行顺序
- 信号量除了做资源计数，也可以做执行顺序控制
- `sem_wait` / `sem_post` 能很自然地构造线程之间的同步链路

### 这段例子还可以注意什么

这里 `pthread_create` 时把 `&i` 作为参数传给了线程函数：

```c
(void*)&i
```

但当前示例里线程函数并没有使用这个参数，所以不会造成实际影响。  
如果后面要在线程里真正读取这个参数，就要格外小心，因为循环变量 `i` 在主线程里会继续变化，直接传地址很容易引发混淆。

## 3. 条件变量

条件变量也是一种线程同步机制，它的核心用途是：

- 当某个条件满足时，通知其他线程继续执行

它通常不是单独使用，而是和互斥锁配合使用。  
原因很简单：条件变量负责“通知条件成立了”，互斥锁负责“保护这份条件对应的共享数据”。

所以可以把条件变量理解成：

- 互斥锁解决“谁能安全访问共享数据”
- 条件变量解决“什么时候该继续往下执行”

### 为什么条件变量通常要配合互斥锁

条件变量一般是围绕共享状态来工作的，比如：

- 缓冲区里是否已经有数据
- 某个任务是否已经准备完成
- 某个线程是否已经初始化结束

这些状态本身通常是多个线程共享的。  
如果没有互斥锁保护，线程在检查条件和修改条件时就可能发生竞争。

所以最常见的思路是：

1. 先加锁
2. 检查共享条件
3. 条件满足时发通知，或者等待通知
4. 最后解锁

## 4. 条件变量常用函数

你这部分先提到的几个函数，是条件变量里最常见的基础接口：

- `pthread_cond_init`
- `pthread_cond_destroy`
- `pthread_cond_wait`
- `pthread_cond_signal`
- `pthread_cond_broadcast`

### `pthread_cond_init`

初始化条件变量：

```c
pthread_cond_t cond;
pthread_cond_init(&cond, NULL);
```

第二个参数通常传 `NULL`，表示使用默认属性。

### `pthread_cond_destroy`

销毁条件变量：

```c
pthread_cond_destroy(&cond);
```

一般在条件变量不再使用时调用。

### `pthread_cond_signal`

这个函数用于通知等待在该条件变量上的线程：

```c
pthread_cond_signal(&cond);
```

它通常表示：某个条件已经满足，可以唤醒一个等待线程继续执行。

如果想一次唤醒所有等待线程，通常还会用到：

```c
pthread_cond_broadcast(&cond);
```

### `pthread_cond_wait`

等待条件变量的函数原型如下：

```c
int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
```

它必须结合互斥锁一起使用，这是条件变量里最值得真正理解的一个接口。

典型写法如下：

```c
pthread_mutex_lock(&g_tMutex);
pthread_cond_wait(&g_tConVar, &g_tMutex);
// 如果条件不满足，会先解锁 g_tMutex 并进入等待
// 条件满足后被唤醒，再重新加锁 g_tMutex

/* 操作临界资源 */

pthread_mutex_unlock(&g_tMutex);
```

这段流程可以拆开理解：

1. 线程先拿到互斥锁
2. 调用 `pthread_cond_wait`
3. 如果条件暂时不满足，线程会进入等待状态，同时自动释放互斥锁
4. 其他线程修改条件并发出通知后，等待线程被唤醒
5. 被唤醒后，它会先重新拿回互斥锁，再继续往下执行

这个“等待时自动解锁，唤醒后自动重新加锁”的行为非常关键。  
正因为有这一步，其他线程才能在等待期间拿到锁、修改共享数据、再发出通知。

### `pthread_cond_broadcast`

`pthread_cond_broadcast` 用于唤醒所有等待在该条件变量上的线程：

```c
pthread_cond_broadcast(&cond);
```

它和 `pthread_cond_signal` 的区别可以直接记成：

- `pthread_cond_signal`：唤醒一个等待线程
- `pthread_cond_broadcast`：唤醒所有等待线程

如果当前场景里可能有多个线程都在等同一个条件，而条件一旦满足后需要大家都重新检查状态，就比较适合使用 `broadcast`。

### 条件变量的使用思路

虽然这里只先列出几个基础函数，但真正理解条件变量时，最重要的不是死记函数名，而是记住它的角色：

- 线程 A 修改了共享状态
- 线程 A 发出通知
- 线程 B 收到通知后继续执行

也就是说，条件变量更像一种“线程之间的消息提醒机制”，提醒的是：

- 条件满足了
- 可以继续处理共享数据了

如果把这一组接口合起来看，条件变量最常见的一套配合关系就是：

- 用互斥锁保护共享状态
- 用 `pthread_cond_wait` 让线程等待条件成立
- 用 `pthread_cond_signal` 或 `pthread_cond_broadcast` 发出通知

这样条件变量这部分就形成了一个完整闭环。
