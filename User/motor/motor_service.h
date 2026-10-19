#ifndef MOTOR_SERVICE_H
#define MOTOR_SERVICE_H
#include "emm_protocol.h"
#include "motor_types.h"

typedef struct {
    void *context;
    int (*send)(void *context, const uint8_t *data, uint8_t length);
    int (*busy)(void *context);
} MotorTransport;

typedef struct {
    MotorSnapshot data;
    MotorTransport transport;
    EmmParser parser;
    EmmCommand active, queued;
    uint8_t pending, queued_valid, transmitted, waiting_tx, query, attempts, poll_index;
    uint32_t now_ms, sent_ms, tx_ms, requested_ms, last_send_ms, next_send_ms, cycle_ms, next_cycle_ms;
} MotorService;

void MotorService_Init(MotorService *s, MotorTransport transport, uint8_t address, uint32_t now_ms);
void MotorService_Process(MotorService *s, uint32_t now_ms);
void MotorService_Receive(MotorService *s, const uint8_t *bytes, size_t count, uint32_t now_ms);
int MotorService_Submit(MotorService *s, const EmmCommand *command);
void MotorService_TransportError(MotorService *s, MotorError error, uint32_t now_ms);
#endif
