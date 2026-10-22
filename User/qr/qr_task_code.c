/*
 * 比赛任务码纯解析层：校验固定长度的两批颜色/位置字段，成功后才发布完整结果。
 * 每批颜色取 1～6 且不重复，位置为 1～3 的排列，两批颜色集合必须一致。
 */
#include "qr_task_code.h"
#include <string.h>

/*
 * 先检查长度、分隔符、字段范围和集合约束，全部通过后写 out；
 * 返回具体错误原因，失败时不发布部分解析结果。
 */
QrCodeError QrTaskCode_Parse(const uint8_t *b, size_t n, QrTaskCode *out)
{
    QrTaskCode code;
    unsigned batch, j, colors[2] = {0u, 0u}, positions, bit;
    uint8_t c, p;
    if (!b || n != QR_TASK_LENGTH) return QR_CODE_LENGTH;
    if (b[3] != '+' || b[7] != '+' || b[11] != '+') return QR_CODE_SEPARATOR;
    for (batch = 0u; batch < 2u; ++batch) {
        positions = 0u;
        for (j = 0u; j < 3u; ++j) {
            c = b[batch * 8u + j]; p = b[batch * 8u + 4u + j];
            if (c < '1' || c > '6') return QR_CODE_COLOR;
            bit = 1u << (c - '1');
            if (colors[batch] & bit) return QR_CODE_DUPLICATE;
            colors[batch] |= bit;
            if (p < '1' || p > '3') return QR_CODE_POSITION;
            bit = 1u << (p - '1');
            if (positions & bit) return QR_CODE_POSITION;
            positions |= bit;
            code.colors[batch][j] = (uint8_t)(c - '0');
            code.positions[batch][j] = (uint8_t)(p - '0');
        }
    }
    if (colors[0] != colors[1]) return QR_CODE_COLOR_SET;
    memcpy(code.text, b, QR_TASK_LENGTH); code.text[QR_TASK_LENGTH] = '\0';
    if (out) *out = code;
    return QR_CODE_OK;
}

const char *QrTaskCode_ErrorName(QrCodeError e)
{
    switch (e) {
    case QR_CODE_OK: return "NONE";
    case QR_CODE_LENGTH: return "LENGTH";
    case QR_CODE_SEPARATOR: return "SEPARATOR";
    case QR_CODE_COLOR: return "COLOR 1-6";
    case QR_CODE_DUPLICATE: return "DUP COLOR";
    case QR_CODE_POSITION: return "POSITION 1-3";
    case QR_CODE_COLOR_SET: return "COLOR SET";
    default: return "UNKNOWN";
    }
}
