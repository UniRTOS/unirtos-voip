/** @file qcm_voip_sdp.h
 * @brief PC SIP 客户端互通所需的 SDP 默认值和边界检查。
 */
#ifndef QCM_VOIP_SDP_H
#define QCM_VOIP_SDP_H
#include "qcm_voip_port.h"
#include "sdp_message.h"
/** @brief 解析 RTCP 地址，缺少 a=rtcp 时使用 RTP 端口加一。
 * @param[in] media 音频媒体描述。
 * @param[out] rtcp 返回 RTCP 地址；全零地址表示使用 RTP 地址。
 * @return 0 成功，-1 表示格式或端口范围错误。
 */
int voip_sdp_audio_rtcp(sdp_media_t *media, quec_media_rtcp_attr_t *rtcp);
/** @brief 先查媒体级再查会话级方向；都未指定时使用 sendrecv。
 * @param[in] session 会话描述。
 * @param[in] media 音频媒体描述。
 * @param[out] direction 远端收发方向。
 * @return 0 成功，-1 参数错误。
 */
int voip_sdp_direction(sdp_message_t *session, sdp_media_t *media, quec_media_direction_e *direction);
/** @brief 判断是否可使用未写 rtpmap 的静态 PCMA PT8。
 * @param[in] media 音频媒体描述。
 * @return true 表示列出 PT8 且未显式覆盖其 rtpmap。
 */
bool voip_sdp_static_pcma(sdp_media_t *media);
#endif /* QCM_VOIP_SDP_H */
