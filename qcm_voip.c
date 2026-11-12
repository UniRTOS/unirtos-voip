#include "qcm_voip.h"
#include "qcm_voip_port.h"
#include "qcm_voip_pcm.h"
#include "qosa_voip.h"

static qcm_voip_status_t status;
static bool started;
extern void quec_voip_media_counters(uint32_t *tx,uint32_t *rx,uint32_t *capture,uint32_t *errors);
static void sip_state(void *ctx,BOOL registered)
{
    (void)ctx;
    qosa_enter_critical(); status.registered=registered; qosa_exit_critical();
}
/** @brief 更新单路通话快照，隔离其它槽位的迟到结束通知。
 * @param[in] void *ctx 未使用的回调上下文。
 * @param[in] quec_voip_call_t *call 管理任务同步上报的呼叫槽位及状态。
 * @return 无。
 * @note 底层按 SIP CID 查找槽位；旧 CID 的清理不能覆盖另一槽位的新呼叫。
 */
static void call_state(void *ctx,quec_voip_call_t *call)
{
    (void)ctx;
    if (!call) return;
    qcm_voip_call_state_t next_state = (qcm_voip_call_state_t)call->state;
    qosa_enter_critical();
    if ((next_state == QCM_VOIP_ENDED || next_state == QCM_VOIP_IDLE) &&
        call->index != status.call_id) {
        qosa_exit_critical();
        return;
    }
    status.call_id=call->index;
    status.state=next_state;
    memcpy(status.peer,call->user_name,sizeof(status.peer));
    status.peer[sizeof(status.peer)-1]=0;
    qosa_exit_critical();
    // 仅切换适配层配置，不在状态回调里执行硬件或阻塞操作。
    voip_pcm_set_call_state(next_state);
}
static bool active(void)
{
    qcm_voip_status_t current;
    qcm_voip_get_status(&current);
    return current.state!=QCM_VOIP_IDLE && current.state!=QCM_VOIP_ENDED;
}
static bool valid_account(const char *text)
{
    size_t n;
    if (!text || !text[0]) return false;
    for (n=0;n<32 && text[n];n++) {
        unsigned char c=text[n];
        if (!((c>='0' && c<='9') || (c>='A' && c<='Z') || (c>='a' && c<='z') || c=='_' || c=='-' || c=='.' || c=='+')) return false;
    }
    return n<32;
}
/** @brief 设置下次通话使用的播放等级与上行数字增益。
 * @param[in] unsigned volume 播放等级 0～11，6 为平台原始 PCM 幅度。
 * @param[in] unsigned capture_gain_percent 上行幅度百分比 100～800，超幅采样限幅。
 * @return 0 设置成功，-1 为参数无效或 SIP 服务尚未停止。
 * @note 仅串行控制任务在 SIP 停止时调用；每条 PCM 流打开时取得配置快照。
 */
int qcm_voip_set_audio(unsigned volume, unsigned capture_gain_percent)
{
    if (started || active()) return -1;
    return voip_pcm_set_audio(volume, capture_gain_percent);
}

/** @brief 配置主叫振铃阶段的下行播放等级。
 * @param[in] unsigned volume 0～11；0 静音，1 为 PCM 幅度 1/32，6 为原幅度；接通音量由 set_audio 单独设置。
 * @return 0 成功，-1 参数无效或服务已启动。
 * @note 仅 SIP 停止时由串行控制任务调用；提前 200 的提示音可由 answered_tone_guard 识别后使用此档位。
 */
int qcm_voip_set_ringback_volume(unsigned volume)
{
    if (started || active()) return -1;
    return voip_pcm_set_ringback_volume(volume);
}

/** @brief 仅在 SIP 停止时启用主叫提前应答提示音兼容。 */
int qcm_voip_set_answered_tone_guard(bool enabled)
{
    if (started || active()) return -1;
    voip_pcm_set_answered_tone_guard(enabled);
    return 0;
}

/** @brief 提供主动查询快照，不产生自动通知。 */
void qcm_voip_get_audio_diag(qcm_voip_audio_diag_t *diag)
{
    voip_pcm_get_audio_diag(diag);
}

