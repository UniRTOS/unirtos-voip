/** @file qcm_voip.h
 * @brief UniRTOS SIP/UDP and G.711 A-law call interface.
 * Blocking control calls must run in one application worker, never an ISR or AT callback.
 */
#ifndef QCM_VOIP_H
#define QCM_VOIP_H
#include <stdint.h>
#include <stdbool.h>
typedef struct {
    char server[16]; /**< SIP server IPv4 address; DNS/IPv6 not exposed in this interface. */
    uint16_t port;
    char account[32];
    char password[32]; /**< Runtime only; never returned by the status API. */
} qcm_voip_config_t;
typedef enum {
    QCM_VOIP_IDLE=0, QCM_VOIP_HOLD=1, QCM_VOIP_DIALING=2, QCM_VOIP_CONNECTED=3,
    QCM_VOIP_INCOMING=4, QCM_VOIP_WAITING=5, QCM_VOIP_ENDED=6, QCM_VOIP_ALERTING=7
} qcm_voip_call_state_t;
typedef struct {
    bool initialized;
    bool registered;
    uint8_t call_id;
    qcm_voip_call_state_t state;
    char peer[32];
    uint32_t tx_packets;
    uint32_t rx_packets;
    uint32_t capture_frames;
    uint32_t capture_errors;
} qcm_voip_status_t;
/** @enum qcm_voip_call_stage_e
 * @brief 呼叫建立的诊断阶段；与呼叫状态、AT 请求返回值分开。
 */
typedef enum
{
    QCM_VOIP_STAGE_NONE = 0, /*!< 尚无呼叫。 */
    QCM_VOIP_STAGE_DIAL, /*!< 创建并发送初始 INVITE。 */
    QCM_VOIP_STAGE_INCOMING, /*!< 收到来电 INVITE。 */
    QCM_VOIP_STAGE_REMOTE_SDP, /*!< 主叫解析应答 SDP。 */
    QCM_VOIP_STAGE_ACK_BUILD, /*!< 主叫生成 ACK。 */
    QCM_VOIP_STAGE_ACK_SEND, /*!< 主叫发送 ACK。 */
    QCM_VOIP_STAGE_MEDIA_QUEUE, /*!< 投递媒体启动请求。 */
    QCM_VOIP_STAGE_MEDIA_START, /*!< 创建 RTP 会话与媒体任务。 */
    QCM_VOIP_STAGE_MEDIA_READY, /*!< RTP 媒体任务已创建，不代表物理发声。 */
    QCM_VOIP_STAGE_ANSWER, /*!< 被叫发送 200 OK，随后等待 ACK。 */
    QCM_VOIP_STAGE_TIMEOUT, /*!< 本端呼叫计时器到期。 */
    QCM_VOIP_STAGE_TERMINATE, /*!< 终止呼叫失败，原始返回值保存于 fail_rc。 */
    QCM_VOIP_STAGE_CANCEL_WAIT, /*!< 已保留取消意图，等待 SIP 临时应答或处理 200。 */
    QCM_VOIP_STAGE_CANCEL_SENT, /*!< CANCEL 已提交 SIP 事务，不代表远端已结束。 */
    QCM_VOIP_STAGE_HANGUP_SENT /*!< BYE 或被叫拒接已提交 SIP 事务。 */
} qcm_voip_call_stage_e;

/** @struct qcm_voip_call_diag_t
 * @brief 最近一次呼叫的诊断快照；结束后保留，新呼叫开始时清零。
 */
typedef struct
{
    uint32_t attempt; /*!< 本次开机的呼叫尝试序号。 */
    uint32_t role; /*!< 0 无呼叫，1 主叫，2 被叫。 */
    int32_t cid; /*!< SIP 栈呼叫标识；与界面中的呼叫槽位不同。 */
    qcm_voip_call_stage_e stage; /*!< 最近执行的呼叫建立阶段。 */
    qcm_voip_call_stage_e fail_stage; /*!< 首个失败阶段；后续清理不覆盖。 */
    int32_t fail_rc; /*!< 首个失败的原始返回值，仅用于诊断。 */
    int32_t sip_event; /*!< 最近关联的 eXosip 事件编号；释放通知不覆盖终止事件。 */
    int32_t sip_code; /*!< 最近收到的 SIP 应答码；0 表示尚无应答。 */
    int32_t record_open; /*!< 0 未执行，1 打开成功，负值打开失败。 */
    int32_t play_open; /*!< 0 未执行，1 打开成功，负值打开失败。 */
    uint32_t send_errors; /*!< 本次 RTP 发送失败次数。 */
    uint32_t play_errors; /*!< 本次收到媒体后写入播放流失败次数。 */
} qcm_voip_call_diag_t;

