---
layout: post
title: "ZYNQ系列-PS端Linux启动文件制作"
date: 2025-10-17 22:04:16 +0800
category: Linux
tags: [ZYNQ]
excerpt: 介绍ZYNQ平台如何移植Linux操作系统
featured: true 
image: /images/posts/ps_image.jpg
---

# 软件准备

1. Vitis 2024.2 Classic（2024.2版本似乎不支持设备树工程）
2. Vivado 2024.2 
3. Vmware Workstations Pro Ubuntu虚拟机
4. arm-xilinx-linux-gnueabi交叉编译工具链 

# 启动环境

​	制作一张存在boot（fat32）分区和rootfs（ext4）分区的SD卡。将BOOT.BIN、zImage、system-top.dtb存放到boot分区，根文件系统解压后放到ext4分区。

# 启动文件制作流程

## 1.BOOT.BIN与system-top.dtb制作

### 1.生成设备树

1. 首先需要在Xilinx官方的GitHub中找到发布的设备树源码，为了保持版本一致性选择2024.2。

```
https://github.com/Xilinx/device-tree-xlnx/tags
```

![image-20250807142457724](../images/\pic\image-20250807142457724.png)

2. 在vitis导入解压后的设备树源码目录。

![image-20250807142830995](../images/\pic\image-20250807142830995.png)

![image-20250807143016745](../images/\pic\image-20250807143016745.png)

3. 创建设备树工程。

​	File->New->platform project.

​	导入设计好的xsa文件，并将Operating system选为device_tree。

![image-20250807143337695](../images/\pic\image-20250807143337695.png)

4. 编译刚刚创建好的设备树工程，此时会根据xsa文件生成定制化的设备树。

![image-20250807143728838](../images/\pic\image-20250807143728838.png)

​	此时在设备树工程下“\ps7_cortexa9_0\device_tree_domain\bsp”目录当中能找到我们需要的三个设备树文件。pcw.dtsi是ps端外设相关设备树，zynq-7000.dtsi是zynq7000系列开发板共有的一些设备树，system-top.dts是我们所设计的platform的顶层设备树，其中引用了pcw.dtsi和zynq-7000.dtsi，如果我们设计了PL端的Block还有有pl.dtsi。

![image-20250807143841831](../images/\pic\image-20250807143841831.png)

​	**注意：**若后面启动开发板的时候发现开发板无法连接网络，则可能是设备树没有正确设置PHY ID，PHY ID取决于PHY芯片strap三个脚的焊接方式，取值范围在0-7；因此要对pcw.dtsi做修改，将gem0节点配置为如下形式。

```c
&gem0 {
	enet-reset = <&gpio0 8 0>;
	phy-mode = "rgmii-id";
	status = "okay";
	xlnx,ptp-enet-clock = <0x69f6bcb>;
	mdio {
        phy0: ethernet-phy@7 {  // reg = <7> 需与硬件匹配
            reg = <7>;
        };
    };
};
```



### 2. 编译生成u-boot.elf

1. 从XIlinx官方的github下载2024.2版本的uboot源码，并解压到wsl或者虚拟机linux操作系统的某个位置。这里在/home下面建立了/uboot。

```
https://github.com/Xilinx/u-boot-xlnx/tags
```

![image-20250807145011722](../images/\pic\image-20250807145011722.png)

2. 修改“xilinx_zynq_virt_defconfig”文件

![image-20250807150829170](../images/\pic\image-20250807150829170.png)

​	搜索CONFIG_DEFAULT_DEVICE_TREE，将对应的值改为“system-top”

![image-20250807150956735](../images/\pic\image-20250807150956735.png)

​	搜索CONFIG_OF_LIST，把“system-top”添加到list中

![image-20250807151159229](../images/\pic\image-20250807151159229.png)

3. 修改“zynq-common.h”文件

![image-20250807152215518](../images/\pic\image-20250807152215518.png)

​	复制粘贴下面u-boot启动所需的环境变量内容到CFG_EXTRA_ENV_SETTINGS宏定义当中

