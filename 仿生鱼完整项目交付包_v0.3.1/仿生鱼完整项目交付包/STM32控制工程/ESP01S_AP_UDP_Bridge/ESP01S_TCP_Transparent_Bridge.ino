/*
 * ESP-01S 透明 TCP 服务器 桥（"简单版"）
 * -------------------------------------------------------------
 * 上电即：开热点 BionicFish-AP + 在端口 9000 开 TCP 服务器。
 * 手机连热点、用 App 以 Wi-Fi TCP 连 192.168.4.1:9000 后，
 * ESP 把 该 TCP 连接 <-> UART0(接 STM32 USART2) 双向透明桥接。
 * ESP 串口是干净的字节管道 —— STM32 固件不用改，裸发 ASCII 即可。
 * -------------------------------------------------------------
 * 用途：让"插上 ESP → 手机连 AP → 上位机控制"立刻能测。
 * 参数改文件顶部常量即可。
 */
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <WiFiServer.h>

static const char *kApSsid     = "BionicFish-AP";   // 热点名
static const char *kApPassword = "12345678";        // 热点密码
static const uint16_t kTcpPort = 9000;              // TCP 端口
static const unsigned long kSerialBaud = 115200UL;  // 与 STM32 USART2 一致

WiFiServer s_server(kTcpPort);
WiFiClient s_client;   // 当前接入的手机 TCP 客户端

void setup()
{
  Serial.begin(kSerialBaud);
  Serial.setRxBufferSize(512);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(kApSsid, kApPassword);
  delay(200);
  s_server.begin();
}

void loop()
{
  // 若手机断开，丢弃并准备接收下一个连接
  if (s_client && !s_client.connected()) {
    s_client.stop();
  }

  // 无客户端时，等待新连接
  if (!s_client) {
    s_client = s_server.available();
  }

  if (s_client && s_client.connected()) {
    // 手机(TCP) -> STM32(串口)
    while (s_client.available() > 0) {
      int c = s_client.read();
      if (c >= 0) {
        Serial.write((uint8_t)c);
      }
    }
    // STM32(串口) -> 手机(TCP)
    while (Serial.available() > 0) {
      int c = Serial.read();
      if (c >= 0) {
        s_client.write((uint8_t)c);
      }
    }
  }
}
