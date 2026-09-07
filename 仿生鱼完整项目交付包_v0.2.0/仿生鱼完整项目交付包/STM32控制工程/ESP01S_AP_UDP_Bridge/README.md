# ESP-01S AP/UDP ↔ UART 透传桥

让 ESP-01S 上电即自建连接：**开热点 `BionicFish-AP`** + 在 **UDP 9000** 端口监听，
并做 **UDP ↔ UART0(接 STM32 USART2)** 的双向透明桥。

- STM32 固件**不需要任何改动**，它仍按原样把 V1 ASCII 帧发给 USART2。
- 出厂 AT 固件做不到 UDP 的透明串口桥，所以必须刷这段自定义固件。

> 参数在 `ESP01S_AP_UDP_Bridge.ino` 顶部常量里改：
> `kApSsid` / `kApPassword` / `kUdpPort`，以及可选 `kStaSsid`/`kStaPassword`。

---

## 一、烧录(取下 ESP，用 CH340 刷一次)

### 接线(此时 ESP 脱离 STM32，单独接 CH340/USB-TTL)

| CH340 | ESP-01S | 说明 |
|---|---|---|
| TX | RXD | CH340 TX → ESP RX |
| RX | TXD | CH340 RX ← ESP TX |
| 3.3V | VCC | 供电(见下) |
| GND | GND | 共地 |
| — | **EN/CH_PD** | **接到 3.3V**(拉高，否则模块不工作) |
| — | **GPIO0** | 烧录时接 GND，烧完断开悬空 |

> **供电**：ESP-01S 发射瞬间可拉 ~300 mA，务必用能供 ~500 mA 的 **3.3V 稳压**，
> 别用 CH340 或单片机板的 3.3V 直接带。CH340 的 3.3V 通常只几十 mA，供不起 ESP。

### Arduino IDE 编译并上传

1. 安装 [Arduino IDE](https://www.arduino.cc/en/software) + ESP8266 板包
   (File → Preferences → Additional Boards Manager URLs 加
   `http://arduino.esp8266.com/stable/package_esp8266com_index.json`；
   然后 Tools → Board → Boards Manager 搜 ESP8266 安装)。
2. Tools 设置：
   - Board: **Generic ESP8266 Module**(或 ESP8266 board 相关)
   - Flash Size: **1M(64K SPIFFS)** 或按你的 ESP-01S 实际容量；`ESP-01` 多为 1M
   - Flash Mode / Upload Speed: 115200(不行再降 9600/57600)
   - Port: 选 CH340 对应的 COM 口(你的是 COM7)
3. 打开本目录 `ESP01S_AP_UDP_Bridge.ino`，按需改参数 → **Upload**。
4. 上传完成后：断开 GPIO0 接地，**给 ESP 复位**，它即开始跑固件。

> 用 esptool 手动刷也可：把 `.ino` 编译出的 `.bin` 用
> `esptool.py --port COM7 write_flash 0x0 固件.bin`(需 python)。无 python 就优先用 Arduino IDE。

---

## 二、装回 STM32(按拓展板定义，交叉收发)

| ESP-01S | STM32 | 说明 |
|---|---|---|
| RXD | **PA2**(USART2 TX) | STM32 发 → ESP 收 |
| TXD | **PA3**(USART2 RX) | ESP 发 → STM32 收 |
| VCC | +3.3V(足够电流) | 建议独立 3.3V 稳压 |
| GND | GND | 共地 |
| EN/CH_PD | +3.3V | **拉高**，不要悬空 |

STM32 端 USART2 波特率 = **115200**，与固件 `kSerialBaud` 一致(勿改一侧)。

---

## 三、安卓配对(App 用 UDP 传输)

1. 手机打开 Wi-Fi，连热点 **`BionicFish-AP`**，密码 **`12345678`**。
2. 打开仿生鱼安卓 App：
   - 传输方式选 **Wi-Fi UDP**
   - 主机(IP)填 **`192.168.4.1`**
   - 端口填 **`9000`**
3. 连接。之后：
   - 手机 → ESP(9000 UDP) → 串口 → STM32：命令帧 `CMD...`
   - STM32 → 串口 → ESP(按行打包 UDP) → 手机：`ACK`/`ERR`/`STA`
4. 首次连接后 STM32 收到一条合法 `CMD` 即建链(`link=1`)，每 200 ms 的 `STA` 帧开始回传。

> ESP 上电引导会往串口吐少量字节(74880 波特启动信息)，只影响 STM32 的会话建链
> 时序；等安卓发出第一条合法 `CMD` 后链路即正常恢复，不影响后续。

---

## 四、验证链路

- 只让 STM32 上电，手机 App 连上后应看到状态刷新(而非"未连接")。
- 手动排障：在电脑上 `nc -u 192.168.4.1 9000`(或串口/网络工具)先连热点再发包，观察是否收到 `STA`。
- 若连不上：核对 ESP 是否在 AP(`192.168.4.1` 可 ping)、波特率是否一致、`EN` 是否拉高、供电是否足。
