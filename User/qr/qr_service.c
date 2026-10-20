#include "qr_service.h"
#include <string.h>

static void record(void *ctx, QrStreamEvent event, const uint8_t *b, size_t n, uint32_t at)
{
    QrService *s = (QrService *)ctx;
    QrSnapshot *d = &s->data;
    QrTaskCode code;
    QrCodeError error;
    if (event == QR_STREAM_TOO_LONG) { ++d->overflows; d->last_error = QR_ERROR_OVERFLOW; return; }
    if (event == QR_STREAM_TIMEOUT) { ++d->frame_timeouts; d->last_error = QR_ERROR_FRAME_TIMEOUT; return; }
    ++d->records;
    error = QrTaskCode_Parse(b, n, &code);
    if (error != QR_CODE_OK) {
        ++d->invalid_records; d->last_error = QR_ERROR_INVALID; d->code_error = error; return;
    }
    ++d->legal_records;
    if (d->valid) {
        if (strcmp(d->current.text, code.text) == 0) ++d->duplicates;
        else { ++d->ignored; d->last_error = QR_ERROR_LOCKED; }
        return;
    }
    d->current = code; d->history = code; d->valid = 1u; d->history_valid = 1u;
    d->accepted_ms = at; d->history_ms = at; ++d->accepted_seq;
    d->last_error = QR_ERROR_NONE; d->code_error = QR_CODE_OK;
    d->timed_out = 0u; s->accepted_pending = 1u;
}
void QrService_Init(QrService *s, uint8_t ready, uint32_t now)
{
    memset(s, 0, sizeof(*s)); s->data.uart_ready = ready;
    s->data.session = 1u; s->data.session_ms = now;
    QrStream_Reset(&s->stream, 0u, now);
    if (!ready) s->data.last_error = QR_ERROR_UART;
}
void QrService_BeginSession(QrService *s, uint32_t now)
{
    ++s->data.session; s->data.session_ms = now; s->data.valid = 0u;
    memset(&s->data.current, 0, sizeof(s->data.current));
    s->data.timed_out = 0u; s->accepted_pending = 0u;
    s->data.last_error = s->data.uart_ready ? QR_ERROR_NONE : QR_ERROR_UART;
    s->data.code_error = QR_CODE_OK;
    /* Accept after CR/LF, or a quiet gap. Never accept the suffix of an in-flight line. */
    QrStream_Reset(&s->stream, 1u, now);
}
void QrService_Receive(QrService *s, const uint8_t *b, size_t n, uint32_t at)
{
    s->data.raw_bytes += (uint32_t)n;
    if (n) s->data.last_byte_ms = at;
    if (!s->data.uart_ready) { QrStream_Damage(&s->stream); return; }
    QrStream_Feed(&s->stream, b, n, at, record, s);
}
void QrService_Process(QrService *s, uint32_t now)
{
    QrStream_Process(&s->stream, now, record, s);
    s->data.timed_out = (uint8_t)(!s->data.valid && (uint32_t)(now - s->data.session_ms) >= QR_WAIT_TIMEOUT_MS);
}
void QrService_TransportError(QrService *s, QrError error, uint32_t count)
{
    if (error == QR_ERROR_UART) s->data.uart_errors += count;
    else s->data.overflows += count;
    s->data.last_error = error; QrStream_Damage(&s->stream);
}
void QrService_SetUartReady(QrService *s, uint8_t ready)
{ s->data.uart_ready = ready; }
int QrService_TakeAccepted(QrService *s, QrTaskCode *out)
{
    if (!s->accepted_pending) return 0;
    s->accepted_pending = 0u; if (out) *out = s->data.current; return 1;
}
const char *QrService_StateName(const QrService *s)
{
    if (s->data.valid) return "VALID / LOCKED";
    if (!s->data.uart_ready) return "UART ERROR";
    return s->data.timed_out ? "WAIT TIMEOUT" : "WAIT CODE";
}
