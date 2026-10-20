#ifndef QR_TASK_CODE_H
#define QR_TASK_CODE_H
#include <stddef.h>
#include <stdint.h>
#define QR_TASK_LENGTH 15u
typedef struct {
    char text[QR_TASK_LENGTH + 1u];
    uint8_t colors[2][3];
    uint8_t positions[2][3]; /* Group 4 is second-batch rough-processing positions. */
} QrTaskCode;
typedef enum {
    QR_CODE_OK, QR_CODE_LENGTH, QR_CODE_SEPARATOR, QR_CODE_COLOR,
    QR_CODE_DUPLICATE, QR_CODE_POSITION, QR_CODE_COLOR_SET
} QrCodeError;
/* Output is only changed on success; no whitespace/prefix stripping. */
QrCodeError QrTaskCode_Parse(const uint8_t *bytes, size_t length, QrTaskCode *out);
const char *QrTaskCode_ErrorName(QrCodeError error);
#endif
