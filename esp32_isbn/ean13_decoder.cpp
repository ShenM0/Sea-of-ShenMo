#include "ean13_decoder.h"
#include <string.h>

/*
 * 自包含的 EAN-13 条码解码器（无第三方依赖）。
 *
 * EAN-13 结构: 95 个模块，共 59 个明暗条纹(run):
 *   起始符 3 run(bar,space,bar) + 左 6 位数字(每位 7 模块 = 4 run)
 *   + 中间符 5 run(space,bar,space,bar,space) + 右 6 位数字(每位 4 run)
 *   + 结束符 3 run(bar,space,bar)
 * 左半边用 L/G 编码(其奇偶组合编码第 1 位数字)，右半边用 R 编码。
 */

/* 7 位编码表，bit=1 表示黑条(bar)，MSB 为最左模块。 */
static const uint8_t L_CODE[10] = {
    0b0001101, 0b0011001, 0b0010011, 0b0111101, 0b0100011,
    0b0110001, 0b0101111, 0b0111011, 0b0110111, 0b0001011};
static const uint8_t G_CODE[10] = {
    0b0100111, 0b0110011, 0b0011011, 0b0100001, 0b0011101,
    0b0111001, 0b0000101, 0b0010001, 0b0001001, 0b0010111};
static const uint8_t R_CODE[10] = {
    0b1110010, 0b1100110, 0b1101100, 0b1000010, 0b1011100,
    0b1001110, 0b1010000, 0b1000100, 0b1001000, 0b1110100};

/* 左 6 位数字的奇偶组合 → 第 1 位数字。0=L(奇), 1=G(偶)。 */
static const uint8_t PARITY_PATTERNS[10][6] = {
    {0, 0, 0, 0, 0, 0}, /* 0 */
    {0, 0, 1, 0, 1, 1}, /* 1 */
    {0, 0, 1, 1, 0, 1}, /* 2 */
    {0, 0, 1, 1, 1, 0}, /* 3 */
    {0, 1, 0, 0, 1, 1}, /* 4 */
    {0, 1, 1, 0, 0, 1}, /* 5 */
    {0, 1, 1, 1, 0, 0}, /* 6 */
    {0, 1, 0, 1, 0, 1}, /* 7 */
    {0, 1, 0, 1, 1, 0}, /* 8 */
    {0, 1, 1, 0, 1, 0}, /* 9 */
};

/* 最大支持宽度(VGA=640)，够用且省内存。 */
#define EAN_MAX_W 800

static uint8_t s_run_val[EAN_MAX_W];
static uint16_t s_run_len[EAN_MAX_W];

static int lookup_code(const uint8_t *table, uint8_t bits)
{
    for (int i = 0; i < 10; ++i) {
        if (table[i] == bits) {
            return i;
        }
    }
    return -1;
}

/* 从第 s 个 run 开始按 59 run 解码。run[s] 必须是黑条。 */
static bool decode_runs(int s, char out[13])
{
    uint32_t total = 0;
    int m[59];
    float modw, cum = 0.0f;
    int prev_mod = 0;

    for (int i = 0; i < 59; ++i) {
        total += s_run_len[s + i];
    }
    modw = (float)total / 95.0f;
    if (modw < 1.0f) {
        return false;
    }

    /* 累计位置取整：模块数由累计像素位置推出，59 条条纹总和恒等于 95 个模块，
     * 单条纹 ±1px 抖动会被平滑吸收，而不是逐条独立四舍五入累积误差。 */
    for (int i = 0; i < 59; ++i) {
        cum += (float)s_run_len[s + i];
        int mod = (int)(cum / modw + 0.5f);
        m[i] = mod - prev_mod;
        if (m[i] < 1 || m[i] > 4) {
            return false;
        }
        prev_mod = mod;
    }

    /* 起始符 1,1,1；中间符 1,1,1,1,1；结束符 1,1,1 */
    if (!(m[0] == 1 && m[1] == 1 && m[2] == 1)) {
        return false;
    }
    if (!(m[27] == 1 && m[28] == 1 && m[29] == 1 && m[30] == 1 && m[31] == 1)) {
        return false;
    }
    if (!(m[56] == 1 && m[57] == 1 && m[58] == 1)) {
        return false;
    }

    int left[6], right[6], parity[6];
    int digits[13];
    int sum, check, first = -1;

    /* 左 6 位数字（run 3..26），每位 space,bar,space,bar */
    for (int d = 0; d < 6; ++d) {
        int base = 3 + d * 4;
        uint8_t pat = 0;
        static const uint8_t vals[4] = {0, 1, 0, 1};
        if (m[base] + m[base + 1] + m[base + 2] + m[base + 3] != 7) {
            return false;
        }
        for (int r = 0; r < 4; ++r) {
            for (int k = 0; k < m[base + r]; ++k) {
                pat = (uint8_t)((pat << 1) | vals[r]);
            }
        }
        int l = lookup_code(L_CODE, pat);
        int g = lookup_code(G_CODE, pat);
        if (l >= 0) {
            left[d] = l;
            parity[d] = 0;
        } else if (g >= 0) {
            left[d] = g;
            parity[d] = 1;
        } else {
            return false;
        }
    }

    /* 右 6 位数字（run 32..55），每位 bar,space,bar,space */
    for (int d = 0; d < 6; ++d) {
        int base = 32 + d * 4;
        uint8_t pat = 0;
        static const uint8_t vals[4] = {1, 0, 1, 0};
        if (m[base] + m[base + 1] + m[base + 2] + m[base + 3] != 7) {
            return false;
        }
        for (int r = 0; r < 4; ++r) {
            for (int k = 0; k < m[base + r]; ++k) {
                pat = (uint8_t)((pat << 1) | vals[r]);
            }
        }
        int dg = lookup_code(R_CODE, pat);
        if (dg < 0) {
            return false;
        }
        right[d] = dg;
    }

    /* 由奇偶组合确定第 1 位数字 */
    for (int i = 0; i < 10; ++i) {
        bool match = true;
        for (int d = 0; d < 6; ++d) {
            if (PARITY_PATTERNS[i][d] != parity[d]) {
                match = false;
                break;
            }
        }
        if (match) {
            first = i;
            break;
        }
    }
    if (first < 0) {
        return false;
    }

    digits[0] = first;
    for (int d = 0; d < 6; ++d) {
        digits[1 + d] = left[d];
        digits[7 + d] = right[d];
    }

    /* EAN-13 校验位验证 */
    sum = 0;
    for (int i = 0; i < 12; ++i) {
        sum += (i % 2 == 0) ? digits[i] : digits[i] * 3;
    }
    check = (10 - (sum % 10)) % 10;
    if (check != digits[12]) {
        return false;
    }

    for (int i = 0; i < 13; ++i) {
        out[i] = (char)('0' + digits[i]);
    }
    return true;
}