/** @brief 获取最近一次呼叫诊断，不等待 SIP 任务或硬件。
 * @param[out] qcm_voip_call_diag_t *diag 输出快照；空指针时不执行。
 * @note 线程安全，普通任务调用；调用者拥有输出内存，不返回密码或报文内容。
 */
void qcm_voip_get_call_diag(qcm_voip_call_diag_t *diag);

/** @struct qcm_voip_net_diag_t
 * @brief 开机累计的来电与注册计数，用于区分未收到信令、忙线拒接和处理错误。
 */
typedef struct {
    uint32_t invites_rx; /*!< SIP 层收到的初始来电事件数。 */
    uint32_t incoming; /*!< 已成功通知应用的来电数。 */
    uint32_t busy_reject; /*!< 因真实活动呼叫而回复 486 的次数。 */
    uint32_t invite_errors; /*!< SDP、资源或其它拒接错误数。 */
    uint32_t queue_drops; /*!< 来电事件投递管理队列失败数。 */
    int32_t invite_cid; /*!< 最近处理的来电 SIP 编号。 */
    int32_t invite_result; /*!< 最近处理结果，0 成功、正值 SIP 码、负值本地错误。 */
    uint32_t reg_failures; /*!< 最终注册失败次数，不计可重试的鉴权挑战。 */
    int32_t reg_code; /*!< 最近注册结果的 SIP 码；0 为无响应。 */
} qcm_voip_net_diag_t;

/** @brief 取得来电和注册诊断快照。
 * @param[out] qcm_voip_net_diag_t *diag 调用者拥有的输出，可为空。
 * @return 无；只复制计数，不阻塞控制任务，不含敏感报文。
 */
void qcm_voip_get_net_diag(qcm_voip_net_diag_t *diag);

/** @brief Initialize/update the account while stopped; 0 succeeds, nonzero is an error. */
int qcm_voip_configure(const qcm_voip_config_t *config);
/** @brief 设置下次通话使用的播放等级与上行数字增益。
 * @param[in] unsigned volume 播放等级 0～11，6 为平台原始 PCM 幅度。
 * @param[in] unsigned capture_gain_percent 上行幅度百分比 100～800，超幅采样限幅。
 * @return 0 设置成功，-1 为参数无效或 SIP 服务尚未停止。
 * @note 仅串行控制任务在 SIP 停止时调用；每条 PCM 流打开时取得配置快照。
 */
int qcm_voip_set_audio(unsigned volume, unsigned capture_gain_percent);
/** @brief 配置主叫振铃阶段的下行播放等级。
 * @param[in] unsigned volume 0～11；0 静音，1 最轻，接通音量由 set_audio 单独设置。
 * @return 0 成功，-1 参数无效或服务已启动。
 * @note 仅 SIP 停止时由串行控制任务调用；不识别服务器在 200 之后混入的提示音。
 */
int qcm_voip_set_ringback_volume(unsigned volume);
/** @brief Register using the active bearer IPv4; blocks until success/failure. */
int qcm_voip_start(const char *local_ipv4);
/** @brief End active calls and stop SIP registration. Returns 0 on success. */
int qcm_voip_stop(void);
/** @brief Dial one peer account (1..31 bytes). Only one active call is exposed. */
int qcm_voip_dial(const char *peer);
/** @brief Accept the current incoming call. */
int qcm_voip_answer(void);
/** @brief Hang up/reject the current call. Idempotent when idle. */
int qcm_voip_hangup(void);
/** @brief Obtain a snapshot without blocking on the SIP worker. */
void qcm_voip_get_status(qcm_voip_status_t *status);
#endif
