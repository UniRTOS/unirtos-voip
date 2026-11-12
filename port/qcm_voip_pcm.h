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
/** @brief 保存单路通话状态及主被叫角色；不操作硬件。 */
void voip_pcm_set_call_state(qcm_voip_call_state_t state);
/** @brief 保存可选的提前应答识别开关；SIP 停止时调用。 */
void voip_pcm_set_answered_tone_guard(bool enabled);
/** @brief 获取当前播放诊断，不含累计值。 */
void voip_pcm_get_audio_diag(qcm_voip_audio_diag_t *diag);
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
