# Embedded-operating-systems

嵌入式作業系統分析與實作 ANALYSIS AND IMPLEMENTATION OF EMBEDDED OPERATING SYSTEMS, 張大緯

Including Lab0~Lab4 and Final project.


## Final project

USB + LCD 16x2 (1602A) + Bluetooth (HC05) + STM32F407-discovery (STM32F407VGTx)

Developed on STM32CubeIDE 1.11.0, and STM32Cube FW_F4 V1.27.1

(notice: "./USB_MP3/docs"   and   "./USB_MP3/result")


    HC05

RXD <--> PD8

TXD <--> PB11

VCC50 <--> 5V

VCC33 <--> 3V

GND <--> GND


    LCD16x2

SCL <--> PA8

SDA <--> PC9

VCC <--> 5V

GND <--> GND


My YT:

https://www.youtube.com/@Jia81920

DEMO:

https://youtu.be/hoxtOU28bmE

How to use:

https://youtu.be/JRACZXBxUdk


## TODO

Add RTOS(ex: FreeRTOS or RT-Thread)

This project is not stable enough. It is recommended to use RTOS to reconstruct it instead of simply using a state machine.


## Resource

ST Community:

https://community.st.com/

https://www.stmcu.com.cn/Product/pro_detail/STM32F407_417/design_resource

YT:

https://www.youtube.com/@mutexembedded2206

https://www.youtube.com/@ControllersTech

https://www.youtube.com/@keysking4403

github:

https://github.com/STMicroelectronics

https://github.com/MakeNTU

Others:

https://01001000.xyz/2020-08-09-Tutorial-STM32CubeIDE-SD-card/

http://elm-chan.org/docs/mmc/mmc_e.html

https://hackmd.io/@cpt/embeddedOS_labs


## Recommend

1. Using Winmerge to check your IDE create a correct file.

    I have encountered VBUS code where the IDE generated errors.

2. Find a simple way to check your hardware device, like using Arduino to check LCD.


## License

MIT
