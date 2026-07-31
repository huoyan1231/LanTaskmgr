/*
 * qrcode.h - minimal QR Code encoder.
 *
 * Scope is deliberately narrow: byte mode, error correction level M,
 * versions 1 to 10. That covers 213 bytes of payload, which is two orders of
 * magnitude more than the "http://192.168.1.12:5555/" this program needs to
 * put on screen, and it keeps the tables small enough to sit in .rdata.
 *
 * Implemented from the ISO/IEC 18004 specification.
 */
#ifndef LTM_QRCODE_H
#define LTM_QRCODE_H

#include "common.h"

#define LTM_QR_MAX_VERSION 10
#define LTM_QR_MAX_SIZE    (4 * LTM_QR_MAX_VERSION + 17) /* 57 */

typedef struct ltm_qr {
    int           size;                                 /* modules per side */
    unsigned char m[LTM_QR_MAX_SIZE][LTM_QR_MAX_SIZE];  /* [y][x], 1 = dark */
} ltm_qr;

/* Encodes `text` (any byte sequence, NUL terminated) into `out`.
 * Returns FALSE if the payload does not fit in version 10. */
BOOL ltm_qr_encode(const char *text, ltm_qr *out);

#endif /* LTM_QRCODE_H */