int qcm_voip_configure(const qcm_voip_config_t *config)
{
    quec_voip_config_t cfg={0};
    int ret;
    if (!config || started || active() || !config->port ||
        !memchr(config->server,0,sizeof(config->server)) ||
        !memchr(config->account,0,sizeof(config->account)) ||
        !memchr(config->password,0,sizeof(config->password)) || !valid_account(config->account)) return -1;
    if (!ipaddr_aton(config->server,&cfg.server.addr) || !IP_IS_V4(&cfg.server.addr) || ip_addr_isany(&cfg.server.addr)) return -1;
    cfg.server.port=config->port;
    cfg.simid=0; cfg.pdp_cid=1;
    memcpy(cfg.account,config->account,sizeof(cfg.account));
    memcpy(cfg.password,config->password,sizeof(cfg.password));
    strcpy(cfg.agent,"UniRTOS-VoIP");
    cfg.expiry=300; cfg.keep_alive_time=30000;
    cfg.ip_rewrite=true; cfg.ring_timeout=60000; cfg.ring_back_timeout=60000;
    cfg.sip_state_cb=sip_state; cfg.call_state_cb=call_state;
    ret=voip_pcm_init();
    if (!ret) ret=quec_voip_init(&cfg);
    memset(cfg.password,0,sizeof(cfg.password));
    if (!ret) { qosa_enter_critical(); status.initialized=true; qosa_exit_critical(); }
    return ret;
}
int qcm_voip_start(const char *local_ipv4)
{
    int ret;
    if (!status.initialized || started || !local_ipv4) return -1;
    ret=quec_voip_set_local_ip(local_ipv4);
    if (!ret) ret=quec_voip_start(true);
    if (!ret) started=true;
    return ret;
}
int qcm_voip_stop(void)
{
    int ret;
    if (!status.initialized) return 0;
    ret=quec_voip_stop();
    if (!ret) { started=false; qosa_enter_critical(); status.registered=false; status.state=QCM_VOIP_IDLE; qosa_exit_critical(); voip_pcm_set_call_state(QCM_VOIP_IDLE); }
    return ret;
}
int qcm_voip_dial(const char *peer)
{
    size_t len;
    if (!valid_account(peer) || !status.registered || active()) return -1;
    len=strnlen(peer,32);
    if (!len || len>=32) return -1;
    return quec_voip_make_call((void *)peer,(unsigned char)len);
}
int qcm_voip_answer(void)
{
    qcm_voip_status_t current;
    qcm_voip_get_status(&current);
    if (current.state!=QCM_VOIP_INCOMING && current.state!=QCM_VOIP_WAITING) return -1;
    return quec_voip_call_op(current.call_id,true);
}
int qcm_voip_hangup(void)
{
    qcm_voip_status_t current;
    qcm_voip_get_status(&current);
    if (current.state==QCM_VOIP_IDLE || current.state==QCM_VOIP_ENDED) return 0;
    return quec_voip_call_op(current.call_id,false);
}
void qcm_voip_get_status(qcm_voip_status_t *out)
{
    if (!out) return;
    qosa_enter_critical();
    *out=status;
    quec_voip_media_counters(&out->tx_packets,&out->rx_packets,&out->capture_frames,&out->capture_errors);
    qosa_exit_critical();
}

/** @brief 复制 SIP 组件保存的呼叫诊断快照。
 * @param[out] qcm_voip_call_diag_t *diag 调用者持有的输出内存，可为空。
 * @note 普通任务调用；底层以短临界区保护，不等待网络或音频资源。
 */
void qcm_voip_get_call_diag(qcm_voip_call_diag_t *diag)
{
    extern void quec_voip_get_call_diag(qcm_voip_call_diag_t *out);
    quec_voip_get_call_diag(diag);
}

/** @brief 将来电与注册计数从 SIP 组件复制给应用。
 * @param[out] qcm_voip_net_diag_t *diag 调用者拥有的输出，可为空。
 * @return 无；普通任务调用，不访问网络。
 */
void qcm_voip_get_net_diag(qcm_voip_net_diag_t *diag)
{
    extern void quec_voip_get_net_diag(qcm_voip_net_diag_t *out);
    quec_voip_get_net_diag(diag);
}
