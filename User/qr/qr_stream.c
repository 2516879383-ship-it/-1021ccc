/*
 * 扫码字节流分帧层：CR/LF 结束记录，空行不产出；支持任意 DMA 分片与连续多行。
 * 超长、超时或传输损坏后丢弃至下一行尾；重扫会话可等待行尾或静默边界重新同步。
 */
#include "qr_stream.h"
#include <string.h>

/*
 * 清空分帧状态；boundary 非零表示新会话仍需等待干净边界，禁止接收旧行后缀。
 */
void QrStream_Reset(QrStream *s, uint8_t boundary, uint32_t now)
{
    memset(s, 0, sizeof(*s)); s->session_sync = boundary; s->last_byte_ms = now;
}
void QrStream_Damage(QrStream *s)
{ s->used = 0u; s->discard = 1u; s->session_sync = 0u; }

/*
 * 按当前时刻推进会话静默同步和半帧超时；不会把缺少行尾的半帧当成完整码。
 */
void QrStream_Process(QrStream *s, uint32_t now, QrStreamCallback cb, void *ctx)
{
    if (s->session_sync && (uint32_t)(now - s->last_byte_ms) >= QR_SESSION_GAP_MS)
        s->session_sync = 0u;
    if (s->used && (uint32_t)(now - s->last_byte_ms) >= QR_FRAME_TIMEOUT_MS) {
        QrStream_Damage(s);
        cb(ctx, QR_STREAM_TIMEOUT, 0, 0u, now);
    }
}

/*
 * 按字节捕获时间先检查超时，再逐字节组行；完整行交回调，
 * 损坏/超长行等待换行后才恢复，CRLF 的第二个字符不会制造重复记录。
 */
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
