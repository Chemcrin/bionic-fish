/*
 * ESP-01S AP/UDP <-> UART 透传桥
 * -------------------------------------------------------------
 * 目标：让 ESP-01S 上电即"自建连接"：开一个 Wi-Fi 热点(SoftAP)，并可选连家里
 *       WiFi(AP_STA)。随后在固定端口上监听 UDP，把 UDP 数据原样写到 UART0
 *       (接 STM32 USART2)，同时把 UART0 收到的数据按行打包成 UDP 发回给
 *       最后向你发包的安卓手机。
 *
 * 用途：仿生鱼项目。STM32 通过 USART2 与 ESP-01S 用 V1 ASCII 协议交换，STM32
 *       固件不含任何 AT 指令，需要的就是一条干净的字节管(transparent bridge)。
 *
 * 注意：出厂 AT 固件无法对 UDP 做透传(会插入 +IPD/CIPSEND)，所以这里用自定义
 *       固件实现 UDP 的透明串口桥。
 *
 * 硬件：ESP-01S 的 UART0 = Serial，波特率必须与 STM32 USART2 一致(115200)。
 * -------------------------------------------------------------
 */
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

// ---------------- 可编辑参数(默认值) ----------------
static const char *kApSsid     = "BionicFish-AP";   // 热点名(安卓连这个)
static const char *kApPassword = "12345678";        // 热点密码(至少8位)
static const uint16_t kUdpPort = 9000;              // UDP 端口(安卓填 192.168.4.1:9000)

// 可选：如果填了 STA_SSID/密码，ESP 会再尝试连你家里 WiFi(AP_STA 共存)。
// 留空则只开热点(纯 AP)。
static const char *kStaSsid     = "";               // 例如 "MyHomeWiFi"
static const char *kStaPassword = "";               // 例如 "homepass"
// -------------------------------------------------------------

static const unsigned long kSerialBaud = 115200UL;  // 与 STM32 USART2 一致

static WiFiUDP s_udp;

// UDP 对端(安卓)。从收到的第一个包记录来源，之后串口行都发回给它。
static IPAddress s_peerIp;
static uint16_t  s_peerPort = 0;

// 串口(STM32 -> 本机)按行缓存，遇到 '\n' 打包成单个 UDP 数据报发出。
static const size_t kLineCap = 512;
static char s_line[kLineCap];
static size_t s_lineLen = 0;

static void SendLineAsDatagram(void)
{
  if (s_peerPort == 0 || s_lineLen == 0) {
    // 还没有安卓先发过包，或这行是空；丢弃或等待对端。
    s_lineLen = 0;
    return;
  }
  s_udp.beginPacket(s_peerIp, s_peerPort);
  s_udp.write((const uint8_t *)s_line, s_lineLen);
  s_udp.endPacket();
  s_lineLen = 0;
}

void setup()
{
  // UART0：既作桥，又由 Arduino 引导加载器占用；运行时仅做透传。
  Serial.begin(kSerialBaud);
  Serial.setRxBufferSize(512);

  // AP 一定开；STA 可选。
  if (strlen(kStaSsid) > 0) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(kApSsid, kApPassword);
    WiFi.begin(kStaSsid, kStaPassword);
  } else {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(kApSsid, kApPassword);
  }
  delay(100);
  // 热点就绪后，本机 AP 地址固定为 192.168.4.1。
  s_udp.begin(kUdpPort);
}

void loop()
{
  // ---- 1) 安卓(UDP) -> STM32：读到数据就整包写到串口 ----
  int pktSize = s_udp.parsePacket();
  if (pktSize > 0) {
    s_peerIp = s_udp.remoteIP();
    s_peerPort = s_udp.remotePort();

    // 一次读完整包(数据报最大 ~1472 字节，足够放控制帧)。
    uint8_t buf[256];
    int got = 0;
    while (s_udp.available() && got < (int)sizeof(buf)) {
      int r = s_udp.read();
      if (r < 0) break;
      buf[got++] = (uint8_t)r;
    }
    if (got > 0) {
      Serial.write(buf, (size_t)got);
      Serial.flush(); // 等 STM32 收完，避免并发覆盖
    }
  }

  // ---- 2) STM32(串口) -> 安卓：按行缓存后打成 UDP 数据报 ----
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n') {
      // 一行结束。若 STM32 用 '\r\n'，'\r' 已包含在缓存中也可原样发送。
      SendLineAsDatagram();
    } else if (s_lineLen < kLineCap - 1) {
      s_line[s_lineLen++] = c;
    } else {
      // 单行过长：丢半行，避免缓冲溢出。
      SendLineAsDatagram(); // 先发掉已缓冲的部分
      s_line[s_lineLen++] = c;
      if (s_lineLen >= kLineCap - 1) s_lineLen = kLineCap - 1;
    }
  }

  // STA 重连(如果配置了家里 WiFi 且掉线)。
  if (strlen(kStaSsid) > 0 && WiFi.status() != WL_CONNECTED) {
    static unsigned long s_lastReconnect = 0;
    unsigned long now = millis();
    if (now - s_lastReconnect > 10000UL) {
      s_lastReconnect = now;
      WiFi.begin(kStaSsid, kStaPassword);
    }
  }
}
