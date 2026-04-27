---
title: "STM32 复习记录：第一阶段从 GPIO 到 RCC"
description: "整理一次 STM32 回顾过程中的第一阶段学习记录，涵盖 GPIO、中断、IIC EEPROM、UART、ADC、RCC，以及 CAN 和 SPI Flash 的初步实践。"
date: "2026-03-21"
draft: false
featured: true
tags: ["STM32", "MCU", "Embedded", "Review"]
readingTime: "8 分钟"
category: "MCU"
---

## 背景

最近在重新梳理 STM32 的基础知识。和第一次学习相比，这次我更想把重点放在“能不能自己写出来、能不能自己调通”上，而不是只停留在看懂例程。

因此我给自己定了一个比较明确的原则：

- 学函数调用
- 用 20% 的时间看资料，用 80% 的时间调试
- 尽量不直接照搬例程
- 尝试只参考官方手册和库函数，自己把程序写出来

这篇文章记录的是第一阶段的回顾情况，主要覆盖 GPIO、中断、IIC、UART、ADC 和 RCC 几部分内容，同时也补上了 CAN 与 SPI Flash 的初步实验。

## 这一阶段的目标

这一阶段我更关注两个能力：

1. 能否把常见外设的初始化流程自己串起来
2. 遇到问题时，能否通过手册、时钟配置和寄存器逻辑定位原因

换句话说，这一轮复习的重点不是“记住多少 API”，而是重新建立一套底层理解。

## 当前完成情况

### GPIO

GPIO 部分我通过阅读 STM32F4 官方手册，完成了 `key2` 控制 LED 亮灭的功能。

这一部分虽然基础，但它是后面所有外设学习的入口，因为很多现象最终都要先回到最基本的引脚配置、输入输出模式和时钟使能是否正确。

### 外部中断

中断部分已经完成了按键控制 LED 状态翻转。

这一块重新梳理后，我觉得最需要注意的是初始化顺序，尤其是下面几个点：

- 初始化 `NVIC`
- 初始化 `SYSCFG` 时钟
- 连接中断线
- 初始化中断配置
- 编写中断服务函数

另外，中断函数命名也必须严格对应，否则现象会非常“像是代码没问题但就是不进中断”。

### IIC

IIC 这一部分我已经完成了基于硬件 IIC 的 EEPROM 读写实验，用来记录开发板重启次数，目前已经跑通了单字节读写。

这部分遇到的一个很具体的问题是：读写之间需要留出至少 `5ms` 的延时，否则结果不稳定。

当前这个实验的最小闭环是：上电先读取 EEPROM 中某个地址的值，计数加一后再写回去，相当于把 EEPROM 当成“重启次数记录器”来用。

直接贴一下这部分的核心代码：

