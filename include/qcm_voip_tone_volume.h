/**
 * @file qcm_voip_tone_volume.h
 * @brief 来电铃声与接通前回铃的 PCM 数字音量；不改变通话音量曲线。
 */
#ifndef QCM_VOIP_TONE_VOLUME_H
#define QCM_VOIP_TONE_VOLUME_H
#include <stdint.h>

/** @brief 在写入音频流之前调整一个铃声样本。
 * @param[in] int16_t sample 已解码的单声道 S16 PCM 样本。
 * @param[in] unsigned level 0 静音；1～6 为 1/32、1/16、1/8、1/4、1/2、1 倍；7～11 为 2～6 倍。
 * @return 调整后的 PCM，放大时饱和到 16 位范围。
 * @note 1～6 每档约差 6 dB；底层播放音量须为 6（原幅度），避免重复调音量。
 *       无共享状态、分配或硬件访问；每个新样本只处理一次，短写重试不得再次缩放。
 */
static inline int16_t qcm_voip_tone_sample(int16_t sample, unsigned level)
{
    int32_t value = sample;
    if (level == 0) return 0;
    if (level > 11U) level = 11U;
    if (level < 6U) value /= (int32_t)(1U << (6U - level));
    else if (level > 6U) value *= (int32_t)(level - 5U);
    if (value > 32767) value = 32767;
    else if (value < -32768) value = -32768;
    return (int16_t)value;
}
#endif
