# pcf8574_7seg

## Getting Started

This driver use to control up to 4 digit 7 led segment using i2c IO expander pcf8574 and 74hc595. Due to limitation of pcf8574 (100khz), It can only drive maximun 4 digit.
This driver is example how to write i2c drive + running thread in linux kernel.

Tested on FreeScale imx6, kernel version 4.1.15, Using on i2c0.

### Prerequisites

To build this project, 
1. Install appropriate arm compiler for your board
2. [Linux kernel for FreeScale](https://github.com/Freescale/linux-fslc)
3. [pcf8574 board](https://www.aliexpress.com/item/LCD1602-Converter-Board-IIC-I2C-TWI-SPI-Serial-Interface-Module-for-1602-LCD-Display-for-Arduino/32756509214.html), anyboard with pcf8574 can work.
4. any 7segment led, in this case i use [this](https://hshop.vn/products/mach-hien-thi-8-led-7-doan) board

***Note:*** 7 segment and 74hc595 must be wired like [this](Schematic.pdf) 

### Installing

1. Modify device tree

Add this section to your device tree. Build and install in board. ***Note***: change number *27* to appropriate i2c address of pcf8574. 
```
&i2c1 {
	pcf8574_7seg: pcf8574_7seg@27 {
		compatible = "nxp,pcf8574_7seg";
		status ="okay";
		reg = <0x27>;
	};
};
```

2. Config the driver 
go to file pcf8574_7seg.c, there are 4 option can be config:
- COMMON_ANODE: check if your 7seg led is common anode
- COMMON_CATHODE: check if your 7seg led is common cathode
- DIGIT: set how many 7seg digit want to use
- NUM_DIGIT: set how many digit there are in 7seg (including unused digit, which in [this board](https://hshop.vn/products/mach-hien-thi-8-led-7-doan) is 8)
```
#define COMMON_ANODE
//#define COMMON_CATHODE
#define DIGIT 4
//#define NUM_DIGIT 8
```

3. Fix *Makefile* kernel directory

```
...
KERNEL_SRC := /your/path/to/kernel/folder
...
```

4. Build module 
```
make CROSS_COMPILER=<your arm compiler>
```
or set in evironment variable
```
set CROSS_COMPILER=<your arm compiler>
make
```
After build complete, it will create file *pcf8574.ko*.

5. Install as module in linux
Copy *pcf8574.ko* to your arm board and run
```
insmod pcf8574.ko
```

### Running

After install module into kernel, there will be attribute created in **/sys/class/pcf8574_7seg/number**
To change number show in 7seg
```
echo 1234 > /sys/class/pcf8574_7seg/number
```

To get the number showing in 7seg
```
cat /sys/class/pcf8574_7seg/number
```

### License

This project is licensed under the GPLv2 License - see the [LICENSE](LICENCE) file for details
