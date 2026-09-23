#include "http_ui.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"

/* 响应头预留区：正文直接写在 out 的这段之后，最后再把头部填回开头并前移正文。
 * 这样整个响应只需要**一块**缓冲，而且不占栈（本机 _Min_Stack_Size 只有 2 KB，
 * 早前把 1 KB 的正文放在栈上是很危险的）。 */
#define HTTP_HEADER_RESERVE 160U

/* 控制页。带 JS：按"连续"时页面每 400 ms 自动重发一次命令，
 * 以维持固件 1 秒的链路看门狗；关页面/断网后固件会在 1 秒内自动停机。
 * 全部是 Flash 常量，不占 RAM，也不需要文件系统。
 *
 * 2026-09-23 改版：
 *   - 移除旧工程的「快/慢档」单电机指令，改为 M1、M2 **各**一组
 *     启动 / 停止 / 反转 三个按键（共 6 键）。
 *   - 新增「低速挡(60%)」开关：勾选后两路都走 60% 占空比，
 *     不勾选为满速挡(95%)。
 *   - 舵机左右方向纠正：请求角语义保持不变，由固件换算层对调；
 *     行程由 ±20° 收窄为 ±15°。 */
static const char kPage[] =
    "<!doctype html><meta charset=utf-8>"
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<title>BionicFish</title><style>"
    "body{font-family:system-ui;margin:12px;background:#101418;color:#e9eff6}"
    "#s{font:13px monospace;line-height:1.5;background:#1b222b;padding:10px;"
    "border-radius:8px;white-space:pre-wrap;margin-bottom:6px}"
    ".lb{color:#8b9aab;font-size:13px;margin:10px 0 4px}"
    "button{display:block;width:100%;padding:13px 0;font-size:16px;border:0;"
    "border-radius:10px;background:#2e3945;color:#e9eff6}"
    "button.on{background:#3ecf8e;color:#05130b;font-weight:700}"
    "button.no{background:#d35454;color:#fff;font-weight:700}"
    ".r{display:flex;gap:8px}"
    ".sw{display:flex;align-items:center;gap:8px;margin:10px 0;font-size:14px;"
    "color:#e9eff6}"
    ".sw input{width:18px;height:18px}"
    "small{color:#8b9aab;display:block;margin-top:12px}</style>"
    "<h3>仿生鱼控制</h3><div id=s>连接中</div>"
    "<div class=lb>电机 1（主推进 370）</div>"
    "<div class=r><button id=A1>启动</button><button id=A0 class=no>停止</button>"
    "<button id=AR>反转</button></div>"
    "<div class=lb>电机 2（N20）</div>"
    "<div class=r><button id=B1>启动</button><button id=B0 class=no>停止</button>"
    "<button id=BR>反转</button></div>"
    "<label class=sw><input type=checkbox id=LOW>低速挡（60% 转速）</label>"
    "<div class=lb>舵机</div>"
    "<div class=r><button id=L>左舵</button><button id=C>直行</button>"
    "<button id=R>右舵</button></div>"
    "<small>两台电机各自独立控制。只要有一路在动、或舵角非 0，页面就每 0.4 秒重发一次命令来维持"
    "固件 1 秒的链路看门狗；关闭页面 / 切后台 / 断网后 1 秒内两路电机自动停止、舵机回中。</small>"
    "<script>var m1='S',m2='S',sv=0,SM=15,low=false,tm=null,bs=0,er=0,"
    "G=function(i){return document.getElementById(i)};"
    "function act(){return m1!='S'||m2!='S'||sv!=0}"
    "function paint(){G('A1').className=(m1=='F')?'on':'';G('AR').className=(m1=='R')?'on':'';"
    "G('B1').className=(m2=='F')?'on':'';G('BR').className=(m2=='R')?'on':'';"
    "G('L').className=(sv>0)?'on':'';G('C').className=(sv==0)?'on':'';"
    "G('R').className=(sv<0)?'on':'';G('LOW').checked=low}"
    "function sync(){if(act()){if(!tm)tm=setInterval(tx,400)}"
    "else if(tm){clearInterval(tm);tm=null}}"
    "function mv(v){return v=='F'?'启动':v=='R'?'反转':'停止'}"
    "function show(t){var o={};t.split('\\n').forEach(function(l){var p=l.split('=');"
    "if(p.length==2)o[p[0].trim()]=p[1].trim()});"
    "G('s').textContent='电机1: '+mv(o.m1||m1)+'    电机2: '+mv(o.m2||m2)"
    "+'\\n舵角: '+(o.servo!=null?o.servo:sv)+'    挡位: '+(low?'60%':'满速')"
    "+'\\n链路: '+(o.link=='1'?'正常':'断开')"
    "+'    故障位: '+(o.err!=null?o.err:'-')}"
    "function tx(){if(bs)return;bs=1;"
    "fetch('/cmd?move='+m1+'&m2='+m2+'&speed='+(low?60:100)+'&servo='+sv+'&t='+Date.now(),"
    "{cache:'no-store'})"
    ".then(function(r){return r.text()}).then(function(t){er=0;show(t)})"
    ".catch(function(){if(++er>3){m1='S';m2='S';sv=0;paint();sync();"
    "G('s').textContent='链路断开，已请求停止'}}).then(function(){bs=0})}"
    "G('A1').onclick=function(){m1='F';paint();tx();sync()};"
    "G('A0').onclick=function(){m1='S';paint();sync();tx()};"
    "G('AR').onclick=function(){m1='R';paint();tx();sync()};"
    "G('B1').onclick=function(){m2='F';paint();tx();sync()};"
    "G('B0').onclick=function(){m2='S';paint();sync();tx()};"
    "G('BR').onclick=function(){m2='R';paint();tx();sync()};"
    "G('LOW').onchange=function(){low=this.checked;paint();tx();sync()};"
    "G('L').onclick=function(){sv=SM;paint();tx();sync()};"
    "G('C').onclick=function(){sv=0;paint();tx();sync()};"
    "G('R').onclick=function(){sv=-SM;paint();tx();sync()};"
    "document.addEventListener('visibilitychange',function(){"
    "if(document.hidden&&act()){m1='S';m2='S';sv=0;paint();sync();tx()}});"
    "paint();tx();</script>";