```c
//EEPROM初始化
void IIC_EEPROM_init(){
	//初始化时钟
	//IIC1 - PB8/PB9
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);
	
	//初始化GPIO
	GPIO_PinAFConfig(GPIOB, GPIO_PinSource8, GPIO_AF_I2C1);
	GPIO_PinAFConfig(GPIOB, GPIO_PinSource9, GPIO_AF_I2C1);	
	GPIO_InitTypeDef GPIO_structInit;
	GPIO_structInit.GPIO_Pin = GPIO_Pin_8|GPIO_Pin_9;
	GPIO_structInit.GPIO_Mode = GPIO_Mode_AF;
	GPIO_structInit.GPIO_OType = GPIO_OType_OD;
	GPIO_structInit.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_structInit.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB, &GPIO_structInit);	
	
	//初始化IIC
	I2C_InitTypeDef I2C_InitStruct;
	I2C_InitStruct.I2C_ClockSpeed = 400000;
	I2C_InitStruct.I2C_Mode = I2C_Mode_I2C;
	I2C_InitStruct.I2C_DutyCycle = I2C_DutyCycle_2;
	I2C_InitStruct.I2C_Ack = I2C_Ack_Enable;
	I2C_InitStruct.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
	I2C_InitStruct.I2C_OwnAddress1 = 0x1f;
	I2C_Init(I2C1, &I2C_InitStruct);
	I2C_Cmd(I2C1, ENABLE);
}

//EEPROM写字节
void eeprom_writeByte(uint8_t addr, uint8_t dat){
	//起始信号
	I2C_GenerateSTART(I2C1, ENABLE);
	uint32_t iic_timeOut = 0x4000;
	while(!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_MODE_SELECT)){
		if((iic_timeOut--)==0) {
			printf("iic generate start time out \r\n");
			return;
		}
	}
	
	//发送设备地址
	I2C_Send7bitAddress(I2C1, 0xa0, I2C_Direction_Transmitter);
	iic_timeOut = 0x1000;
	while(!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)){
		if((iic_timeOut--)==0){ 
			printf("iic send device address time out 1\r\n");
			return;
		}
	}		
	
	//发送写入的地址
	I2C_SendData(I2C1, addr);
	iic_timeOut = 0x1000;
	while(!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_BYTE_TRANSMITTED)){
		if((iic_timeOut--)==0) {
			printf("iic send write address time out\r\n");
			return;
		}
	}

	//发送写入的数据
	I2C_SendData(I2C1, dat);
	iic_timeOut = 0x1000;
	while(!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_BYTE_TRANSMITTED)){
		if((iic_timeOut--)==0){
			printf("iic send write data time out\r\n");
			return;
		} 
	}
	
	//停止信号
	I2C_GenerateSTOP(I2C1, ENABLE);
}

//EEPROM读字节
int eeprom_readByte(uint8_t addr, uint8_t* dat){	
	uint32_t iic_timeOut = 0x1000*10;
	while(I2C_GetFlagStatus(I2C1, I2C_FLAG_BUSY)){
		if((iic_timeOut--)==0){
			printf("iic device is busy\r\n");
			return -1; 
		} 
	}
	
	//起始信号
	I2C_GenerateSTART(I2C1, ENABLE);
	iic_timeOut = 0x1000;
	while(!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_MODE_SELECT)){
		if((iic_timeOut--)==0){
			printf("iic generate start time out\r\n");
			return -1;
		} 
	}
	
	//发送设备地址
	I2C_Send7bitAddress(I2C1, 0xa0, I2C_Direction_Transmitter);
	iic_timeOut = 0x1000;
	while(!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)){
		if((iic_timeOut--)==0) {
			printf("iic send device address time out 2\r\n");
			return -1;
		}
	}	
	
	//清除EV6事件
	I2C_Cmd(I2C1, ENABLE);

	//发送读取的地址
	I2C_SendData(I2C1, addr);
	iic_timeOut = 0x1000;
	while(!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_BYTE_TRANSMITTED)){
		if((iic_timeOut--)==0) {
			printf("iic send write address time out\r\n");
			return -1;
		}
	}

	//起始信号
	I2C_GenerateSTART(I2C1, ENABLE);
	iic_timeOut = 0x1000;
	while(!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_MODE_SELECT)){
		if((iic_timeOut--)==0) {
			printf("iic generate start time out\r\n");
			return -1;
		}
	}
	
	//发送设备地址
	I2C_Send7bitAddress(I2C1, 0xa0, I2C_Direction_Receiver);
	iic_timeOut = 0x1000;
	while(!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED)){
		if((iic_timeOut--)==0) {
			printf("iic send device address time out\r\n");
			return -1;
		}
	}	
	
	//接受数据
	iic_timeOut = 0x1000;
	while(!I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_BYTE_RECEIVED)){
		if((iic_timeOut--)==0) {
			printf("iic receive device data time out\r\n");
			return -1;
		}		
	}
	*dat = I2C_ReceiveData(I2C1);
	
	//发送非应答信号
	I2C_AcknowledgeConfig(I2C1, DISABLE);
	
	//停止信号
	I2C_GenerateSTOP(I2C1, ENABLE);
	
	return 0;
}
```

### UART

UART 目前已经完成串口回环。

这里遇到过一次比较典型的问题：回环返回值异常。最后定位发现，不是串口逻辑本身有问题，而是时钟配置不匹配。开发板实际使用的是 `8MHz` 晶振，而库文件默认是按 `25MHz` 晶振配置的。

最终通过修改库文件中的以下参数解决了问题：

- `HSE_VALUE`
- `PLL_M`

这个问题让我再次意识到，串口异常很多时候不是出在串口，而是出在时钟树。

### ADC

ADC 这部分已经完成了读取光敏电阻电压变化的实验。

这里比较容易漏掉的点有两个：

1. 初始化 `ADC_Common` 和 `ADC` 之后，还需要配置规则通道
2. 每次读取 ADC 值前，需要重新使能一次转换

如果这两个步骤缺失，程序可能能跑，但读出来的数据会不符合预期。

### RCC

RCC 这部分我主要是通过阅读野火教程并结合官方手册，重新理解了时钟树的配置方式和整体工作流程。

虽然这一部分没有像 GPIO 或 ADC 那样直接对应某个“可见实验现象”，但它的重要性其实更高。很多外设初始化问题、波特率异常问题、采样异常问题，最后都能追溯到 RCC 配置是否正确。