static bool decode_row(const uint8_t *row, int width, char out[13], bool mirror)
{
    uint8_t mn = 255, mx = 0, th, prev, b;
    uint16_t cnt;
    int nruns = 0;

    if (width > EAN_MAX_W) {
        return false;
    }

    for (int x = 0; x < width; ++x) {
        uint8_t v = row[x];
        if (v < mn) {
            mn = v;
        }
        if (v > mx) {
            mx = v;
        }
    }
    if ((int)mx - (int)mn < 40) {
        return false; /* 对比度太低，跳过 */
    }
    th = (uint8_t)((mn + mx) >> 1);

    /* 二值化 + 游程编码：1=黑条，0=白空。mirror 时从右往左扫描。 */
    if (!mirror) {
        prev = (row[0] < th) ? 1 : 0;
        cnt = 1;
        for (int x = 1; x < width; ++x) {
            b = (row[x] < th) ? 1 : 0;
            if (b == prev) {
                ++cnt;
            } else {
                s_run_val[nruns] = prev;
                s_run_len[nruns] = cnt;
                ++nruns;
                prev = b;
                cnt = 1;
                if (nruns >= EAN_MAX_W - 1) {
                    break;
                }
            }
        }
    } else {
        prev = (row[width - 1] < th) ? 1 : 0;
        cnt = 1;
        for (int x = width - 2; x >= 0; --x) {
            b = (row[x] < th) ? 1 : 0;
            if (b == prev) {
                ++cnt;
            } else {
                s_run_val[nruns] = prev;
                s_run_len[nruns] = cnt;
                ++nruns;
                prev = b;
                cnt = 1;
                if (nruns >= EAN_MAX_W - 1) {
                    break;
                }
            }
        }
    }
    s_run_val[nruns] = prev;
    s_run_len[nruns] = cnt;
    ++nruns;

    if (nruns < 60) {
        return false; /* 条码至少 59 run，加上前后静区 */
    }

    /* 尝试每个候选起点：当前 run 是黑条，且前面是白(静区) */
    for (int s = 1; s + 58 < nruns; ++s) {
        if (s_run_val[s] != 1) {
            continue;
        }
        if (s_run_val[s - 1] != 0) {
            continue;
        }
        if (decode_runs(s, out)) {
            return true;
        }
    }
    return false;
}

/* 采样一条斜线 y = y0 + slope*x 并解码（用于容错条码轻微倾斜）。 */
static bool decode_angled(const uint8_t *gray, int width, int height,
                          float slope, int y0, char out[13])
{
    static uint8_t line[EAN_MAX_W];
    for (int x = 0; x < width; ++x) {
        int y = y0 + (int)(slope * (float)x);
        if (y < 0 || y >= height) {
            line[x] = 255U;
        } else {
            line[x] = gray[(size_t)y * width + x];
        }
    }
    return decode_row(line, width, out, false);
}

bool ean13_decode(const uint8_t *gray, int width, int height, char out[13])
{
    int step = height / 64;
    if (step < 1) {
        step = 1;
    }
    /* 水平方向（含镜像） */
    for (int pass = 0; pass < 2; ++pass) {
        bool mirror = (pass == 1);
        for (int y = height / 8; y < height * 7 / 8; y += step) {
            if (decode_row(gray + (size_t)y * width, width, out, mirror)) {
                return true;
            }
        }
    }
    /* 倾斜 ±10° / ±20° 容错（手持条码常有轻微倾斜） */
    static const float slopes[4] = {0.176f, -0.176f, 0.364f, -0.364f};
    for (int a = 0; a < 4; ++a) {
        float s = slopes[a];
        int lo = (s > 0.0f) ? -(int)(s * width) : 0;
        int hi = (s > 0.0f) ? height : height - (int)(s * width);
        for (int y0 = lo; y0 < hi; y0 += step) {
            if (decode_angled(gray, width, height, s, y0, out)) {
                return true;
            }
        }
    }
    return false;
}