```c
	"fdt_high=0x20000000\0" \
	"initrd_high=0x20000000\0" \
	"scriptaddr=0x20000\0" \
	"script_size_f=0x40000\0" \
	"fdt_addr_r=0x1f00000\0" \
	"pxefile_addr_r=0x2000000\0" \
	"kernel_addr_r=0x2000000\0" \
	"scriptaddr=0x3000000\0" \
	"ramdisk_addr_r=0x3100000\0" \
	"ethaddr=00:0a:36:00:00:00\0" \
	"kernel_image=zImage\0" \
	"kernel_load_address=0x2080000\0" \
	"ramdisk_image=uramdisk.image.gz\0" \
	"ramdisk_load_address=0x4000000\0" \
	"devicetree_image=system-top.dtb\0" \
	"devicetree_load_address=0x2000000\0" \
	"bitstream_image=system.bit.bin\0" \
	"boot_image=BOOT.bin\0" \
	"loadbit_addr=0x100000\0" \
	"loadbootenv_addr=0x2000000\0" \
	"kernel_size=0x500000\0" \
	"devicetree_size=0x20000\0" \
	"ramdisk_size=0x5E0000\0" \
	"boot_size=0xF00000\0" \
	"fdt_high=0x20000000\0" \
	"initrd_high=0x20000000\0" \
	"bootenv=uEnv.txt\0" \
	"loadbootenv=load mmc 0 ${loadbootenv_addr} ${bootenv}\0" \
	"importbootenv=echo Importing environment from SD ...; " \
	"env import -t ${loadbootenv_addr} $filesize\0" \
	"sd_uEnvtxt_existence_test=test -e mmc 0 /uEnv.txt\0" \
	"preboot=if test $modeboot = sdboot && env run sd_uEnvtxt_existence_test; " \
		"then if env run loadbootenv; " \
		"then env run importbootenv; " \
	"fi; " \
	"fi; \0" \
	"mmc_loadbit=echo Loading bitstream from SD/MMC/eMMC to RAM.. && " \
	"mmcinfo && " \
	"load mmc 0 ${loadbit_addr} ${bitstream_image} && " \
	"fpga load 0 ${loadbit_addr} ${filesize}\0" \
	"uenvboot=" \
	"if run loadbootenv; then " \
		"echo Loaded environment from ${bootenv}; " \
		"run importbootenv; " \
	"fi; " \
	"if test -n $uenvcmd; then " \
		"echo Running uenvcmd ...; " \
		"run uenvcmd; " \
	"fi\0" \
	"sdboot=if mmcinfo; then " \
		"run uenvboot; " \
		"echo Copying Linux from SD to RAM... && " \
		"load mmc 0 ${kernel_load_address} ${kernel_image} && " \
		"load mmc 0 ${devicetree_load_address} ${devicetree_image} && " \
		"bootz ${kernel_load_address} - ${devicetree_load_address}; " \
	"fi\0" \
	"default_bootcmd=run sdboot;\0" \
	"bootargs=console=ttyPS0,115200 root=/dev/mmcblk0p2 rootwait rw\0" \
```

![image-20250807152453715](../images/\pic\image-20250807152453715.png)

4. 将前面生成的三个设备树文件粘贴到arch/arm/dts/目录下，如果有pl.dtsi也要将pl.dtsi粘贴到这里。

5. 修改arch/arm/dts/目录下的Makefile文件，搜索“dtb-$(CONFIG_ARCH_ZYNQ)”，在其末尾加上“system-top.dtb”，这样编译后我们就能得到启动文件之一的system-top.dtb

![image-20250807153300681](../images/\pic\image-20250807153300681.png)

6. 配置uboot项目，在uboot源码的根目录下执行

```bash
make ARCH=arm CROSS_COMPILE=arm-xilinx-linux-gnueabi- xilinx_zynq_virt_defconfig
```

![image-20250807153434727](../images/\pic\image-20250807153434727.png)

7. 使用manuconfig进行图形化配置，同样在uboot源码的根目录下执行

```bash
make ARCH=arm CROSS_COMPILE=arm-xilinx-linux-gnueabi- menuconfig
```

​	将自动boot前的时延改为5s，便于后续在uboot下调试

![image-20250807153748534](../images/\pic\image-20250807153748534.png)

​	前面在zynq-common.h中复制粘贴一大串环境变量的时候有个变量名称为default_bootcmd，因此我们在这里也要同样改为default_bootcmd，此时default_bootcmd就是boot执行的第一个脚本。

![image-20250807153920347](../images/\pic\image-20250807153920347.png)

​	将环境变量保存在fat分区下。

![image-20250807154154170](../images/\pic\image-20250807154154170.png)

​	记住波特率，配置串口号。

![image-20250807154312492](../images/\pic\image-20250807154312492.png)

8. 回到uboot源码的根目录执行下面的命令来编译整个uboot工程。

```bash
make ARCH=arm CROSS_COMPILE=arm-xilinx-linux-gnueabi- -j20
```

​	在源码的根目录下我们就能找到，所生成的u-boot.elf文件。

![image-20250807154649766](../images/\pic\image-20250807154649766.png)

### 3. 打包BOOT.BIN文件

1. 创建应用工程

​	File->New->Application Project

​	导入和前面设备树工程一样的xsa文件

![image-20250807155018747](../images/\pic\image-20250807155018747.png)

