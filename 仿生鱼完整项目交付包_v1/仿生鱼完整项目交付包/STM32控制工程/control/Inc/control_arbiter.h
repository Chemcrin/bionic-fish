/** @file control_arbiter.h @brief 命令语义、序号窗口和失联看门狗的纯逻辑层。 */
#ifndef CONTROL_ARBITER_H
#define CONTROL_ARBITER_H

#include <stdbool.h>
#include <stdint.h>

#include "app_types.h"
#include "ascii_protocol.h"

typedef struct {
    bool sequence_valid;
    bool link_alive;
    uint16_t last_sequence;
    BfRemoteCommand last_command;
    uint32_t last_valid_command_ms;
    uint32_t sequence_gap_count;
} ControlSession;

typedef enum {
    CONTROL_NEW_COMMAND = 0,
    CONTROL_DUPLICATE_COMMAND,
    CONTROL_REJECTED
} ControlDecision;

void ControlSession_Init(ControlSession *session);
ProtocolError Control_ValidateCommand(const BfRemoteCommand *command);
ControlDecision Control_ClassifyCommand(const ControlSession *session,
                                        const BfRemoteCommand *command,
                                        ProtocolError *error);
void Control_AcceptNew(ControlSession *session, const BfRemoteCommand *command, uint32_t now_ms);
void Control_AcceptDuplicate(ControlSession *session, uint32_t now_ms);
void Control_ForceDisconnect(ControlSession *session);
bool Control_CheckTimeout(ControlSession *session, uint32_t now_ms);

#endif /* CONTROL_ARBITER_H */
