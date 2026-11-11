#ifndef QCM_VOIP_PCM_H
#define QCM_VOIP_PCM_H
#include <stdint.h>
#include "qcm_voip.h"
int voip_pcm_init(void);
/** @brief 保存主叫回铃等级，不影响通话及麦克风配置。
 * @param[in] unsigned volume 播放等级 0～11。
 * @return 0 成功，-1 超出范围；失败保留原值。
 */
int voip_pcm_set_ringback_volume(unsigned volume);
/** @brief 按当前 SIP 状态选择下一条播放流的等级。
 * @param[in] bool enabled true 为主叫振铃，false 为正常通话。
 * @return 无；只更新短临界区中的状态，不调用硬件。
 */
void voip_pcm_use_ringback(bool enabled);
/** @brief 保存 PCM 流打开时使用的播放等级及上行数字增益。
 * @param[in] unsigned volume 播放等级 0～11。
 * @param[in] unsigned capture_gain_percent 上行幅度百分比 100～800。
 * @return 0 成功，-1 参数无效；失败不改变原配置。
 * @note 由组件在 SIP 停止时调用；短临界区保护配置快照，不执行硬件操作。
 */
int voip_pcm_set_audio(unsigned volume, unsigned capture_gain_percent);
int voip_pcm_open(void **out, int rate, unsigned channels, int record);
int voip_pcm_close(void *handle);
/** @brief 交还句柄；即使关闭失败，调用者也不得再使用或重试该句柄。 */
int voip_pcm_dispose(void *handle);
int voip_pcm_read_alaw(void *handle, uint8_t *data, unsigned size);
int voip_pcm_write_alaw(void *handle, const uint8_t *data, unsigned size);

#endif
