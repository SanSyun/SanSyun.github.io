---
title: "Linux 应用开发流程简述"
description: "整理 Linux 应用开发中常用的 GCC 编译流程，涵盖预处理、编译、汇编、链接、静态库和动态库，以及一份可复用的通用 Makefile 写法。"
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