这次整理下来，我对下面几个点的印象比较深：

- 如果使用 `HSE` 或者 `HSE` 经过 `PLL` 倍频后的时钟作为系统时钟 `SYSCLK`，当 `HSE` 故障时，不仅 `HSE` 会被关闭，`PLL` 也会被关闭，此时会自动切换到内部高速时钟 `HSI` 作为备用系统时钟，直到 `HSE` 恢复正常。这里 `HSI = 16MHz`
- `VCO` 输入时钟经过倍频系数 `N` 之后，得到 `VCO` 输出时钟，这个输出范围必须控制在 `192~432MHz`。如果配置 `N = 336`，那么 `VCO` 输出就是 `336MHz`。如果要把系统时钟继续往上推，最直接下手的位置就是这里的 `N`
- 这套 `PLL` 时钟关系整理成公式之后会清晰很多：

```text
VCOCLK_IN  = PLLCLK_IN / M   = HSE / 25   = 1MHz
VCOCLK_OUT = VCOCLK_IN * N   = 1MHz * 336 = 336MHz
PLLCLK_OUT = VCOCLK_OUT / P  = 336 / 2    = 168MHz
USBCLK     = VCOCLK_OUT / Q  = 336 / 7    = 48MHz
```

- `APB2` 总线时钟 `PCLK2` 由 `HCLK` 经过高速 `APB2` 预分频器得到，分频因子可以是 `[1, 2, 4, 8, 16]`，具体由 `RCC_CFGR` 的 `PPRE2` 位设置。它属于高速总线，像 `USART1`、`SPI1` 这些高速外设都挂在这条总线上
- `PCLK1` 属于低速总线时钟，最高是 `42MHz`，片上低速外设挂在这条总线上，比如 `USART2/3/4/5`、`SPI2/3`、`I2C1/2`

### CAN

原本我把 CAN 记在“待推进”里，但这次整理代码时发现，其实自己已经把最小实验跑起来了，只是还没有把它扩展成更完整的收发验证流程。

目前完成的是 `CAN1` 的回环模式测试。

直接贴代码：

```c
//CAN初始化
void CAN_init(){
	//初始化时钟
	//PA11-CAN1_RX PA12-CAN1_TX
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_CAN1, ENABLE);
	
	//初始化GPIO
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource11, GPIO_AF_CAN1);
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource12, GPIO_AF_CAN1);	
	GPIO_InitTypeDef GPIO_structInit;
	GPIO_structInit.GPIO_Pin = GPIO_Pin_11|GPIO_Pin_12;
	GPIO_structInit.GPIO_Mode = GPIO_Mode_AF;
	GPIO_structInit.GPIO_OType = GPIO_OType_PP;
	GPIO_structInit.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_structInit.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_structInit);	
	
	//初始化CAN1
	CAN_InitTypeDef CAN_InitStruct;
	CAN_InitStruct.CAN_Prescaler = 6;
	CAN_InitStruct.CAN_Mode = CAN_Mode_LoopBack;
	CAN_InitStruct.CAN_SJW = CAN_SJW_2tq;
	CAN_InitStruct.CAN_BS1 = CAN_BS1_4tq;
	CAN_InitStruct.CAN_BS2 = CAN_BS2_2tq;
	CAN_InitStruct.CAN_TTCM = DISABLE;
	CAN_InitStruct.CAN_ABOM = ENABLE;
	CAN_InitStruct.CAN_AWUM = ENABLE;
	CAN_InitStruct.CAN_NART = DISABLE;
	CAN_InitStruct.CAN_RFLM = DISABLE;
	CAN_InitStruct.CAN_TXFP = DISABLE;
	CAN_Init(CAN1, &CAN_InitStruct);
	
	//初始化CAN1过滤器
	uint32_t device_id = 0x0728;
	uint32_t device_mask = 0xffffffff;//全匹配
	CAN_FilterInitTypeDef CAN_FilterInitStruct;
	CAN_FilterInitStruct.CAN_FilterIdHigh = ((((uint32_t)device_id<<3)|0x00000004|0x00000000)&0xffff0000)>>16;
	CAN_FilterInitStruct.CAN_FilterIdLow = (((uint32_t)device_id<<3)|0x00000004|0x00000000)&0x0000ffff;
	CAN_FilterInitStruct.CAN_FilterMaskIdHigh = (uint16_t)(device_mask>>16);
	CAN_FilterInitStruct.CAN_FilterMaskIdLow = (uint16_t)(device_mask&0x0000ffff);
	CAN_FilterInitStruct.CAN_FilterFIFOAssignment = CAN_Filter_FIFO0;
	CAN_FilterInitStruct.CAN_FilterNumber = 13;
	CAN_FilterInitStruct.CAN_FilterMode = CAN_FilterMode_IdMask;
	CAN_FilterInitStruct.CAN_FilterScale = CAN_FilterScale_32bit;
	CAN_FilterInitStruct.CAN_FilterActivation = ENABLE;
	CAN_FilterInit(&CAN_FilterInitStruct);
	
	printf("CAN1 has been initalled\r\n");
}

//CAN发送消息
void CAN_sendMsg(CAN_TypeDef* CANx, uint8_t* dat, uint8_t length){
	CanTxMsg TxMessage;
	TxMessage.ExtId = 0x0728;
	TxMessage.IDE = CAN_Id_Extended;
	TxMessage.DLC = length;
	TxMessage.RTR = CAN_RTR_Data;
	for(int i=0; i<length; i++){
		TxMessage.Data[i] = *(dat+i);
	}
	
	CAN_Transmit(CANx, &TxMessage);
	printf("Message has been transmit in CAN1\r\n");
}

//CAN接受消息
uint8_t CAN_recvMsg(CAN_TypeDef* CANx, uint8_t* datas){
	CanRxMsg RxMessage;
	CAN_Receive(CANx, CAN_FIFO0, &RxMessage);
	if(RxMessage.ExtId==0x0728){
		memcpy(datas, RxMessage.Data, RxMessage.DLC);
		printf("Message has been received from CAN1\r\n");
		return RxMessage.DLC;
	}
	else{
		return 0;
	}
}
```