​	输入工程名称，这里是用来做生成fsbl文件的，因此命名为fsblapp

![image-20250807155147635](../images/\pic\image-20250807155147635.png)

​	操作系统保持为standalone（裸机），架构保持为32bit。

![image-20250807155342918](../images/\pic\image-20250807155342918.png)

​	选择为Zynq FSBL工程模板，点击Finash即可

![image-20250807155431815](../images/\pic\image-20250807155431815.png)

2. 编辑fsbl_debug.h头文件，便于出现bug时打印调试信息。

![image-20250807155748755](../images/\pic\image-20250807155748755.png)

3. 编译FSBL工程

![image-20250807155510187](../images/\pic\image-20250807155510187.png)

4. 创建BOOT.BIN

![image-20250807155908977](../images/\pic\image-20250807155908977.png)

​	在下方Boot image partitions处，按序添加fsbl.elf、design_1_wrapper.bit和u-boot.elf文件，其中u-boot.elf就是上一节得到的u-boot.elf文件。这三个文件的顺序也就是启动的顺序。最后点击Create Image，就得到了需要的BOOT.BIN文件。

![image-20250807160059092](../images/\pic\image-20250807160059092.png)

​	如果xsa文件有更新，右键fsbl的硬件平台，点击Update Hardware Specification，再重新编译fsbl项目即可。

### 4. 编译生成zImage文件

​	前面已经得到了启动所需三个文件中的两个文件，现在是最后一个内核镜像文件。

1. 从Xilinx官方的GitHub中下载内核的源码，版本选择为2024.2。

```
https://github.com/Xilinx/linux-xlnx/tags
```

![image-20250807160755592](../images/\pic\image-20250807160755592.png)

2. 在虚拟机中解压源码，并且和uboot的源码放在同一个目录下便于管理

![image-20250807161127159](../images/\pic\image-20250807161127159.png)

3. 将前面生成的三个设备树文件粘贴到arch/arm/boot/dts/xilinx/目录下，如果有pl.dtsi也要将pl.dtsi粘贴到这里。
5. 修改arch/arm/boot/dts/xilinx/目录下的Make file文件

​	在末尾添加system-top.dtb

![image-20250807161442697](../images/\pic\image-20250807161442697.png)

5. 根据前面配置的内容使用下面命令做项目配置

```bash
make ARCH=arm CROSS_COMPILE=arm-xilinx-linux-gnueabi- xilinx_zynq_defconfig
```

![image-20250807161843918](../images/\pic\image-20250807161843918.png)

6. 在内核源码的根目录下编译内核

```bash
make ARCH=arm CROSS_COMPILE=arm-xilinx-linux-gnueabi- -j20
```

​	执行后我们就得到了所需要的最后一个启动文件zImage

# 启动开发板

​	我这里默认已经准备好了一个SD卡，如果没有准备好可以查看下面关于分区的教程。

```
https://blog.csdn.net/qq_45159348/article/details/125584759
```

​	将前面获得的三个文件放到SD卡的boot分区即可

![image-20250807162411640](../images/\pic\image-20250807162411640.png)

# 小结

​	本文主要介绍了启动文件的制作流程，至于各个文件的含义可以自行从网上了解，后续关于根文件系统的移植与配置我会在下一篇文章中展开。为了更加方便维护制作启动文件的工程，可以写一个脚本，当整个有文件更新时，运行脚本即可。其中UBOOT_PATH、KERNEL_PATH和DIST_PATH自定义即可。

```bash
UBOOT_PATH=~/zynq_base/u-boot
KERNEL_PATH=~/zynq_base/kernel
DIST_PATH=/mnt/e/Project/Vitis2024/output
cd $UBOOT_PATH
make ARCH=arm CROSS_COMPILE=arm-xilinx-linux-gnueabi- -j20
cd $KERNEL_PATH
make ARCH=arm CROSS_COMPILE=arm-xilinx-linux-gnueabi- -j20
cp $KERNEL_PATH/arch/arm/boot/dts/xilinx/system-top.dtb $DIST_PATH
cp $UBOOT_PATH/u-boot.elf $DIST_PATH
cp $KERNEL_PATH//arch/arm/boot/zImage $DIST_PATH
```



# 参考文章

[深度：一文看懂Linux内核！Linux内核架构和工作原理详解](https://blog.csdn.net/jinking01/article/details/104547290)

[【一文秒懂】Linux设备树详解](https://uniondong.github.io/docs/linux/linux_driver_develop_basic/一文秒懂linux设备树详解/)

[3_【正点原子】领航者ZYNQ之嵌入式Linux开发指南_V3.1](https://touhouxingchen.fun/docs/3_【正点原子】领航者ZYNQ之嵌入式Linux开发指南_V3.1.pdf)