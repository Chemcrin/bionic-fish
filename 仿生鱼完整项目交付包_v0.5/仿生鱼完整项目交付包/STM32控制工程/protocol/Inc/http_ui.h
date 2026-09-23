/**
 * @file http_ui.h
 * @brief 极简 HTTP 控制页：与 App 的 ASCII 协议**共用同一个 TCP 端口**。
 *
 * 分流规则：一个客户端连上来后，**只看它的第一个载荷字节**——
 *   'G'（GET）→ 本模块接管，按 HTTP 处理；
 *   '<'        → 原 ASCII 协议路径，一行代码都不改。
 * 因此 ESP-01S 只暴露一个 CIPSERVER 端口也能同时服务手机 App 和浏览器。
 *
 * 本模块是**纯逻辑**：不碰 HAL、不碰 esp_link、不做任何 I/O，便于宿主机测试。
 * 端点解析出的动作不直接驱动执行器，而是交给 app.c 复用与远端 CMD **完全相同**的
 * 校验/序号/失效保护路径（见 app.c 的 RouteCommand）。
 */
#ifndef HTTP_UI_H
#define HTTP_UI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_types.h"

/* 请求行缓冲：只需装下 "GET /cmd?move=F&x=1 HTTP/1.1"，192 字节足够。 */
#define HTTP_REQUEST_BYTES 192U
/* 响应缓冲：**整包**（响应头 + 正文）都写在调用方这一块缓冲里，模块内部只用这块，
 * 既不占栈也不需要第二份正文副本。控制页带 JS：2026-09-23 改版后（双电机各三键 +
 * 低速挡开关 + ±15° 舵机）页面涨到约 3.3 KB，故由 3584 抬到 4096 留足余量。
 * 若把页面继续改大，下面的 kPageMustFit 编译期断言会直接报错，不会静默截断。 */
#define HTTP_RESPONSE_BYTES 4096U

/* 网页端点解析出的意图。 */
typedef enum {
    HTTP_ACTION_NONE = 0,   /* 纯展示：/ 或 /api/status 或 /favicon.ico */
    HTTP_ACTION_STOP,
    HTTP_ACTION_FORWARD,
    HTTP_ACTION_REVERSE,    /* M1 后退（2026-09-12 起已支持，不再被单向策略拒绝） */
    HTTP_ACTION_SERVO        /* 见 HttpRequest.servo_deg */
} HttpAction;

typedef struct {
    char request[HTTP_REQUEST_BYTES]; /* 请求行（不含 CRLF），已 NUL 结尾 */
    size_t length;
    bool line_done;      /* 请求行已收齐 */
    bool done;           /* 头区结束（空行）——可以处理了 */
    bool overflow;       /* 请求行超长 */
    bool pending_cr;     /* 上一个字节是 CR */
    bool header_line_empty; /* 当前行开头就遇到 CRLF ⇒ 空行 */
    int servo_deg;       /* HTTP_ACTION_SERVO 时的目标角度；/cmd 的 servo= 也会填这里 */
    uint16_t step_speed; /* /cmd 的 speed=；兼容字段，已不改变占空比 */
    /* /cmd 的 m2=（M2 方向）：+1 前进 / 0 停止 / -1 后退；m2_present=false 等同停止。 */
    bool m2_present;
    int m2_dir;
} HttpRequest;

/** 每个新连接开始时调用一次。 */
void HttpRequest_Reset(HttpRequest *request);

/**
 * 逐字节喂入。返回 true 表示请求已可处理（头区结束）或已判定超长。
 * 之后必须调用 HttpRequest_Reset 才能复用。
 */
bool HttpRequest_Feed(HttpRequest *request, uint8_t byte);

/**
 * 解析请求行并生成完整 HTTP 响应（含响应头，`Connection: close`）。
 * @param request   已收齐的请求
 * @param snapshot  当前系统快照，用于在页面上显示状态
 * @param sta_ip    STA 侧地址文本（可为 "0.0.0.0"）
 * @param ap_ip     AP 侧地址文本
 * @param out       输出缓冲，至少 HTTP_RESPONSE_BYTES
 * @param capacity  out 的容量
 * @param action    输出：解析出的意图（必须非空）
 * @return 写入 out 的字节数；0 表示缓冲不足（调用方应回 500 或直接关闭）
 */
size_t HttpUi_BuildResponse(HttpRequest *request, const BfSystemSnapshot *snapshot,
                            const char *sta_ip, const char *ap_ip,
                            char *out, size_t capacity, HttpAction *action);

#endif /* HTTP_UI_H */
