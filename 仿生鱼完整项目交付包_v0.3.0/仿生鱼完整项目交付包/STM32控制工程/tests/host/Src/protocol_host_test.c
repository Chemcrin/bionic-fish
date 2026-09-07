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
    assert(strstr(status_frame, "roll=NA,pitch=NA,yaw=NA") != 0);
    assert(strstr(status_frame, "n20_") == 0);

    Protocol_Init(&parser);
    event = FeedText(&parser,
        "<CMD,seq=65535,move=S,turn=C,step_speed=60,servo=0>\r\n");
    assert(event.kind == PROTO_EVENT_COMMAND);
    assert(event.command.sequence == 65535U);
    assert(event.command.move == BF_MOVE_STOP);

    /* 旧版上位机多发的已移除字段按未知字段规则忽略，不再控制任何硬件。 */
    Protocol_Init(&parser);
    event = FeedText(&parser,
        "<CMD,seq=1,move=S,turn=L,step_speed=100,servo=-30,n20_dir=FWD,n20_pwm=100,foo=x>\n");
    assert(event.kind == PROTO_EVENT_COMMAND);
    assert(event.command.step_rpm == 100U);
    assert(event.command.servo_deg == -30);
    assert(Control_ValidateCommand(&event.command) == PROTO_ERR_NONE);

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
    assert(Control_ValidateCommand(&event.command) == PROTO_ERR_STEPPER_HW_UNCONFIRMED);

    Protocol_Init(&parser);
    event = FeedText(&parser,
        "<CMD,seq=4,move=R,turn=C,step_speed=100,servo=0>\n");
    assert(event.kind == PROTO_EVENT_COMMAND);
    assert(Control_ValidateCommand(&event.command) == PROTO_ERR_STEP_REVERSE_UNSUPPORTED);

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
