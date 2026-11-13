/** @file qcm_voip_tone_guard.c
 * @brief 定点 Goertzel 与最小二乘能量投影，识别常用电话提示音。
 * @note 仅在主叫 SIP 200 后由 PCM 接收线程使用。没有浮点、堆分配或等待。
 */
#include "qcm_voip_tone_guard.h"
#include <string.h>

/* 8 kHz 下的 cos/sin，Q15；单音 425/450 Hz，双音 440+480 Hz。
 * 参考 Asterisk indications.conf.sample 的 cn/us 等提示音配置。
 * 双音在 20 ms 窗口内不正交，必须消除交叉项，不能直接相加 Goertzel 功率。 */
static const int32_t cos_q15[4] = {30959, 30743, 30831, 30467};
static const int32_t sin_q15[4] = {10736, 11342, 11100, 12063};
/* inv(B * B^T)，Q24；B 为 160 点的 cos440/sin440/cos480/sin480 基。 */
static const int32_t dual_inverse[4][4] = {
    {221913, -6100, 38115, -35720},
    {-6100, 222296, 26851, 45221},
    {38115, 26851, 217759, -1099},
    {-35720, 45221, -1099, 226324}
};

/** @brief 计算一个完整窗口的匹配率及有界语音确认状态。 */
static void classify(voip_tone_guard_t *g)
{
    int32_t mean = 0, real[4], imag[4], vector[4];
    int64_t energy = 0, best = 0, projected = 0;
    unsigned i, j, k, kind = 0;
    for (i = 0; i < 160; ++i) mean += g->frame[i] / 8;
    mean /= 160;
    for (i = 0; i < 160; ++i) {
        int32_t x = g->frame[i] / 8 - mean;
        energy += (int64_t)x * x;
    }
    g->score = 0;
    g->kind = 0;
    /* 去直流后的 RMS 约小于 64：不把静音或噪声底当作接听语音。 */
    if (energy < 10240) { g->quiet = true; return; }
    for (k = 0; k < 4; ++k) {
        int32_t s1 = 0, s2 = 0;
        for (i = 0; i < 160; ++i) {
            int32_t s = g->frame[i] / 8 - mean +
                (int32_t)((int64_t)2 * cos_q15[k] * s1 / 32768) - s2;
            s2 = s1; s1 = s;
        }
        real[k] = s1 - (int32_t)((int64_t)cos_q15[k] * s2 / 32768);
        imag[k] = (int32_t)((int64_t)sin_q15[k] * s2 / 32768);
        if (k < 2) {
            /* 单音正交基的逆 Gram 矩阵为 diag(1/80, 1/80)。 */
            projected = ((int64_t)real[k] * real[k] + (int64_t)imag[k] * imag[k]) * 209715;
            if (projected > best) { best = projected; kind = k + 1; }
        }
    }
    vector[0] = real[2]; vector[1] = imag[2];
    vector[2] = real[3]; vector[3] = imag[3];
    projected = 0;
    for (i = 0; i < 4; ++i)
        for (j = 0; j < 4; ++j)
            projected += (int64_t)vector[i] * vector[j] * dual_inverse[i][j];
    if (projected > best) { best = projected; kind = 3; }
    /* 输入先缩小 8 倍，相关值及 Q24 投影使用 int64_t，避免满幅输入溢出。 */
    best /= 16777216;
    g->score = best >= energy ? 100U : (unsigned)(best * 100 / energy);
    g->quiet = g->score >= 94U;
    if (g->quiet) {
        g->kind = kind;
        g->voice_frames = 0;
    } else if (energy >= 40960 && g->voice_frames < 15) {
        /* 非提示音第一帧即恢复通话音量；累计 300 ms 后本流不再识别，避免通话中抽动。 */
        if (++g->voice_frames == 15) g->speech = true;
    }
}

void voip_tone_guard_reset(voip_tone_guard_t *g)
{
    memset(g, 0, sizeof(*g));
    g->quiet = true;
}

bool voip_tone_guard_process(voip_tone_guard_t *g, const int16_t *samples, unsigned count)
{
    if (g->speech) return false;
    while (count) {
        unsigned n = 160U - g->used;
        if (n > count) n = count;
        memcpy(g->frame + g->used, samples, n * sizeof(*samples));
        g->used += n; samples += n; count -= n;
        if (g->used == 160U) {
            g->used = 0;
            classify(g);
            if (g->speech) break;
        }
    }
    return !g->speech && g->quiet;
}
