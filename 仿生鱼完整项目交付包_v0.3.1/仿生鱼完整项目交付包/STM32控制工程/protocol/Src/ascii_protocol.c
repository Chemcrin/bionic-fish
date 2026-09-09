#include "ascii_protocol.h"

#include <stdio.h>
#include <string.h>

enum {
    FIELD_SEQ       = (1U << 0),
    FIELD_MOVE      = (1U << 1),
    FIELD_TURN      = (1U << 2),
    FIELD_STEP      = (1U << 3),
    FIELD_SERVO     = (1U << 4),
    FIELD_REQUIRED  = FIELD_SEQ | FIELD_MOVE | FIELD_TURN | FIELD_STEP | FIELD_SERVO
};

static ProtocolEvent EventError(ProtocolError error)
{
    ProtocolEvent event;
    memset(&event, 0, sizeof(event));
    event.kind = PROTO_EVENT_ERROR;
    event.error = error;
    return event;
}

static bool IsPrintablePayloadByte(uint8_t byte)
{
    return (byte >= 0x20U) && (byte <= 0x7EU);
}

static bool TextEquals(const char *text, const char *expected)
{
    return strcmp(text, expected) == 0;
}

static bool ParseUnsigned(const char *text, uint32_t maximum, uint32_t *value)
{
    uint32_t result = 0U;
    const char *p = text;

    if ((text == 0) || (*text == '\0') || (value == 0)) {
        return false;
    }
    while (*p != '\0') {
        uint8_t digit;
        if ((*p < '0') || (*p > '9')) {
            return false;
        }
        digit = (uint8_t)(*p - '0');
        if (result > ((maximum - digit) / 10U)) {
            return false;
        }
        result = result * 10U + digit;
        p++;
    }
    *value = result;
    return true;
}

static bool ParseSigned(const char *text, int32_t minimum, int32_t maximum, int32_t *value)
{
    bool negative = false;
    uint32_t magnitude;
    const char *p = text;

    if ((text == 0) || (*text == '\0') || (value == 0)) {
        return false;
    }
    if (*p == '-') {
        negative = true;
        p++;
    } else if (*p == '+') {
        p++;
    }
    if (!ParseUnsigned(p, negative ? (uint32_t)(-(minimum + 1L)) + 1UL : (uint32_t)maximum,
                       &magnitude)) {
        return false;
    }
    if (negative) {
        if (magnitude > ((uint32_t)(-(minimum + 1L)) + 1UL)) {
            return false;
        }
        *value = -(int32_t)magnitude;
    } else {
        *value = (int32_t)magnitude;
    }
    return (*value >= minimum) && (*value <= maximum);
}

static bool MarkSeen(uint32_t *seen, uint32_t bit)
{
    if ((*seen & bit) != 0U) {
        return false;
    }
    *seen |= bit;
    return true;
}

