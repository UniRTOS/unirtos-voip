/**
 * @file qcm_voip_resample.c
 * @brief 63 抽头 Q15 FIR 实现二倍抽取和插值，不在音频路径分配内存。
 */
#include "qcm_voip_resample.h"
#include <limits.h>

// 16 kHz 采样、3.4 kHz 截止频率、Hamming 窗；直流增益归一化到 32768。
static const int16_t g_filter[VOIP_RESAMPLE_TAPS] = {
    -14, 20, 27, -12, -45, -9, 63, 49, -66, -109, 34, 176, 49, -219,
    -187, 198, 362, -71, -524, -188, 600, 577, -497, -1062, 104, 1575,
    752, -2030, -2587, 2344, 10114, 13920, 10114, 2344, -2587, -2030,
    752, 1575, 104, -1062, -497, 577, 600, -188, -524, -71, 362, 198,
    -187, -219, 49, 176, 34, -109, -66, 49, 63, -9, -45, -12, 27, 20, -14
};

/**
 * @brief 推入一个采样并完成卷积，采用 64 位累加避免满幅输入溢出。
 * @param[in,out] voip_resample_t *state 当前流的历史状态。
 * @param[in] int16_t sample 输入采样。
 * @param[in] unsigned gain 插值时为 2，抽取时为 1。
 * @return 饱和到 16 位范围的 PCM 采样。
 */
static int16_t voip_filter_sample(voip_resample_t *state, int16_t sample, unsigned gain)
{
    int64_t accumulator = 0;
    unsigned i;
    unsigned index = state->position;
    state->history[index] = sample;
    for (i = 0; i < VOIP_RESAMPLE_TAPS; ++i) {
        accumulator += (int32_t)state->history[index] * g_filter[i];
        index = index ? index - 1U : VOIP_RESAMPLE_TAPS - 1U;
    }
    state->position = (state->position + 1U) % VOIP_RESAMPLE_TAPS;
    accumulator *= gain;
    accumulator = accumulator >= 0 ? (accumulator + 16384) / 32768 : -((-accumulator + 16384) / 32768);
    if (accumulator > INT16_MAX) return INT16_MAX;
    if (accumulator < INT16_MIN) return INT16_MIN;
    return (int16_t)accumulator;
}

/** @brief 实现头文件声明的 16 kHz 到 8 kHz 连续流转换。 */
size_t voip_resample_down(voip_resample_t *state, const int16_t *input, size_t count, int16_t *output)
{
    size_t i;
    size_t used = 0;
    if (!state || !input || !output) return 0;
    for (i = 0; i < count; ++i) {
        int16_t value = voip_filter_sample(state, input[i], 1U);
        state->phase ^= 1U;
        if (!state->phase) output[used++] = value;
    }
    return used;
}

/** @brief 实现头文件声明的 8 kHz 到 16 kHz 连续流转换。 */
size_t voip_resample_up(voip_resample_t *state, const int16_t *input, size_t count, int16_t *output)
{
    size_t i;
    if (!state || !input || !output) return 0;
    for (i = 0; i < count; ++i) {
        output[2U * i] = voip_filter_sample(state, input[i], 2U);
        output[2U * i + 1U] = voip_filter_sample(state, 0, 2U);
    }
    return count * 2U;
}
