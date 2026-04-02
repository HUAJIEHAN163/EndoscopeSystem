STM32CubeMX 最终完整配置方案
1. GPIO Output — LED
在 Pinout 视图中右键引脚设置：

引脚	模式	User Label
PI0	GPIO_Output	LED_R
PG7	GPIO_Output	LED_G
PH11	GPIO_Output	LED_B
PI0 如果能配置详细参数：

GPIO output level: High

GPIO mode: Output Push Pull

Maximum output speed: Low

Pull-up/Pull-down: No pull

PG7、PH11 保持默认即可，初始电平我们在代码中处理。

2. GPIO Input — 按键
引脚	模式	User Label
PA0	GPIO_Input	KEY1
PG3	GPIO_Input	KEY2
如果能配置详细参数：

Pull-up/Pull-down: Pull-up

3. USART3 — 串口
位置：Connectivity → USART3

参数	值
Mode	Asynchronous
Baud Rate	115200
Word Length	8 Bits
Stop Bits	1
Parity	None
Data Direction	Receive and Transmit
引脚确认：

PB10 → USART3_TX

PB12 → USART3_RX

4. TIM2 — 定时器
位置：Timers → TIM2

参数	值
Clock Source	Internal Clock
Prescaler (PSC)	6399
Counter Mode	Up
Counter Period (ARR)	999
Auto-reload preload	Enable
64MHz / (6399+1) / (999+1) = 10Hz → 每 100ms 触发一次中断

5. NVIC — 中断
位置：System Core → NVIC

中断	使能	抢占优先级
TIM2 global interrupt	✅	3
USART3 global interrupt	✅	1
其余保持默认不动。

6. Runtime Context 检查
所有以上外设都必须分配给 Cortex-M4：

PI0, PG7, PH11, PA0, PG3 → M4

USART3 → M4

TIM2 → M4

检查方法：点击每个外设/引脚，确认右侧 Runtime context 为 Cortex-M4。

7. 生成代码
Ctrl + S 保存

弹窗点 Yes 生成代码

配置完成生成代码后告诉我，我会读取生成的代码然后编写完整的应用逻辑。