/* 控制页必须**完整**装进响应缓冲：一旦放不下就会被 snprintf 悄悄截断，
 * 页面会缺掉尾部的 <script>，浏览器看着像"没反应"，极难排查。
 * 用编译期断言把这个失败变成构建错误。 */
typedef char kPageMustFit[((sizeof(kPage) + 1U) <
                           (HTTP_RESPONSE_BYTES - HTTP_HEADER_RESERVE)) ? 1 : -1];

static const char kHttpOkHtml[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Cache-Control: no-store\r\n"
    "Connection: keep-alive\r\n"
    "Content-Length: ";

static const char kHttpOkText[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/plain; charset=utf-8\r\n"
    "Cache-Control: no-store\r\n"
    "Connection: keep-alive\r\n"
    "Content-Length: ";

static const char kHttpNoContent[] =
    "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\nConnection: keep-alive\r\n\r\n";

static const char kHttpBadRequest[] =
    "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain; charset=utf-8\r\n"
    "Content-Length: 12\r\nConnection: keep-alive\r\n\r\nbad request\n";

static const char kHttpNotFound[] =
    "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain; charset=utf-8\r\n"
    "Content-Length: 10\r\nConnection: keep-alive\r\n\r\nnot found\n";

/* 与 BfAttitude 的 ddeg 一致：一位小数，带符号。 */
static void AppendDdeg(char *out, size_t capacity, int16_t ddeg)
{
    long value = (long)ddeg;
    unsigned long magnitude = (value < 0L) ? (unsigned long)(-value) : (unsigned long)value;
    (void)snprintf(out, capacity, "%s%lu.%01lu", (value < 0L) ? "-" : "",
                   magnitude / 10UL, magnitude % 10UL);
}

/* 把正文直接写在 out 的头区预留之后；Emit 再把头部填回开头并前移正文。 */
static char *BodyAt(char *out) { return out + HTTP_HEADER_RESERVE; }

static size_t Emit(char *out, size_t capacity, const char *header)
{
    const char *body = BodyAt(out);
    size_t header_length = strlen(header);
    size_t body_length = strlen(body);
    char length_text[16];
    int written = snprintf(length_text, sizeof(length_text), "%lu\r\n\r\n",
                           (unsigned long)body_length);
    size_t length_length;
    size_t total;

    if (written <= 0) {
        return 0U;
    }
    length_length = (size_t)written;
    /* 头部必须装得进预留区，否则前移会覆盖尚未拷贝的正文。 */
    if ((header_length + length_length) > HTTP_HEADER_RESERVE) {
        return 0U;
    }
    total = header_length + length_length + body_length;
    if (total >= capacity) {
        return 0U;
    }
    memmove(out + header_length + length_length, body, body_length + 1U);
    memcpy(out, header, header_length);
    memcpy(out + header_length, length_text, length_length);
    out[total] = '\0';
    return total;
}

void HttpRequest_Reset(HttpRequest *request)
{
    if (request == 0) {
        return;
    }
    memset(request, 0, sizeof(*request));
}

bool HttpRequest_Feed(HttpRequest *request, uint8_t byte)
{
    if ((request == 0) || request->done || request->overflow) {
        return false;
    }
    if (!request->line_done) {
        if (byte == '\n') {
            request->request[request->length] = '\0';
            request->line_done = true;
            /* 请求行刚结束 ⇒ 下一行可能直接就是空行（无头部的请求） */
            request->header_line_empty = true;
            return false;
        }
        if (byte == '\r') {
            return false;
        }
        if ((request->length + 1U) >= sizeof(request->request)) {
            request->overflow = true;
            request->request[request->length] = '\0';
            return true;
        }
        request->request[request->length] = (char)byte;
        request->length++;
        return false;
    }
    /* 头区：只关心什么时候出现空行，头部内容一概忽略。 */
    if (byte == '\r') {
        request->pending_cr = true;
        return false;
    }
    if (byte == '\n') {
        if (request->pending_cr && request->header_line_empty) {
            request->done = true;
            return true;
        }
        request->header_line_empty = request->pending_cr;
        request->pending_cr = false;
        return false;
    }
    request->pending_cr = false;
    request->header_line_empty = false;
    return false;
}

/* 取 "GET /cmd?move=F HTTP/1.1" 里的 target，**含查询串**（到空格为止）。
 * 不能在 '?' 处截断：端点匹配和参数解析都依赖查询串。 */
static const char *RequestTarget(const char *line, size_t *length)
{
    const char *p = line;
    while ((*p != '\0') && (*p != ' ')) {
        p++; /* 跳过方法 */
    }
    while (*p == ' ') {
        p++;
    }
    *length = 0U;
    while ((p[*length] != '\0') && (p[*length] != ' ')) {
        (*length)++;
    }
    return p;
}

/* 在查询串里找 "key=" 并解析其后的十进制整数（允许负号）。 */
static bool QueryInt(const char *line, const char *key, int *value)
{
    const char *p = strstr(line, key);
    bool negative = false;
    long result = 0L;
    int digits = 0;

    if (p == 0) {
        return false;
    }
    p += strlen(key);
    if (*p == '-') {
        negative = true;
        p++;
    }
    while ((*p >= '0') && (*p <= '9')) {
        result = (result * 10L) + (long)(*p - '0');
        if (result > 100000L) {
            return false;
        }
        digits++;
        p++;
    }
    if (digits == 0) {
        return false;
    }
    *value = (int)(negative ? -result : result);
    return true;
}

static int ClampServo(int value)
{
    if (value < CFG_SERVO_SAFE_MIN_DEG) {
        return CFG_SERVO_SAFE_MIN_DEG;
    }
    if (value > CFG_SERVO_SAFE_MAX_DEG) {
        return CFG_SERVO_SAFE_MAX_DEG;
    }
    return value;
}

/* 动作端点回带一行结果状态：浏览器/脚本点完立刻能确认是否生效，
 * 不必再等下一次轮询（固件 1 秒失联保护会把舵机回中，晚了就看不到）。 */
/* BfMove -> 单字母；非法值一律按停止上报。 */
static char MoveCode(BfMove move)
{
    switch (move) {
    case BF_MOVE_FORWARD:
        return 'F';
    case BF_MOVE_REVERSE:
        return 'R';
    case BF_MOVE_STOP:
    default:
        return 'S';
    }
}

static void AppendResult(char *out, size_t capacity, const BfSystemSnapshot *snapshot,
                         const char *prefix, const char *value)
{
    (void)snprintf(out, capacity,
                   "%s%s\nstep=%u\nm1=%c\nm2=%c\nservo=%d\nlink=%u\nerr=%lu\n",
                   prefix, value,
                   snapshot->step_running ? 1U : 0U,
                   MoveCode(snapshot->motor_a), MoveCode(snapshot->motor_b),
                   (int)snapshot->servo_deg,
                   snapshot->command_link_alive ? 1U : 0U,
                   (unsigned long)snapshot->active_faults);
}

static void AppendStatusText(char *out, size_t capacity, const BfSystemSnapshot *snapshot,
                             const char *sta_ip, const char *ap_ip)
{
    char roll[12];
    char pitch[12];
    char yaw[12];

    if (snapshot->attitude.valid) {
        AppendDdeg(roll, sizeof(roll), snapshot->attitude.roll_ddeg);
        AppendDdeg(pitch, sizeof(pitch), snapshot->attitude.pitch_ddeg);
        AppendDdeg(yaw, sizeof(yaw), snapshot->attitude.yaw_ddeg);
    } else {
        (void)strcpy(roll, "NA");
        (void)strcpy(pitch, "NA");
        (void)strcpy(yaw, "NA");
    }
    (void)snprintf(out, capacity,
                   "link=%u\nstep=%u\nm1=%c\nm2=%c\nservo=%d\nroll=%s\npitch=%s\nyaw=%s\nerr=%lu\n"
                   "ap=%s\nsta=%s\ncmd=%lu\n",
                   snapshot->command_link_alive ? 1U : 0U,
                   snapshot->step_running ? 1U : 0U,
                   MoveCode(snapshot->motor_a), MoveCode(snapshot->motor_b),
                   (int)snapshot->servo_deg, roll, pitch, yaw,
                   (unsigned long)snapshot->active_faults, ap_ip, sta_ip,
                   (unsigned long)snapshot->last_sequence);
}

size_t HttpUi_BuildResponse(HttpRequest *request, const BfSystemSnapshot *snapshot,
                            const char *sta_ip, const char *ap_ip,
                            char *out, size_t capacity, HttpAction *action)
{
    char query[HTTP_REQUEST_BYTES];
    size_t target_length = 0U;
    const char *target;
    char *body;
    size_t body_capacity;
    int value = 0;

    if ((request == 0) || (snapshot == 0) || (out == 0) || (action == 0)) {
        return 0U;
    }
    if (capacity <= (HTTP_HEADER_RESERVE + 8U)) {
        return 0U;
    }
    body = BodyAt(out);
    body_capacity = capacity - HTTP_HEADER_RESERVE;
    *action = HTTP_ACTION_NONE;
    request->servo_deg = 0;
    request->step_speed = 0U;
    request->m2_present = false;
    request->m2_dir = 0;

    if (request->overflow) {
        memcpy(out, kHttpBadRequest, sizeof(kHttpBadRequest) - 1U);
        return sizeof(kHttpBadRequest) - 1U;
    }

    target = RequestTarget(request->request, &target_length);
    if (target_length >= sizeof(query)) {
        target_length = sizeof(query) - 1U;
    }
    memcpy(query, target, target_length);
    query[target_length] = '\0';

    /* 控制页：纯静态，状态由页面自己轮询 /cmd 拿。 */
    if ((target_length == 1U) && (query[0] == '/')) {
        (void)snprintf(body, body_capacity, "%s", kPage);
        return Emit(out, capacity, kHttpOkHtml);
    }

    if (strncmp(query, "/api/status", 11U) == 0) {
        AppendStatusText(body, body_capacity, snapshot, sta_ip, ap_ip);
        return Emit(out, capacity, kHttpOkText);
    }

    if ((strncmp(query, "/favicon.ico", 12U) == 0) ||
        (strncmp(query, "/robots.txt", 11U) == 0)) {
        memcpy(out, kHttpNoContent, sizeof(kHttpNoContent) - 1U);
        return sizeof(kHttpNoContent) - 1U;
    }

    if (strncmp(query, "/cmd", 4U) == 0) {
        /* move= 必填（F/S/R），servo= 选填：两者可以合成一条命令，
         * 这样"前进同时左舵"只需要一个请求。 */
        const char *mv = strstr(query, "move=");
        const char *p;
        char letter[2];
        if ((mv == 0) || (mv[5] == '\0')) {
            memcpy(out, kHttpBadRequest, sizeof(kHttpBadRequest) - 1U);
            return sizeof(kHttpBadRequest) - 1U;
        }
        switch (mv[5]) {
        case 'F': case 'f': *action = HTTP_ACTION_FORWARD; break;
        case 'S': case 's': *action = HTTP_ACTION_STOP; break;
        case 'R': case 'r': *action = HTTP_ACTION_REVERSE; break;
        default:
            memcpy(out, kHttpBadRequest, sizeof(kHttpBadRequest) - 1U);
            return sizeof(kHttpBadRequest) - 1U;
        }
        /* m2= 选填，是 M2 的方向。缺省（老页面/老脚本）等同停止，
         * 与 ASCII 协议里 m2 字段的缺省语义保持一致。 */
        p = strstr(query, "m2=");
        if ((p != 0) && (p[3] != '\0')) {
            switch (p[3]) {
            case 'F': case 'f': request->m2_present = true; request->m2_dir = 1; break;
            case 'S': case 's': request->m2_present = true; request->m2_dir = 0; break;
            case 'R': case 'r': request->m2_present = true; request->m2_dir = -1; break;
            default:
                memcpy(out, kHttpBadRequest, sizeof(kHttpBadRequest) - 1U);
                return sizeof(kHttpBadRequest) - 1U;
            }
        }
        if (QueryInt(query, "servo=", &value)) {
            request->servo_deg = ClampServo(value);
        }
        /* speed= 兼容字段：只认 60/100，已不改变占空比。 */
        if (QueryInt(query, "speed=", &value) && ((value == 60) || (value == 100))) {
            request->step_speed = (uint16_t)value;
        }
        letter[0] = mv[5];
        letter[1] = '\0';
        AppendResult(body, body_capacity, snapshot, "move=", letter);
        return Emit(out, capacity, kHttpOkText);
    }

    if (strncmp(query, "/servo", 6U) == 0) {
        if (!QueryInt(query, "deg=", &value)) {
            memcpy(out, kHttpBadRequest, sizeof(kHttpBadRequest) - 1U);
            return sizeof(kHttpBadRequest) - 1U;
        }
        value = ClampServo(value);
        request->servo_deg = value;
        *action = HTTP_ACTION_SERVO;
        {
            char number[8];
            (void)snprintf(number, sizeof(number), "%d", value);
            AppendResult(body, body_capacity, snapshot, "servo=", number);
        }
        return Emit(out, capacity, kHttpOkText);
    }

    memcpy(out, kHttpNotFound, sizeof(kHttpNotFound) - 1U);
    return sizeof(kHttpNotFound) - 1U;
}
