#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ascii_protocol.h"
#include "control_arbiter.h"
#include "ring_buffer.h"

static ProtocolEvent FeedText(ProtocolParser *parser, const char *text)
{
    ProtocolEvent event = {0};
    while (*text != '\0') {
        ProtocolEvent candidate = Protocol_Feed(parser, (uint8_t)*text++);
        if (candidate.kind != PROTO_EVENT_NONE) {
            event = candidate;
        }
    }
    return event;
}

static BfRemoteCommand StopCommand(uint16_t sequence)
{
    BfRemoteCommand command;
    memset(&command, 0, sizeof(command));
    command.sequence = sequence;
    command.move = BF_MOVE_STOP;
    command.turn = BF_TURN_CENTER;
    command.step_rpm = 60U;
    command.servo_deg = 0;
    return command;
}

static void CheckPayloadBoundary(size_t payload_length, bool valid)
{
    ProtocolParser parser;
    ProtocolEvent event;
    char frame[CFG_PROTOCOL_PAYLOAD_MAX + 5U];
    const char *prefix = "<CMD,seq=12,move=S,turn=C,step_speed=60,servo=0,extra=";
    size_t offset = strlen(prefix);
    memcpy(frame, prefix, offset);
    while (offset < payload_length + 1U) {
        frame[offset++] = 'x';
    }
    frame[offset++] = '>';
    frame[offset++] = '\n';
    frame[offset] = '\0';
    Protocol_Init(&parser);
    event = FeedText(&parser, frame);
    if (valid) {
        assert(event.kind == PROTO_EVENT_COMMAND && event.command.sequence == 12U);
    } else {
        assert(event.kind == PROTO_EVENT_ERROR && event.error == PROTO_ERR_TOO_LONG);
        event = FeedText(&parser, "<CMD,seq=13,move=S,turn=C,step_speed=60,servo=0>\n");
        assert(event.kind == PROTO_EVENT_COMMAND && event.command.sequence == 13U);
    }
}

