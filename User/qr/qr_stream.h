#ifndef QR_STREAM_H
#define QR_STREAM_H
#include "qr_config.h"
#include <stddef.h>
#include <stdint.h>
typedef enum { QR_STREAM_RECORD, QR_STREAM_TOO_LONG, QR_STREAM_TIMEOUT } QrStreamEvent;
typedef void (*QrStreamCallback)(void *context, QrStreamEvent event,
                               const uint8_t *bytes, size_t length, uint32_t at_ms);
typedef struct {
    uint8_t record[QR_RECORD_CAPACITY];
    size_t used;
    uint32_t last_byte_ms;
    uint8_t discard, session_sync;
} QrStream;
void QrStream_Reset(QrStream *s, uint8_t session_boundary, uint32_t now_ms);
/* Damage requires a terminator before trusting any more bytes. */
void QrStream_Damage(QrStream *s);
void QrStream_Feed(QrStream *s, const uint8_t *bytes, size_t length, uint32_t at_ms,
                   QrStreamCallback callback, void *context);
void QrStream_Process(QrStream *s, uint32_t now_ms, QrStreamCallback callback, void *context);
#endif
