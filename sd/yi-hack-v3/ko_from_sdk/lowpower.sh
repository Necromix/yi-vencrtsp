#!/bin/sh

#�ر�USB PHY���ܵ�Դ��0x20120078 [12]bit����Ϊ0
himm 0x20120078 0x0c0301a0

#�ر�USB 2.0 PHY��ʱ�ӣ�0x200300b8 [7]bit����0
himm 0x200300b8 0x127

#SAR ADC POWER_DOWN_MODEL���򿪺��ڲ�ʹ��ADC��ʱ���Զ�power_down��[14]bit����1
himm 0x200b0000 0x0000c0ff

#��PWM
himm 0x20030038 0x2

#�ر�IR [0]bit����0
himm 0x20070000 0x0

#IR �ܽŸ��ó�gpio
himm 0x200f0070 0x0

#UART2��ʹ�ܣ�0x200A0000 [9][8][0]bit������Ϊ0
himm 0x200A0030 0x0

#UART2�ܽŸ��ó�gpio
himm 0x200f00cc 0x0
himm 0x200f00d0 0x0

#�ر�SPI0��SPI1
himm 0x200C0004 0x7F00
himm 0x200E0004 0x7F00

#spi0 �ܽŸ��ó�gpio
himm 0x200f0040 0x0    # 000��GPIO3_3��
himm 0x200f0044 0x0    # 000��GPIO3_4��
himm 0x200f0048 0x0    # 000��GPIO3_5��
himm 0x200f004c 0x0    # 000��GPIO3_6��

#spi1 �ܽŸ��ó�gpio
himm 0x200f0030 0x0    # 000��GPIO0_3��
himm 0x200f0050 0x0    # 000��GPIO3_7��
himm 0x200f0054 0x0    # 000��GPIO4_0��
himm 0x200f0058 0x0    # 000��GPIO4_1��
himm 0x200f005c 0x0    # 000��GPIO4_2��

#AUDIO CODEC LINE IN �ر�������
#himm 0x20050068 0xa8022c2c
#himm 0x2005006c 0xf5035a4a