int main(void)
{
    ProtocolParser parser;
    ProtocolEvent event;
    ControlSession session;
    BfRemoteCommand command;
    BfSystemSnapshot snapshot;
    uint8_t storage[8];
    RingBuffer ring;
    uint8_t byte;
    ProtocolError error;
    char status_frame[192];

    assert(RingBuffer_Init(&ring, storage, sizeof(storage)));
    assert(RingBuffer_PushFromIsr(&ring, 0x11U));
    assert(RingBuffer_Pop(&ring, &byte) && (byte == 0x11U));
    assert(strcmp(Protocol_ErrorName(PROTO_ERR_FRAME_TIMEOUT), "E_FRAME_TIMEOUT") == 0);
    assert(strcmp(Protocol_ErrorName(PROTO_ERR_CONFIGURATION), "E_CONFIGURATION") == 0);
    memset(&snapshot, 0, sizeof(snapshot));
    assert(Protocol_EncodeStatus(status_frame, sizeof(status_frame), &snapshot) != 0U);
    assert(strstr(status_frame, "step_rpm=NA,step_est=NA,step_actual=NA,step_on=0,") != 0);
    assert(strstr(status_frame, "roll=NA,pitch=NA,yaw=NA") != 0);
    assert(strstr(status_frame, "n20_") == 0);
    snapshot.step_running = true;
    snapshot.command_link_alive = true;
    snapshot.last_sequence = UINT16_MAX;
    snapshot.servo_deg = -20;
    snapshot.motor_a = BF_MOVE_FORWARD;
    snapshot.motor_b = BF_MOVE_REVERSE;
    snapshot.attitude.valid = true;
    snapshot.attitude.roll_ddeg = snapshot.attitude.pitch_ddeg = snapshot.attitude.yaw_ddeg = INT16_MIN;
    snapshot.active_faults = UINT32_MAX;
    /* Match the application's actual status buffer, including the new step_on field. */
    assert(Protocol_EncodeStatus(status_frame, 160U, &snapshot) != 0U);
    assert(strstr(status_frame, "step_rpm=NA,step_est=NA,step_actual=NA,step_on=1,") != 0);
    /* 2026-09-12 新增：两路电机方向必须如实出现在帧尾（err 之前）。 */
    assert(strstr(status_frame, "m1=F,m2=R,err=4294967295>") != 0);

    Protocol_Init(&parser);
    event = FeedText(&parser,
        "<CMD,seq=65535,move=S,turn=C,step_speed=60,servo=0>\r\n");
    assert(event.kind == PROTO_EVENT_COMMAND);
    assert(event.command.sequence == 65535U);
    assert(event.command.move == BF_MOVE_STOP);

    /* 旧版上位机多发的已移除字段按未知字段规则忽略，不再控制任何硬件。
     * 2026-09-23：舵角安全行程收窄到 ±15°，此处改用 -15° 才落在合法范围。 */
    Protocol_Init(&parser);
    event = FeedText(&parser,
        "<CMD,seq=1,move=S,turn=L,step_speed=100,servo=-15,n20_dir=FWD,n20_pwm=100,foo=x>\n");
    assert(event.kind == PROTO_EVENT_COMMAND);
    assert(event.command.step_rpm == 100U);
    assert(event.command.servo_deg == -15);
    assert(Control_ValidateCommand(&event.command) == PROTO_ERR_NONE);

    Protocol_Init(&parser);
    event = FeedText(&parser,
        "<CMD,seq=1,move=S,turn=C,step_speed=60,servo=0,foo=x=y>\n");
    assert(event.kind == PROTO_EVENT_ERROR && event.error == PROTO_ERR_FIELD);
    CheckPayloadBoundary(CFG_PROTOCOL_PAYLOAD_MAX - 1U, true);
    CheckPayloadBoundary(CFG_PROTOCOL_PAYLOAD_MAX, false);

    Protocol_Init(&parser);
    event = FeedText(&parser,
        "<CMD,seq=1,seq=2,move=S,turn=C,step_speed=60,servo=0>\n");
    assert((event.kind == PROTO_EVENT_ERROR) && (event.error == PROTO_ERR_DUP_FIELD));
    assert(event.sequence_present && (event.sequence == 1U));

    Protocol_Init(&parser);
    event = FeedText(&parser,
        "<CMD,seq=1,move=S,turn=C,step_speed=60>\n");
    assert((event.kind == PROTO_EVENT_ERROR) && (event.error == PROTO_ERR_MISSING_FIELD));

    Protocol_Init(&parser);
    event = FeedText(&parser,
        "<CMD,seq=2,move=S,turn=R,step_speed=60,servo=-10>\n");
    assert(event.kind == PROTO_EVENT_COMMAND);
    assert(Control_ValidateCommand(&event.command) == PROTO_ERR_SEMANTIC);

    Protocol_Init(&parser);
    event = FeedText(&parser,
        "<CMD,seq=3,move=F,turn=C,step_speed=60,servo=0>\n");
    assert(event.kind == PROTO_EVENT_COMMAND);
    assert(Control_ValidateCommand(&event.command) == PROTO_ERR_NONE);

    Protocol_Init(&parser);
    event = FeedText(&parser,
        "<CMD,seq=3,move=F,turn=C,step_speed=100,servo=0>\n");
    assert(event.kind == PROTO_EVENT_COMMAND);
    assert(Control_ValidateCommand(&event.command) == PROTO_ERR_NONE);

    Protocol_Init(&parser);
    event = FeedText(&parser,
        "<CMD,seq=3,move=F,turn=C,step_speed=10,servo=0>\n");
    assert(event.kind == PROTO_EVENT_ERROR && event.error == PROTO_ERR_RANGE);

    Protocol_Init(&parser);
    event = FeedText(&parser,
        "<CMD,seq=4,move=R,turn=C,step_speed=100,servo=0>\n");
    assert(event.kind == PROTO_EVENT_COMMAND);
    /* 2026-09-12：直流电机可换向，move=R 不再被单向策略拒绝。 */
    assert(Control_ValidateCommand(&event.command) == PROTO_ERR_NONE);

    /* 舵角行程已收窄到 +/-15：16 度必须被拒。
     * （用 -16 而不是 -21，是为了紧贴新边界，防止边界漂移悄悄放宽。） */
    Protocol_Init(&parser);
    event = FeedText(&parser,
        "<CMD,seq=5,move=S,turn=L,step_speed=60,servo=-16>\n");
    assert(event.kind == PROTO_EVENT_ERROR && event.error == PROTO_ERR_RANGE);

    /* 边界内侧 -15 必须通过。 */
    Protocol_Init(&parser);
    event = FeedText(&parser,
        "<CMD,seq=5,move=S,turn=L,step_speed=60,servo=-15>\n");
    assert(event.kind == PROTO_EVENT_COMMAND);
    assert(event.command.servo_deg == -15);

    /* m2 是可选字段：缺省必须落到停止，不能靠 memset 变成"前进"。 */
    Protocol_Init(&parser);
    event = FeedText(&parser, "<CMD,seq=6,move=F,turn=C,step_speed=60,servo=0>\n");
    assert(event.kind == PROTO_EVENT_COMMAND && event.command.m2 == BF_MOVE_STOP);

    Protocol_Init(&parser);
    event = FeedText(&parser, "<CMD,seq=7,move=F,turn=C,step_speed=60,servo=0,m2=R>\n");
    assert(event.kind == PROTO_EVENT_COMMAND && event.command.m2 == BF_MOVE_REVERSE);

    Protocol_Init(&parser);
    event = FeedText(&parser, "<CMD,seq=8,move=F,turn=C,step_speed=60,servo=0,m2=X>\n");
    assert(event.kind == PROTO_EVENT_ERROR && event.error == PROTO_ERR_RANGE);

    Protocol_Init(&parser);
    event = FeedText(&parser, "<CMD,seq=9,move=F,turn=C,step_speed=60,servo=0,m2=F,m2=R>\n");
    assert(event.kind == PROTO_EVENT_ERROR && event.error == PROTO_ERR_DUP_FIELD);

    ControlSession_Init(&session);
    command = StopCommand(65535U);
    assert(Control_ValidateCommand(&command) == PROTO_ERR_NONE);
    assert(Control_ClassifyCommand(&session, &command, &error) == CONTROL_NEW_COMMAND);
    Control_AcceptNew(&session, &command, 100U);
    command.sequence = 0U;
    assert(Control_ClassifyCommand(&session, &command, &error) == CONTROL_NEW_COMMAND);
    Control_AcceptNew(&session, &command, 101U);
    assert(Control_ClassifyCommand(&session, &command, &error) == CONTROL_DUPLICATE_COMMAND);
    command.servo_deg = 1;
    assert(Control_ClassifyCommand(&session, &command, &error) == CONTROL_REJECTED);
    assert(error == PROTO_ERR_SEQ_CONFLICT);
    command = StopCommand(65535U);
    assert(Control_ClassifyCommand(&session, &command, &error) == CONTROL_REJECTED);
    assert(error == PROTO_ERR_SEQ_OLD);
    command = StopCommand(0x8000U);
    assert(Control_ClassifyCommand(&session, &command, &error) == CONTROL_REJECTED);
    assert(error == PROTO_ERR_SEQ_AMBIGUOUS);

    command = StopCommand(4U);
    Control_AcceptNew(&session, &command, 0xFFFFFFF0UL);
    Control_ForceDisconnect(&session);
    assert(!session.link_alive && !session.sequence_valid);
    Control_AcceptNew(&session, &command, 0xFFFFFFF0UL);
    assert(!Control_CheckTimeout(&session, 0x00000010UL));
    assert(Control_CheckTimeout(&session, 0x00000400UL));

    puts("protocol_host_test: PASS");
    return 0;
}