static ProtocolEvent ParsePayload(ProtocolParser *parser)
{
    ProtocolEvent event;
    BfRemoteCommand command;
    char *token;
    char *next;
    uint32_t seen = 0U;
    uint16_t field_count = 0U;

#define RETURN_PAYLOAD_ERROR(error_code) do { \
        ProtocolEvent payload_error = EventError((error_code)); \
        payload_error.sequence_present = ((seen & FIELD_SEQ) != 0U); \
        payload_error.sequence = command.sequence; \
        return payload_error; \
    } while (0)

    memset(&event, 0, sizeof(event));
    memset(&command, 0, sizeof(command));
    token = parser->payload;

    while (token != 0) {
        char *equals;
        next = strchr(token, ',');
        if (next != 0) {
            *next = '\0';
            next++;
        }
        field_count++;
        if (field_count > CFG_PROTOCOL_MAX_FIELDS) {
            RETURN_PAYLOAD_ERROR(PROTO_ERR_FIELD);
        }

        if (field_count == 1U) {
            if (!TextEquals(token, "CMD")) {
                RETURN_PAYLOAD_ERROR(PROTO_ERR_TYPE);
            }
            token = next;
            continue;
        }

        equals = strchr(token, '=');
        if ((equals == 0) || (equals == token) || (equals[1] == '\0') ||
            (strchr(equals + 1, '=') != 0)) {
            RETURN_PAYLOAD_ERROR(PROTO_ERR_FIELD);
        }
        *equals = '\0';
        equals++;

        if (TextEquals(token, "seq")) {
            uint32_t value;
            if (!MarkSeen(&seen, FIELD_SEQ)) {
                RETURN_PAYLOAD_ERROR(PROTO_ERR_DUP_FIELD);
            }
            if (!ParseUnsigned(equals, 65535U, &value)) {
                RETURN_PAYLOAD_ERROR(PROTO_ERR_RANGE);
            }
            command.sequence = (uint16_t)value;
            event.sequence_present = true;
            event.sequence = command.sequence;
        } else if (TextEquals(token, "move")) {
            if (!MarkSeen(&seen, FIELD_MOVE)) {
                RETURN_PAYLOAD_ERROR(PROTO_ERR_DUP_FIELD);
            }
            if (TextEquals(equals, "F")) {
                command.move = BF_MOVE_FORWARD;
            } else if (TextEquals(equals, "R")) {
                command.move = BF_MOVE_REVERSE;
            } else if (TextEquals(equals, "S")) {
                command.move = BF_MOVE_STOP;
            } else {
                RETURN_PAYLOAD_ERROR(PROTO_ERR_RANGE);
            }
        } else if (TextEquals(token, "turn")) {
            if (!MarkSeen(&seen, FIELD_TURN)) {
                RETURN_PAYLOAD_ERROR(PROTO_ERR_DUP_FIELD);
            }
            if (TextEquals(equals, "L")) {
                command.turn = BF_TURN_LEFT;
            } else if (TextEquals(equals, "R")) {
                command.turn = BF_TURN_RIGHT;
            } else if (TextEquals(equals, "C")) {
                command.turn = BF_TURN_CENTER;
            } else {
                RETURN_PAYLOAD_ERROR(PROTO_ERR_RANGE);
            }
        } else if (TextEquals(token, "step_speed")) {
            uint32_t value;
            if (!MarkSeen(&seen, FIELD_STEP)) {
                RETURN_PAYLOAD_ERROR(PROTO_ERR_DUP_FIELD);
            }
            if (!ParseUnsigned(equals, 100U, &value) || ((value != 60U) && (value != 100U))) {
                RETURN_PAYLOAD_ERROR(PROTO_ERR_RANGE);
            }
            command.step_rpm = (uint16_t)value;
        } else if (TextEquals(token, "servo")) {
            int32_t value;
            if (!MarkSeen(&seen, FIELD_SERVO)) {
                RETURN_PAYLOAD_ERROR(PROTO_ERR_DUP_FIELD);
            }
            if (!ParseSigned(equals, -30, 30, &value)) {
                RETURN_PAYLOAD_ERROR(PROTO_ERR_RANGE);
            }
            command.servo_deg = (int8_t)value;
        }
        /* 未知的、语法正确的 key=value 字段按协议约定忽略。 */
        token = next;
    }

    if ((field_count == 0U) || ((seen & FIELD_REQUIRED) != FIELD_REQUIRED)) {
        RETURN_PAYLOAD_ERROR(PROTO_ERR_MISSING_FIELD);
    }

    event.kind = PROTO_EVENT_COMMAND;
    event.error = PROTO_ERR_NONE;
    event.sequence_present = true;
    event.sequence = command.sequence;
    event.command = command;
#undef RETURN_PAYLOAD_ERROR
    return event;
}

void Protocol_Init(ProtocolParser *parser)
{
    Protocol_Reset(parser);
}

void Protocol_Reset(ProtocolParser *parser)
{
    if (parser == 0) {
        return;
    }
    parser->state = PROTO_WAIT_START;
    parser->length = 0U;
    parser->payload[0] = '\0';
}

ProtocolEvent Protocol_Feed(ProtocolParser *parser, uint8_t byte)
{
    ProtocolEvent event;

    memset(&event, 0, sizeof(event));
    if (parser == 0) {
        return EventError(PROTO_ERR_FORMAT);
    }

    switch (parser->state) {
    case PROTO_WAIT_START:
        if (byte == '<') {
            parser->length = 0U;
            parser->state = PROTO_COLLECT;
        }
        break;

    case PROTO_COLLECT:
        if (byte == '<') {
            /* 新帧头到来时立刻重同步，丢弃未完成旧帧。 */
            parser->length = 0U;
        } else if (byte == '>') {
            if (parser->length == 0U) {
                Protocol_Reset(parser);
                return EventError(PROTO_ERR_FORMAT);
            }
            parser->payload[parser->length] = '\0';
            parser->state = PROTO_EXPECT_EOL;
        } else if ((byte == '\r') || (byte == '\n') || !IsPrintablePayloadByte(byte)) {
            Protocol_Reset(parser);
            return EventError(PROTO_ERR_FORMAT);
        } else if (parser->length >= (CFG_PROTOCOL_PAYLOAD_MAX - 1U)) {
            parser->state = PROTO_DISCARD;
            return EventError(PROTO_ERR_TOO_LONG);
        } else {
            parser->payload[parser->length++] = (char)byte;
        }
        break;

    case PROTO_EXPECT_EOL:
        if (byte == '\n') {
            event = ParsePayload(parser);
            Protocol_Reset(parser);
            return event;
        }
        if (byte == '\r') {
            parser->state = PROTO_EXPECT_LF_AFTER_CR;
            break;
        }
        Protocol_Reset(parser);
        return EventError(PROTO_ERR_FORMAT);

    case PROTO_EXPECT_LF_AFTER_CR:
        if (byte == '\n') {
            event = ParsePayload(parser);
            Protocol_Reset(parser);
            return event;
        }
        Protocol_Reset(parser);
        return EventError(PROTO_ERR_FORMAT);

    case PROTO_DISCARD:
        if (byte == '<') {
            parser->length = 0U;
            parser->state = PROTO_COLLECT;
        } else if ((byte == '\n') || (byte == '\r')) {
            Protocol_Reset(parser);
        }
        break;

    default:
        Protocol_Reset(parser);
        return EventError(PROTO_ERR_FORMAT);
    }
    return event;
}

