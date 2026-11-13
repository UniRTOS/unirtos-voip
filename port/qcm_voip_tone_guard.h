/** @file qcm_voip_tone_guard.h
 * @brief 提前应答服务器的窄带提示音识别；不推断真实接听状态。
 */
#ifndef QCM_VOIP_TONE_GUARD_H
#define QCM_VOIP_TONE_GUARD_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int16_t frame[160]; /*!< 8 kHz、20 ms 窗口，跨短 RTP 负载拼接。 */
    unsigned used; /*!< 当前窗口填充量，始终小于 160。 */
    unsigned voice_frames; /*!< 非提示音有效帧预算，饱和于 15；不对外输出。 */
    unsigned kind; /*!< 当前匹配模板：0 无、1=425 Hz、2=450 Hz、3=440+480 Hz。 */
    unsigned score; /*!< 当前窗口模板解释的能量百分比，0～100。 */
    bool speech; /*!< 已检测到至少 300 ms 非提示音，此流余下时间保持通话音量。 */
    bool quiet; /*!< 当前帧是否采用提示音档位；静音和首个不完整窗口也采用低档。 */
} voip_tone_guard_t;

/** @brief 新播放流开始时清空识别历史；无动态分配。 */
void voip_tone_guard_reset(voip_tone_guard_t *guard);
/** @brief 分析解码后的 8 kHz PCM，不修改数据，不改变 SIP 状态。
 * @param[in,out] guard 当前播放流独占的识别状态。
 * @param[in] samples PCM 采样；数量可小于或大于 160。
 * @param[in] count 采样数。
 * @return true 应采用提示音档位；false 采用原通话档位。
 * @note 这是音频启发式识别，音乐、其他频率提示音和相似纯音可能无法区分。
 */
bool voip_tone_guard_process(voip_tone_guard_t *guard, const int16_t *samples, unsigned count);
#endif