### SPI Flash

SPI 这部分也不再只是停留在看例程了。我已经写了一个最小可用的 SPI Flash 读写流程，能完成指定地址的读取、扇区擦除和字符串写入。

直接贴代码：

```c
//FLASH初始化
void SPI_FLASH_init(){
	//时钟初始化
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1, ENABLE);

	//初始化GPIO
	//时钟和数据IO
	GPIO_PinAFConfig(GPIOB, GPIO_PinSource3, GPIO_AF_SPI1);
	GPIO_PinAFConfig(GPIOB, GPIO_PinSource4, GPIO_AF_SPI1);	
	GPIO_PinAFConfig(GPIOB, GPIO_PinSource5, GPIO_AF_SPI1);	
	GPIO_InitTypeDef GPIO_structInit;
	GPIO_structInit.GPIO_Pin = GPIO_Pin_3|GPIO_Pin_4|GPIO_Pin_5;
	GPIO_structInit.GPIO_Mode = GPIO_Mode_AF;
	GPIO_structInit.GPIO_OType = GPIO_OType_PP;
	GPIO_structInit.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_structInit.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB, &GPIO_structInit);	
	//CS脚
	GPIO_structInit.GPIO_Pin = GPIO_Pin_14;
	GPIO_structInit.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_structInit.GPIO_OType = GPIO_OType_PP;
	GPIO_structInit.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_structInit.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB, &GPIO_structInit);
	
	//初始化SPI
	SPI_InitTypeDef SPI_InitStruct;
	SPI_InitStruct.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
	SPI_InitStruct.SPI_Mode = SPI_Mode_Master;
	SPI_InitStruct.SPI_DataSize = SPI_DataSize_8b;
	SPI_InitStruct.SPI_CPOL = SPI_CPOL_High;
	SPI_InitStruct.SPI_CPHA = SPI_CPHA_2Edge;
	SPI_InitStruct.SPI_NSS = SPI_NSS_Soft;
	SPI_InitStruct.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_2;
	SPI_InitStruct.SPI_FirstBit = SPI_FirstBit_MSB; 
	SPI_InitStruct.SPI_CRCPolynomial = 0;
	SPI_Init(SPI1, &SPI_InitStruct);
	
	//使能SPI
	SPI_Cmd(SPI1, ENABLE);
}

//读写一个字节
uint8_t SPI_WriteReadByte(uint8_t tx_data){
	uint8_t rx_data;
	while(SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE)==RESET);
	SPI_I2S_SendData(SPI1, tx_data);
	while(SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE)==RESET);
	rx_data = SPI_I2S_ReceiveData(SPI1);
	return rx_data;
}

//SPI写使能
void SPI_Write_enable(){
	GPIO_WriteBit(GPIOB, GPIO_Pin_14, Bit_RESET);
	SPI_WriteReadByte(0x06);//write enable
	GPIO_WriteBit(GPIOB, GPIO_Pin_14, Bit_SET);
}

//SPI写等待
void SPI_Write_wait(){
	uint8_t status;
	GPIO_WriteBit(GPIOB, GPIO_Pin_14, Bit_RESET);
	//持续读S0是否是write in progress
	SPI_WriteReadByte(0x05);
	do{
		status = SPI_WriteReadByte(0x05);
	}while((status&0x01)==0x01);
	GPIO_WriteBit(GPIOB, GPIO_Pin_14, Bit_SET);
}

//SPI擦除
void SPI_SectorErase(uint32_t addr){
	SPI_Write_enable();
	SPI_Write_wait();
	GPIO_WriteBit(GPIOB, GPIO_Pin_14, Bit_RESET);
	SPI_WriteReadByte(0x20);//
	SPI_WriteReadByte((addr>>16)&0xff);//写地址
	SPI_WriteReadByte((addr>>8)&0xff);
	SPI_WriteReadByte(addr&0xff);
	GPIO_WriteBit(GPIOB, GPIO_Pin_14, Bit_SET);	
	SPI_Write_wait();
}

void SPI_WriteData(uint8_t* buffer, uint32_t addr, uint32_t length){
	SPI_Write_enable();
	GPIO_WriteBit(GPIOB, GPIO_Pin_14, Bit_RESET);
	SPI_WriteReadByte(0x02);//program
	SPI_WriteReadByte((addr>>16)&0xff);//写地址
	SPI_WriteReadByte((addr>>8)&0xff);
	SPI_WriteReadByte(addr&0xff);
	for(int i = 0; i< length; i++){
		SPI_WriteReadByte(buffer[i]);
	}
	GPIO_WriteBit(GPIOB, GPIO_Pin_14, Bit_SET);
	SPI_Write_wait();
}

void SPI_ReadData(uint8_t* buffer, uint32_t addr, uint32_t length){
	GPIO_WriteBit(GPIOB, GPIO_Pin_14, Bit_RESET);
	SPI_WriteReadByte(0x03);//read
	SPI_WriteReadByte((addr>>16)&0xff);//写地址
	SPI_WriteReadByte((addr>>8)&0xff);
	SPI_WriteReadByte(addr&0xff);	
	for(int i = 0; i< length; i++){
		buffer[i] = SPI_WriteReadByte(addr+i);
	}
	GPIO_WriteBit(GPIOB, GPIO_Pin_14, Bit_SET);
}
```

