#include "qrcode.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Per-version tables, error correction level M                        */
/* ------------------------------------------------------------------ */

/* Total codewords available in the symbol (data + error correction). */
static const short kTotalCodewords[LTM_QR_MAX_VERSION + 1] = {
    0, 26, 44, 70, 100, 134, 172, 196, 242, 292, 346
};
/* Error correction codewords per block. */
static const unsigned char kEccPerBlock[LTM_QR_MAX_VERSION + 1] = {
    0, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26
};
/* Number of error correction blocks. */
static const unsigned char kNumBlocks[LTM_QR_MAX_VERSION + 1] = {
    0, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5
};

#define QR_MAX_CODEWORDS 346
#define QR_MAX_BLOCKS    5
#define QR_MAX_ECC       26

/* ------------------------------------------------------------------ */
/* GF(2^8) arithmetic, primitive polynomial x^8+x^4+x^3+x^2+1 (0x11D)  */
/* ------------------------------------------------------------------ */

static unsigned char gf_mul(unsigned char x, unsigned char y)
{
    int z = 0;
    int i;
    for (i = 7; i >= 0; --i) {
        z = (z << 1) ^ ((z >> 7) * 0x11D);
        z ^= ((y >> i) & 1) * x;
    }
    return (unsigned char)(z & 0xFF);
}

/* Coefficients of the divisor polynomial, highest power first, monic and
 * with the leading term omitted. */
static void rs_generator(int degree, unsigned char *out)
{
    unsigned char root = 1;
    int i, j;

    memset(out, 0, (size_t)degree);
    out[degree - 1] = 1;

    for (i = 0; i < degree; ++i) {
        for (j = 0; j < degree; ++j) {
            out[j] = gf_mul(out[j], root);
            if (j + 1 < degree) {
                out[j] ^= out[j + 1];
            }
        }
        root = gf_mul(root, 0x02);
    }
}

