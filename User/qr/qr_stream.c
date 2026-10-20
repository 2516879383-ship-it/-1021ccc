#include "qr_stream.h"
#include <string.h>

void QrStream_Reset(QrStream *s, uint8_t boundary, uint32_t now)
{
    memset(s, 0, sizeof(*s)); s->session_sync = boundary; s->last_byte_ms = now;
}
void QrStream_Damage(QrStream *s)
{ s->used = 0u; s->discard = 1u; s->session_sync = 0u; }

void QrStream_Process(QrStream *s, uint32_t now, QrStreamCallback cb, void *ctx)
{
    if (s->session_sync && (uint32_t)(now - s->last_byte_ms) >= QR_SESSION_GAP_MS)
        s->session_sync = 0u;
    if (s->used && (uint32_t)(now - s->last_byte_ms) >= QR_FRAME_TIMEOUT_MS) {
        QrStream_Damage(s);
        cb(ctx, QR_STREAM_TIMEOUT, 0, 0u, now);
    }
}

void QrStream_Feed(QrStream *s, const uint8_t *b, size_t n, uint32_t at,
                   QrStreamCallback cb, void *ctx)
{
    size_t i;
    /* at is the capture timestamp, not the time the main loop drained a queue. */
    QrStream_Process(s, at, cb, ctx);
    for (i = 0u; i < n; ++i) {
        s->last_byte_ms = at;
        if (b[i] == '\r' || b[i] == '\n') {
            if (s->used && !s->discard && !s->session_sync)
                cb(ctx, QR_STREAM_RECORD, s->record, s->used, at);
            s->used = 0u; s->discard = 0u; s->session_sync = 0u;
            /* Empty lines, including the LF after CR, do not create records. */
        } else if (!s->discard && !s->session_sync) {
            if (s->used == sizeof(s->record)) {
                QrStream_Damage(s);
                cb(ctx, QR_STREAM_TOO_LONG, 0, 0u, at);
            } else s->record[s->used++] = b[i];
        }
    }
}