## 还在继续打磨的部分

虽然 CAN 和 SPI Flash 都已经做了最小实验，但它们离“真正完成闭环”还有一段距离。

- `SPI`
- `CAN`

接下来我更想补的是这些内容：

- 不只是能跑通，还要把时序、异常情况和边界条件补全
- 不只是做回环，还要验证真实节点之间的通信
- 不只是把功能写出来，还要把驱动整理成自己能复用的模板

## 这一轮复习的收获

这次 STM32 回顾过程中，我越来越明确一件事：基础外设的学习不能只靠“看过”，一定要落到“自己写过、自己调过、自己修过问题”。

目前这一阶段给我印象最深的几个点是：

- 中断问题往往卡在初始化链路是否完整
- IIC 问题往往和时序要求有关
- UART 问题很可能本质是时钟配置问题
- ADC 问题很容易出在转换触发和通道配置
- CAN 问题除了看发送接收，还必须同时看过滤器和工作模式
- SPI Flash 问题不能只盯着 SPI 外设本身，还要看 Flash 指令和忙状态
- RCC 虽然抽象，但它实际上影响了几乎所有外设

## 下一步计划

接下来我准备继续推进下面几项：

- 把 `SPI Flash` 读写流程继续补完整，处理页写和状态检查
- 把 `CAN` 从回环测试推进到更接近真实通信的场景
- 把当前阶段用到的初始化流程做一份自己的模板整理
- 尝试减少对现成例程的依赖，进一步强化独立排错能力

## 小结

这一阶段的复习并不追求一次把所有外设都做完，而是想重新建立一种更扎实的学习方式：

- 少一点照抄
- 多一点自己写
- 少一点“看懂了”
- 多一点“调通了”

现在回头看，这一阶段已经不只是 GPIO 到 RCC 的复习了。我实际上已经开始把 IIC EEPROM、CAN 回环、SPI Flash 这些实验一点点串起来。虽然它们还谈不上成熟，但正是这种“先自己搭起来，再自己修问题”的过程，让我感觉这次复习比第一次学习更扎实。

如果后续我继续把 SPI、CAN 以及更复杂的定时器、DMA 也按这个方式补起来，那么这轮 STM32 回顾的价值就不仅是“复习知识点”，而是把 MCU 基础重新打成一套更稳的工程能力。