static void rs_remainder(const unsigned char *data, int len,
                         const unsigned char *gen, int degree,
                         unsigned char *out)
{
    int i, j;
    memset(out, 0, (size_t)degree);
    for (i = 0; i < len; ++i) {
        unsigned char factor = (unsigned char)(data[i] ^ out[0]);
        memmove(out, out + 1, (size_t)(degree - 1));
        out[degree - 1] = 0;
        for (j = 0; j < degree; ++j) {
            out[j] ^= gf_mul(gen[j], factor);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Matrix construction helpers                                          */
/* ------------------------------------------------------------------ */

typedef struct qr_ctx {
    ltm_qr       *q;
    int           size;
    int           version;
    unsigned char fn[LTM_QR_MAX_SIZE][LTM_QR_MAX_SIZE]; /* 1 = function module */
} qr_ctx;

static void set_fn(qr_ctx *c, int x, int y, int dark)
{
    if (x < 0 || y < 0 || x >= c->size || y >= c->size) {
        return;
    }
    c->q->m[y][x] = (unsigned char)(dark ? 1 : 0);
    c->fn[y][x] = 1;
}

static int abs_i(int v) { return (v < 0) ? -v : v; }

static void draw_finder(qr_ctx *c, int cx, int cy)
{
    int dx, dy;
    for (dy = -4; dy <= 4; ++dy) {
        for (dx = -4; dx <= 4; ++dx) {
            int dist = MAX(abs_i(dx), abs_i(dy));
            int x = cx + dx;
            int y = cy + dy;
            if (x >= 0 && x < c->size && y >= 0 && y < c->size) {
                set_fn(c, x, y, (dist != 2 && dist != 4));
            }
        }
    }
}

static void draw_alignment(qr_ctx *c, int cx, int cy)
{
    int dx, dy;
    for (dy = -2; dy <= 2; ++dy) {
        for (dx = -2; dx <= 2; ++dx) {
            set_fn(c, cx + dx, cy + dy, MAX(abs_i(dx), abs_i(dy)) != 1);
        }
    }
}

static int alignment_positions(int version, int *out)
{
    int num, step, pos, i;

    if (version == 1) {
        return 0;
    }
    num = version / 7 + 2;
    step = (version * 4 + num * 2 + 1) / (num * 2 - 2) * 2;
    out[0] = 6;
    pos = (version * 4 + 17) - 7;
    for (i = num - 1; i >= 1; --i) {
        out[i] = pos;
        pos -= step;
    }
    return num;
}

static int get_bit(unsigned long v, int i)
{
    return (int)((v >> i) & 1u);
}

static void draw_format_bits(qr_ctx *c, int mask)
{
    /* Level M encodes as 00. */
    unsigned long data = (unsigned long)(0u << 3) | (unsigned long)mask;
    unsigned long rem = data;
    unsigned long bits;
    int           i;

    for (i = 0; i < 10; ++i) {
        rem = (rem << 1) ^ ((rem >> 9) * 0x537u);
    }
    bits = ((data << 10) | rem) ^ 0x5412u;

    for (i = 0; i <= 5; ++i) {
        set_fn(c, 8, i, get_bit(bits, i));
    }
    set_fn(c, 8, 7, get_bit(bits, 6));
    set_fn(c, 8, 8, get_bit(bits, 7));
    set_fn(c, 7, 8, get_bit(bits, 8));
    for (i = 9; i < 15; ++i) {
        set_fn(c, 14 - i, 8, get_bit(bits, i));
    }

    for (i = 0; i < 8; ++i) {
        set_fn(c, c->size - 1 - i, 8, get_bit(bits, i));
    }
    for (i = 8; i < 15; ++i) {
        set_fn(c, 8, c->size - 15 + i, get_bit(bits, i));
    }
    set_fn(c, 8, c->size - 8, 1); /* always dark */
}

static void draw_version_bits(qr_ctx *c)
{
    unsigned long rem;
    unsigned long bits;
    int           i;

    if (c->version < 7) {
        return;
    }
    rem = (unsigned long)c->version;
    for (i = 0; i < 12; ++i) {
        rem = (rem << 1) ^ ((rem >> 11) * 0x1F25u);
    }
    bits = ((unsigned long)c->version << 12) | rem;

    for (i = 0; i < 18; ++i) {
        int bit = get_bit(bits, i);
        int a = c->size - 11 + i % 3;
        int b = i / 3;
        set_fn(c, a, b, bit);
        set_fn(c, b, a, bit);
    }
}

static void draw_function_patterns(qr_ctx *c)
{
    int align[8];
    int num, i, j;

    for (i = 0; i < c->size; ++i) {
        set_fn(c, 6, i, (i % 2) == 0);
        set_fn(c, i, 6, (i % 2) == 0);
    }

    draw_finder(c, 3, 3);
    draw_finder(c, c->size - 4, 3);
    draw_finder(c, 3, c->size - 4);

    num = alignment_positions(c->version, align);
    for (i = 0; i < num; ++i) {
        for (j = 0; j < num; ++j) {
            /* The three finder corners already own those cells. */
            if ((i == 0 && j == 0) || (i == 0 && j == num - 1) ||
                (i == num - 1 && j == 0)) {
                continue;
            }
            draw_alignment(c, align[i], align[j]);
        }
    }

    draw_format_bits(c, 0); /* placeholder, rewritten once a mask is chosen */
    draw_version_bits(c);
}

static void draw_codewords(qr_ctx *c, const unsigned char *data, int len)
{
    int i = 0; /* bit index into data */
    int right;

    for (right = c->size - 1; right >= 1; right -= 2) {
        int vert;
        if (right == 6) {
            right = 5; /* skip the vertical timing column */
        }
        for (vert = 0; vert < c->size; ++vert) {
            int j;
            for (j = 0; j < 2; ++j) {
                int x = right - j;
                int upward = ((right + 1) & 2) == 0;
                int y = upward ? (c->size - 1 - vert) : vert;
                if (!c->fn[y][x] && i < len * 8) {
                    c->q->m[y][x] = (unsigned char)((data[i >> 3] >> (7 - (i & 7))) & 1);
                    ++i;
                }
                /* Any leftover remainder modules stay light. */
            }
        }
    }
}

static int mask_condition(int mask, int x, int y)
{
    switch (mask) {
    case 0:  return (x + y) % 2 == 0;
    case 1:  return y % 2 == 0;
    case 2:  return x % 3 == 0;
    case 3:  return (x + y) % 3 == 0;
    case 4:  return (x / 3 + y / 2) % 2 == 0;
    case 5:  return (x * y) % 2 + (x * y) % 3 == 0;
    case 6:  return ((x * y) % 2 + (x * y) % 3) % 2 == 0;
    default: return ((x + y) % 2 + (x * y) % 3) % 2 == 0;
    }
}

static void apply_mask(qr_ctx *c, int mask)
{
    int x, y;
    for (y = 0; y < c->size; ++y) {
        for (x = 0; x < c->size; ++x) {
            if (!c->fn[y][x] && mask_condition(mask, x, y)) {
                c->q->m[y][x] ^= 1;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Mask penalty (ISO/IEC 18004 clause 8.8.2)                            */
/* ------------------------------------------------------------------ */

static const unsigned char kFinderRun[11]    = { 1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0 };
static const unsigned char kFinderRunRev[11] = { 0, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1 };

static int line_penalty(const unsigned char *line, int n)
{
    int score = 0;
    int run_color = -1;
    int run_len = 0;
    int i;

    for (i = 0; i < n; ++i) {
        if (line[i] == run_color) {
            ++run_len;
            if (run_len == 5) {
                score += 3;
            } else if (run_len > 5) {
                score += 1;
            }
        } else {
            run_color = line[i];
            run_len = 1;
        }
    }
    for (i = 0; i + 11 <= n; ++i) {
        if (memcmp(line + i, kFinderRun, 11) == 0 ||
            memcmp(line + i, kFinderRunRev, 11) == 0) {
            score += 40;
        }
    }
    return score;
}

static long penalty_score(const ltm_qr *q)
{
    int  size = q->size;
    long score = 0;
    long dark = 0;
    long total;
    long k;
    unsigned char col[LTM_QR_MAX_SIZE];
    int  x, y;

    for (y = 0; y < size; ++y) {
        score += line_penalty(q->m[y], size);
        for (x = 0; x < size; ++x) {
            dark += q->m[y][x];
        }
    }
    for (x = 0; x < size; ++x) {
        for (y = 0; y < size; ++y) {
            col[y] = q->m[y][x];
        }
        score += line_penalty(col, size);
    }

    for (y = 0; y + 1 < size; ++y) {
        for (x = 0; x + 1 < size; ++x) {
            unsigned char v = q->m[y][x];
            if (v == q->m[y][x + 1] && v == q->m[y + 1][x] && v == q->m[y + 1][x + 1]) {
                score += 3;
            }
        }
    }

    total = (long)size * size;
    k = ((dark * 20 - total * 10 < 0 ? total * 10 - dark * 20 : dark * 20 - total * 10)
         + total - 1) / total - 1;
    if (k < 0) {
        k = 0;
    }
    if (k > 9) {
        k = 9;
    }
    score += k * 10;

    return score;
}

/* ------------------------------------------------------------------ */
/* Encoding                                                            */
/* ------------------------------------------------------------------ */

static int char_count_bits(int version)
{
    return (version <= 9) ? 8 : 16;
}

static int data_capacity_codewords(int version)
{
    return kTotalCodewords[version] - kEccPerBlock[version] * kNumBlocks[version];
}

static int pick_version(int payload_len)
{
    int v;
    for (v = 1; v <= LTM_QR_MAX_VERSION; ++v) {
        int cap_bits = data_capacity_codewords(v) * 8;
        int need_bits = 4 + char_count_bits(v) + payload_len * 8;
        if (need_bits <= cap_bits) {
            return v;
        }
    }
    return 0;
}

typedef struct bitwriter {
    unsigned char *buf;
    int            cap_bytes;
    int            nbits;
} bitwriter;

static void bw_put(bitwriter *w, unsigned long value, int nbits)
{
    int i;
    for (i = nbits - 1; i >= 0; --i) {
        int bit = (int)((value >> i) & 1u);
        int idx = w->nbits >> 3;
        if (idx >= w->cap_bytes) {
            return;
        }
        if (bit) {
            w->buf[idx] |= (unsigned char)(1 << (7 - (w->nbits & 7)));
        }
        ++w->nbits;
    }
}

BOOL ltm_qr_encode(const char *text, ltm_qr *out)
{
    unsigned char datacw[QR_MAX_CODEWORDS];
    unsigned char final[QR_MAX_CODEWORDS];
    unsigned char gen[QR_MAX_ECC];
    unsigned char ecc[QR_MAX_BLOCKS][QR_MAX_ECC];
    bitwriter     bw;
    qr_ctx        ctx;
    ltm_qr        best;
    long          best_score = 0;
    int           version, size, len;
    int           data_cw, num_blocks, ecc_len;
    int           short_len, num_short;
    int           i, j, pos, mask, best_mask = 0;

    if (text == NULL || out == NULL) {
        return FALSE;
    }
    len = (int)strlen(text);
    version = pick_version(len);
    if (version == 0) {
        return FALSE;
    }
    size = version * 4 + 17;

    data_cw = data_capacity_codewords(version);
    num_blocks = kNumBlocks[version];
    ecc_len = kEccPerBlock[version];
    short_len = data_cw / num_blocks;
    num_short = num_blocks - (data_cw % num_blocks);

    /* --- bit stream ------------------------------------------------ */
    memset(datacw, 0, sizeof(datacw));
    bw.buf = datacw;
    bw.cap_bytes = data_cw;
    bw.nbits = 0;

    bw_put(&bw, 0x4, 4);                                  /* byte mode      */
    bw_put(&bw, (unsigned long)len, char_count_bits(version)); /* length    */
    for (i = 0; i < len; ++i) {
        bw_put(&bw, (unsigned char)text[i], 8);
    }
    {
        int remaining = data_cw * 8 - bw.nbits;
        bw_put(&bw, 0, MIN(4, remaining));                /* terminator     */
        bw_put(&bw, 0, (8 - (bw.nbits % 8)) % 8);         /* byte align     */
    }
    for (i = bw.nbits / 8; i < data_cw; ++i) {
        datacw[i] = (unsigned char)(((i - bw.nbits / 8) % 2 == 0) ? 0xEC : 0x11);
    }

    /* --- error correction and interleaving ------------------------- */
    rs_generator(ecc_len, gen);
    pos = 0;
    for (i = 0; i < num_blocks; ++i) {
        int blen = short_len + ((i < num_short) ? 0 : 1);
        rs_remainder(datacw + pos, blen, gen, ecc_len, ecc[i]);
        pos += blen;
    }

    pos = 0;
    for (i = 0; i <= short_len; ++i) {
        int off = 0;
        for (j = 0; j < num_blocks; ++j) {
            int blen = short_len + ((j < num_short) ? 0 : 1);
            if (i < blen) {
                final[pos++] = datacw[off + i];
            }
            off += blen;
        }
    }
    for (i = 0; i < ecc_len; ++i) {
        for (j = 0; j < num_blocks; ++j) {
            final[pos++] = ecc[j][i];
        }
    }

    /* --- matrix ----------------------------------------------------- */
    memset(out, 0, sizeof(*out));
    out->size = size;

    memset(&ctx, 0, sizeof(ctx));
    ctx.q = out;
    ctx.size = size;
    ctx.version = version;

    draw_function_patterns(&ctx);
    draw_codewords(&ctx, final, kTotalCodewords[version]);

    /* --- mask selection --------------------------------------------- */
    memset(&best, 0, sizeof(best));
    for (mask = 0; mask < 8; ++mask) {
        long score;
        apply_mask(&ctx, mask);
        draw_format_bits(&ctx, mask);
        score = penalty_score(out);
        if (mask == 0 || score < best_score) {
            best_score = score;
            best_mask = mask;
            best = *out;
        }
        apply_mask(&ctx, mask); /* XOR is its own inverse */
    }

    *out = best;
    out->size = size;
    LTM_UNUSED(best_mask);
    return TRUE;
}