const char *Protocol_ErrorName(ProtocolError error)
{
    switch (error) {
    case PROTO_ERR_FORMAT: return "E_FORMAT";
    case PROTO_ERR_FRAME_TIMEOUT: return "E_FRAME_TIMEOUT";
    case PROTO_ERR_TOO_LONG: return "E_TOO_LONG";
    case PROTO_ERR_TYPE: return "E_TYPE";
    case PROTO_ERR_FIELD: return "E_FIELD";
    case PROTO_ERR_DUP_FIELD: return "E_DUP_FIELD";
    case PROTO_ERR_MISSING_FIELD: return "E_MISSING_FIELD";
    case PROTO_ERR_RANGE: return "E_RANGE";
    case PROTO_ERR_SEMANTIC: return "E_SEMANTIC";
    case PROTO_ERR_SEQ_CONFLICT: return "E_SEQ_CONFLICT";
    case PROTO_ERR_SEQ_OLD: return "E_SEQ_OLD";
    case PROTO_ERR_SEQ_AMBIGUOUS: return "E_SEQ_AMBIGUOUS";
    case PROTO_ERR_STEP_REVERSE_UNSUPPORTED: return "E_STEP_REVERSE_UNSUPPORTED";
    case PROTO_ERR_STEPPER_DISABLED: return "E_STEPPER_DISABLED";
    case PROTO_ERR_CONFIGURATION: return "E_CONFIGURATION";
    case PROTO_ERR_RX_OVERFLOW: return "E_RX_OVERFLOW";
    case PROTO_ERR_NONE:
    default: return "E_UNKNOWN";
    }
}

size_t Protocol_EncodeAck(char *out, size_t capacity, uint16_t sequence, bool duplicate)
{
    int written;
    if ((out == 0) || (capacity == 0U)) {
        return 0U;
    }
    written = snprintf(out, capacity, "<ACK,seq=%u,result=%s>\n", (unsigned int)sequence,
                       duplicate ? "DUP" : "OK");
    return ((written < 0) || ((size_t)written >= capacity)) ? 0U : (size_t)written;
}

size_t Protocol_EncodeError(char *out, size_t capacity, bool has_sequence, uint16_t sequence,
                            ProtocolError error)
{
    int written;
    if ((out == 0) || (capacity == 0U)) {
        return 0U;
    }
    if (has_sequence) {
        written = snprintf(out, capacity, "<ERR,seq=%u,code=%s>\n", (unsigned int)sequence,
                           Protocol_ErrorName(error));
    } else {
        written = snprintf(out, capacity, "<ERR,seq=NA,code=%s>\n", Protocol_ErrorName(error));
    }
    return ((written < 0) || ((size_t)written >= capacity)) ? 0U : (size_t)written;
}

static void DdegToText(int16_t ddeg, char *out, size_t capacity)
{
    long signed_value = (long)ddeg;
    unsigned long magnitude = (signed_value < 0L) ? (unsigned long)(-signed_value)
                                                  : (unsigned long)signed_value;
    (void)snprintf(out, capacity, "%s%lu.%01lu", (signed_value < 0L) ? "-" : "",
                   magnitude / 10UL, magnitude % 10UL);
}

size_t Protocol_EncodeStatus(char *out, size_t capacity, const BfSystemSnapshot *snapshot)
{
    char roll[12];
    char pitch[12];
    char yaw[12];
    int written;

    if ((out == 0) || (capacity == 0U) || (snapshot == 0)) {
        return 0U;
    }
    if (snapshot->attitude.valid) {
        DdegToText(snapshot->attitude.roll_ddeg, roll, sizeof(roll));
        DdegToText(snapshot->attitude.pitch_ddeg, pitch, sizeof(pitch));
        DdegToText(snapshot->attitude.yaw_ddeg, yaw, sizeof(yaw));
    } else {
        (void)strcpy(roll, "NA");
        (void)strcpy(pitch, "NA");
        (void)strcpy(yaw, "NA");
    }
    written = snprintf(out, capacity,
                       "<STA,seq=%u,link=%u,step_rpm=NA,step_est=NA,step_actual=NA,step_on=%u,servo=%d,"
                       "roll=%s,pitch=%s,yaw=%s,err=%lu>\n",
                       (unsigned int)snapshot->last_sequence,
                       snapshot->command_link_alive ? 1U : 0U,
                       snapshot->step_running ? 1U : 0U,
                       (int)snapshot->servo_deg, roll, pitch, yaw,
                       (unsigned long)snapshot->active_faults);
    return ((written < 0) || ((size_t)written >= capacity)) ? 0U : (size_t)written;
}
