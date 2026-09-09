/** @file ascii_protocol.h @brief 严格的 ASCII 控制帧解析与状态/应答编码。 */
#ifndef ASCII_PROTOCOL_H
#define ASCII_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_config.h"
#include "app_types.h"

typedef enum {
    PROTO_ERR_NONE = 0,
    PROTO_ERR_FORMAT,
    PROTO_ERR_FRAME_TIMEOUT,
    PROTO_ERR_TOO_LONG,
    PROTO_ERR_TYPE,
    PROTO_ERR_FIELD,
    PROTO_ERR_DUP_FIELD,
    PROTO_ERR_MISSING_FIELD,
    PROTO_ERR_RANGE,
    PROTO_ERR_SEMANTIC,
    PROTO_ERR_SEQ_CONFLICT,
    PROTO_ERR_SEQ_OLD,
    PROTO_ERR_SEQ_AMBIGUOUS,
    PROTO_ERR_STEP_REVERSE_UNSUPPORTED,
    PROTO_ERR_STEPPER_DISABLED,
    PROTO_ERR_CONFIGURATION,
    PROTO_ERR_RX_OVERFLOW
} ProtocolError;

typedef enum {
    PROTO_EVENT_NONE = 0,
    PROTO_EVENT_COMMAND,
    PROTO_EVENT_ERROR
} ProtocolEventKind;

typedef struct {
    ProtocolEventKind kind;
    ProtocolError error;
    bool sequence_present;
    uint16_t sequence;
    BfRemoteCommand command;
} ProtocolEvent;

typedef enum {
    PROTO_WAIT_START = 0,
    PROTO_COLLECT,
    PROTO_EXPECT_EOL,
    PROTO_EXPECT_LF_AFTER_CR,
    PROTO_DISCARD
} ProtocolParserState;

typedef struct {
    ProtocolParserState state;
    char payload[CFG_PROTOCOL_PAYLOAD_MAX];
    uint16_t length;
} ProtocolParser;

void Protocol_Init(ProtocolParser *parser);
void Protocol_Reset(ProtocolParser *parser);
ProtocolEvent Protocol_Feed(ProtocolParser *parser, uint8_t byte);
const char *Protocol_ErrorName(ProtocolError error);
size_t Protocol_EncodeAck(char *out, size_t capacity, uint16_t sequence, bool duplicate);
size_t Protocol_EncodeError(char *out, size_t capacity, bool has_sequence, uint16_t sequence,
                            ProtocolError error);
size_t Protocol_EncodeStatus(char *out, size_t capacity, const BfSystemSnapshot *snapshot);

#endif /* ASCII_PROTOCOL_H */
