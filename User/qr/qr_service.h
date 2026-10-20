#ifndef QR_SERVICE_H
#define QR_SERVICE_H
#include "qr_stream.h"
#include "qr_task_code.h"
typedef enum {
    QR_ERROR_NONE, QR_ERROR_INVALID, QR_ERROR_FRAME_TIMEOUT,
    QR_ERROR_OVERFLOW, QR_ERROR_UART, QR_ERROR_LOCKED
} QrError;
typedef struct {
    QrTaskCode current, history;
    uint8_t valid, history_valid, uart_ready, timed_out;
    uint32_t session, session_ms, accepted_ms, history_ms, accepted_seq;
    uint32_t raw_bytes, last_byte_ms, records, legal_records, invalid_records;
    uint32_t duplicates, ignored, overflows, uart_errors, frame_timeouts;
    QrError last_error;
    QrCodeError code_error;
} QrSnapshot;
typedef struct { QrStream stream; QrSnapshot data; uint8_t accepted_pending; } QrService;
void QrService_Init(QrService *s, uint8_t uart_ready, uint32_t now_ms);
/* Caller must first discard transport backlog atomically. */
void QrService_BeginSession(QrService *s, uint32_t now_ms);
void QrService_Receive(QrService *s, const uint8_t *bytes, size_t count, uint32_t capture_ms);
void QrService_Process(QrService *s, uint32_t now_ms);
void QrService_TransportError(QrService *s, QrError error, uint32_t count);
void QrService_SetUartReady(QrService *s, uint8_t ready);
/* One-shot publication. Snapshot remains locked until BeginSession. */
int QrService_TakeAccepted(QrService *s, QrTaskCode *out);
const char *QrService_StateName(const QrService *s);
#endif
