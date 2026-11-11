/**
 * @file qcm_voip_resample.h
 * @brief 板端 16 kHz PCM 与网络 8 kHz PCMA 之间的定点采样转换。
 */
#ifndef QCM_VOIP_RESAMPLE_H
#define QCM_VOIP_RESAMPLE_H
#include <stdint.h>
#include <stddef.h>

/** FIR 抽头数量；系数采用 Q15 表示。 */
#define VOIP_RESAMPLE_TAPS 63U
/**
 * @struct voip_resample_t
 * @brief 每条音频流独占的滤波状态，打开流时清零。
 */
typedef struct {
    int16_t history[VOIP_RESAMPLE_TAPS]; /*!< 输入采样历史。 */
    unsigned position;                 /*!< 下一个历史写入位置。 */
    unsigned phase;                    /*!< 二倍抽取的相位，取值 0 或 1。 */
} voip_resample_t;

/**
 * @brief 将 16 kHz PCM 低通滤波后抽取到 8 kHz，支持跨块连续输入。
 * @param[in,out] voip_resample_t *state 本录音流的状态。
 * @param[in] const int16_t *input 输入采样。
 * @param[in] size_t count 输入采样数。
 * @param[out] int16_t *output 至少可容纳 (count + 1) / 2 个采样。
 * @return 本次输出的采样数。
 */
size_t voip_resample_down(voip_resample_t *state, const int16_t *input, size_t count, int16_t *output);

/**
 * @brief 将 8 kHz PCM 插零并低通滤波，恢复到板端 16 kHz。
 * @param[in,out] voip_resample_t *state 本播放流的状态。
 * @param[in] const int16_t *input 输入采样。
 * @param[in] size_t count 输入采样数。
 * @param[out] int16_t *output 至少可容纳 count * 2 个采样。
 * @return 本次输出的采样数。
 */
size_t voip_resample_up(voip_resample_t *state, const int16_t *input, size_t count, int16_t *output);
#endif /* QCM_VOIP_RESAMPLE_H */
