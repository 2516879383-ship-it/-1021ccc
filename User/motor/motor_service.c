/*
 * 保留的单电机服务：用于原单机示例和兼容测试，当前主程序七轴调度使用 MotorBus。
 * 与七轴服务共用协议层和传输抽象，但轮询周期、超时和状态机各自独立。
 */
#include "motor_service.h"
#include <string.h>

static const uint8_t queries[] = { EMM_POSITION, EMM_SPEED, EMM_STATUS };
static int reached(uint32_t now, uint32_t deadline) { return (int32_t)(now - deadline) >= 0; }

static void finish(MotorService *s, uint8_t result)
{
    if (s->query) {
        s->poll_index = (uint8_t)((s->poll_index + 1u) % 3u);
        if (!s->poll_index) s->next_cycle_ms = s->cycle_ms + 100u;
    } else {
        ++s->data.command_seq;
        s->data.command_code = s->active.code;
        s->data.command_result = result;
    }
    s->pending = 0u;
    if (result != EMM_ACK_OK) {
        s->data.last_error = result == EMM_ACK_TIMEOUT ? MOTOR_ERROR_TIMEOUT : MOTOR_ERROR_REJECTED;
    }
}

/*
 * 只让当前已发查询发布新鲜字段；迟到/无关应答不确认命令，异步 9F 不作为 ACK。
 */
static void received(void *context, const EmmFrame *f)
{
    MotorService *s = (MotorService *)context;
    int matches = s->pending && s->transmitted && f->code == s->active.code;
    ++s->data.rx_count; s->data.last_rx_ms = s->now_ms;
    /* Only the outstanding query may publish a fresh sample, not duplicate/late data. */
    if (matches && s->query) switch (f->code) {
    case EMM_POSITION:
        s->data.position_counts = f->position; s->data.position_ms = s->now_ms;
        ++s->data.position_seq; s->data.valid |= MOTOR_VALID_POSITION; break;
    case EMM_SPEED:
        s->data.speed_rpm = f->speed; s->data.speed_ms = s->now_ms;
        ++s->data.speed_seq; s->data.valid |= MOTOR_VALID_SPEED; break;
    case EMM_STATUS:
        s->data.status_flags = f->flags; s->data.status_ms = s->now_ms;
        ++s->data.status_seq; s->data.valid |= MOTOR_VALID_STATUS; break;
    default: break;
    }
    /* Async completion (9F), unrelated codes and late replies cannot acknowledge a command. */
    if (matches) {
        s->last_send_ms = s->now_ms;
        if (s->query) finish(s, EMM_ACK_OK);
        else if (f->result != 0x9Fu) finish(s, f->result);
    }
}

void MotorService_Init(MotorService *s, MotorTransport transport, uint8_t address, uint32_t now_ms)
{
    memset(s, 0, sizeof(*s)); s->transport = transport; s->data.address = address;
    s->next_send_ms = now_ms; s->next_cycle_ms = now_ms;
    s->last_send_ms = now_ms - 5u;
    EmmParser_Init(&s->parser, address);
}

int MotorService_Submit(MotorService *s, const EmmCommand *c)
{
    if (s->queued_valid || (s->pending && !s->query) || !c || c->length < 4u || c->length > sizeof(c->bytes) ||
        c->bytes[0] != s->data.address) return 0;
    s->queued = *c; s->queued_valid = 1u; return 1;
}

void MotorService_Receive(MotorService *s, const uint8_t *bytes, size_t count, uint32_t now_ms)
{
    s->now_ms = now_ms;
    EmmParser_Feed(&s->parser, bytes, count, received, s);
    s->data.bad_frames = s->parser.rejected;
}

void MotorService_TransportError(MotorService *s, MotorError error, uint32_t now_ms)
{
    s->now_ms = now_ms;
    if (s->pending) finish(s, EMM_ACK_TRANSPORT);
    s->data.last_error = error; s->data.online = 0u;
    s->data.valid = 0u; s->parser.used = 0u;
    s->next_send_ms = now_ms + 100u;
}

/*
 * 推进单轴三字段轮询、一个待发控制及 DMA/ACK 期限。
 * 查询可有限重试，普通控制不按查询策略重放；本函数按主循环调用，不阻塞等待反馈。
 */
void MotorService_Process(MotorService *s, uint32_t now_ms)
{
    s->now_ms = now_ms;
    s->data.online = (uint8_t)(s->data.valid == 7u &&
        (uint32_t)(now_ms - s->data.position_ms) <= MOTOR_DATA_STALE_MS &&
        (uint32_t)(now_ms - s->data.speed_ms) <= MOTOR_DATA_STALE_MS &&
        (uint32_t)(now_ms - s->data.status_ms) <= MOTOR_DATA_STALE_MS);
    if (s->pending && s->transmitted) {
        if (s->waiting_tx) {
            if (!s->transport.busy(s->transport.context)) {
                s->waiting_tx = 0u; s->sent_ms = now_ms; s->last_send_ms = now_ms;
            } else if ((uint32_t)(now_ms - s->tx_ms) >= 20u) {
                MotorService_TransportError(s, MOTOR_ERROR_UART, now_ms);
            }
        } else if ((uint32_t)(now_ms - s->sent_ms) >= 100u) {
            ++s->data.timeouts; s->data.last_error = MOTOR_ERROR_TIMEOUT;
            s->parser.used = 0u;
            /* Drain late data before retrying an indistinguishable wire request. */
            s->next_send_ms = now_ms + 100u;
            if (s->query && s->attempts < 3u) {
                s->transmitted = 0u; s->requested_ms = s->next_send_ms;
            }
            else finish(s, EMM_ACK_TIMEOUT);
        }
    }
    if (!s->pending && reached(now_ms, s->next_send_ms)) {
        if (s->queued_valid) {
            s->active = s->queued; s->queued_valid = 0u; s->query = 0u;
        } else {
            if (!s->poll_index && !reached(now_ms, s->next_cycle_ms)) return;
            if (!s->poll_index) s->cycle_ms = now_ms;
            s->active = Emm_Query(s->data.address, queries[s->poll_index]); s->query = 1u;
        }
        s->pending = 1u; s->transmitted = 0u; s->attempts = 0u;
        s->requested_ms = now_ms;
    }
    if (s->pending && !s->transmitted && reached(now_ms, s->next_send_ms) &&
        reached(now_ms, s->last_send_ms + 5u) && !s->transport.busy(s->transport.context)) {
        if (s->transport.send(s->transport.context, s->active.bytes, s->active.length)) {
            ++s->attempts; ++s->data.tx_count; s->transmitted = 1u; s->waiting_tx = 1u;
            s->tx_ms = now_ms; s->last_send_ms = now_ms;
        }
    }
    if (s->pending && !s->transmitted && reached(now_ms, s->next_send_ms) &&
        (uint32_t)(now_ms - s->requested_ms) >= 100u) {
        MotorService_TransportError(s, MOTOR_ERROR_UART, now_ms);
    }
}
