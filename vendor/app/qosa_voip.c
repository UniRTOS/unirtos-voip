/*
 * @file quec_voip.c
 * @brief VoIP SIP call and media management implementation.
 * @author evin.zhu
 * @date 2026-05-29
 *
 * @copyright Copyright (c) 2026 Quectel Wireless Solution, Co., Ltd.
 * All Rights Reserved.
 * Quectel Wireless Solution Proprietary and Confidential.
 */

//#include "quec_nw.h"
//#include "osa_q_sys.h"
//#include "quec_compat.h"
// #include "quec_nw.h"
// #include "osa_q_sys.h"
//#include "quec_nw.h"
//#include "osa_q_sys.h"
//#include "quec_compat.h"
#include "stdio.h"
#include "string.h"
#include <stdlib.h>
// #include "osa_log.h"
// #include "quec_pro_config.h"


#include "sockets.h"
#if 1
// #include "osip2/osip_mt.h"
#include "eXosip2/eXosip.h"
#include "eXosip2.h"

// #include "eXosip2/eX_setup.h"

#include <ortp/ortp.h>
#include <ortp/payloadtype.h>
#include <ortp/telephonyevents.h>
// #include "quec_nw.h"


#include "rtp_g711.h"

#ifdef ERROR_INFO
#undef ERROR_INFO
#endif





// #include "ql_audio.h"
#include "qosa_voip.h"
#include "qcm_voip_sdp.h"
#include "qcm_voip_pcm.h"
#include "qcm_voip_platform.h"


#include "sdp_message.h"

#include "quec_cust_feature.h"

#define voip_log(msg, args...) CPUartLogPrintf("voip[%d] " msg, __LINE__, ##args)

#define QL_PCM_BLOCK_FLAG      (0x01)
#define QL_PCM_NONBLOCK_FLAG   (0x02)
#define QL_PCM_READ_FLAG       (0x04)
#define QL_PCM_WRITE_FLAG      (0x08)
#define QUEC_VOIP_RTP_AUDIO_BUFFER_SIZE 320

static volatile int keepRunning = 1;
const char          sip_allow[]
    = {"INVITE, ACK, BYE, CANCEL, OPTIONS, MESSAGE, INFO, UPDATE, REGISTER, "
       "REFER, NOTIFY, PUBLISH, SUBSCRIBE"};

static const quec_rtp_standard_pt_map_t standard_pt_map[] = {
    /* Audio Codecs (0-22) */
    {"PCMU", 0},  /* μ-law, 8000 Hz, 64 kbps */
    {"GSM", 3},   /* GSM, 8000 Hz, 13.5 kbps */
    {"G723", 4},  /* G.723.1, 8000 Hz, 6.3 kbps */
    {"LPC", 7},   /* LPC, 8000 Hz, 5.6 kbps */
    {"PCMA", 8},  /* A-law, 8000 Hz, 64 kbps */
    {"G722", 9},  /* G.722, 16000 Hz, 64 kbps */
    {"L16", 11},  /* L16 Mono, 44100 Hz */
    {"CN", 13},   /* Comfort Noise, 8000 Hz */
    {"G729", 18}, /* G.729, 8000 Hz, 8 kbps */
    /* Telephone Events and Extensions (101+) */
    {"TELEPHONE-EVENT", 101}, /* DTMF/Telephone Events, 8000 Hz */
    /* End of table */
    {NULL, -1}};

/* Static variable to track next available dynamic payload type (97-127) */
static int next_dynamic_pt = 97;
typedef enum
{
    QUEC_VOIP_EVENT_INIT,
    QUEC_VOIP_EVENT_START,
    QUEC_VOIP_EVENT_STOP,
    QUEC_VOIP_EVENT_REGIST,
    QUEC_VOIP_EVENT_REGIST_DONE,
    QUEC_VOIP_EVENT_UNREGIST,
    QUEC_VOIP_EVENT_NETWORK_CHECK,
    QUEC_VOIP_EVENT_CALL_INVITE,
    QUEC_VOIP_EVENT_CALL_ACCEPT,
    QUEC_VOIP_EVENT_CALL_RELEASE,
    QUEC_VOIP_EVENT_CALL_REJECT,
    QUEC_VOIP_EVENT_CALL_WAIT,
    QUEC_VOIP_EVENT_CALL_HOLD,
    QUEC_VOIP_EVENT_CALL_REFER,
    QUEC_VOIP_EVENT_CALL_MO,
    QUEC_VOIP_EVENT_CALL_MO_ALERTING,
    QUEC_VOIP_EVENT_CALL_MO_ANSWERED,
    QUEC_VOIP_EVENT_CALL_TIMEOUT,
    QUEC_VOIP_EVENT_CALL_MEDIA_CREAT,
    QUEC_VOIP_EVENT_CALL_MEDIA_DESTROY,
    QUEC_VOIP_EVENT_CFG_SET,
    QUEC_VOIP_EVENT_CFG_GET,
    QUEC_VOIP_EVENT_REGIST_FAILED, /*!< 最终注册失败由管理任务更新在线状态并重试。 */
    QUEC_VOIP_EVENT_MAX
} quec_voip_event_e;
typedef struct
{
    quec_sem_t sem;
    UINT32     param1;
    UINT32     param2;
    UINT32     param3;
    UINT32     param4;
    void      *pParam;
} quec_voip_sem_t;

typedef struct
{
    unsigned int ring;
    quec_timer_t ring_m;
    unsigned int ring_back;
    quec_timer_t ring_back_m;
} quec_voip_sip_time_t;

typedef struct
{
    quec_voip_call_state_e state;
    quec_media_mode_e      rtp_mode;
    BOOL                   mo_call;
    BOOL                   cancel_requested; /*!< 主叫请求取消；等待临时应答或处理晚到的 200。 */
    BOOL                   cancel_sent; /*!< 终止请求已交给 SIP 栈，防止重复发送。 */
    int                    call_id;
    int                    tid;
    int                    did;
    int                    index;
    char                   user_name[32];
    void                  *rtp_context;
    quec_voip_addr_t public;
    quec_voip_media_t    media;
    quec_voip_sip_time_t timer;
    quec_task_t          recv_task;
    quec_task_t          send_task;
    OrtpEvDispatcher    *d;
    volatile bool stop_rx;
    volatile bool stop_tx;
} quec_voip_call_session_t;

typedef struct
{
    BOOL             enable;
    quec_voip_addr_t server;
    unsigned char    pdp_cid;
    unsigned char    simid;
    char             account[32];
    char             password[32];
    char             agent[32];
    unsigned short   expiry;
    unsigned short   keep_alive_time;
    BOOL             enable_masquerade;
    BOOL             auto_connect;
    unsigned int     ring_timeout;
    unsigned int     ring_back_timeout;
    void            *sip_cb_ctx;
    void (*sip_state_cb)(void *ctx, BOOL state);
    void *call_cb_ctx;
    void (*call_state_cb)(void *ctx, quec_voip_call_t *call_info);
} quec_voip_sip_config_t;

typedef struct
{
    quec_voip_sip_config_t   sip_cfg;
    void                    *sip_context;
    quec_voip_addr_t         public_ip;
    quec_voip_addr_t         local;
    BOOL                     is_regist;
    int                      reg_id; /*!< 当前注册对象编号，重试复用同一 UDP 监听和注册。 */
    BOOL                     unregistering; /*!< 停止期间禁止新来电及普通注册成功回调。 */
    BOOL                     unregister_sent; /*!< 本次注销已入栈；忽略此前的 NAT 删除应答。 */
    int                      unregister_result; /*!< 当前注销应答/错误；两任务以短临界区同步。 */
    void                    *pending_start_event;
    quec_timer_t             check_timer;
	unsigned int             check_time;
	int             register_check_time;
    voip_thread_t            sip_task;
    quec_voip_rtp_profile_t *support_profile;
} quec_voip_sip_info_t;

typedef struct
{
    quec_voip_call_session_t call[6];
    quec_voip_sip_info_t     sip_info;
} quec_voip_t;

typedef struct
{
    quec_voip_event_e event;
    quec_voip_t      *voip;
    void             *p_data;
} quec_voip_manage_t;

typedef struct
{
    quec_sem_t      sem;
    UINT8          *data;
    UINT16          len;
    quec_voip_err_e ret;
} quec_voip_event_data_t;

typedef struct
{
    quec_voip_cfg_item_e item;
    unsigned short       len;
    unsigned char        data[sizeof(quec_voip_config_t)];
} quec_voip_cfg_req_t;

typedef struct
{
    BOOL auto_regist;
} quec_voip_start_req_t;



static void            quec_voip_sip_stop_check_timer(quec_voip_t *voip);
static BOOL            quec_voip_sip_start_check_timer(quec_voip_t *voip);
static int quec_voip_regist_state(quec_voip_t *voip, BOOL state);
static quec_voip_err_e quec_voip_cfg_set_inner(quec_voip_t *voip, quec_voip_cfg_item_e item, const void *value, unsigned short len);
static quec_voip_err_e quec_voip_cfg_get_inner(quec_voip_t *voip, quec_voip_cfg_item_e item, void *value, unsigned short *len);
int                    quec_voip_call_op(unsigned char call_id, BOOL accept);

/*
 * Get standard RTP payload type number from MIME type.
 * Returns the standard payload type (0-96) if found, otherwise -1.
 */
static int quec_get_standard_pt_from_mime(const char *mime_type)
{
    int i = 0;
    if (mime_type == NULL || strlen(mime_type) == 0)
        return -1;

    for (i = 0; standard_pt_map[i].mime_type != NULL; i++)
    {
        if (osip_strncasecmp(mime_type, standard_pt_map[i].mime_type, strlen(standard_pt_map[i].mime_type)) == 0)
        {
            return standard_pt_map[i].payload_type;
        }
    }
    return -1;
}

static int pcma_open(void **fd, int sample_rate, unsigned char chn, unsigned char flag, void *argv);
static int pcma_close(void *fd);
static int pcma_read(void *fd, void *data, unsigned short len, quec_voip_rtp_payload_t *pt);
static int pcma_write(void *fd, void *data, unsigned short len, quec_voip_rtp_payload_t *pt);
static int tel_event_open(void **fd, int sample_rate, unsigned char chn, unsigned char flag, void *argv);
static int tel_event_close(void *fd);
static int tel_event_write(void *fd, void *data, unsigned short len, quec_voip_rtp_payload_t *pt);
static int tel_event_read(void *fd, void *data, unsigned short len, quec_voip_rtp_payload_t *pt);

quec_voip_media_profile_t support_table[] = {
    {&quec_payload_type_pcma8000, {pcma_open, pcma_close, pcma_write, pcma_read, NULL, NULL}},
    {&quec_payload_type_telephone_event, {tel_event_open, tel_event_close, tel_event_write, tel_event_read, NULL, NULL}},
};
	
quec_voip_t *quec_voip = NULL;

static qcm_voip_call_diag_t g_call_diag;
static qcm_voip_net_diag_t g_net_diag; /*!< 开机累计的来电与注册诊断，不含账号、口令或报文。 */

/** @brief 读取来电投递和注册失败计数。
 * @param[out] qcm_voip_net_diag_t *out 调用者拥有的输出，可为空。
 * @return 无；短临界区内复制，不等待网络或媒体线程。
 */
void quec_voip_get_net_diag(qcm_voip_net_diag_t *out)
{
    if (!out) return;
    qosa_enter_critical(); *out = g_net_diag; qosa_exit_critical();
}

/** @brief 记录管理任务处理来电的结果。
 * @param[in] int cid SIP 呼叫编号。
 * @param[in] int result 0 为已交给界面，正值为拒接应答码，负值为本地错误。
 * @return 无；累计计数不随下一通呼叫清零。
 */
static void voip_incoming_result(int cid, int result)
{
    qosa_enter_critical();
    g_net_diag.invite_cid = cid;
    g_net_diag.invite_result = result;
    if (result == 0) ++g_net_diag.incoming;
    else if (result == 486) ++g_net_diag.busy_reject;
    else ++g_net_diag.invite_errors;
    qosa_exit_critical();
}

/** @brief 为新呼叫清空诊断，保留单调尝试序号。
 * @param[in] unsigned role 1 主叫，2 被叫。
 * @param[in] int cid SIP 呼叫标识，初始主叫尚未分配时为 0。
 */
static void voip_call_diag_begin(unsigned role, int cid)
{
    uint32_t attempt = 0;
    qosa_enter_critical();
    attempt = g_call_diag.attempt + 1U;
    qosa_memset(&g_call_diag, 0, sizeof(g_call_diag));
    g_call_diag.attempt = attempt;
    g_call_diag.role = role;
    g_call_diag.cid = cid;
    qosa_exit_critical();
}

/** @brief 记录建立阶段及首个失败，清理阶段不得覆盖原始原因。
 * @param[in] qcm_voip_call_stage_e stage 当前诊断阶段。
 * @param[in] int rc 本阶段原始结果，0 成功。
 */
static void voip_call_diag_step(qcm_voip_call_stage_e stage, int rc)
{
    qosa_enter_critical();
    g_call_diag.stage = stage;
    if (rc != 0 && g_call_diag.fail_rc == 0)
    {
        g_call_diag.fail_stage = stage;
        g_call_diag.fail_rc = rc;
    }
    qosa_exit_critical();
}

/** @brief 保存当前呼叫的 SIP 事件与应答码，不保存鉴权或报文正文。
 * @param[in] const eXosip_event_t *event SIP 事件线程持有的有效事件。
 */
static void voip_call_diag_event(const eXosip_event_t *event)
{
    qosa_enter_critical();
    if (event->cid > 0 && g_call_diag.attempt != 0 &&
        (g_call_diag.cid == 0 || g_call_diag.cid == event->cid))
    {
        /* 释放只是清理通知，保留之前的 BYE、取消或失败事件。 */
        if (event->type == EXOSIP_CALL_RELEASED &&
            (g_call_diag.sip_event == EXOSIP_CALL_CLOSED ||
             g_call_diag.sip_event == EXOSIP_CALL_REQUESTFAILURE ||
             g_call_diag.sip_event == EXOSIP_CALL_SERVERFAILURE ||
             g_call_diag.sip_event == EXOSIP_CALL_GLOBALFAILURE ||
             g_call_diag.sip_event == EXOSIP_CALL_NOANSWER ||
             g_call_diag.sip_event == EXOSIP_CALL_CANCELLED))
        {
            qosa_exit_critical();
            return;
        }
        g_call_diag.cid = event->cid;
        g_call_diag.sip_event = event->type;
        if (event->response != NULL)
        {
            g_call_diag.sip_code = event->response->status_code;
        }
    }
    qosa_exit_critical();
}

/** @brief 记录媒体流打开结果；不在音频热路径打印日志。
 * @param[in] bool record true 为录音，false 为播放。
 * @param[in] int rc 打开接口返回值，0 表示成功。
 */
static void voip_call_diag_open(bool record, int rc)
{
    qosa_enter_critical();
    if (record)
    {
        g_call_diag.record_open = rc == 0 ? 1 : -1;
    }
    else
    {
        g_call_diag.play_open = rc == 0 ? 1 : -1;
    }
    qosa_exit_critical();
    if (rc != 0)
    {
        voip_call_diag_step(QCM_VOIP_STAGE_MEDIA_START, rc);
    }
}

/** @brief 读取单次呼叫诊断，挂断后仍可查询。
 * @param[out] qcm_voip_call_diag_t *out 调用者拥有的快照，可为空。
 * @note 普通任务可并发调用；只复制固定长度对象。
 */
void quec_voip_get_call_diag(qcm_voip_call_diag_t *out)
{
    if (out == NULL)
    {
        return;
    }
    qosa_enter_critical();
    *out = g_call_diag;
    qosa_exit_critical();
}



/*
 * Set support profile for media with proper payload type allocation.
 *
 * Algorithm:
 *   1. Check if payload MIME type is in the standard mapping (0-96)
 *   2. If standard, use the mapped payload type number
 *   3. If not standard (dynamic), allocate from 97-127 sequentially
 *   4. Set the payload in the profile at the determined index
 *
 * Returns:
 *   0 on success
 *  -1 on failure
 */
static int quec_voip_set_support_profile(quec_voip_rtp_profile_t **profile, quec_voip_media_profile_t *info)
{
    quec_voip_rtp_profile_t      *set_profile = NULL;
    quec_voip_rtp_payload_t      *payload = NULL;
    quec_voip_media_profile_cb_t *cb = NULL;
    int                           pos = 0;
    int                           standard_pt = -1;

    if (profile == NULL || info == NULL || info->payload == NULL || info->cb.media_open == NULL || info->cb.media_close == NULL || info->cb.media_read == NULL
        || info->cb.media_write == NULL)
    {
        voip_log("ERROR: invalid parameter\n");
        return -1;
    }

    set_profile = *profile;
    payload = info->payload;

    if (payload->mime_type == NULL || strlen(payload->mime_type) == 0)
    {
        voip_log("ERROR: payload mime_type is empty\n");
        return -1;
    }

    /* Create new profile if not exists */
    if (set_profile == NULL)
    {
        set_profile = rtp_profile_new("voip_support_profile");
        if (set_profile == NULL)
        {
            voip_log("ERROR: failed to create RTP profile\n");
            return -1;
        }
        voip_log("Created new RTP profile=%p\n", set_profile);
        next_dynamic_pt = 97; /* Reset dynamic PT counter for new profile */
    }

    /* Determine payload type number */
    standard_pt = quec_get_standard_pt_from_mime(payload->mime_type);
    if (standard_pt >= 0)
    {
        /* Standard payload type - use mapped number directly */
        pos = standard_pt;
        voip_log("Using standard PT %d for MIME type '%s'\n", pos, payload->mime_type);
    }
    else
    {
        /* Dynamic payload type - allocate sequentially from 97 */
        if (next_dynamic_pt > 127)
        {
            voip_log("ERROR: dynamic PT exhausted (>127)\n");
            goto err_exit;
        }
        pos = next_dynamic_pt;
        voip_log("Allocating dynamic PT %d for MIME type '%s'\n", pos, payload->mime_type);
        next_dynamic_pt++;
    }

    payload->user_data = (void *)&info->cb;
    payload->user_data_len = sizeof(quec_voip_media_profile_cb_t);

    /* Set payload in profile at determined position */
    if (rtp_profile_set_payload_new(set_profile, pos, payload))
    {
        voip_log("ERROR: failed to set payload at index %d\n", pos);
        goto err_exit;
    }

    voip_log("Successfully set payload '%s' at PT %d in profile %p\n", payload->mime_type, pos, set_profile);

    *profile = set_profile;
    return 0;

err_exit:
    if (*profile == NULL && set_profile)
    {
        rtp_profile_destroy(set_profile);
    }
    return -1;
}

/*
 * Check if audio_sdp contains any PayloadType supported by the RTP profile.
 *
 * Matching rule:
 *   1. Standard PT (< 96): payload index must exist in support_profile and the
 *      payload's mime_type/clock_rate must match the SDP rtpmap.
 *   2. Dynamic PT (>= 96): match support_profile by mime_type/clock_rate only,
 *      regardless of the payload index advertised in SDP.
 *
 * Updates list with all supported local PT indexes and returns the number of
 * collected entries.
 *
 * Returns:
 *   >0 number of supported PT indexes stored in list
 *    0 if no supported payload is found
 *   -1 if parameters are invalid
 */
static int quec_audio_sdp_has_supported_payload(sdp_media_t *audio_sdp, quec_voip_rtp_profile_t *support_profile, unsigned char *list, unsigned char list_len)
{
    int                      i = 0, j = 0;
    int                      payload_idx = 0;
    int                      clock_rate = 0;
    int                      support_count = 0;
    char                    *payload_str = NULL;
    char                    *attr_field = NULL;
    char                    *attr_value = NULL;
    sdp_attribute_t         *attr = NULL;
    quec_voip_rtp_payload_t *pt = NULL;
    int                      find_idx = -1;
    int                      k = 0;
    char                     mime_type[64] = {0};
    char                     rtpmap_payload_idx_str[16] = {0};

    if (audio_sdp == NULL || support_profile == NULL || list == NULL || list_len == 0)
    {
        voip_log("ERROR: audio_sdp=%p, support_profile=%p, list=%p, list_len=%d\n", audio_sdp, support_profile, list, list_len);
        return -1;
    }

    memset(list, 0, list_len);
    /* RFC 3551: PT8 has a static PCMA/8000 mapping; rtpmap is optional. */
    if (voip_sdp_static_pcma(audio_sdp) && rtp_profile_get_payload(support_profile, 8))
        list[support_count++] = 8;

    /* Iterate through all payload types in the audio SDP media */
    for (i = 0; i < audio_sdp->m_payloads.nb_elt; i++)
    {
        payload_str = (char *)osip_list_get(&audio_sdp->m_payloads, i);
        if (payload_str == NULL)
            continue;

        /* Convert payload string (e.g., "0", "8", "101") to integer */
        payload_idx = atoi(payload_str);
        memset(mime_type, 0, sizeof(mime_type));
        clock_rate = 0;

        /* Search for rtpmap attribute matching this payload index */
        for (j = 0; j < audio_sdp->a_attributes.nb_elt; j++)
        {
            attr = (sdp_attribute_t *)osip_list_get(&audio_sdp->a_attributes, j);
            if (attr == NULL || attr->a_att_field == NULL || attr->a_att_value == NULL)
                continue;

            attr_field = attr->a_att_field;
            attr_value = attr->a_att_value;

            /* Check if this is a rtpmap attribute */
            if (osip_strncasecmp(attr_field, "rtpmap", 6) != 0)
                continue;

            /* Parse rtpmap format: "<payload_idx>
       * <mime_type>/<clock_rate>[/<encoding_params>]" */
            /* e.g., "0 PCMU/8000" or "101 telephone-events/8000" */
            memset(rtpmap_payload_idx_str, 0, sizeof(rtpmap_payload_idx_str));

            /* Extract payload index from rtpmap value */
            sscanf(attr_value, "%15s", rtpmap_payload_idx_str);
            if (atoi(rtpmap_payload_idx_str) != payload_idx)
                continue; /* This rtpmap does not match current payload */

            /* Extract MIME type and clock rate */
            char *mime_start = strchr(attr_value, ' ');
            if (mime_start == NULL)
                continue;
            mime_start++; /* Skip the space */

            char *rate_start = strchr(mime_start, '/');
            if (rate_start == NULL)
                continue;

            /* Copy MIME type */
            int mime_len = rate_start - mime_start;
            if (mime_len >= sizeof(mime_type))
                mime_len = sizeof(mime_type) - 1;
            strncpy(mime_type, mime_start, mime_len);
            mime_type[mime_len] = '\0';

            /* Extract clock rate */
            rate_start++; /* Skip the '/' */
            clock_rate = atoi(rate_start);

            if (payload_idx < 96)
            {
                pt = rtp_profile_get_payload(support_profile, payload_idx);
                if (pt != NULL && pt->mime_type != NULL && osip_strcasecmp(pt->mime_type, mime_type) == 0 && pt->clock_rate == clock_rate)
                {
                    for (k = 0; k < support_count; k++)
                    {
                        if (list[k] == payload_idx)
                            break;
                    }

                    if (k == support_count)
                    {
                        if (support_count >= list_len)
                        {
                            voip_log("Supported payload list is full: idx=%d, max=%d\n", payload_idx, list_len);
                            return support_count;
                        }

                        list[support_count++] = payload_idx;
                    }

                    voip_log(
                        "Found supported standard payload: idx=%d, mime=%s, "
                        "rate=%d, count=%d\n",
                        payload_idx,
                        mime_type,
                        clock_rate,
                        support_count
                    );
                }

                voip_log(
                    "Standard payload not supported: idx=%d, mime=%s, rate=%d, "
                    "profile_mime=%s, profile_rate=%d\n",
                    payload_idx,
                    mime_type,
                    clock_rate,
                    (pt != NULL && pt->mime_type != NULL) ? pt->mime_type : "NULL",
                    (pt != NULL) ? pt->clock_rate : -1
                );
            }
            else
            {
                find_idx = rtp_profile_find_payload_number(support_profile, mime_type, clock_rate, 1);
                if (find_idx >= 0)
                {
                    for (k = 0; k < support_count; k++)
                    {
                        if (list[k] == find_idx)
                            break;
                    }

                    if (k == support_count)
                    {
                        if (support_count >= list_len)
                        {
                            voip_log("Supported payload list is full: idx=%d, local=%d, max=%d\n", payload_idx, find_idx, list_len);
                            return support_count;
                        }

                        list[support_count++] = find_idx;
                    }

                    voip_log(
                        "Found supported dynamic payload: idx=%d, local=%d, "
                        "mime=%s, rate=%d, count=%d\n",
                        payload_idx,
                        find_idx,
                        mime_type,
                        clock_rate,
                        support_count
                    );
                }

                voip_log("Dynamic payload not supported: idx=%d, mime=%s, rate=%d\n", payload_idx, mime_type, clock_rate);
            }
            break; /* Found the rtpmap for this payload, no need to search further */
        }
    }

    voip_log("Supported payload count in audio_sdp: %d\n", support_count);
    return support_count;
}
int tel_event_open(void **fd, int sample_rate, unsigned char chn, unsigned char flag, void *argv)
{
    return QUEC_VOIP_OK;
}

int tel_event_close(void *fd)
{
    return QUEC_VOIP_OK;
}

int tel_event_write(void *fd, void *data, unsigned short len, quec_voip_rtp_payload_t *pt)
{
    return QUEC_VOIP_OK;
}

int tel_event_read(void *fd, void *data, unsigned short len, quec_voip_rtp_payload_t *pt)
{
    return QUEC_VOIP_OK;
}

static int quec_voip_set_public_addr(struct eXosip_t *context_sip, quec_voip_t *voip, quec_voip_call_session_t *call_info)
{
    
    struct eXosip_account_info *account = NULL;
    char                        addr_buff[64] = {0};

    if (context_sip == NULL || voip == NULL || call_info == NULL)
    {
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }
    voip_log("Setting public address for call_id=%d\n", call_info->call_id);
    account = (struct eXosip_account_info *)(&(context_sip->account_entries[0]));
    if (account != NULL && account->nat_ip != NULL && strlen(account->nat_ip) != 0)
    {
        voip_log("Using NAT IP from account: %s:%d\n", account->nat_ip, account->nat_port);
        strncpy(addr_buff, account->nat_ip, sizeof(addr_buff) - 1);
        call_info->public.port = account->nat_port ? account->nat_port : voip->sip_info.local.port;
    }
    else
    {
        voip_log("No NAT IP found in account, attempting to guess local IP\n");
        if (eXosip_guess_localip(context_sip, AF_INET, addr_buff, sizeof(addr_buff)))
        {
            ipaddr_ntoa_r(&voip->sip_info.local.addr, addr_buff, sizeof(addr_buff));
        }
        // Contact/Via 属于 SIP 信令，不能使用 SDP 中的 RTP 媒体端口。
        call_info->public.port = voip->sip_info.local.port;
    }

    if (ipaddr_aton(addr_buff, &call_info->public.addr) == 0)
    {
        return QUEC_VOIP_ERR_IP_GET_FAILED;
    }
    voip_log("Public address set to %s:%d for call_id=%d\n", addr_buff, call_info->public.port, call_info->call_id);
    return QUEC_VOIP_OK;
}

static int quec_voip_build_local_sdp(quec_voip_call_session_t *call_info, char *local_sdp, unsigned short sdp_len)
{
    int                      offset = 0;
    int                      i = 0;
    uint32_t                 random = 0;
    char                     addr_str[64] = {0};
    const char              *direction = "inactive";
    quec_voip_rtp_payload_t *pt = NULL;

    if (call_info == NULL || local_sdp == NULL || sdp_len == 0)
    {
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }

    switch (call_info->rtp_mode)
    {
        case RTP_SESSION_RECVONLY:
            direction = "recvonly";
            break;
        case RTP_SESSION_SENDONLY:
            direction = "sendonly";
            break;
        case RTP_SESSION_SENDRECV:
            direction = "sendrecv";
            break;
        default:
            direction = "inactive";
            break;
    }

    random = osip_build_random_number();
    ipaddr_ntoa_r(&call_info->public.addr, addr_str, sizeof(addr_str));

    offset = snprintf(
        local_sdp,
        sdp_len,
        "v=0\r\n"
        "o=QuecVoip %u %u IN IP4 %s\r\n"
        "s=QuecVoip 1.6.5\r\n"
        "t=0 0\r\n"
        "m=audio %d RTP/AVP",
        random,
        random + 2,
        addr_str,
        call_info->media.rtp_local.port
    );

    if (offset <= 0 || offset >= sdp_len)
    {
        /*
     * Resolve the advertised RTP address for the current call, preferring the
     * NAT address learned by eXosip when masquerading is enabled.
     */
        return QUEC_VOIP_ERR_NO_MEMORY;
    }

    for (i = 0; i < RTP_PROFILE_MAX_PAYLOADS; i++)
    {
        pt = (call_info->media.profile == NULL) ? NULL : call_info->media.profile->payload[i];
        if (pt == NULL)
        {
            continue;
        }

        offset += snprintf(local_sdp + offset, sdp_len - offset, " %d", i);
        if (offset >= sdp_len)
        {
            return QUEC_VOIP_ERR_NO_MEMORY;
        }
    }

    offset += snprintf(
        local_sdp + offset,
        sdp_len - offset,
        "\r\nc=IN IP4 %s\r\na=rtcp:%d IN IP4 "
        "%s\r\na=%s\r\na=ptime:20\r\na=maxptime:150\r\n",
        addr_str,
        call_info->media.rtcp_local.port,
        addr_str,
        direction
    );

    if (offset >= sdp_len)
    {
        /*
     * Build a local audio SDP from the current call media profile so both
     * initial INVITE and 200 OK reuse the same offer/answer description logic.
     */
        return QUEC_VOIP_ERR_NO_MEMORY;
    }

    for (i = 0; i < RTP_PROFILE_MAX_PAYLOADS; i++)
    {
        pt = (call_info->media.profile == NULL) ? NULL : call_info->media.profile->payload[i];
        if (pt == NULL)
        {
            continue;
        }

        offset += snprintf(local_sdp + offset, sdp_len - offset, "a=rtpmap:%d %s/%d\r\n", i, pt->mime_type, pt->clock_rate);
        if (offset >= sdp_len)
        {
            return QUEC_VOIP_ERR_NO_MEMORY;
        }

        if (pt->send_fmtp)
        {
            offset += snprintf(local_sdp + offset, sdp_len - offset, "a=fmtp:%d %s\r\n", i, pt->send_fmtp);
        }
        else if (pt->recv_fmtp)
        {
            offset += snprintf(local_sdp + offset, sdp_len - offset, "a=fmtp:%d %s\r\n", i, pt->recv_fmtp);
        }

        if (offset >= sdp_len)
        {
            return QUEC_VOIP_ERR_NO_MEMORY;
        }
    }

    return QUEC_VOIP_OK;
}

static int quec_voip_clone_support_profile(quec_voip_rtp_profile_t **profile, quec_voip_rtp_profile_t *support_profile, const char *name)
{
    unsigned char            i = 0;
    quec_voip_rtp_profile_t *new_profile = NULL;
    quec_voip_rtp_payload_t *pt = NULL;

    if (profile == NULL || support_profile == NULL || name == NULL)
    {
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }

    new_profile = rtp_profile_new(name);
    if (new_profile == NULL)
    {
        return QUEC_VOIP_ERR_NO_MEMORY;
    }

    for (i = 0; i < RTP_PROFILE_MAX_PAYLOADS; i++)
    {
        pt = support_profile->payload[i];
        if (pt == NULL)
        {
            continue;
        }

        rtp_profile_set_payload(new_profile, i, payload_type_clone(pt));
    }

    *profile = new_profile;
    return QUEC_VOIP_OK;
}

/* Stop the worker before releasing audio, RTP or profile objects it uses. */
static void voip_media_stop(quec_voip_call_session_t *call, bool transmit)
{
    quec_task_t *worker=transmit ? &call->send_task : &call->recv_task;
    if (transmit) call->stop_tx=true; else call->stop_rx=true;
    if (*worker) {
        voip_thread_join((voip_thread_t)*worker,NULL);
        *worker=NULL;
    }
}

/** @brief 停止媒体任务并释放 PCM/RTP，供重新协商及本地主动结束复用。
 * @param[in,out] quec_voip_call_session_t *call_info 当前会话，空指针时不处理。
 * @return 无。
 * @note 不持有 SIP 锁；先等待工作线程退出。保留 SIP CID/对话和 profile，供迟到信令清理。
 *       media_close 转交失败的 PCM 句柄给适配层回收链，下次打开前仍须成功回收。
 */
static void quec_voip_stop_media_for_profile_update(quec_voip_call_session_t *call_info)
{
    quec_voip_rtp_payload_t      *pt = NULL;
    quec_voip_media_profile_cb_t *media_cb = NULL;

    if (call_info == NULL)
    {
        return;
    }

    voip_media_stop(call_info,false);
    voip_media_stop(call_info,true);
    pt = call_info->media.pref_pt;
    if (pt != NULL)
    {
        media_cb = (quec_voip_media_profile_cb_t *)pt->user_data;
        if (media_cb != NULL && media_cb->media_close != NULL)
        {
            if (media_cb->fd_write != NULL)
            {
                media_cb->media_close(media_cb->fd_write);
                media_cb->fd_write = NULL;
            }
            if (media_cb->fd_read != NULL)
            {
                media_cb->media_close(media_cb->fd_read);
                media_cb->fd_read = NULL;
            }
        }
    }

    if (call_info->d != NULL)
    {
        ortp_ev_dispatcher_destroy(call_info->d);
        call_info->d = NULL;
    }
    if (call_info->rtp_context != NULL)
    {
        rtp_session_destroy((RtpSession *)call_info->rtp_context);
        call_info->rtp_context = NULL;
    }
    call_info->media.pref_pt = NULL;
}

/*
 * Parse the peer SDP from the answered dialog and rebuild the call media
 * profile with the payloads both sides can actually use.
 */
/*
 * Clone the global supported RTP profile into a per-call profile so payload
 * negotiation and runtime changes do not mutate the shared template.
 */
static int quec_voip_apply_remote_audio_sdp(quec_voip_t *voip, quec_voip_call_session_t *call_info, int did)
{
    struct eXosip_t         *context_sip = NULL;
    sdp_message_t           *remote_sdp = NULL;
    sdp_media_t             *audio_sdp = NULL;
    sdp_connection_t        *attr_c = NULL;
    quec_media_rtcp_attr_t   media_rtcp = {0};
    quec_media_direction_e   media_direct = QUEC_MEDIA_DIRECTION_SENDRECV;
    unsigned char            support_pt_list[128] = {0};
    int                      supported_pt_count = 0;
    int                      i = 0;
    char                    *audio_port = NULL;
    quec_voip_rtp_profile_t *call_profile = NULL;
    quec_voip_rtp_payload_t *pt = NULL;
    char                     profile_name[32] = {0};
    BOOL                     media_started = FALSE;

    if (voip == NULL || call_info == NULL || did <= 0)
    {
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }
	voip_log("quec_voip_apply_remote_audio_sdp start");
    context_sip = (struct eXosip_t *)voip->sip_info.sip_context;
    eXosip_lock(context_sip);
    remote_sdp = eXosip_get_remote_sdp(context_sip, did);
    eXosip_unlock(context_sip);
    if (!remote_sdp) return QUEC_VOIP_ERR_INVALID_PARAM;
    audio_sdp = eXosip_get_audio_media(remote_sdp);
    attr_c = eXosip_get_audio_connection(remote_sdp);
    audio_port = (audio_sdp == NULL) ? NULL : audio_sdp->m_port;

    if (audio_sdp == NULL || attr_c == NULL || attr_c->c_addr == NULL || audio_port == NULL || strlen(audio_port) == 0)
    {
        sdp_message_free(remote_sdp);
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }

    if (ipaddr_aton(attr_c->c_addr, &call_info->media.rtp_remote.addr) == 0)
    {
        sdp_message_free(remote_sdp);
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }
    call_info->media.rtp_remote.port = atoi(audio_port);

    if (voip_sdp_audio_rtcp(audio_sdp, &media_rtcp))
    {
        sdp_message_free(remote_sdp);
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }

    call_info->media.rtcp_remote.port = media_rtcp.port;
    if (ip_addr_isany(&media_rtcp.addr))
    {
        ip_addr_copy(call_info->media.rtcp_remote.addr, call_info->media.rtp_remote.addr);
    }
    else
    {
        ip_addr_copy(call_info->media.rtcp_remote.addr, media_rtcp.addr);
    }

    supported_pt_count = quec_audio_sdp_has_supported_payload(audio_sdp, voip->sip_info.support_profile, support_pt_list, sizeof(support_pt_list));
    if (supported_pt_count <= 0)
    {
        sdp_message_free(remote_sdp);
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }

    snprintf(profile_name, sizeof(profile_name), "call_%d", call_info->call_id);
    call_profile = rtp_profile_new(profile_name);
    if (call_profile == NULL)
    {
        sdp_message_free(remote_sdp);
        return QUEC_VOIP_ERR_NO_MEMORY;
    }

    for (i = 0; i < supported_pt_count; i++)
    {
        pt = rtp_profile_get_payload(voip->sip_info.support_profile, support_pt_list[i]);
        if (pt == NULL)
        {
            rtp_profile_destroy(call_profile);
            sdp_message_free(remote_sdp);
            return QUEC_VOIP_ERR_INVALID_PARAM;
        }
        rtp_profile_set_payload(call_profile, support_pt_list[i], payload_type_clone(pt));
    }

    media_started = (call_info->rtp_context != NULL || call_info->recv_task != NULL || call_info->send_task != NULL);
    if (media_started)
    {
        quec_voip_stop_media_for_profile_update(call_info);
    }

    if (call_info->media.profile)
    {
        rtp_profile_destroy(call_info->media.profile);
    }
    call_info->media.profile = call_profile;

    if (voip_sdp_direction(remote_sdp, audio_sdp, &media_direct) || media_direct == QUEC_MEDIA_DIRECTION_NONE)
    {
        call_info->rtp_mode = RTP_SESSION_SENDRECV;
    }
    else if (media_direct == QUEC_MEDIA_DIRECTION_SENDONLY)
    {
        call_info->rtp_mode = QUEC_MEDIA_DIRECTION_RECVONLY;
    }
    else if (media_direct == QUEC_MEDIA_DIRECTION_RECVONLY)
    {
        call_info->rtp_mode = QUEC_MEDIA_DIRECTION_SENDONLY;
    }
    else
    {
        call_info->rtp_mode = (quec_media_mode_e)media_direct;
    }

    sdp_message_free(remote_sdp);
    return QUEC_VOIP_OK;
}


/* MIKE_MEDIA_DBG: media path rate counters.
 * Report every N events only (never per frame). This link runs at 20ms (50Hz);
 * per-frame printing would flood the log UART and would itself disturb the 20ms tick.
 * Remove: delete this block, g_media_dbg, and every line containing "MIKE_" or "g_media_dbg".
 */
#ifndef VOIP_MEDIA_VERBOSE
#define VOIP_MEDIA_VERBOSE 1 /* set 1 to restore per-frame prints (50Hz, short debug only) */
#endif

/* MIKE_MEDIA_DBG: keep the voip[<line>] prefix while printing through RTI_LOG,
 * so the trace is also captured in uart-trace / log2acat mode.
 * WARNING: RTI_LOG formats into a 128-byte buffer and truncates the tail. */
#define media_dbg_log(msg, args...)                  \
    do {                                             \
        voip_log(msg, ##args);                       \
    } while (0)
typedef struct
{
    unsigned int tick;       /* record TICK events     -> producer rate */
    unsigned int tick_ms;    /* ms of the previous MIKE_TK report */
    unsigned int tick_last_ms; /* ms of the previous TICK event */
    unsigned int tick_gap_max; /* worst TICK->TICK gap in ms (0 = unset) */
    unsigned int tick_gap_min; /* best  TICK->TICK gap in ms (0 = unset) */
    unsigned int tick_other; /* other record events, incl. UNDERRUN */
    unsigned int rd_call;    /* pcma_read calls */
    unsigned int rd_ok;      /* frames obtained */
    unsigned int rd_fail;    /* acm_audio_record_get failed */
    unsigned int rd_bytes;   /* accumulated LPCM bytes */
    unsigned int snd_loop;   /* send task iterations */
    unsigned int snd_ok;     /* RTP packets sent */
    unsigned int snd_idle;   /* media_read returned 0 */
    unsigned int rcv_loop;   /* recv task iterations */
    unsigned int rcv_ok;     /* packets received and played */
    unsigned int rcv_idle;   /* recv idle */
} voip_media_dbg_t;

static voip_media_dbg_t g_media_dbg = {0};
void quec_voip_media_counters(uint32_t *tx,uint32_t *rx,uint32_t *capture,uint32_t *errors)
{
    *tx=g_media_dbg.snd_ok; *rx=g_media_dbg.rcv_ok;
    *capture=g_media_dbg.rd_ok; *errors=g_media_dbg.rd_fail;
}

static int pcma_open(void **fd,int rate,unsigned char channels,unsigned char flags,void *arg)
{
    (void)arg;
    return voip_pcm_open(fd,rate,channels,(flags & QL_PCM_READ_FLAG)!=0);
}
static int pcma_close(void *fd) { return voip_pcm_dispose(fd); }
static int pcma_write(void *fd,void *data,unsigned short len,quec_voip_rtp_payload_t *pt)
{
    (void)pt;
    return voip_pcm_write_alaw(fd,data,len);
}
static int pcma_read(void *fd,void *data,unsigned short len,quec_voip_rtp_payload_t *pt)
{
    int ret;
    (void)pt;
    g_media_dbg.rd_call++;
    ret=voip_pcm_read_alaw(fd,data,len);
    if (ret>0) { g_media_dbg.rd_ok++; g_media_dbg.rd_bytes+=ret*2; }
    else if (ret<0) g_media_dbg.rd_fail++;
    return ret;
}

static int sdp_complete_200ok(quec_voip_t *voip, quec_voip_call_session_t *call_info)
{
    uint32_t         random = 0;
    char             local_sdp[512] = {0};
    char             set_buff[256] = {0};
    osip_contact_t  *contact = NULL;
    osip_message_t  *answer = NULL;
    int              ret = 0;
    struct eXosip_t *excontext;
    random = osip_build_random_number();
    voip_log("eXosip msg random=%d\n,tid=%d,cid=%d", random, call_info->tid, call_info->call_id);

    if (voip == NULL || voip->sip_info.sip_context == NULL || call_info == NULL)
    {
        voip_log("param is null\n");
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }

    excontext = (struct eXosip_t *)voip->sip_info.sip_context;

    ret = eXosip_call_build_answer(excontext, call_info->tid, 200, &answer);
    if (ret || answer == NULL)
    {
        voip_log("eXosip msg failed,ret=%d\n", ret);
        return ret;
    }

    ret = quec_voip_build_local_sdp(call_info, local_sdp, sizeof(local_sdp));
    if (ret)
    {
        return ret;
    }

    voip_log("sdp=%s\n", local_sdp);

    ret += osip_message_set_body(answer, local_sdp, strlen(local_sdp));

    ret += osip_message_set_content_type(answer, "application/sdp");
    ret += osip_message_set_allow(answer, sip_allow);

    snprintf(set_buff, sizeof(set_buff), "replaces, 100rel, timer, norefersub");
    // Supported: replaces, 100rel, timer, norefersub
    ret += osip_message_set_supported(answer, set_buff);
    voip_log("eXosip msg random=%d\n", random);
    memset(set_buff, 0, sizeof(set_buff));
    snprintf(
        set_buff,
        sizeof(set_buff),
        "talk, hold, conference, presence, as-feature-event, dialog, "
        "line-seize, call-info, sla, include-session-description, "
        "presence.winfo, message-summary, "
        "refer"
    );

    ret += osip_message_set_header(answer, "Allow-Events", set_buff);

    osip_message_get_contact(answer, 0, &contact);
    if (contact && contact->url)
    {
        voip_log("contact->url->host=%s\n", contact->url->host);
        // 修改 host
        osip_free(contact->url->host);
        contact->url->host = osip_strdup(ipaddr_ntoa(&call_info->public.addr));

        voip_log("port=%s\n", contact->url->port);
        // 修改 port
        osip_free(contact->url->port);
        snprintf(set_buff, sizeof(set_buff), "%d", call_info->public.port);
        contact->url->port = osip_strdup(set_buff);
        voip_log("name=%s\n", contact->url->username);
        // 修改 username
        // osip_free(contact->url->username);
        // contact->url->username = osip_strdup("1001");
    }

    ret += eXosip_call_send_answer(excontext, call_info->tid, 200, answer);

    voip_log("eXosip msg i=%d\n", ret);

    return ret;
}

static int sip_send_180_ring(struct eXosip_t *excontext, quec_voip_call_session_t *call_info)
{
    int             ret = 0;
    osip_message_t *answer = NULL;
    int             status = 180;
    char            set_buff[256] = {0};
    osip_contact_t *contact = NULL;

    if (call_info == NULL)
    {
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }
    ret = eXosip_call_build_answer(excontext, call_info->tid, status, &answer);
    if (ret || answer == NULL)
    {
        voip_log("eXosip msg failed,ret=%d\n", ret);
        return ret;
    }
    voip_log("excontext=%p,sip=%p\n", excontext, answer);
    ret = osip_message_set_allow(answer, sip_allow);
    if (ret)
    {
        voip_log("set allow failed,ret=%d\n", ret);
        return ret;
    }

    snprintf(set_buff, sizeof(set_buff), "replaces, 100rel, timer, norefersub");
    ret += osip_message_set_supported(answer, set_buff);

    memset(set_buff, 0, sizeof(set_buff));
    snprintf(
        set_buff,
        sizeof(set_buff),
        "talk, hold, conference, presence, as-feature-event, dialog, "
        "line-seize, call-info, sla, include-session-description, "
        "presence.winfo, message-summary, "
        "refer"
    );

    ret += osip_message_set_header(answer, "Allow-Events", set_buff);
    osip_message_get_contact(answer, 0, &contact);
    if (contact && contact->url)
    {
        voip_log("contact->url->host=%s\n", contact->url->host);

        // 修改 host
        osip_free(contact->url->host);
        contact->url->host = osip_strdup(ipaddr_ntoa(&call_info->public.addr));

        voip_log("port=%s\n", contact->url->port);
        // 修改 port
        osip_free(contact->url->port);
        snprintf(set_buff, sizeof(set_buff), "%d", call_info->public.port);
        contact->url->port = osip_strdup(set_buff);
        voip_log("name=%s\n", contact->url->username);
        // 修改 username
        // osip_free(contact->url->username);
        // contact->url->username = osip_strdup("1001");
    }

    ret += eXosip_call_send_answer(excontext, call_info->tid, status, answer);

    voip_log("180 ring ret=%d\n", ret);
    return ret;
}

static int sip_send_refer(struct eXosip_t *excontext, int did, const char *refer_to)
{
    int             ret = 0;
    osip_message_t *refer_sip = NULL;
    int             status = 180;
    char            set_buff[256] = {0};

    ret = eXosip_call_build_refer(excontext, did, refer_to, &refer_sip);
    if (ret || refer_sip == NULL)
    {
        voip_log("eXosip msg failed,ret=%d\n", ret);
        return ret;
    }
    voip_log("excontext=%p,sip=%p\n", excontext, refer_sip);

    snprintf(set_buff, sizeof(set_buff), "replaces, 100rel, timer, norefersub, trickle-ice");
    ret += osip_message_set_supported(refer_sip, set_buff);

    memset(set_buff, 0, sizeof(set_buff));
    snprintf(
        set_buff,
        sizeof(set_buff),
        "talk, hold, conference, presence, as-feature-event, dialog, "
        "line-seize, call-info, sla, include-session-description, "
        "presence.winfo, message-summary, "
        "refer"
    );

    ret += osip_message_set_header(refer_sip, "Allow-Events", set_buff);

    ret += osip_message_set_accept(refer_sip, "message/sipfrag;version=2.0");

    ret += eXosip_call_send_request(excontext, did, refer_sip);

    voip_log("180 ring ret=%d\n", ret);
    return ret;
}

quec_task_t  voip_ref = NULL;
quec_task_t  voip_manmage_ref = NULL;
quec_queue_t quec_voip_manage_m = NULL;

#define QUEC_VOIP_MANAGE_MSG_MAX 50
/**
 * @brief Allocates event data and copies the payload for asynchronous voip processing.
 *
 * @param buff Payload buffer to copy.
 * @param len Payload length in bytes.
 * @return Pointer to the allocated event data, or NULL on failure.
 */
static quec_voip_event_data_t *quec_voip_creat_event_data(uint8 *buff, uint16 len)
{
    quec_voip_event_data_t *event_data = NULL;

    event_data = osip_malloc(sizeof(quec_voip_event_data_t));
    if (event_data == NULL)
    {
        return NULL;
    }

    memset(event_data, 0, sizeof(quec_voip_event_data_t));

    quec_rtos_semaphore_create(&event_data->sem, 0);
    if (event_data->sem == NULL)
    {
        free(event_data);
        event_data = NULL;
        return NULL;
    }

    if (len)
    {
        event_data->data = osip_malloc(len);
        if (event_data->data == NULL)
        {
            quec_rtos_semaphore_delete(event_data->sem);
            free(event_data);
            event_data = NULL;
            return NULL;
        }

        memcpy(event_data->data, buff, len);
    }

    event_data->len = len;
    event_data->ret = QUEC_VOIP_OK;

    return event_data;
}

/**
 * @brief Releases the semaphore, payload, and wrapper allocated for an event.
 *
 * @param event_data Event data to release.
 * @return QUEC_OK on success, otherwise an error code.
 */
static quec_voip_err_e quec_voip_event_data_destroy(quec_voip_event_data_t *event_data)
{
    if (event_data == NULL)
    {
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }

    quec_rtos_semaphore_delete(event_data->sem);

    if (event_data->data)
    {
        free(event_data->data);
        event_data->data = NULL;
    }

    free(event_data);
    event_data = NULL;

    return QUEC_VOIP_OK;
}

static void quec_voip_complete_start_wait(quec_voip_t *voip, quec_voip_err_e ret)
{
    quec_voip_event_data_t *event = NULL;

    if (voip == NULL)
    {
        return;
    }

    event = (quec_voip_event_data_t *)voip->sip_info.pending_start_event;
    if (event == NULL)
    {
        return;
    }

    voip->sip_info.pending_start_event = NULL;
    event->ret = ret;
    quec_rtos_semaphore_release(event->sem);
}

int quec_voip_send_manage_event(quec_voip_t *voip, quec_voip_event_e voip_event, void *data, unsigned short len)
{
    quec_voip_manage_t manage_msg = {0};
    char              *p_data = NULL;
    if (voip_event >= QUEC_VOIP_EVENT_MAX || (data != NULL && len == 0))
    {
        return -1;
    }
    voip_log("SEND EVENT:%d", voip_event);
    if (data)
    {
        p_data = osip_malloc(len);
        if (p_data == NULL)
        {
            return -1;
        }

        memcpy(p_data, data, len);
        manage_msg.p_data = p_data;
    }

    voip_log("data=%p,p_data=%p", data, p_data);

    manage_msg.event = voip_event;
    manage_msg.voip = voip;
    if (quec_rtos_queue_release(quec_voip_manage_m, sizeof(quec_voip_manage_t), (uint8_t *)&manage_msg, QUEC_NO_WAIT))
    {
        if (manage_msg.p_data)
        {
            free(manage_msg.p_data);
        }
        return -1;
    }

    return 0;
}

typedef struct
{
    quec_voip_media_profile_t *media;
    unsigned char              count;
} quec_voip_sip_init_t;



static void quec_voip_notify_call_state(quec_voip_t *voip, quec_voip_call_session_t *call_info)
{
    quec_voip_call_t call_report = {0};
    if (voip == NULL || call_info == NULL || voip->sip_info.sip_cfg.call_state_cb == NULL)
    {
        return;
    }

    call_report.index = call_info->index;
    call_report.state = call_info->state;
    memcpy(call_report.user_name, call_info->user_name, strlen(call_info->user_name));
    

    if (voip->sip_info.sip_cfg.call_state_cb)
    {
        voip_log("notify call state index=%d state=%d user=%s", call_report.index, call_report.state, call_report.user_name);
        voip->sip_info.sip_cfg.call_state_cb((void *)(voip->sip_info.sip_cfg.call_cb_ctx), &call_report);        
    }
}

static quec_voip_err_e quec_voip_start_init(quec_voip_media_profile_t *media_support, unsigned char count)
{
    quec_voip_sip_init_t    send_cfg = {0};
    int                     ret = 0;
    quec_voip_event_data_t *send_cfg_manager = NULL;
    if (media_support == NULL || count == 0)
    {
        voip_log("voip invalid param");
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }

    if (quec_voip)
    {
        voip_log("voip already init");
        return QUEC_VOIP_ERR_SIP_PARAM_ILLEGAL;
    }

    send_cfg.media = osip_malloc(count * sizeof(quec_voip_media_profile_t));
    if (send_cfg.media == NULL)
    {
        voip_log("voip osip_malloc fail");
        return QUEC_VOIP_ERR_NO_MEMORY;
    }

    memcpy(send_cfg.media, media_support, count * sizeof(quec_voip_media_profile_t));
    send_cfg.count = count;
    send_cfg_manager = quec_voip_creat_event_data((uint8 *)&send_cfg, sizeof(quec_voip_sip_init_t));

    if (send_cfg_manager == NULL)
    {
        voip_log("voip osip_malloc fail");
        ret = QUEC_VOIP_ERR_NO_MEMORY;
        goto destroy;
    }

    voip_log("WILL SEND");
    if (quec_voip_send_manage_event(NULL, QUEC_VOIP_EVENT_INIT, (void *)&send_cfg_manager, sizeof(quec_voip_sip_init_t *)))
    {
        voip_log("voip send fail");
        ret = QUEC_VOIP_ERR_UNKNOWN;
        goto destroy;
    }

    if (send_cfg_manager->sem)
    {
        quec_rtos_semaphore_wait(send_cfg_manager->sem, QUEC_WAIT_FOREVER);
    }

    ret = send_cfg_manager->ret;
destroy:

    if (send_cfg.media)
    {
        free(send_cfg.media);
        send_cfg.media = NULL;
    }

    quec_voip_event_data_destroy(send_cfg_manager);

    voip_log("voip init wait result done");

    return ret;
}
static int quec_voip_send_network_check(quec_voip_t *voip)
{
    int ret;
    if (voip == NULL)
    {
        return -1;
    }

    if (quec_voip_send_manage_event(voip, QUEC_VOIP_EVENT_NETWORK_CHECK, NULL, 0))
    {
        ret = -1;
    }
    return ret;
}

int quec_voip_send_unregist(void)
{
    int ret;

    if (quec_voip_send_manage_event(NULL, QUEC_VOIP_EVENT_UNREGIST, NULL, 0))
    {
        return -1;
    }
    return 0;
}

/*
 * APP filled local IPv4. APP queries its own bearer (CH390/WiFi/other) for the
 * address and hands it over here; VOIP then registers with it regardless of the
 * configured carrier mode. Only the address is updated, not the SIP port.
 */
int quec_voip_set_local_ip(const char *ip)
{
    quec_voip_addr_t addr = {0};

    if (ip == NULL || ip[0] == '\0')
    {
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }

    if (ipaddr_aton(ip, &addr.addr) == 0)
    {
        voip_log("set local ip fail,invalid ip=%s", ip);
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }

    return quec_voip_cfg_set(QUEC_VOIP_CFG_ITEM_LOCAL_IP, (const void *)&addr, sizeof(addr));
}

quec_voip_err_e quec_voip_sip_stop(void)
{
    return quec_voip_stop();
}

quec_voip_err_e quec_voip_start(BOOL auto_regist)
{
    quec_voip_event_data_t *event = NULL;
    quec_voip_start_req_t   req = {0};
    quec_voip_err_e         ret = QUEC_VOIP_OK;

    if (quec_voip == NULL)
    {
        return QUEC_VOIP_ERR_NOT_INIT;
    }

    if (auto_regist != FALSE && auto_regist != TRUE)
    {
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }

    req.auto_regist = auto_regist;

    event = quec_voip_creat_event_data((uint8 *)&req, sizeof(req));
    if (event == NULL)
    {
        return QUEC_VOIP_ERR_NO_MEMORY;
    }

    if (quec_voip_send_manage_event(quec_voip, QUEC_VOIP_EVENT_START, (void *)&event, sizeof(quec_voip_event_data_t *)))
    {
        quec_voip_event_data_destroy(event);
        return QUEC_VOIP_ERR_UNKNOWN;
    }

    quec_rtos_semaphore_wait(event->sem, QUEC_WAIT_FOREVER);
    ret = event->ret;
    quec_voip_event_data_destroy(event);
    return ret;
}

quec_voip_err_e quec_voip_stop(void)
{
    quec_voip_event_data_t *event = NULL;
    quec_voip_err_e         ret = QUEC_VOIP_OK;

    if (quec_voip == NULL)
    {
        return QUEC_VOIP_ERR_NOT_INIT;
    }

    /* Close all calls first, then stop SIP service. */
    quec_voip_call_op(0xff, FALSE);

    event = quec_voip_creat_event_data(NULL, 0);
    if (event == NULL)
    {
        return QUEC_VOIP_ERR_NO_MEMORY;
    }

    if (quec_voip_send_manage_event(quec_voip, QUEC_VOIP_EVENT_STOP, (void *)&event, sizeof(quec_voip_event_data_t *)))
    {
        quec_voip_event_data_destroy(event);
        return QUEC_VOIP_ERR_UNKNOWN;
    }

    quec_rtos_semaphore_wait(event->sem, QUEC_WAIT_FOREVER);
    ret = event->ret;
    quec_voip_event_data_destroy(event);
    return ret;
}

static int quec_voip_send_regist(quec_voip_t *voip)
{
    int ret;
    if (voip == NULL)
    {
        return -1;
    }

    if (quec_voip_send_manage_event(voip, QUEC_VOIP_EVENT_REGIST, NULL, 0))
    {
        return -1;
    }
    return 0;
}

static int quec_voip_send_regist_done(quec_voip_t *voip)
{
    int ret;
    if (voip == NULL)
    {
        return -1;
    }

    if (quec_voip_send_manage_event(voip, QUEC_VOIP_EVENT_REGIST_DONE, NULL, 0))
    {
        return -1;
    }
    return 0;
}

typedef enum
{
    QUEC_VOIP_EVENT_MEDIA_CREAT,
    QUEC_VOIP_EVENT_MEDIA_UPDATE,
    QUEC_VOIP_EVENT_MEDIA_RTCP_SR_BYE,
    QUEC_VOIP_EVENT_MEDIA_RTCP_RR_BYE,
    QUEC_VOIP_EVENT_MEDIA_DESTROY,
} quec_voip_media_event_e;
typedef struct
{
    quec_voip_media_event_e type;
    int                     tid; /**< unique id for transactions (to be used for answers) */
    int                     did; /**< unique id for SIP dialogs */
    int                     cid; /**< unique id for SIP calls (but multiple dialogs!) */
} quec_voip_media_event_t;

typedef struct
{
    char account[32];
} quec_voip_mo_call_t;

typedef struct
{
    char refer_to[32];  /* The address to be referred to */
} quec_voip_refer_param_t;

static quec_voip_call_session_t *quec_voip_find_call_by_cid(quec_voip_t *voip, int cid)
{
    unsigned char i = 0;

    if (voip == NULL || cid <= 0)
    {
        return NULL;
    }

    for (i = 0; i < 6; i++)
    {
        if (voip->call[i].call_id == cid && voip->call[i].state != QUEC_VOIP_CALL_IDLE)
        {
            return &voip->call[i];
        }
    }

    return NULL;
}

static int quec_voip_send_call_event(quec_voip_t *voip, quec_voip_media_event_t *event, quec_voip_event_e type)
{
    if (event == NULL)
    {
        return -1;
    }

    if (quec_voip_send_manage_event(voip, type, (void *)event, sizeof(quec_voip_media_event_t)))
    {
        return -1;
    }
    return 0;
}

/* UniRTOS application owns PDP activation and supplies the local IPv4. */
static int quec_voip_network_check(quec_voip_t *voip)
{
    if (!voip) return QUEC_VOIP_ERR_INVALID_PARAM;
    if (!voip->sip_info.sip_cfg.enable) { quec_voip_send_unregist(); return QUEC_VOIP_OK; }
    if (voip->sip_info.is_regist) return QUEC_VOIP_OK;
    if (ip_addr_isany(&voip->sip_info.local.addr)) return QUEC_VOIP_ERR_IP_GET_FAILED;
    return quec_voip_send_regist(voip);
}

/** @brief 停止时只注销本会话的 Contact，等待有限时间，不删除其它设备的绑定。
 * @note 管理线程调用；等待时不持有 SIP 锁，SIP 线程继续处理应答及鉴权。
 * @return 2xx 为服务器确认；-1 本地构造/发送失败；-2 超时；其它正数为 SIP 失败码。
 */
static int quec_voip_unregister_wait(quec_voip_t *voip, struct eXosip_t *context)
{
    uint32_t start = qcm_voip_uptime_ms();
    int sent = 0, result = 0;
    qosa_enter_critical();
    voip->sip_info.unregistering = TRUE;
    voip->sip_info.unregister_sent = FALSE;
    voip->sip_info.unregister_result = 0;
    qosa_exit_critical();
    while ((uint32_t)(qcm_voip_uptime_ms() - start) < 5000U)
    {
        if (!sent)
        {
            osip_message_t *request = NULL;
            eXosip_reg_t *binding = NULL;
            int rc = OSIP_WRONG_STATE;
            eXosip_lock(context);
            for (eXosip_reg_t *r = context->j_reg; r != NULL; r = r->next)
                if (r->r_id == voip->sip_info.reg_id) { binding = r; break; }
            if (!binding) rc = OSIP_NOTFOUND;
            else if (!binding->r_last_tr || binding->r_last_tr->state == NICT_COMPLETED ||
                     binding->r_last_tr->state == NICT_TERMINATED)
            {
                /* 保留旧 From/Call-ID/Contact/鉴权信息；不用 Contact:* 清除同账号其它设备。 */
                rc = eXosip_register_build_register(context, voip->sip_info.reg_id, 0, &request);
                if (!rc)
                {
                    osip_contact_t *contact = NULL;
                    for (int i = 0; osip_message_get_contact(request, i, &contact) == 0; ++i)
                    {
                        osip_generic_param_t *expires = NULL;
                        osip_contact_param_get_byname(contact, "expires", &expires);
                        if (expires)
                        {
                            char *zero = osip_strdup("0");
                            if (!zero) { rc = OSIP_NOMEM; break; }
                            osip_free(expires->gvalue);
                            expires->gvalue = zero;
                        }
                    }
                    if (!rc) rc = eXosip_register_send_register(context, voip->sip_info.reg_id, request);
                    else osip_message_free(request);
                    /* send_register 在成功和失败时均接管 request。 */
                    if (!rc) {
                        qosa_enter_critical(); voip->sip_info.unregister_sent = TRUE; qosa_exit_critical();
                    }
                }
            }
            eXosip_unlock(context);
            if (!rc) sent = 1;
            else if (rc != OSIP_WRONG_STATE) { result = -1; break; }
        }
        qosa_enter_critical(); result = voip->sip_info.unregister_result; qosa_exit_critical();
        if (result) break;
        quec_rtos_task_sleep_ms(20);
    }
    return result ? result : -2;
}

/** @brief SIP 线程直接完成注销等待，不投递给正在等待的管理线程。
 * @note 调用时已持有 SIP 锁并运行 automatic_action；旧普通 REGISTER 成功不算注销成功。
 */
static BOOL quec_voip_unregister_event(quec_voip_t *voip, const eXosip_event_t *event)
{
    BOOL stopping, sent;
    osip_header_t *expires = NULL;
    struct eXosip_t *context = voip->sip_info.sip_context;
    int result;
    qosa_enter_critical();
    stopping = voip->sip_info.unregistering;
    sent = voip->sip_info.unregister_sent;
    qosa_exit_critical();
    if (!stopping) return FALSE;
    if (!sent) return TRUE;
    if (event->rid != voip->sip_info.reg_id || !event->request) return TRUE;
    osip_message_get_expires(event->request, 0, &expires);
    if (!expires || !expires->hvalue || strcmp(expires->hvalue, "0")) return TRUE;
    result = event->response ? event->response->status_code : -2;
    for (eXosip_reg_t *r = context->j_reg; r != NULL; r = r->next)
        if (r->r_id == event->rid && r->r_last_tr && r->r_last_tr->transactionid != event->tid)
            return TRUE; /* 忽略旧删除应答，以及 401/407 启动新鉴权后的旧事务。 */
    if (result >= 200 || result < 0)
    {
        qosa_enter_critical(); voip->sip_info.unregister_result = result; qosa_exit_critical();
    }
    return TRUE;
}

/** @brief 单账号 Demo 只接受发往当前 Contact 用户名的新 INVITE。
 * @return 0 接受；480 停止中；404 非当前账号。From 是主叫，To 可能经过转接，不用于匹配。
 * @note 在分配来电槽位和开铃之前校验；会话内 re-INVITE 不走此规则。
 */
static int quec_voip_invite_account_check(const quec_voip_t *voip, const osip_message_t *request)
{
    char account[32];
    BOOL accepting;
    qosa_enter_critical();
    accepting = voip->sip_info.sip_cfg.enable && !voip->sip_info.unregistering;
    memcpy(account, voip->sip_info.sip_cfg.account, sizeof(account));
    qosa_exit_critical();
    account[sizeof(account) - 1] = 0;
    if (!accepting) return 480;
    if (!request || !request->req_uri || !request->req_uri->username || !account[0] ||
        strcmp(request->req_uri->username, account)) return 404;
    return 0;
}

/** @brief 有界注销后释放 SIP 任务及上下文；返回值表示本地停止结果。
 * @note 服务器是否确认单独保存在 unreg_code；失败/断网也完成本地停止，来电账号校验继续兜底。
 */
static int quec_voip_unregist(quec_voip_t *voip)
{
    struct eXosip_t *context;
    if (!voip) return -1;
    context = (struct eXosip_t *)voip->sip_info.sip_context;
    if (context)
    {
        if (voip->sip_info.reg_id > 0 && voip->sip_info.sip_task)
        {
            int result = quec_voip_unregister_wait(voip, context);
            qosa_enter_critical(); g_net_diag.unreg_code = result; qosa_exit_critical();
        }
        /* 应答处理结束后才退出读取线程；join 完成前不释放其访问的 SIP 对象。 */
        keepRunning = 0;
        if (voip->sip_info.sip_task) voip_thread_join(voip->sip_info.sip_task, NULL);
        eXosip_quit(context);
        osip_free(context);
        voip->sip_info.sip_task = NULL;
        voip->sip_info.sip_context = NULL;
        voip->sip_info.reg_id = 0;
        qosa_enter_critical(); voip->sip_info.unregistering = FALSE; qosa_exit_critical();
    }
    if (voip->sip_info.sip_cfg.enable) quec_voip_send_network_check(voip);
    return 0;
}

/** @brief 分发呼叫终止事件，鉴权挑战交由 SIP 栈重试而不释放应用会话。
 * @param[in] quec_voip_t *voip 当前 VoIP 实例。
 * @param[in] const eXosip_event_t *event SIP 线程当前持有的事件。
 * @return 0 为已处理/保留鉴权会话，其他值为参数或消息投递失败。
 * @note 在 SIP 锁内、eXosip_automatic_action 之后调用；仅已重试/仍允许重试的挑战保留会话。
 */
static int quec_voip_handle_call_end_event(quec_voip_t *voip, const eXosip_event_t *event)
{
    quec_voip_media_event_t release = {0};
    if (voip == NULL || event == NULL)
    {
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }
    if (event->type == EXOSIP_CALL_REQUESTFAILURE && event->response != NULL &&
        (event->response->status_code == 401 || event->response->status_code == 407))
    {
        eXosip_call_t *sip_call = NULL;
        eXosip_dialog_t *dialog = NULL;
        osip_transaction_t *transaction = NULL;
        if (event->did > 0)
        {
            _eXosip_call_dialog_find(voip->sip_info.sip_context, event->did, &sip_call, &dialog);
        }
        if (sip_call == NULL)
        {
            _eXosip_call_find(voip->sip_info.sip_context, event->cid, &sip_call);
        }
        if (sip_call != NULL)
        {
            transaction = _eXosip_find_last_out_invite(sip_call, dialog);
            if ((transaction != NULL && transaction->transactionid != event->tid) ||
                (dialog != NULL ? dialog->d_retry : sip_call->c_retry) < 3)
            {
                voip_log("INVITE authentication retry retained cid=%d code=%d", event->cid, event->response->status_code);
                return QUEC_VOIP_OK;
            }
        }
    }
    release.cid = event->cid;
    release.tid = event->tid;
    release.did = event->did;
    release.type = QUEC_VOIP_EVENT_MEDIA_DESTROY;
    return quec_voip_send_call_event(voip, &release, QUEC_VOIP_EVENT_CALL_MEDIA_DESTROY);
}

/** @brief 将最终注册失败排队给管理任务，自动鉴权已启动的新事务不算离线。
 * @param[in] quec_voip_t *voip 当前实例。
 * @param[in] const eXosip_event_t *event SIP 任务持有的失败事件。
 * @return 0 为处理成功，非零表示队列投递失败。
 * @note 调用者持有 SIP 锁且已运行 automatic_action；不在这里关闭注册或监听。
 */
static int quec_voip_registration_failed(quec_voip_t *voip, const eXosip_event_t *event)
{
    struct eXosip_t *context = voip->sip_info.sip_context;
    int info[2] = {event->rid, event->response ? event->response->status_code : 0};
    for (eXosip_reg_t *reg = context->j_reg; reg != NULL; reg = reg->next)
    {
        if (reg->r_id == event->rid && reg->r_last_tr != NULL &&
            reg->r_last_tr->transactionid != event->tid)
            return QUEC_VOIP_OK;
    }
    return quec_voip_send_manage_event(voip, QUEC_VOIP_EVENT_REGIST_FAILED, info, sizeof(info));
}

/** @brief 管理任务处理最终注册失败，清除在线状态并安排重试。
 * @param[in,out] quec_voip_t *voip 当前实例。
 * @param[in] const int *info 两个整数，依次为注册编号与 SIP 应答码。
 * @return 无；停止后的旧注册事件被忽略，不打断现有音频会话。
 */
static void quec_voip_registration_recover(quec_voip_t *voip, const int *info)
{
    if (!info || !voip->sip_info.sip_cfg.enable || info[0] != voip->sip_info.reg_id) return;
    qosa_enter_critical();
    ++g_net_diag.reg_failures;
    g_net_diag.reg_code = info[1];
    qosa_exit_critical();
    quec_voip_regist_state(voip, FALSE);
    if ((voip->sip_info.pending_start_event != NULL || voip->sip_info.sip_cfg.auto_connect) &&
        quec_voip_sip_start_check_timer(voip)) return;
    quec_voip_sip_stop_check_timer(voip);
    quec_voip_complete_start_wait(voip, QUEC_VOIP_ERR_SIP_REGIST_FAILED);
}

void start_voip_service(void *argv)
{
    osip_message_t         *answer = NULL;
    int                     i = 0;
    int                     status = 200;
    int                     value = 0;
    eXosip_event_t         *event = NULL;
    quec_voip_media_event_t quec_event = {0};

    quec_voip_t     *voip = (quec_voip_t *)argv;
    struct eXosip_t *context_sip = NULL;
	quec_voip_sip_config_t*   sip_cfg;
	unsigned short   expiry_min=0;
    if (voip == NULL)
    {
        voip_log("error ,voip is null!!!!");
        return;
    }

    context_sip = (struct eXosip_t *)voip->sip_info.sip_context;
	sip_cfg=&voip->sip_info.sip_cfg;
	expiry_min=sip_cfg->expiry;
	
    if (context_sip == NULL)
    {
        voip_log("error ,sip context is null!!!!");
        return;
    }

    for (; keepRunning;)
    {
        /* Update wait timeout based on current register expiry time */
        if (voip->sip_info.register_check_time > 0) {
            expiry_min = voip->sip_info.register_check_time;
        }
    	expiry_min = MIN(expiry_min, 100);
        event = eXosip_event_wait(context_sip, 0, 100);

        if (!event)
        {
#ifdef OSIP_MONOTHREAD
            eXosip_execute(context_sip);
#endif

            eXosip_lock(context_sip);
            eXosip_automatic_action(context_sip);
            eXosip_unlock(context_sip);
            osip_usleep(10000);
            continue;
        }

#ifdef OSIP_MONOTHREAD
        eXosip_execute(context_sip);
#endif

        eXosip_lock(context_sip);
        eXosip_automatic_action(context_sip);
        voip_log("event->type=%d\n", event->type);
        voip_log("tid=%d,cid=%d,did=%d", event->tid, event->cid, event->did);
        voip_call_diag_event(event);
        quec_event.cid = event->cid;
        quec_event.tid = event->tid;
        quec_event.did = event->did;
        switch (event->type)
        {
            case EXOSIP_REGISTRATION_SUCCESS:
                if (quec_voip_unregister_event(voip, event) || !voip->sip_info.sip_cfg.enable ||
                    event->rid != voip->sip_info.reg_id) break;
                voip_log("registrered successfully\n");
                osip_contact_t *contact;

                osip_message_get_contact(event->request, 0, &contact);
                if (contact && contact->url && contact->url->host)
                {
                    voip_log("user=%s,host=%s,port=%s\n", contact->url->username, contact->url->host, contact->url->port);
                }
                quec_voip_send_regist_done(voip);
                break;

            case EXOSIP_REGISTRATION_FAILURE:
                if (quec_voip_unregister_event(voip, event) || !voip->sip_info.sip_cfg.enable) break;
                quec_voip_registration_failed(voip, event);
                break;
            case EXOSIP_MESSAGE_NEW: {
                osip_message_t *answer;
                int             i;
                int             status = 200;

                i = eXosip_message_build_answer(context_sip, event->tid, status, &answer);

                if (i != 0)
                {
                    voip_log("failed to %d %s", status, event->request->sip_method);
                    break;
                }

                i = eXosip_message_send_answer(context_sip, event->tid, status, answer);

                if (i != 0)
                {
                    voip_log("failed to %d %s", status, event->request->sip_method);
                    break;
                }

                voip_log("%s  with %d,%s,tid=%d", event->textinfo, status, event->request->sip_method, event->tid);
            }
            break;
            case EXOSIP_CALL_MESSAGE_NEW: {
                voip_log("%s  with %d,%s,tid=%d", event->textinfo, status, event->request->sip_method, event->tid);

                i = eXosip_call_build_answer(context_sip, event->tid, status, &answer);

                if (i != 0)
                {
                    voip_log("failed to %d %s", status, event->request->sip_method);
                    break;
                }

                osip_message_set_allow(answer, sip_allow);

                i = eXosip_call_send_answer(context_sip, event->tid, status, answer);

                if (i != 0)
                {
                    voip_log("failed to %d %s", status, event->request->sip_method);
                    break;
                }
            }
            break;
            case EXOSIP_CALL_INVITE: {
                int queued;
                int code = quec_voip_invite_account_check(voip, event->request);
                qosa_enter_critical(); ++g_net_diag.invites_rx; qosa_exit_critical();
                if (code)
                {
                    eXosip_call_send_answer(context_sip, event->tid, code, NULL);
                    voip_incoming_result(event->cid, code);
                    break;
                }
                quec_event.type = QUEC_VOIP_EVENT_MEDIA_CREAT;
                queued = quec_voip_send_call_event(voip, &quec_event, QUEC_VOIP_EVENT_CALL_INVITE);
                if (queued)
                {
                    qosa_enter_critical(); ++g_net_diag.queue_drops; qosa_exit_critical();
                    voip_incoming_result(event->cid, queued);
                    eXosip_call_send_answer(context_sip, event->tid, 503, NULL);
                }
            }
            break;
            case EXOSIP_CALL_PROCEEDING:
            case EXOSIP_CALL_RINGING:
                quec_event.type = QUEC_VOIP_EVENT_MEDIA_CREAT;
				
                quec_voip_send_call_event(voip, &quec_event, QUEC_VOIP_EVENT_CALL_MO_ALERTING);
                break;
            case EXOSIP_CALL_ANSWERED:
                quec_event.type = QUEC_VOIP_EVENT_MEDIA_CREAT;
				quec_voip_send_call_event(voip, &quec_event, QUEC_VOIP_EVENT_CALL_MO_ANSWERED);
			
                break;
            case EXOSIP_CALL_REINVITE: {
                voip_log("Received INVITE");
                quec_event.type = QUEC_VOIP_EVENT_MEDIA_DESTROY;
                quec_voip_send_call_event(voip, &quec_event, QUEC_VOIP_EVENT_CALL_MEDIA_DESTROY);
                quec_event.type = QUEC_VOIP_EVENT_MEDIA_UPDATE;
                /*
       * Push a control event into the manage queue. Variable-size payloads are
       * copied here so the producer thread can return immediately after
       * enqueueing.
       */
                quec_voip_send_call_event(voip, &quec_event, QUEC_VOIP_EVENT_CALL_INVITE);
                break;
            }

            case EXOSIP_CALL_CLOSED:
            case EXOSIP_CALL_RELEASED:
            case EXOSIP_CALL_REQUESTFAILURE:
            case EXOSIP_CALL_SERVERFAILURE:
            case EXOSIP_CALL_GLOBALFAILURE:
            case EXOSIP_CALL_NOANSWER:
            case EXOSIP_CALL_CANCELLED:
                quec_voip_handle_call_end_event(voip, event);
                break;
            case EXOSIP_CALL_ACK: {
                // if (quec_send_sdp_msg((void *)&sdp_msg, QUEC_RTP_READY))
                {
                    //    eXosip_call_send_answer(context_sip, event->tid, 400, NULL);
                    quec_event.type = QUEC_VOIP_EVENT_MEDIA_CREAT;
                    quec_voip_send_call_event(voip, &quec_event, QUEC_VOIP_EVENT_CALL_MEDIA_CREAT);
                }
            }
            break;
            default:
                voip_log("received unknown eXosip event (type, did, cid) = (%d, %d, %d)\n", event->type, event->did, event->cid);
        }

        eXosip_unlock(context_sip);
        eXosip_event_free(event);
    }

    /* The manager owns context destruction after joining this worker. */
    return;
}

static void *voip_sip_main(void *arg)
{
    start_voip_service(arg);
    return NULL;
}
static int voip_sip_worker_start(quec_voip_t *voip)
{
    voip_thread_attr_t attr={.stackSize=10*1024,.taskName="voip_sip"};
    keepRunning=1;
    return voip_thread_create(&voip->sip_info.sip_task,&attr,voip_sip_main,voip);
}

static int quec_voip_sip_init(quec_voip_t **voip, quec_voip_sip_init_t *init_cfg)
{
    struct eXosip_t           *sip_context = NULL;
    quec_voip_t               *voip_p = NULL;
    quec_voip_media_profile_t *media_support = NULL;
    unsigned char              i = 0;
    if (init_cfg == NULL)
    {
        return -1;
    }

    if (voip == NULL)
    {
        return -1;
    }

    voip_p = *voip;

    if (voip_p == NULL)
    {
        voip_p = osip_malloc(sizeof(quec_voip_t));
        if (voip_p == NULL)
        {
            return QUEC_VOIP_ERR_NO_MEMORY;
        }
        memset(voip_p, 0, sizeof(quec_voip_t));
        *voip = voip_p;
        voip_log("voip=%p", voip_p);
    }
    voip_log("[mike]step1");
    sip_context = (struct eXosip_t *)voip_p->sip_info.sip_context;
    voip_log("[mike]step4");
    if (sip_context == NULL)
    {
        sip_context = (struct eXosip_t *)eXosip_malloc();

        if (sip_context == NULL)
        {
            return -1;
        }

        if (eXosip_init(sip_context))
        {
            voip_log("eXosip_init failed\n");
            eXosip_quit(sip_context);
            osip_free(sip_context);
            return -1;
        }
        voip_p->sip_info.sip_context = (void *)sip_context;
    }

    for (i = 0; i < init_cfg->count; i++)
    {
        media_support = (quec_voip_media_profile_t *)(init_cfg->media + i);
        quec_voip_set_support_profile(&voip_p->sip_info.support_profile, media_support);
    }

    if (voip_p->sip_info.sip_task == NULL)
    {
        if (voip_sip_worker_start(voip_p)) return QUEC_VOIP_ERR_NO_MEMORY;
        voip_log("voip_app_task_entry\n");
    }
    voip_log("voip=%p,regist[%p]=%d", voip_p, &voip_p->sip_info.is_regist, voip_p->sip_info.is_regist);
    return 0;
}
static int quec_voip_regist_state(quec_voip_t *voip, BOOL state)
{
    if (voip == NULL)
    {
        return -1;
    }

    if (state)
    {
        voip->sip_info.is_regist = TRUE;
    }
    else
    {
        voip->sip_info.is_regist = FALSE;
    }

    if (voip->sip_info.sip_cfg.sip_state_cb)
    {
        voip_log("notify sip state=%d", voip->sip_info.is_regist);
        voip->sip_info.sip_cfg.sip_state_cb(voip->sip_info.sip_cfg.sip_cb_ctx, voip->sip_info.is_regist);
        
    }
    voip_log("set state=%d", voip->sip_info.is_regist);
    return 0;
}

static int quec_voip_regist(quec_voip_t *voip)
{
    char                    from[128] = {0};
    char                    proxy[128] = {0};
    char                    contact[128] = {0};
    char                    via_srt[128] = {0};
    char                    prog_name[32] = {0};
    int                     optval = 0;
    struct eXosip_t        *sip_context = NULL;
    quec_voip_sip_config_t *cfg = NULL;
    int                     regid = 0;
    osip_message_t         *reg = NULL;
    int                     port_range[2] = {0};

    if (voip == NULL)
    {
        return -1;
    }

    cfg = &voip->sip_info.sip_cfg;
    sip_context = (struct eXosip_t *)voip->sip_info.sip_context;

    if (cfg->pdp_cid >= QUEC_VOIP_CID_MAX || strlen(cfg->account) == 0 || ip_addr_isany(&cfg->server.addr) || ip_addr_isany(&voip->sip_info.local.addr))
    {
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }

    // 刷新失败后复用原注册对象；不能再次 bind 已占用的本地 SIP 端口。
    if (sip_context != NULL && voip->sip_info.reg_id > 0)
    {
        eXosip_lock(sip_context);
        optval = eXosip_register_build_register(sip_context, voip->sip_info.reg_id,
                    cfg->expiry ? cfg->expiry : QUEC_VOIP_SIP_EXPIRY, &reg);
        if (optval == 0) optval = eXosip_register_send_register(sip_context, voip->sip_info.reg_id, reg);
        eXosip_unlock(sip_context);
        return optval;
    }

    if (sip_context == NULL)
    {
        sip_context = (struct eXosip_t *)eXosip_malloc();

        if (sip_context == NULL)
        {
            return QUEC_VOIP_ERR_NO_MEMORY;
        }

        if (eXosip_init(sip_context))
        {
            voip_log("eXosip_init failed\n");
            eXosip_quit(sip_context);
            osip_free(sip_context);
            voip->sip_info.sip_context = NULL;
            return QUEC_VOIP_ERR_SIP_REGIST_FAILED;
        }
        voip->sip_info.sip_context = (void *)sip_context;
    }

    if (voip->sip_info.sip_task == NULL)
    {
        if (voip_sip_worker_start(voip)) return QUEC_VOIP_ERR_NO_MEMORY;
        voip_log("voip_app_task_entry\n");
    }

    optval = cfg->enable_masquerade ? 1 : 0;
    eXosip_set_option(sip_context, EXOSIP_OPT_AUTO_MASQUERADE_CONTACT, &optval);

    optval = cfg->keep_alive_time ? cfg->keep_alive_time : QUEC_VOIP_SIP_KEEPALIVE_TIME;

    eXosip_set_option(sip_context, EXOSIP_OPT_UDP_KEEP_ALIVE, (void *)&optval);

    if (strlen(cfg->agent) == 0)
    {
        snprintf(prog_name, sizeof(prog_name), "%s (%s)", "QuecVoip", eXosip_get_version());
    }
    else
    {
        snprintf(prog_name, sizeof(prog_name), "%s", cfg->agent);
    }
    eXosip_set_user_agent(sip_context, prog_name);

    eXosip_set_option(sip_context, EXOSIP_OPT_SET_OC_LOCAL_ADDRESS, ipaddr_ntoa(&voip->sip_info.local.addr));

    eXosip_set_option(sip_context, EXOSIP_OPT_SET_IPV4_FOR_GATEWAY, ipaddr_ntoa(&voip->sip_info.local.addr));

    port_range[0] = voip->sip_info.local.port;
    if (voip->sip_info.local.port == 0)
    {
        port_range[0] = QUEC_VOIP_GET_RANDOM_PORT;  // should use 5060?,use random Dynamic port
        voip->sip_info.local.port = port_range[0];
    }

    port_range[1] = port_range[0] + QUEC_VOIP_PORT_RANGE;
    voip_log("voip_port port_range %d,~%d\n", port_range[0], port_range[1]);
    eXosip_set_option(sip_context, EXOSIP_OPT_SET_OC_PORT_RANGE, (void *)&port_range);

    optval = eXosip_listen_addr(sip_context, IPPROTO_UDP, ipaddr_ntoa(&voip->sip_info.local.addr), port_range[0], AF_INET, 0);
    if (optval)
    {
        voip_log("eXosip_listen_addr failed\n");
        return -1;
    }

    if (eXosip_add_authentication_info(sip_context, cfg->account, cfg->account, cfg->password, NULL, NULL))
    {
        voip_log("eXosip_add_authentication_info failed\n");
        return QUEC_VOIP_ERR_SIP_REGIST_FAILED;
    }

    //  snprintf(from, sizeof(from), "sip:%s@%s", cfg->account,
    //           ipaddr_ntoa(&voip->sip_info.local.addr));
    snprintf(from, sizeof(from), "sip:%s@%s", cfg->account, ipaddr_ntoa(&cfg->server.addr));
    snprintf(proxy, sizeof(proxy), "sip:%s:%d", ipaddr_ntoa(&cfg->server.addr), cfg->server.port);
    

    voip_log("Building REGISTER with local address: %s:%d", ipaddr_ntoa(&voip->sip_info.local.addr), voip->sip_info.local.port);
    snprintf(contact, sizeof(contact), "sip:%s@%s:%d", cfg->account, ipaddr_ntoa(&voip->sip_info.local.addr), voip->sip_info.local.port);

    optval = cfg->expiry ? cfg->expiry : QUEC_VOIP_SIP_EXPIRY;
    voip_log("Building REGISTER with contact: %s", contact);
    eXosip_lock(sip_context);
    regid = eXosip_register_build_initial_register(sip_context, from, proxy, contact, optval, &reg);

    if (regid < 1)
    {
        voip_log("eXosip_register_build_initial_register failed\n");
        eXosip_unlock(sip_context);
        return -1;
    }

    osip_message_set_allow(reg, sip_allow);

    voip->sip_info.reg_id = regid;
    optval = eXosip_register_send_register(sip_context, regid, reg);
    eXosip_unlock(sip_context);

    if (optval != 0)
    {
        voip_log("eXosip_register_send_register failed\n");
        return -1;
    }
    return 0;
}

static void quec_voip_sip_check_timer(void *argv)
{
    quec_voip_t *voip = (quec_voip_t *)argv;
    if (voip == NULL)
        return;

    quec_voip_send_network_check(voip);
}


static const unsigned int quec_voip_sip_retry_schedule_ms[] = {3000, 5000, 7000};

static QuecOSStatus quec_voip_sip_timer_stop(quec_timer_t check_timer)
{
    QuecOSStatus err = kNoErr;

    if (check_timer == NULL)
    {
        voip_log("quecvoip_sip_timer not init");
        return kNotInitializedErr;
    }

    err = quec_rtos_timer_stop(check_timer);
    if (err != kNoErr)
    {
        voip_log("quec_voip_sip_timer_stop failed");
    }

    return err;
}

static void quec_voip_sip_stop_check_timer(quec_voip_t *voip)
{
    voip->sip_info.check_time = 0;
    if (voip->sip_info.check_timer == NULL)
    {
        voip_log("timer is null");
        return;
    }

    voip_log("timer stop");
    quec_voip_sip_timer_stop(voip->sip_info.check_timer);
}



static bool quec_soc_voipsip_timer_is_running(quec_timer_t timer)
{
    return voip_timer_running(timer);
}

static BOOL quec_voip_sip_start_check_timer(quec_voip_t *voip)
{
    unsigned int next_delay = 0;

    if (voip == NULL)
    {
        return FALSE;
    }

    if (voip->sip_info.check_timer == NULL)
    {
        quec_rtos_timer_create(&voip->sip_info.check_timer);
    }

    if (quec_soc_voipsip_timer_is_running(voip->sip_info.check_timer))
    {
        return TRUE;
    }

    switch (voip->sip_info.check_time)
    {
        case 0:
            next_delay = quec_voip_sip_retry_schedule_ms[0];
            break;
        case 3000:
            next_delay = quec_voip_sip_retry_schedule_ms[1];
            break;
        case 5000:
            next_delay = quec_voip_sip_retry_schedule_ms[2];
            break;
        default:
            return FALSE;
    }

    voip->sip_info.check_time = next_delay;
    quec_rtos_timer_start(voip->sip_info.check_timer, next_delay, FALSE, quec_voip_sip_check_timer, voip);
    return TRUE;
}


void quec_voip_call_ring_timeout_cb(UINT32 *argv)
{
    quec_voip_call_session_t *call_info = argv;
    quec_voip_media_event_t   event;
    if (call_info == NULL)
    {
        return;
    }

    event.cid = call_info->call_id;
    event.tid = call_info->tid;
    event.did = call_info->did;
    event.type = QUEC_VOIP_EVENT_MEDIA_DESTROY;
    quec_voip_send_call_event(NULL, &event, QUEC_VOIP_EVENT_CALL_TIMEOUT);
}

/** @brief 为单路来电选择槽位，结束中的旧事务不再占用忙线状态。
 * @param[in] const quec_voip_t *voip 管理任务独占的呼叫集合。
 * @param[in] int cid 新来电的 SIP 编号。
 * @return 非负为槽位；-1 活动呼叫忙，-2 无空槽，-3 已结束 CID 的迟到请求。
 * @note 先查完整集合中的同 CID，再查空槽；不覆盖 END 槽位保留的 SIP 清理信息。
 */
static int quec_voip_incoming_slot(const quec_voip_t *voip, int cid)
{
    int free_slot = -2;
    int same_slot = -2;
    int busy = 0;
    for (int i = 0; i < 6; ++i)
    {
        const quec_voip_call_session_t *call = &voip->call[i];
        if (call->state == QUEC_VOIP_CALL_IDLE)
        {
            if (free_slot < 0) free_slot = i;
        }
        else if (call->call_id == cid)
        {
            if (call->state == QUEC_VOIP_CALL_END) return -3;
            same_slot = i;
        }
        else if (call->state != QUEC_VOIP_CALL_END)
        {
            busy = 1;
        }
    }
    if (same_slot >= 0) return same_slot;
    return busy ? -1 : free_slot;
}

/** @brief 校验来电 SDP、创建独立媒体配置并通知界面响铃。
 * @param[in,out] quec_voip_t *voip 管理任务拥有的实例。
 * @param[in] quec_voip_media_event_t *event 来电事务副本。
 * @return 0 成功，非零为忙线、资源、SDP 或响应错误。
 * @note END 槽位等候 SIP 最终清理；每次取得的 SDP 副本在本函数释放。
 */
static int quec_voip_call_invite(quec_voip_t *voip, quec_voip_media_event_t *event)
{
    osip_message_t   *answer = NULL;
    struct eXosip_t  *context_sip = NULL;
    unsigned char     i = 0;
    char             *audio_port = NULL;  // audio_port
    sdp_connection_t *attr_c = NULL;
    int               rtcp_port = 0;
    char              rtp_server[32] = {0};
    char              rtcp_server[32] = {0};
    char              temp_buff[256] = {0};

    sdp_message_t              *remote_sdp = NULL;
    sdp_media_t                *audio_sdp = NULL;
    quec_media_rtcp_attr_t      media_rtcp = {0};
    quec_voip_call_session_t   *call_info;
    uint8_t                     find_postion = 0xff;
    uint8_t                     support_pt_list[128] = {0};
    int                         supported_pt_count = 0;
    quec_voip_rtp_payload_t    *pt = NULL;
    quec_media_direction_e      media_direct = QUEC_MEDIA_DIRECTION_SENDRECV;
    osip_contact_t             *contact = NULL;
    int                         ret = QUEC_VOIP_OK;

    eXosip_dialog_t    *jd = NULL;
    eXosip_call_t      *jc = NULL;
    osip_transaction_t *invite_tr = NULL;

    if (voip == NULL || event == NULL || event->cid <= 0) return QUEC_VOIP_ERR_INVALID_PARAM;
    context_sip = (struct eXosip_t *)voip->sip_info.sip_context;
    if (!context_sip) return QUEC_VOIP_ERR_NOT_INIT;
    ret = quec_voip_incoming_slot(voip, event->cid);
    if (ret < 0)
    {
        int code = ret == -1 ? 486 : (ret == -3 ? 481 : 503);
        eXosip_lock(context_sip);
        eXosip_call_send_answer(context_sip, event->tid, code, NULL);
        eXosip_unlock(context_sip);
        voip_incoming_result(event->cid, code);
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }
    find_postion = (uint8_t)ret;
    call_info = &voip->call[find_postion];
    // 重复初始 INVITE 不重新开铃、不覆盖已连接状态，也不延长原响铃定时器。
    if (call_info->state != QUEC_VOIP_CALL_IDLE && event->type != QUEC_VOIP_EVENT_MEDIA_UPDATE)
        return QUEC_VOIP_OK;
    ret = QUEC_VOIP_OK;
    eXosip_lock(context_sip);
    remote_sdp = eXosip_get_remote_sdp(context_sip, event->did);
    eXosip_unlock(context_sip);
    audio_sdp = remote_sdp ? eXosip_get_audio_media(remote_sdp) : NULL;
    if (!audio_sdp)
    {
        ret = QUEC_VOIP_ERR_INVALID_PARAM;
        goto err_exit;
    }

    voip_log("eXosip msg audio_sdp=%p\n", audio_sdp);

    audio_port = audio_sdp->m_port;  // audio_port
    attr_c = eXosip_get_audio_connection(remote_sdp);
    voip_log("audio_sdp port = %s,tid=%d,did=%d\n", audio_port, event->tid, event->did);

    if (attr_c && attr_c->c_addr && audio_port && strlen(audio_port) != 0)
    {
        voip_log("%s : %s\n", attr_c->c_addrtype, attr_c->c_addr);
        if (ipaddr_aton(attr_c->c_addr, &call_info->media.rtp_remote.addr) == 0)
        {
            voip_log("error");
            ret = QUEC_VOIP_ERR_INVALID_PARAM;
            goto err_exit;
        }
        call_info->media.rtp_remote.port = atoi(audio_port);
    }
    else
    {
        voip_log("error");
        ret = QUEC_VOIP_ERR_INVALID_PARAM;
        goto err_exit;
    }

    if (voip_sdp_audio_rtcp(audio_sdp, &media_rtcp))
    {
        voip_log("rtcp error");
        ret = QUEC_VOIP_ERR_INVALID_PARAM;
        goto err_exit;
    }

    call_info->media.rtcp_remote.port = media_rtcp.port;
    if (ip_addr_isany(&media_rtcp.addr))
    {
        ip_addr_copy(call_info->media.rtcp_remote.addr, call_info->media.rtp_remote.addr);
    }
    else
    {
        ip_addr_copy(call_info->media.rtcp_remote.addr, media_rtcp.addr);
    }

    ip_addr_copy(call_info->media.rtcp_local.addr, voip->sip_info.local.addr);
    ip_addr_copy(call_info->media.rtp_local.addr, voip->sip_info.local.addr);
    call_info->media.rtp_local.port = QUEC_VOIP_GET_RANDOM_PORT;
    call_info->media.rtcp_local.port = call_info->media.rtp_local.port + 1;

    call_info->mo_call = FALSE;

    call_info->state = QUEC_VOIP_CALL_INCOMING;
    if (event->type == QUEC_VOIP_EVENT_MEDIA_CREAT)
    {
        voip_call_diag_begin(2, event->cid);
        voip_call_diag_step(QCM_VOIP_STAGE_INCOMING, 0);
    }

    call_info->call_id = event->cid;

    call_info->timer.ring = voip->sip_info.sip_cfg.ring_timeout;

    call_info->index = find_postion;

    eXosip_lock(context_sip);
    if (event->did > 0) _eXosip_call_dialog_find(context_sip, event->did, &jc, &jd);
    if (jc) invite_tr = _eXosip_find_last_invite(jc, jd);
    if (invite_tr && invite_tr->from && invite_tr->from->url && invite_tr->from->url->username)
    {
        unsigned count = MIN(strlen(invite_tr->from->url->username), sizeof(call_info->user_name) - 1U);
        memcpy(call_info->user_name, invite_tr->from->url->username, count);
        call_info->user_name[count] = 0;
    }
    eXosip_unlock(context_sip);

    supported_pt_count = quec_audio_sdp_has_supported_payload(audio_sdp, voip->sip_info.support_profile, support_pt_list, sizeof(support_pt_list));
    if (supported_pt_count <= 0)
    {
        voip_log("no supported payload found in offer\n");
        ret = QUEC_VOIP_ERR_SIP_PARAM_ILLEGAL;
        goto err_exit;
    }

    if (call_info->media.profile == NULL)
    {
        snprintf(temp_buff, sizeof(temp_buff), "call_%d", call_info->call_id);
        call_info->media.profile = rtp_profile_new(temp_buff);
        if (call_info->media.profile == NULL)
        {
            voip_log("profile creat failed\n");
            ret = QUEC_VOIP_ERR_INVALID_PARAM;
            goto err_exit;
        }
    }
    for (i = 0; i < supported_pt_count; i++)
    {
        pt = rtp_profile_get_payload(voip->sip_info.support_profile, support_pt_list[i]);
        if (pt == NULL)
        {
            voip_log("pt is null\n");
            ret = QUEC_VOIP_ERR_INVALID_PARAM;
            goto err_exit;
        }
        rtp_profile_set_payload(call_info->media.profile, support_pt_list[i], payload_type_clone(pt));
    }

    // osip_message_get_contact(remote_sdp, 0, &contact);

    ret = quec_voip_set_public_addr(context_sip, voip, call_info);
    if (ret != QUEC_VOIP_OK)
    {
        goto err_exit;
    }
    ipaddr_ntoa_r(&call_info->public.addr, temp_buff, sizeof(temp_buff));

    if (voip_sdp_direction(remote_sdp, audio_sdp, &media_direct) || media_direct == QUEC_MEDIA_DIRECTION_NONE)
    {
        call_info->rtp_mode = RTP_SESSION_SENDRECV;
    }
    else
    {
        if (media_direct == QUEC_MEDIA_DIRECTION_SENDONLY)
        {
            call_info->rtp_mode = QUEC_MEDIA_DIRECTION_RECVONLY;
        }
        else if (media_direct == QUEC_MEDIA_DIRECTION_RECVONLY)
        {
            call_info->rtp_mode = QUEC_MEDIA_DIRECTION_SENDONLY;
        }
        else
        {
            call_info->rtp_mode = (quec_media_mode_e)media_direct;
        }
    }

    voip_log("rtp mode=%d\n", call_info->rtp_mode);

    call_info->tid = event->tid;
    call_info->did = event->did;

    voip_log("public ip=%s,port=%d\n", temp_buff, call_info->public.port);

    if (event->type != QUEC_VOIP_EVENT_MEDIA_UPDATE)
    {
        if (sip_send_180_ring(context_sip, call_info))
        {
            voip_log("profile creat failed\n");
            ret = QUEC_VOIP_ERR_UNKNOWN;
            goto err_exit;
        }
    }

    if (call_info->timer.ring_m == NULL && quec_rtos_timer_create(&call_info->timer.ring_m))
    {
        ret = QUEC_VOIP_ERR_NO_MEMORY;
        goto err_exit;
    }

    quec_voip_notify_call_state(voip, call_info);
    quec_rtos_timer_start(call_info->timer.ring_m, call_info->timer.ring, FALSE, quec_voip_call_ring_timeout_cb, (uint32)call_info);

    sdp_message_free(remote_sdp);
    voip_incoming_result(event->cid, 0);
    return QUEC_VOIP_OK;
err_exit:
    if (call_info->media.profile)
    {
        rtp_profile_destroy(call_info->media.profile);
    }
    if (call_info->timer.ring_m)
    {
        quec_rtos_timer_stop(call_info->timer.ring_m);
        quec_rtos_timer_delete(call_info->timer.ring_m);
    }
    memset(call_info, 0, sizeof(quec_voip_call_session_t));
    if (remote_sdp) sdp_message_free(remote_sdp);
    eXosip_lock(context_sip);
    eXosip_call_send_answer(context_sip, event->tid, 415, answer);
    eXosip_unlock(context_sip);
    voip_incoming_result(event->cid, ret);
    return ret;
}

static int quec_voip_call_accept(quec_voip_t *voip, quec_voip_media_event_t *event)
{
    uint8_t                   find_postion = 0xff;
    quec_voip_call_session_t *call_info = NULL;
    char                      i = 0;
    int                       ret = QUEC_VOIP_OK;
    if (voip == NULL || event == NULL)
    {
        voip_log("param is null");
        ret = QUEC_VOIP_ERR_INVALID_PARAM;
        goto exit;
    }

    for (i = 0; i < 6; i++)
    {
        call_info = &voip->call[i];
        voip_log("cid=%d,tid=%d,did=%d,state=%d", call_info->call_id, call_info->tid, call_info->did, call_info->state);
        if (call_info->call_id == event->cid && (call_info->state == QUEC_VOIP_CALL_INCOMING || call_info->state == QUEC_VOIP_CALL_HOLD))
        {
            find_postion = i;
            break;
        }
    }

    if (find_postion == 0xff)
    {
        voip_log("can not find call");
        ret = QUEC_VOIP_ERR_INVALID_PARAM;
        goto exit;
    }

    voip_log("event tid=%d,cid=%d", event->tid, event->cid);
    ret = sdp_complete_200ok(voip, call_info);
    voip_call_diag_step(QCM_VOIP_STAGE_ANSWER, ret);
    if (ret != QUEC_VOIP_OK)
    {
        voip_log("can not find call");
        ret = QUEC_VOIP_ERR_UNKNOWN;
        goto exit;
    }

    call_info->state = QUEC_VOIP_CALL_CONNECT;
    quec_voip_notify_call_state(voip, call_info);

exit:
    if (call_info->timer.ring_m)
    {
        quec_rtos_timer_stop(call_info->timer.ring_m);
        quec_rtos_timer_delete(call_info->timer.ring_m);
        call_info->timer.ring_m = NULL;
    }

    return ret;
}

/** @brief 按主被叫角色终止呼叫；主叫的无临时应答窗口保留取消意图。
 * @param[in,out] quec_voip_t *voip 管理任务持有的会话实例。
 * @param[in] quec_voip_media_event_t *event 要结束的 SIP 呼叫标识。
 * @return 0 为已提交终止或已保留取消意图；非零时保留会话以便重试。
 * @note 仅管理任务调用；内部获取 SIP 锁。未接通主叫按 CID 取消；已接通按 DID 挂断。
 */
static int quec_voip_call_release(quec_voip_t *voip, quec_voip_media_event_t *event)
{
    quec_voip_call_session_t *call_info = NULL;
    struct eXosip_t *context_sip = NULL;
    osip_message_t *answer = NULL;
    int ret = QUEC_VOIP_OK;
    int did = 0;
    BOOL outgoing_early = FALSE;
    if (voip == NULL || event == NULL || voip->sip_info.sip_context == NULL)
    {
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }
    call_info = quec_voip_find_call_by_cid(voip, event->cid);
    if (call_info == NULL)
    {
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }
    context_sip = voip->sip_info.sip_context;
    if (call_info->mo_call)
    {
        call_info->cancel_requested = TRUE;
        if (call_info->cancel_sent)
        {
            return QUEC_VOIP_OK;
        }
        outgoing_early = call_info->state == QUEC_VOIP_CALL_ORIGINAL ||
                         call_info->state == QUEC_VOIP_CALL_ALERTING;
    }
    // 未接通的主叫按 CID 找当前初始 INVITE；鉴权重试可释放原 early dialog。
    // 已接通的主叫/被叫才使用 DID。与网络任务共享的事务必须在 SIP 锁内操作。
    did = outgoing_early ? 0 : call_info->did;
    eXosip_lock(context_sip);
    if (!call_info->mo_call && did <= 0)
    {
        ret = eXosip_call_build_answer(context_sip, call_info->tid, 486, &answer);
        if (ret == 0 && answer != NULL)
        {
            ret = eXosip_call_send_answer(context_sip, call_info->tid, 486, answer);
        }
        else if (ret == 0)
        {
            ret = QUEC_VOIP_ERR_UNKNOWN;
        }
    }
    else
    {
        ret = eXosip_call_terminate(context_sip, call_info->call_id, did);
    }
    eXosip_unlock(context_sip);
    voip_log("terminate cid=%d cached_did=%d used_did=%d state=%d rc=%d",
             call_info->call_id, call_info->did, did, call_info->state, ret);
    if (ret == OSIP_WRONG_STATE && outgoing_early)
    {
        // 尚无 1xx 或 200 已到但管理任务尚未 ACK；保留意图，后续事件完成取消。
        voip_call_diag_step(QCM_VOIP_STAGE_CANCEL_WAIT, 0);
        return QUEC_VOIP_OK;
    }
    if (ret < 0 || (!call_info->mo_call && did <= 0 && ret != 0))
    {
        voip_call_diag_step(QCM_VOIP_STAGE_TERMINATE, ret);
        return QUEC_VOIP_ERR_UNKNOWN;
    }
    // 本地 eXosip 的 CANCEL 返回 1、BYE 返回 0；仅代表 SIP 事务已提交。
    voip_call_diag_step(ret == 1 ? QCM_VOIP_STAGE_CANCEL_SENT : QCM_VOIP_STAGE_HANGUP_SENT, 0);
    call_info->cancel_sent = TRUE;
    // 对外通知结束之前释放本地媒体，不能等待服务器迟到的 CLOSED/RELEASED。
    // 保留 SIP 会话标识，迟到 200 仍可 ACK/BYE，最终销毁仍按原 CID 执行。
    quec_voip_stop_media_for_profile_update(call_info);
    call_info->state = QUEC_VOIP_CALL_END;
    if (call_info->timer.ring_m)
    {
        quec_rtos_timer_stop(call_info->timer.ring_m);
        quec_rtos_timer_delete(call_info->timer.ring_m);
        call_info->timer.ring_m = NULL;
    }
    quec_voip_notify_call_state(voip, call_info);
    return QUEC_VOIP_OK;
}

static void quec_recv_tev_cb(RtpSession *session, unsigned long type, unsigned long dummy, void *user_data)
{
    voip_log("Receiving telephony event:%lu\n", type);
}

static void quec_recv_payload_change_cb(RtpSession *session, void *arg1, void *arg2, void *arg3)
{
    voip_log("Receiving payload change is %d", session->rcv.pt);
}


static void quec_voip_rtcp_sr_rr(const OrtpEventData *evd, void *ctx)
{
    quec_voip_call_session_t *call = (quec_voip_call_session_t *)ctx;
    mblk_t                   *block = NULL;
    unsigned char            *r_pos = NULL;
    quec_voip_media_event_t   event = {0};
    char                      i = 0;
    BOOL                      find_bye = FALSE;
    if (call == NULL || evd == NULL || evd->packet == NULL)
    {
        voip_log("voip param is null");
        return;
    }
    if (rtcp_is_RR(evd->packet))
    {
        voip_log("voip param is RR");
        event.type = QUEC_VOIP_EVENT_MEDIA_RTCP_RR_BYE;
    }
    else if (rtcp_is_SR(evd->packet))
    {
        voip_log("voip param is SR");
        event.type = QUEC_VOIP_EVENT_MEDIA_RTCP_SR_BYE;
    }
    else
    {
        voip_log("UNKNOWN TYPE");
        return;
    }

    r_pos = evd->packet->b_rptr;
    do {
        struct timeval        reception_date;
        const report_block_t *rb;
        if (rtcp_is_BYE(evd->packet))
        {
            voip_log("voip param is BYE");
            find_bye = TRUE;
        }
    } while (rtcp_next_packet(evd->packet));

    evd->packet->b_rptr = r_pos;

    if (find_bye)
    {
        event.cid = call->call_id;
        voip_log("rtcp bye event send");
        quec_voip_send_call_event(NULL, &event, QUEC_VOIP_EVENT_CALL_MEDIA_DESTROY);
    }
}

quec_queue_t quec_rtp_recv_msgq = NULL;
quec_timer_t quec_rtp_timer = NULL;
quec_task_t  quec_rtp_recv = NULL;
static void quec_rtp_recv_task(void *argv)
{
    unsigned int                  ts = 0;
    unsigned int                  inc = 0;
    unsigned int                  ts_cur = 0; /* MIKE_MEDIA_DBG: ts used by the current receive */
    int                           have_more;
    int                           len = 0;
    quec_voip_call_session_t     *call = (quec_voip_call_session_t *)argv;
    RtpSession                   *session = NULL;
    unsigned char                 recvData[QUEC_VOIP_RTP_AUDIO_BUFFER_SIZE] = {0};
    quec_voip_rtp_payload_t      *pt = NULL;
    quec_voip_media_profile_cb_t *media_cb = NULL;
    //    int           totoal_len = sizeof(pcma_buff);
    int offset = 0;
    if (call == NULL || call->media.pref_pt == NULL)
    {
        voip_log("argv is null");

        return;
    }

check_again:
    pt = call->media.pref_pt;
    voip_log("pt=%s", pt->mime_type);
    media_cb = (quec_voip_media_profile_cb_t *)pt->user_data;
    if (media_cb == NULL || media_cb->media_open == NULL || media_cb->media_write == NULL || media_cb->media_close == NULL)
    {
        voip_log("pt=%s cb is null", pt->mime_type);
        return;
    }

    if (call->rtp_mode == RTP_SESSION_RECVONLY || call->rtp_mode == RTP_SESSION_SENDRECV)
    {
        int open_rc = media_cb->media_open(&media_cb->fd_write, pt->clock_rate, pt->channels, (QL_PCM_NONBLOCK_FLAG | QL_PCM_WRITE_FLAG), NULL);
        voip_call_diag_open(false, open_rc);
        if (open_rc != 0) { voip_log("playback open failed rc=%d", open_rc); return; }
    }
    session = (RtpSession *)call->rtp_context;
    while (!call->stop_rx)
    {
        ortp_ev_dispatcher_iterate(call->d);
        if (call->rtp_mode != RTP_SESSION_RECVONLY && call->rtp_mode != RTP_SESSION_SENDRECV)
        {
            if (media_cb->fd_write)
            {
                media_cb->media_close(media_cb->fd_write);
                media_cb->fd_write = NULL;
            }
            voip_log("rtp mode=%d", call->rtp_mode);
            voip_log("will suspend 200ms");
            quec_rtos_task_sleep_ms(200);
            continue;
        }
        else
        {
            if (media_cb->fd_write == NULL)
            {
                goto check_again;
            }

        }
        have_more = 0;
        len = 0;
        ts_cur = ts;
        g_media_dbg.rcv_loop++; /* MIKE_MEDIA_DBG */
        len = rtp_session_recv_with_ts(session, (uint8_t *)recvData, QUEC_VOIP_RTP_AUDIO_BUFFER_SIZE, ts, &have_more);
#if VOIP_MEDIA_VERBOSE
        voip_log("len=%d", len);
#endif
        if (len > 0)
        {
            g_media_dbg.rcv_ok++; /* MIKE_MEDIA_DBG */
            int written = media_cb->media_write(media_cb->fd_write, (void *)recvData, len, pt);
            if (written != len)
            {
                qosa_enter_critical(); ++g_call_diag.play_errors; qosa_exit_critical();
            }

            inc = len * 8 / pt->bits_per_sample;
#if VOIP_MEDIA_VERBOSE
            voip_log("recv tm_inc=%d", inc);
#endif
            ts += inc;
            inc = 0;
        }
        else
        {
            g_media_dbg.rcv_idle++; /* MIKE_MEDIA_DBG */
            quec_rtos_task_sleep_ms(10);
            ts += pt->clock_rate / 50;
            inc = 0;
        }
        /* MIKE_MEDIA_DBG: report after the voice frame has been played out */
        if ((g_media_dbg.rcv_loop % 100) == 0)
            media_dbg_log("MIKE_RCV lp=%u ok=%u idle=%u ts=%u", g_media_dbg.rcv_loop, g_media_dbg.rcv_ok, g_media_dbg.rcv_idle, ts_cur);
        if (have_more)
        {
            continue;
        }
    }
}

static void quec_rtp_send_task(void *argv)
{
    unsigned int                  ts = 0;
    unsigned int                  inc = 0;
    int                           len = 0;
    quec_voip_call_session_t     *call = (quec_voip_call_session_t *)argv;
    RtpSession                   *session = NULL;
    unsigned char                 read_data[QUEC_VOIP_RTP_AUDIO_BUFFER_SIZE] = {0};
    quec_voip_rtp_payload_t      *pt = NULL;
    quec_voip_media_profile_cb_t *media_cb = NULL;
    //    int           totoal_len = sizeof(pcma_buff);
    int offset = 0;
    if (call == NULL || call->media.pref_pt == NULL)
    {
        voip_log("argv is null");
        return;
    }

check_again:
    pt = call->media.pref_pt;
    voip_log("pt=%s", pt->mime_type);
    media_cb = (quec_voip_media_profile_cb_t *)pt->user_data;
    if (media_cb == NULL || media_cb->media_open == NULL || media_cb->media_read == NULL || media_cb->media_close == NULL)
    {
        voip_log("pt=%s cb is null", pt->mime_type);
        return;
    }

    if (call->rtp_mode == RTP_SESSION_SENDONLY || call->rtp_mode == RTP_SESSION_SENDRECV)
    {
        int open_rc = media_cb->media_open(&media_cb->fd_read, pt->clock_rate, pt->channels, (QL_PCM_BLOCK_FLAG | QL_PCM_READ_FLAG), NULL);
        voip_call_diag_open(true, open_rc);
        if (open_rc != 0) { voip_log("record open failed rc=%d", open_rc); return; }
    }
    session = (RtpSession *)call->rtp_context;

    while (!call->stop_tx)
    {
        if (call->rtp_mode != RTP_SESSION_SENDONLY && call->rtp_mode != RTP_SESSION_SENDRECV)
        {
            if (media_cb->fd_read)
            {
                media_cb->media_close(media_cb->fd_read);
                media_cb->fd_read = NULL;
            }
            voip_log("rtp mode=%d", call->rtp_mode);
            quec_rtos_task_sleep_ms(20);
            continue;
        }
        else
        {
            if (media_cb->fd_read == NULL)
            {
                goto check_again;
            }

        }

        len = 0;
		//阻塞接口
        g_media_dbg.snd_loop++; /* MIKE_MEDIA_DBG */
        len = media_cb->media_read(media_cb->fd_read, read_data, QUEC_VOIP_RTP_AUDIO_BUFFER_SIZE, pt);
#if VOIP_MEDIA_VERBOSE
        voip_log("len=%d", len);
#endif
        if (len > 0)
        {
            int sent = rtp_session_send_with_ts(session, (uint8_t *)read_data, len, ts);
            if (sent > 0) g_media_dbg.snd_ok++;
            else
            {
                qosa_enter_critical(); ++g_call_diag.send_errors; qosa_exit_critical();
            }
            inc = len * 8 / pt->bits_per_sample;
#if VOIP_MEDIA_VERBOSE
            voip_log("send tm_inc=%d", inc);
#endif
        }
        else
        {
            g_media_dbg.snd_idle++; /* MIKE_MEDIA_DBG */
            quec_rtos_task_sleep_ms(20);
        }
        /* MIKE_MEDIA_DBG: report after the RTP packet has been sent */
        if ((g_media_dbg.snd_loop % 100) == 0)
            media_dbg_log("MIKE_SND lp=%u ok=%u idle=%u ts=%u", g_media_dbg.snd_loop, g_media_dbg.snd_ok, g_media_dbg.snd_idle, ts);
        ts += inc;
        inc = 0;
    }
}
#ifndef RECORD_TASK_PRIORITY
#ifdef  QUECTEL_PROJECT_CUST
#define RECORD_TASK_PRIORITY         (74)  //@20241017 Fixed Voice delay caused by receiving short messages during a call after UAC is enabled
#else
#define RECORD_TASK_PRIORITY         (80)
#endif
#endif

static void *voip_rx_main(void *arg) { quec_rtp_recv_task(arg); return NULL; }
static void *voip_tx_main(void *arg) { quec_rtp_send_task(arg); return NULL; }
static int voip_media_start(quec_voip_call_session_t *call,bool transmit)
{
    voip_thread_attr_t attr={0};
    attr.stackSize=5*1024;
    attr.taskName=transmit ? "voip_tx" : "voip_rx";
    if (transmit) call->stop_tx=false; else call->stop_rx=false;
    return voip_thread_create((voip_thread_t *)(transmit ? &call->send_task : &call->recv_task),&attr,
                              transmit ? voip_tx_main : voip_rx_main,call);
}

static int quec_voip_call_media_creat(quec_voip_t *voip, quec_voip_media_event_t *event)
{
    uint8_t                       find_postion = 0xff;
    quec_voip_call_session_t     *call_info = NULL;
    RtpSession                   *session = NULL;
    quec_voip_rtp_profile_t      *call_profile = NULL;
    quec_voip_rtp_payload_t      *pt = NULL;
    quec_voip_media_profile_cb_t *media_cb = NULL;
    char                          i = 0;
    char                         *ssrc = NULL;
    char                          task_name[16] = {0};
    // quec_media_mode_e             rtp_mode=RTP_SESSION_SENDRECV;
    if (voip == NULL || event == NULL)
    {
        voip_log("param is null");
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }

    for (i = 0; i < 6; i++)
    {
        call_info = &voip->call[i];
        voip_log("cid=%d,tid=%d,did=%d,state=%d", call_info->call_id, call_info->tid, call_info->did, call_info->state);
        if (call_info->call_id == event->cid &&
            (call_info->state == QUEC_VOIP_CALL_CONNECT ||
             (call_info->mo_call && !call_info->cancel_requested && call_info->state == QUEC_VOIP_CALL_ALERTING)))
        {
            find_postion = i;
            break;
        }
    }

    if (find_postion == 0xff)
    {
        voip_log("can not find call");
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }

    call_info->call_id = event->cid;
    call_info->tid = event->tid;
    call_info->did = event->did;

    call_profile = call_info->media.profile;

    if (call_profile == NULL)
    {
        voip_log("can not find call");
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }

    for (i = 0; i < RTP_PROFILE_MAX_PAYLOADS; i++)
    {
        pt = call_profile->payload[i];
        if (pt)
        {
            voip_log("i=%d,type=%s", i, pt->mime_type);
            break;
        }
    }

    if (pt == NULL)
    {
        voip_log("can not find call");
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }

    static BOOL ortp_ready=FALSE;
    if (!ortp_ready) { ortp_init(); ortp_scheduler_init(); ortp_ready=TRUE; }
    ortp_scheduler_init();
    session = call_info->rtp_context;
    // rtp_mode=(call_info->rtp_mode==RTP_SESSION_INACTIVE)?RTP_SESSION_RECVONLY:call_info->rtp_mode;
    if (session == NULL)
    {
        session = rtp_session_new(RTP_SESSION_SENDRECV);
        call_info->rtp_context = session;
    }
    // rtp_session_set_symmetric_rtp(session, TRUE);
    if (!session) return QUEC_VOIP_ERR_NO_MEMORY;
    rtp_session_set_profile(session, call_profile);
    rtp_session_set_scheduling_mode(session, 1);
    rtp_session_set_blocking_mode(session, 1);
    rtp_session_set_connected_mode(session, TRUE);

    call_info->media.pref_pt = pt;
    rtp_session_set_send_payload_type(session, i);

    rtp_session_signal_connect(session, "telephone-event", (RtpCallback)quec_recv_tev_cb, (void *)call_info);
    rtp_session_signal_connect(session, "payload_type_changed", (RtpCallback)quec_recv_payload_change_cb, (void *)call_info);

    rtp_session_set_local_addr(session, ipaddr_ntoa(&call_info->media.rtp_local.addr), call_info->media.rtp_local.port, call_info->media.rtcp_local.port);
    rtp_session_set_remote_addr_full(
        session,
        ipaddr_ntoa(&call_info->media.rtp_remote.addr),
        call_info->media.rtp_remote.port,
        ipaddr_ntoa(&call_info->media.rtcp_remote.addr),
        call_info->media.rtcp_remote.port
    );
    // rtp_session_set_remote_addr(session, sdp_msg->rtp.remote_addr,
    // sdp_msg->rtp.remote_port);
    ssrc = getenv("SSRC");
    if (ssrc != NULL)
    {
        voip_log("using SSRC=%i.\n", atoi(ssrc));
        rtp_session_set_ssrc(session, atoi(ssrc));
    }

    if (call_info->mo_call && call_info->state == QUEC_VOIP_CALL_ALERTING)
    {
        // 180/183 的下行提示音不代表接听；保持振铃状态，只接收不采集麦克风。
        call_info->rtp_mode = RTP_SESSION_RECVONLY;
    }
    else if (call_info->rtp_mode != RTP_SESSION_SENDRECV)
    {
        call_info->state = QUEC_VOIP_CALL_HOLD;
    }
    else
    {
        call_info->state = QUEC_VOIP_CALL_CONNECT;
    }

    OrtpEvDispatcher *d = NULL;

    d = ortp_ev_dispatcher_new(session);
    call_info->d = d;
    if (!d) { quec_voip_stop_media_for_profile_update(call_info); return QUEC_VOIP_ERR_NO_MEMORY; }

    ortp_ev_dispatcher_connect(d, ORTP_EVENT_RTCP_PACKET_RECEIVED, RTCP_SR, quec_voip_rtcp_sr_rr, call_info);

    ortp_ev_dispatcher_connect(d, ORTP_EVENT_RTCP_PACKET_RECEIVED, RTCP_RR, quec_voip_rtcp_sr_rr, call_info);


    if (call_info->recv_task == NULL)
    {
        snprintf(task_name, sizeof(task_name), "call_R%d", call_info->call_id);
        if (voip_media_start(call_info,false)) { quec_voip_stop_media_for_profile_update(call_info); return QUEC_VOIP_ERR_NO_MEMORY; }
    }

    if (call_info->send_task == NULL && (call_info->rtp_mode == RTP_SESSION_SENDRECV || call_info->rtp_mode == RTP_SESSION_SENDONLY))
    {
        snprintf(task_name, sizeof(task_name), "call_S%d", call_info->call_id);
        if (voip_media_start(call_info,true)) { quec_voip_stop_media_for_profile_update(call_info); return QUEC_VOIP_ERR_NO_MEMORY; }
    }

    rtp_session_rr(session);

    return QUEC_VOIP_OK;
}

/*
 * Finalize the MO answered path in the manage thread: validate remote SDP, send
 * ACK for the 200 OK, and queue media creation only after the call context is
 * ready.
 */
/*
 * Clone the global supported RTP profile into a per-call profile so payload
 * negotiation and runtime changes do not mutate the shared template.
 */
/** @brief 完成主叫 200 应答；取消交叉时 ACK 后结束，不创建媒体。
 * @param[in,out] quec_voip_t *voip 管理任务持有的会话实例。
 * @param[in,out] quec_voip_media_event_t *event 当前应答的呼叫、事务和对话编号。
 * @return 0 为处理成功，非零为 SDP、ACK、终止或媒体入队失败。
 * @note 仅管理任务调用；媒体请求携带副本，ACK 所有权交给 SIP 发送接口。
 */
static int quec_voip_call_mo_answered(quec_voip_t *voip, quec_voip_media_event_t *event)
{
    quec_voip_call_session_t *call_info = NULL;
    struct eXosip_t          *context_sip = NULL;
    osip_message_t           *ack = NULL;
    osip_message_t           *bye = NULL;
    int                       ret = QUEC_VOIP_OK;

    if (voip == NULL || event == NULL)
    {
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }

    call_info = quec_voip_find_call_by_cid(voip, event->cid);
    if (call_info == NULL || call_info->mo_call == FALSE)
    {
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }

    call_info->tid = event->tid;
    call_info->did = event->did;

    context_sip = (struct eXosip_t *)voip->sip_info.sip_context;

    if (call_info->cancel_requested)
    {
        // CANCEL 与 200 交叉时仍须 ACK，再发 BYE；不能重新启动录放音。
        ret = eXosip_call_build_ack(context_sip, event->tid, &ack);
        voip_call_diag_step(QCM_VOIP_STAGE_ACK_BUILD, ret != 0 ? ret : (ack == NULL ? QUEC_VOIP_ERR_UNKNOWN : 0));
        if (ret != 0 || ack == NULL)
        {
            ret = QUEC_VOIP_ERR_UNKNOWN;
            goto exit;
        }
        ret = eXosip_call_send_ack(context_sip, event->tid, ack);
        voip_call_diag_step(QCM_VOIP_STAGE_ACK_SEND, ret);
        if (ret != 0)
        {
            goto exit;
        }
        // ACK 已完成，内部状态转为已接通以选择 BYE；不通知界面，也不启动媒体。
        call_info->state = QUEC_VOIP_CALL_CONNECT;
        call_info->cancel_sent = FALSE;
        ret = quec_voip_call_release(voip, event);
        goto exit;
    }

    ret = quec_voip_apply_remote_audio_sdp(voip, call_info, event->did);
    voip_call_diag_step(QCM_VOIP_STAGE_REMOTE_SDP, ret);
    if (ret)
    {
        voip_log("mo answer sdp match fail ret=%d,cid=%d,tid=%d,did=%d", ret, event->cid, event->tid, event->did);

        if (event->tid > 0)
        {
            if (eXosip_call_build_ack(context_sip, event->tid, &ack) == 0 && ack != NULL)
            {
                if (eXosip_call_send_ack(context_sip, event->tid, ack))
                {
                    voip_log("mo answer ack send fail,cid=%d,tid=%d", event->cid, event->tid);					
                }
            }
            else
            {
                voip_log("mo answer ack build fail,cid=%d,tid=%d", event->cid, event->tid);
            }
        }

        if (event->did > 0)
        {
            if (eXosip_call_build_request(context_sip, event->did, "BYE", &bye) == 0 && bye != NULL)
            {
                osip_message_set_header(bye, "Reason", "SIP;cause=488;text=\"Not Acceptable Here\"");
                if (eXosip_call_send_request(context_sip, event->did, bye))
                {
                    voip_log("mo answer bye send fail,cid=%d,did=%d", event->cid, event->did);
                    if (eXosip_call_terminate(context_sip, event->cid, event->did))
                    {
                        voip_log("mo answer terminate fail,cid=%d,did=%d", event->cid, event->did);
                    }
                }
            }
            else
            {
                voip_log("mo answer bye build fail,cid=%d,did=%d", event->cid, event->did);
                if (eXosip_call_terminate(context_sip, event->cid, event->did))
                {
                    voip_log("mo answer terminate fail,cid=%d,did=%d", event->cid, event->did);
                }
            }
        }

        call_info->state = QUEC_VOIP_CALL_END;
        goto exit;
    }

    ret = eXosip_call_build_ack(context_sip, event->tid, &ack);
    voip_call_diag_step(QCM_VOIP_STAGE_ACK_BUILD, ret != 0 ? ret : (ack == NULL ? QUEC_VOIP_ERR_UNKNOWN : 0));
    if (ret || ack == NULL)
    {
        ret = QUEC_VOIP_ERR_UNKNOWN;
        goto exit;
    }

    ret = eXosip_call_send_ack(context_sip, event->tid, ack);
    voip_call_diag_step(QCM_VOIP_STAGE_ACK_SEND, ret);
    if (ret)
    {
        ret = QUEC_VOIP_ERR_UNKNOWN;
        goto exit;
    }

    event->type = QUEC_VOIP_EVENT_MEDIA_CREAT;
    ret = quec_voip_send_call_event(voip, event, QUEC_VOIP_EVENT_CALL_MEDIA_CREAT);
    voip_call_diag_step(QCM_VOIP_STAGE_MEDIA_QUEUE, ret);
    if (ret != 0)
    {
        ret = QUEC_VOIP_ERR_UNKNOWN;
        goto exit;
    }
    call_info->state = QUEC_VOIP_CALL_CONNECT;
    quec_voip_notify_call_state(voip, call_info);

exit:
    if (call_info->timer.ring_m)
    {
        quec_rtos_timer_stop(call_info->timer.ring_m);
        quec_rtos_timer_delete(call_info->timer.ring_m);
        call_info->timer.ring_m = NULL;
    }
    return ret;
}

/*
 * Keep MO ringing/proceeding state updates in the manage thread so call context
 * mutations are serialized in one place.
 */
/** @brief 更新主叫临时应答；有取消意图时继续终止而不恢复振铃状态。
 * @param[in,out] quec_voip_t *voip 当前会话实例。
 * @param[in] quec_voip_media_event_t *event 最新事务和对话编号。
 * @return 0 为更新成功或取消已提交，其他为无效会话或终止失败。
 * @note 仅管理任务调用，不拥有 event 内存。
 */
static int quec_voip_call_mo_alerting(quec_voip_t *voip, quec_voip_media_event_t *event)
{
    quec_voip_call_session_t *call_info = NULL;

    if (voip == NULL || event == NULL)
    {
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }

    call_info = quec_voip_find_call_by_cid(voip, event->cid);
    if (call_info == NULL || call_info->mo_call == FALSE)
    {
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }

    // 已接通后的迟到临时应答不覆盖事务/对话编号，也不退回振铃或改回低音量。
    // 取消中的呼叫仍需最新编号，供终止路径处理 CANCEL 与 200 的交叉。
    if (!call_info->cancel_requested && call_info->state != QUEC_VOIP_CALL_ORIGINAL &&
        call_info->state != QUEC_VOIP_CALL_ALERTING)
        return QUEC_VOIP_OK;
    call_info->tid = event->tid;
    call_info->did = event->did;
    if (call_info->cancel_requested)
    {
        return quec_voip_call_release(voip, event);
    }
    call_info->state = QUEC_VOIP_CALL_ALERTING;
    quec_voip_notify_call_state(voip, call_info);
    // 没有 SDP 的 180 仅更新状态；有 SDP 的 180/183 才启动下行回铃媒体。
    // 200 应答路径会先关闭这一条流，再按通话等级打开双向录放。
    if (event->did > 0 && call_info->rtp_context == NULL &&
        quec_voip_apply_remote_audio_sdp(voip, call_info, event->did) == QUEC_VOIP_OK)
    {
        int ret = quec_voip_call_media_creat(voip, event);
        if (ret) voip_log("early media start failed cid=%d rc=%d", event->cid, ret);
    }
    return QUEC_VOIP_OK;
}

static int quec_voip_call_media_destroy(quec_voip_t *voip, quec_voip_media_event_t *event)
{
    uint8_t                       find_postion = 0xff;
    quec_voip_call_session_t     *call_info = NULL;
    RtpSession                   *session = NULL;
    quec_voip_rtp_payload_t      *pt = NULL;
    quec_voip_media_profile_cb_t *media_cb = NULL;
    char                          i = 0;
    char                         *ssrc = NULL;
    char                          task_name[16] = {0};
    quec_media_mode_e             rtp_mode;
    if (voip == NULL || event == NULL)
    {
        voip_log("param is null");
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }

    for (i = 0; i < 6; i++)
    {
        call_info = &voip->call[i];
        voip_log("cid=%d,tid=%d,state=%d", call_info->call_id, call_info->tid, call_info->state);
        if (call_info->call_id == event->cid)
        {
            find_postion = i;
            break;
        }
    }

    if (find_postion == 0xff)
    {
        voip_log("can not find call");
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }

    pt = call_info->media.pref_pt;
    rtp_mode = call_info->rtp_mode;
    session = call_info->rtp_context;

    do {
        if (event->type != QUEC_VOIP_EVENT_MEDIA_DESTROY && event->type != QUEC_VOIP_EVENT_MEDIA_RTCP_SR_BYE)
        {
            break;
        }

#if 1
        if (event->type == QUEC_VOIP_EVENT_MEDIA_DESTROY && pt)
        {
            voip_media_stop(call_info,false);
            media_cb = (quec_voip_media_profile_cb_t *)pt->user_data;
            if (media_cb && media_cb->fd_write && media_cb->media_close)
            {
                media_cb->media_close(media_cb->fd_write);
                media_cb->fd_write = NULL;
                voip_log("audio read close");
            }
        }
#endif

        if (rtp_mode == RTP_SESSION_RECVONLY)
        {
            rtp_mode = RTP_SESSION_INACTIVE;
        }
        else if (rtp_mode == RTP_SESSION_SENDRECV)
        {
            rtp_mode = RTP_SESSION_SENDONLY;
        }
        else
        {
            break;
        }

        if (session)
        {
            rtp_session_rr_bye(session, NULL);  // send SR or RR
        }
    } while (0);

    do {
        if (event->type != QUEC_VOIP_EVENT_MEDIA_DESTROY && event->type != QUEC_VOIP_EVENT_MEDIA_RTCP_RR_BYE)
        {
            break;
        }

        if (rtp_mode == RTP_SESSION_SENDONLY)
        {
            rtp_mode = RTP_SESSION_INACTIVE;
        }
        else if (rtp_mode == RTP_SESSION_SENDRECV)
        {
            rtp_mode = RTP_SESSION_RECVONLY;
        }
        else
        {
            break;
        }

        if (!pt)
        {
            break;
        }

        voip_media_stop(call_info,true);
        media_cb = (quec_voip_media_profile_cb_t *)pt->user_data;
        if (media_cb && media_cb->fd_read && media_cb->media_close)
        {
            media_cb->media_close(media_cb->fd_read);
            media_cb->fd_read = NULL;
            voip_log("audio write close");
        }

        if (session)
        {
            rtp_session_sr_bye(session, NULL);  // send SR or RR
        }
    } while (0);

    call_info->rtp_mode = rtp_mode;
    if (event->type == QUEC_VOIP_EVENT_MEDIA_DESTROY)
    {
        voip_media_stop(call_info,false);
        voip_media_stop(call_info,true);
        if (pt && pt->user_data) {
            media_cb=pt->user_data;
            if (media_cb->fd_read) { media_cb->media_close(media_cb->fd_read); media_cb->fd_read=NULL; }
            if (media_cb->fd_write) { media_cb->media_close(media_cb->fd_write); media_cb->fd_write=NULL; }
        }
        call_info->state = QUEC_VOIP_CALL_END;
        quec_voip_notify_call_state(voip, call_info);

        if (session)
        {
            if (call_info->d)
            {
                ortp_ev_dispatcher_destroy(call_info->d);
            }
            rtp_session_destroy(session);
            // ortp_exit();
            ortp_global_stats_display();
            voip_log("call[%d] rtp destroy", call_info->call_id);
        }

        if (call_info->media.profile)
        {
            rtp_profile_destroy(call_info->media.profile);
        }
        /* 4xx/5xx/6xx final response for the outgoing INVITE reaches here */
        /* directly; call_accept / call_release / call_mo_answered never run, */
        /* so the ring timer must be freed before the memset below. */
        if (call_info->timer.ring_m)
        {
            quec_rtos_timer_stop(call_info->timer.ring_m);
            quec_rtos_timer_delete(call_info->timer.ring_m);
            call_info->timer.ring_m = NULL;
        }

        voip_log("will destory");
        memset(call_info, 0, sizeof(quec_voip_call_session_t));
    }
    return QUEC_VOIP_OK;
}

/** @brief 将接听或挂断操作投递给管理任务，并向调用者传播入队失败。
 * @param[in] unsigned char call_id 应用呼叫槽位，0xff 表示匹配全部活动槽位。
 * @param[in] BOOL accept true 为接听，false 为拒接/挂断。
 * @return 0 表示单个请求已入队；非零为未初始化、未找到或入队失败。
 * @note 只提交请求，最终状态由回调通知；不得从 ISR 调用。
 */
int quec_voip_call_op(unsigned char call_id, BOOL accept)
{
    quec_voip_media_event_t call_event = {0};
    char                    i = 0;
    int ret = 0;

    if (quec_voip == NULL) return QUEC_VOIP_ERR_NOT_INIT;
    call_event.cid = call_id;
    voip_log("accept=%d", accept);
    for (i = 0; i < 6; i++)
    {
        if (((quec_voip->call[i].state == QUEC_VOIP_CALL_ALERTING || quec_voip->call[i].state == QUEC_VOIP_CALL_INCOMING) && accept)
            || (quec_voip->call[i].state != QUEC_VOIP_CALL_IDLE && !accept))
        {
            call_event.cid = quec_voip->call[i].call_id;
            call_event.tid = quec_voip->call[i].tid;
            call_event.did = (quec_voip->call[i].did == 0) ? -1 : quec_voip->call[i].did;
            voip_log("cid=%d,tid=%d,did=%d,status=%d", call_event.cid, call_event.tid, call_event.did, quec_voip->call[i].state);

            if (call_id == 0xff || call_id == quec_voip->call[i].index)
            {
                if (accept)
                {
                    call_event.type = QUEC_VOIP_EVENT_MEDIA_CREAT;
                    ret = quec_voip_send_call_event(NULL, &call_event, QUEC_VOIP_EVENT_CALL_ACCEPT);
                }
                else
                {
                    call_event.type = QUEC_VOIP_EVENT_MEDIA_DESTROY;
                    ret = quec_voip_send_call_event(NULL, &call_event, QUEC_VOIP_EVENT_CALL_RELEASE);
                }

                if (ret != 0) return ret;
                if (call_id != 0xff)
                {
                    break;
                }
            }
        }
    }

    if (i == 6)
    {
        voip_log("find error");
        return 2;
    }

    return 0;
}

/*
 * Build the initial MO call context and INVITE in one place, but leave
 * follow-up state transitions to quec_voip_manage via queued events.
 */
/*
 * Tear down RTP resources in stages so RTCP BYE and media handles are released
 * consistently before the call context is cleared.
 */
static int quec_voip_make_call_inner(quec_voip_t *voip, quec_voip_mo_call_t *mo_call)
{
    quec_voip_call_session_t *call_info = NULL;
    struct eXosip_t          *context_sip = NULL;
    osip_message_t           *invite = NULL;
    osip_contact_t           *contact = NULL;
    char                      from[64] = {0};
    char                      to[64] = {0};
    char                      route[64] = {0};
    char                      local_sdp[512] = {0};
    char                      profile_name[32] = {0};
    unsigned char             i = 0;
    int                       ret = QUEC_VOIP_OK;

    if (voip == NULL || mo_call == NULL || mo_call->account[0] == '\0')
    {
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }

    if (voip->sip_info.is_regist == FALSE || voip->sip_info.sip_context == NULL)
    {
        return QUEC_VOIP_ERR_NOT_INIT;
    }

    for (i = 0; i < 6; i++)
    {
        if (voip->call[i].state == QUEC_VOIP_CALL_IDLE)
        {
            call_info = &voip->call[i];
            break;
        }
    }

    if (call_info == NULL)
    {
        return QUEC_VOIP_ERR_UNKNOWN;
    }

    context_sip = (struct eXosip_t *)voip->sip_info.sip_context;
    memset(call_info, 0, sizeof(quec_voip_call_session_t));

    call_info->state = QUEC_VOIP_CALL_ORIGINAL;
    call_info->mo_call = TRUE;
    voip_call_diag_begin(1, 0);
    voip_call_diag_step(QCM_VOIP_STAGE_DIAL, 0);
    call_info->index = i;
    call_info->rtp_mode = RTP_SESSION_SENDRECV;
    call_info->timer.ring = voip->sip_info.sip_cfg.ring_timeout;
    strncpy(call_info->user_name, mo_call->account, sizeof(call_info->user_name) - 1);
    ip_addr_copy(call_info->media.rtp_local.addr, voip->sip_info.local.addr);
    ip_addr_copy(call_info->media.rtcp_local.addr, voip->sip_info.local.addr);
    call_info->media.rtp_local.port = QUEC_VOIP_GET_RANDOM_PORT;
    call_info->media.rtcp_local.port = call_info->media.rtp_local.port + 1;

    ret = quec_voip_set_public_addr(context_sip, voip, call_info);
    if (ret)
    {
        memset(call_info, 0, sizeof(quec_voip_call_session_t));
        return ret;
    }

    snprintf(profile_name, sizeof(profile_name), "call_%u", i);
    ret = quec_voip_clone_support_profile(&call_info->media.profile, voip->sip_info.support_profile, profile_name);
    if (ret)
    {
        memset(call_info, 0, sizeof(quec_voip_call_session_t));
        return ret;
    }

    ret = quec_voip_build_local_sdp(call_info, local_sdp, sizeof(local_sdp));
    if (ret)
    {
        rtp_profile_destroy(call_info->media.profile);
        memset(call_info, 0, sizeof(quec_voip_call_session_t));
        return ret;
    }

    snprintf(from, sizeof(from), "sip:%s@%s", voip->sip_info.sip_cfg.account, ipaddr_ntoa(&voip->sip_info.local.addr));
    snprintf(to, sizeof(to), "sip:%s@%s:%d", mo_call->account, ipaddr_ntoa(&voip->sip_info.sip_cfg.server.addr), voip->sip_info.sip_cfg.server.port);
    snprintf(route, sizeof(route), "sip:%s@%s:%d", mo_call->account, ipaddr_ntoa(&voip->sip_info.sip_cfg.server.addr), voip->sip_info.sip_cfg.server.port);

    ret = eXosip_call_build_initial_invite(context_sip, &invite, to, from, route, NULL);
    if (ret || invite == NULL)
    {
        rtp_profile_destroy(call_info->media.profile);
        memset(call_info, 0, sizeof(quec_voip_call_session_t));
        return QUEC_VOIP_ERR_UNKNOWN;
    }
	/**/
	char set_buff[100]={"0"};
	osip_message_get_contact(invite, 0, &contact);
	if (contact && contact->url)
	{
		voip_log("contact->url->host=%s\n", contact->url->host);
		// 修改 host
		osip_free(contact->url->host);
		contact->url->host = osip_strdup(ipaddr_ntoa(&call_info->public.addr));

		voip_log("port=%s\n", contact->url->port);
		// 修改 port
		osip_free(contact->url->port);
		snprintf(set_buff, sizeof(set_buff), "%d", call_info->public.port);
		contact->url->port = osip_strdup(set_buff);
		voip_log("name=%s\n", contact->url->username);
		// 修改 username
		// osip_free(contact->url->username);
		// contact->url->username = osip_strdup("1001");
	}
	
#if 1
		char		via_srt[128] = {0};
		osip_via_t *via = NULL;
	
		osip_message_get_via(invite, 0, &via);
		if (via != NULL)
		{
			osip_list_special_free(&invite->vias, (void (*)(void *)) & osip_via_free);
		}
	
		snprintf(
			via_srt,
			200,
			"SIP/2.0/%s %s:%d;rport;branch=z9hG4bK%u",
			context_sip->transport,
			ipaddr_ntoa(&call_info->public.addr),
			call_info->public.port,
			osip_build_random_number()
		);
		osip_message_set_via(invite, via_srt);
#endif
    osip_message_set_body(invite, local_sdp, strlen(local_sdp));
    osip_message_set_content_type(invite, "application/sdp");
    osip_message_set_allow(invite, sip_allow);
    osip_message_set_supported(invite, "replaces, 100rel, timer, norefersub");
    osip_message_set_header(
        invite,
        "Allow-Events",
        "talk, hold, conference, presence, as-feature-event, dialog, line-seize, "
        "call-info, sla, include-session-description, presence.winfo, "
        "message-summary, refer"
    );

    ret = eXosip_call_send_initial_invite(context_sip, invite);
    if (ret <= 0)
    {
        rtp_profile_destroy(call_info->media.profile);
        memset(call_info, 0, sizeof(quec_voip_call_session_t));
        return QUEC_VOIP_ERR_UNKNOWN;
    }

    call_info->call_id = ret;
    if (call_info->timer.ring_m == NULL)
    {
        if (quec_rtos_timer_create(&call_info->timer.ring_m))
        {
            rtp_profile_destroy(call_info->media.profile);
            return QUEC_VOIP_ERR_UNKNOWN;
        }
    }

    quec_rtos_timer_start(call_info->timer.ring_m, call_info->timer.ring, FALSE, quec_voip_call_ring_timeout_cb, (uint32)call_info);

    quec_voip_notify_call_state(voip, call_info);
    return QUEC_VOIP_OK;
}

/*
 * Internal REFER request handler: extracts refer_to parameter, finds active call,
 * and sends REFER request to SIP stack.
 * voip: VoIP context structure
 * refer_param: REFER parameters containing target address
 * Returns: QUEC_VOIP_OK on success, error code on failure
 */
static int quec_voip_send_refer_inner(quec_voip_t *voip, quec_voip_refer_param_t *refer_param)
{
    struct eXosip_t *context_sip = NULL;
    int              refer_did = -1;
    int              i = 0;
    int              ret = QUEC_VOIP_OK;

    if (voip == NULL || refer_param == NULL || refer_param->refer_to[0] == '\0')
    {
        voip_log("quec_voip_send_refer_inner: invalid parameters");
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }

    if (voip->sip_info.sip_context == NULL)
    {
        voip_log("quec_voip_send_refer_inner: SIP context not initialized");
        return QUEC_VOIP_ERR_NOT_INIT;
    }

    context_sip = (struct eXosip_t *)voip->sip_info.sip_context;

    /* Find first active call session with valid did */
    for (i = 0; i < 6; i++)
    {
        if (voip->call[i].did > 0 && voip->call[i].state != QUEC_VOIP_CALL_IDLE)
        {
            refer_did = voip->call[i].did;
            voip_log("quec_voip_send_refer_inner: found active call with did=%d, index=%d", refer_did, i);
            break;
        }
    }

    if (refer_did <= 0)
    {
        voip_log("quec_voip_send_refer_inner: no active call found for refer");
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }

    /* Send REFER request */
    ret = sip_send_refer(context_sip, refer_did, refer_param->refer_to);
    voip_log("quec_voip_send_refer_inner: sip_send_refer returned %d, refer_to=%s", ret, refer_param->refer_to);

    return ret;
}

/*
 * External MO call entry: validate the callee account and enqueue the outgoing
 * call request so session creation runs in the unified manage thread.
 * account points to the callee SIP account string buffer, and len is the valid
 * byte count excluding the trailing '\0'.
 */
int quec_voip_make_call(void *account, unsigned char len)
{
    quec_voip_mo_call_t     mo_call = {0};
    quec_voip_event_data_t *event = NULL;
    int                     ret = QUEC_VOIP_OK;
    if (account == NULL || len == 0 || len >= sizeof(mo_call.account))
    {
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }

    memcpy(mo_call.account, account, len);
    mo_call.account[len] = '\0';

    if (quec_voip == NULL)
    {
        return QUEC_VOIP_ERR_NOT_INIT;
    }

    event = quec_voip_creat_event_data((uint8 *)&mo_call, sizeof(quec_voip_mo_call_t));

    if (event == NULL)
    {
        voip_log("voip osip_malloc fail");
        ret = QUEC_VOIP_ERR_NO_MEMORY;
    }

    if (quec_voip_send_manage_event(NULL, QUEC_VOIP_EVENT_CALL_MO, (void *)&event, sizeof(quec_voip_event_data_t *)))
    {
        voip_log("voip send fail");
        return QUEC_VOIP_ERR_UNKNOWN;
    }

    quec_rtos_semaphore_wait(event->sem, QUEC_WAIT_FOREVER);
    ret = event->ret;
    quec_voip_event_data_destroy(event);
    return ret;
}

static int quec_voip_make_hold_session(quec_voip_t *voip, quec_voip_call_session_t *call_info)
{
    struct eXosip_t          *context_sip = NULL;
    osip_message_t           *invite = NULL;
    quec_media_mode_e         old_rtp_mode = RTP_SESSION_INACTIVE;
    quec_media_mode_e         target_rtp_mode = RTP_SESSION_INACTIVE;
    quec_voip_call_state_e    target_state = QUEC_VOIP_CALL_IDLE;
    char                      local_sdp[512] = {0};
    int                       ret = QUEC_VOIP_OK;

    if (voip == NULL || call_info == NULL)
    {
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }

    if (call_info == NULL || call_info->did <= 0 || call_info->media.profile == NULL)
    {
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }

    context_sip = (struct eXosip_t *)voip->sip_info.sip_context;
    if (context_sip == NULL)
    {
        return QUEC_VOIP_ERR_NOT_INIT;
    }

    old_rtp_mode = call_info->rtp_mode;
    if (call_info->state == QUEC_VOIP_CALL_HOLD)
    {
        target_rtp_mode = RTP_SESSION_SENDRECV;
        target_state = QUEC_VOIP_CALL_CONNECT;
    }
    else
    {
        target_rtp_mode = RTP_SESSION_SENDONLY;
        target_state = QUEC_VOIP_CALL_HOLD;
    }

    call_info->rtp_mode = target_rtp_mode;
    ret = quec_voip_build_local_sdp(call_info, local_sdp, sizeof(local_sdp));
    if (ret)
    {
        call_info->rtp_mode = old_rtp_mode;
        return ret;
    }

    ret = eXosip_call_build_request(context_sip, call_info->did, "INVITE", &invite);
    if (ret || invite == NULL)
    {
        call_info->rtp_mode = old_rtp_mode;
        return QUEC_VOIP_ERR_UNKNOWN;
    }

    ret = osip_message_set_body(invite, local_sdp, strlen(local_sdp));
    ret += osip_message_set_content_type(invite, "application/sdp");
    ret += osip_message_set_allow(invite, sip_allow);
    ret += osip_message_set_supported(invite, "replaces, 100rel, timer, norefersub");
    ret += osip_message_set_header(
        invite,
        "Allow-Events",
        "talk, hold, conference, presence, as-feature-event, dialog, line-seize, "
        "call-info, sla, include-session-description, presence.winfo, "
        "message-summary, refer"
    );
    if (ret)
    {
        osip_message_free(invite);
        call_info->rtp_mode = old_rtp_mode;
        return QUEC_VOIP_ERR_UNKNOWN;
    }

    ret = eXosip_call_send_request(context_sip, call_info->did, invite);
    if (ret)
    {
        call_info->rtp_mode = old_rtp_mode;
        return QUEC_VOIP_ERR_UNKNOWN;
    }

    call_info->state = target_state;
    quec_voip_notify_call_state(voip, call_info);
    return QUEC_VOIP_OK;
}

static int quec_voip_make_hold_inner(quec_voip_t *voip, unsigned char call_id)
{
    unsigned char i = 0;
    unsigned char found = FALSE;
    int           ret = QUEC_VOIP_OK;

    if (voip == NULL)
    {
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }

    for (i = 0; i < 6; i++)
    {
        if ((voip->call[i].state == QUEC_VOIP_CALL_CONNECT || voip->call[i].state == QUEC_VOIP_CALL_HOLD)
            && (call_id == 0xff || call_id == voip->call[i].index))
        {
            found = TRUE;
            ret = quec_voip_make_hold_session(voip, &voip->call[i]);
            if (ret || call_id != 0xff)
            {
                break;
            }
        }
    }

    if (found == FALSE)
    {
        voip_log("hold find error call_id=%d", call_id);
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }

    return ret;
}

/*
 * External HOLD entry: enqueue hold/unhold by APP-side call index so the actual
 * re-INVITE is sent in the unified manage thread.
 */
int quec_voip_make_hold(unsigned char call_id)
{
    quec_voip_event_data_t *event = NULL;
    int                     ret = QUEC_VOIP_OK;

    if (quec_voip == NULL)
    {
        return QUEC_VOIP_ERR_NOT_INIT;
    }

    event = quec_voip_creat_event_data((uint8 *)&call_id, sizeof(call_id));

    if (event == NULL)
    {
        voip_log("voip osip_malloc fail");
        ret = QUEC_VOIP_ERR_NO_MEMORY;
    }

    if (quec_voip_send_manage_event(NULL, QUEC_VOIP_EVENT_CALL_HOLD, (void *)&event, sizeof(quec_voip_event_data_t *)))
    {
        voip_log("voip send fail");
        return QUEC_VOIP_ERR_UNKNOWN;
    }

    quec_rtos_semaphore_wait(event->sem, QUEC_WAIT_FOREVER);
    ret = event->ret;
    quec_voip_event_data_destroy(event);
    return ret;
}

/*
 * Send REFER request to transfer an active call to another party.
 * refer_to: The SIP address to refer the call to (e.g., "sip:1234@server.com" or "1234")
 * len: Length of refer_to string (without null terminator)
 * Returns: QUEC_VOIP_OK on success, error code on failure
 */
int quec_voip_send_refer(void *refer_to, unsigned char len)
{
    quec_voip_event_data_t *event = NULL;
    quec_voip_refer_param_t refer_param = {0};
    int ret = QUEC_VOIP_OK;

    if (refer_to == NULL || len == 0 || len >= sizeof(refer_param.refer_to))
    {
        voip_log("quec_voip_send_refer: invalid parameters, len=%d", len);
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }

    if (quec_voip == NULL)
    {
        voip_log("quec_voip_send_refer: voip not initialized");
        return QUEC_VOIP_ERR_NOT_INIT;
    }

    memcpy(refer_param.refer_to, refer_to, len);
    refer_param.refer_to[len] = '\0';

    voip_log("quec_voip_send_refer: refer_to=%s", refer_param.refer_to);

    event = quec_voip_creat_event_data((uint8 *)&refer_param, sizeof(quec_voip_refer_param_t));

    if (event == NULL)
    {
        voip_log("quec_voip_send_refer: osip_malloc fail");
        return QUEC_VOIP_ERR_NO_MEMORY;
    }

    if (quec_voip_send_manage_event(NULL, QUEC_VOIP_EVENT_CALL_REFER, (void *)&event, sizeof(quec_voip_event_data_t *)))
    {
        voip_log("quec_voip_send_refer: send event fail");
        quec_voip_event_data_destroy(event);
        return QUEC_VOIP_ERR_UNKNOWN;
    }

    quec_rtos_semaphore_wait(event->sem, QUEC_WAIT_FOREVER);
    ret = event->ret;
    quec_voip_event_data_destroy(event);

    voip_log("quec_voip_send_refer: result=%d", ret);
    return ret;
}

void quec_voip_manage(void *argv)
{
    quec_voip_event_e        event = QUEC_VOIP_EVENT_INIT;
    quec_voip_manage_t       event_msg = {0};
    quec_voip_sip_init_t    *init_cfg = NULL;
    quec_voip_t             *voip = NULL;
    int                      ret = 0;
    quec_voip_media_event_t *call_event = NULL;
    quec_voip_event_data_t  *event_pdata = NULL;
    quec_voip_cfg_req_t     *cfg_req = NULL;

    while (1)
    {
        ret = QUEC_VOIP_OK;
        quec_rtos_queue_wait(quec_voip_manage_m, (uint8_t *)&event_msg, sizeof(quec_voip_manage_t), QUEC_WAIT_FOREVER);
        voip = event_msg.voip;
        if (voip == NULL)
        {
            voip = quec_voip;
            voip_log("VOIP IS NULL");
        }
        call_event = NULL;
        event_pdata = NULL;
        cfg_req = NULL;
        voip_log("event=%d", event_msg.event);
        switch (event_msg.event)
        {
                // 流程初始化时起该EVENT
            case QUEC_VOIP_EVENT_INIT:
                if (event_msg.p_data == NULL)
                {
                    voip_log("INIT event data is null");
                    break;
                }
                event_pdata = *(quec_voip_event_data_t **)event_msg.p_data;
                if (event_pdata == NULL)
                {
                    voip_log("INIT param is null");
                    break;
                }
                init_cfg = (quec_voip_sip_init_t *)event_pdata->data;
                if (init_cfg == NULL)
                {
                    voip_log("INIT cfg is null");
                    break;
                }

                ret = quec_voip_sip_init(&quec_voip, init_cfg);

                if (ret != 0)
                {
                    voip_log("INIT fail");
                }
                if (event_pdata->sem)
                {
                    event_pdata->ret = ret;
                    quec_rtos_semaphore_release(event_pdata->sem);
                }
                break;
            case QUEC_VOIP_EVENT_START:
                if (event_msg.p_data == NULL)
                {
                    voip_log("START event data is null");
                    break;
                }
                event_pdata = *(quec_voip_event_data_t **)event_msg.p_data;
                if (event_pdata == NULL)
                {
                    voip_log("START param is null");
                    break;
                }
                if (event_pdata->data == NULL || event_pdata->len != sizeof(quec_voip_start_req_t))
                {
                    event_pdata->ret = QUEC_VOIP_ERR_INVALID_PARAM;
                    quec_rtos_semaphore_release(event_pdata->sem);
                    break;
                }
                else
                {
                    quec_voip_start_req_t *start_req = (quec_voip_start_req_t *)event_pdata->data;
                    BOOL                   old_enable = voip->sip_info.sip_cfg.enable;
                    BOOL                   old_auto_connect = voip->sip_info.sip_cfg.auto_connect;

                    if (start_req->auto_regist != FALSE && start_req->auto_regist != TRUE)
                    {
                        event_pdata->ret = QUEC_VOIP_ERR_INVALID_PARAM;
                        quec_rtos_semaphore_release(event_pdata->sem);
                        break;
                    }

                    if (voip->sip_info.pending_start_event != NULL)
                    {
                        event_pdata->ret = QUEC_VOIP_ERR_SIP_PARAM_ILLEGAL;
                        quec_rtos_semaphore_release(event_pdata->sem);
                        break;
                    }

                    quec_voip_sip_stop_check_timer(voip);
                    voip->sip_info.sip_cfg.enable = TRUE;
                    voip->sip_info.sip_cfg.auto_connect = start_req->auto_regist;
                    ret = quec_voip_network_check(voip);
                    if (ret)
                    {
                        voip->sip_info.sip_cfg.enable = old_enable;
                        voip->sip_info.sip_cfg.auto_connect = old_auto_connect;
                        if (old_auto_connect == FALSE)
                        {
                            quec_voip_sip_stop_check_timer(voip);
                        }
                        event_pdata->ret = ret;
                        quec_rtos_semaphore_release(event_pdata->sem);
                    }
                    else if (voip->sip_info.is_regist)
                    {
                        event_pdata->ret = QUEC_VOIP_OK;
                        quec_rtos_semaphore_release(event_pdata->sem);
                    }
                    else
                    {
                        voip->sip_info.pending_start_event = event_pdata;
                    }
                }
                break;
            case QUEC_VOIP_EVENT_STOP:
                if (event_msg.p_data != NULL)
                {
                    event_pdata = *(quec_voip_event_data_t **)event_msg.p_data;
                }
                if (event_pdata == NULL)
                {
                    voip_log("STOP param is null");
                    break;
                }
                else
                {
                    BOOL old_enable = voip->sip_info.sip_cfg.enable;
                    BOOL old_auto_connect = voip->sip_info.sip_cfg.auto_connect;
                    BOOL old_is_regist = voip->sip_info.is_regist;

                    voip->sip_info.sip_cfg.enable = FALSE;
                    voip->sip_info.sip_cfg.auto_connect = FALSE;
                    quec_voip_regist_state(voip, FALSE);
                    quec_voip_sip_stop_check_timer(voip);
                    ret = quec_voip_unregist(voip);
                    for (unsigned c=0;c<6;c++) {
                        if (voip->call[c].state!=QUEC_VOIP_CALL_IDLE) {
                            quec_voip_media_event_t ended={.type=QUEC_VOIP_EVENT_MEDIA_DESTROY,.cid=voip->call[c].call_id};
                            quec_voip_call_media_destroy(voip,&ended);
                        }
                    }
                    if (ret)
                    {
                        voip->sip_info.sip_cfg.enable = old_enable;
                        voip->sip_info.sip_cfg.auto_connect = old_auto_connect;
                        quec_voip_regist_state(voip, old_is_regist);
                    }
                    else
                    {
                        quec_voip_complete_start_wait(voip, QUEC_VOIP_ERR_SIP_REGIST_FAILED);
                    }
                    if (event_pdata->sem)
                    {
                        event_pdata->ret = ret;
                        quec_rtos_semaphore_release(event_pdata->sem);
                    }
                }
                break;
            case QUEC_VOIP_EVENT_REGIST:
                ret = quec_voip_regist(voip);
                if (ret) quec_voip_complete_start_wait(voip,ret);
                break;
            case QUEC_VOIP_EVENT_REGIST_FAILED:
                quec_voip_registration_recover(voip, (const int *)event_msg.p_data);
                break;
            case QUEC_VOIP_EVENT_REGIST_DONE:
                /* STOP 等待期间排队的旧注册成功不得把本地重新标为在线。 */
                if (!voip->sip_info.sip_cfg.enable || !voip->sip_info.sip_context ||
                    voip->sip_info.reg_id <= 0) break;
                qosa_enter_critical(); g_net_diag.reg_code = 200; qosa_exit_critical();
                /*
       * Central serializer for VOIP state transitions. All asynchronous
       * SIP/network/ media events are normalized here before touching shared
       * call context.
       */
                quec_voip_regist_state(voip, TRUE);
                quec_voip_sip_stop_check_timer(voip);
                quec_voip_complete_start_wait(voip, QUEC_VOIP_OK);
                break;
            case QUEC_VOIP_EVENT_UNREGIST:
                if (event_msg.p_data != NULL)
                {
                    event_pdata = *(quec_voip_event_data_t **)event_msg.p_data;
                }
                quec_voip_regist_state(voip, FALSE);
                quec_voip_sip_stop_check_timer(voip);
                ret = quec_voip_unregist(voip);
                quec_voip_complete_start_wait(voip, QUEC_VOIP_ERR_SIP_REGIST_FAILED);
                if (event_pdata && event_pdata->sem)
                {
                    event_pdata->ret = ret;
                    quec_rtos_semaphore_release(event_pdata->sem);
                }
                break;
            case QUEC_VOIP_EVENT_NETWORK_CHECK:
                if (event_msg.p_data != NULL)
                {
                    event_pdata = *(quec_voip_event_data_t **)event_msg.p_data;
                }

                ret = quec_voip_network_check(voip);
                if (ret && voip->sip_info.sip_cfg.auto_connect)
                {
                    quec_voip_sip_start_check_timer(voip);
                }

                if (event_pdata && event_pdata->sem)
                {
                    event_pdata->ret = ret;
                    quec_rtos_semaphore_release(event_pdata->sem);
                }
                break;
            case QUEC_VOIP_EVENT_CALL_INVITE:
                call_event = (quec_voip_media_event_t *)event_msg.p_data;

                ret = quec_voip_call_invite(voip, call_event);
                if (ret)
                {
                    voip_log("call invite is error");
                }
                else
                {
                    if (call_event->type == QUEC_VOIP_EVENT_MEDIA_UPDATE)
                    {
                        quec_voip_send_call_event(NULL, call_event, QUEC_VOIP_EVENT_CALL_ACCEPT);
                    }
                    voip_log("wait accept");
                }
                break;
            case QUEC_VOIP_EVENT_CALL_ACCEPT:
                call_event = (quec_voip_media_event_t *)event_msg.p_data;

                ret = quec_voip_call_accept(voip, call_event);
                if (ret)
                {
                    voip_log("call accept error");
                    call_event->type = QUEC_VOIP_EVENT_MEDIA_DESTROY;
                    quec_voip_send_call_event(voip, call_event, QUEC_VOIP_EVENT_CALL_RELEASE);
                }
                else
                {
                    if (call_event->type == QUEC_VOIP_EVENT_MEDIA_UPDATE)
                    {
                        quec_voip_send_call_event(voip, call_event, QUEC_VOIP_EVENT_CALL_MEDIA_CREAT);
                    }
                    voip_log("call accept sus,wait ack");
                }
                break;
            case QUEC_VOIP_EVENT_CALL_MO:
                if (event_msg.p_data == NULL)
                {
                    voip_log("CALL_MO event data is null");
                    break;
                }
                event_pdata = *(quec_voip_event_data_t **)event_msg.p_data;
                if (event_pdata == NULL)
                {
                    voip_log("CALL_MO param is null");
                    break;
                }
                ret = quec_voip_make_call_inner(voip, (quec_voip_mo_call_t *)event_pdata->data);
                if (ret)
                {
                    voip_log("mo call fail ret=%d", ret);
                }
                event_pdata->ret = ret;
                quec_rtos_semaphore_release(event_pdata->sem);
                break;
			case QUEC_VOIP_EVENT_CALL_REFER:
				if (event_msg.p_data == NULL)
                {
                    voip_log("CALL_REFER event data is null");
                    break;
                }
                event_pdata = *(quec_voip_event_data_t **)event_msg.p_data;
                if (event_pdata == NULL)
                {
                    voip_log("CALL_REFER param is null");
                    break;
                }
                
                /* Get refer parameters */
                quec_voip_refer_param_t *refer_param = (quec_voip_refer_param_t *)event_pdata->data;
                
                /* Process REFER request */
                ret = quec_voip_send_refer_inner(voip, refer_param);
                
                event_pdata->ret = ret;
                quec_rtos_semaphore_release(event_pdata->sem);
                break;
			case QUEC_VOIP_EVENT_CALL_HOLD:
				if (event_msg.p_data == NULL)
                {
                    voip_log("CALL_MO event data is null");
                    break;
                }
                event_pdata = *(quec_voip_event_data_t **)event_msg.p_data;
                if (event_pdata == NULL)
                {
                    voip_log("CALL_MO param is null");
                    break;
                }
                ret = quec_voip_make_hold_inner(voip, *(unsigned char *)event_pdata->data);
                if (ret)
                {
                    voip_log("hold call fail ret=%d", ret);
                }
                event_pdata->ret = ret;
                quec_rtos_semaphore_release(event_pdata->sem);
				break;
            case QUEC_VOIP_EVENT_CALL_MO_ALERTING:
                call_event = (quec_voip_media_event_t *)event_msg.p_data;
                ret = quec_voip_call_mo_alerting(voip, call_event);
                if (ret)
                {
                    voip_log("mo alerting fail ret=%d", ret);
                }
                break;
            case QUEC_VOIP_EVENT_CALL_MO_ANSWERED:
                call_event = (quec_voip_media_event_t *)event_msg.p_data;
                ret = quec_voip_call_mo_answered(voip, call_event);
                if (ret)
                {
                    voip_log("mo answer fail ret=%d", ret);
                    call_event->type = QUEC_VOIP_EVENT_MEDIA_DESTROY;
                    quec_voip_send_call_event(voip, call_event, QUEC_VOIP_EVENT_CALL_RELEASE);
                }
                break;
            case QUEC_VOIP_EVENT_CALL_TIMEOUT:
                voip_call_diag_step(QCM_VOIP_STAGE_TIMEOUT, QUEC_VOIP_ERR_TIMEOUT);
                /* fallthrough */
            case QUEC_VOIP_EVENT_CALL_RELEASE: {
                call_event = (quec_voip_media_event_t *)event_msg.p_data;
                quec_voip_call_release(voip, call_event);
            }
            break;
            case QUEC_VOIP_EVENT_CALL_MEDIA_CREAT:
                call_event = (quec_voip_media_event_t *)event_msg.p_data;

                voip_call_diag_step(QCM_VOIP_STAGE_MEDIA_START, 0);
                ret = quec_voip_call_media_creat(voip, call_event);
                voip_call_diag_step(ret == 0 ? QCM_VOIP_STAGE_MEDIA_READY : QCM_VOIP_STAGE_MEDIA_START, ret);
                if (ret)
                {
                    voip_log("call accept error");
                }
                break;
            case QUEC_VOIP_EVENT_CALL_MEDIA_DESTROY:
                call_event = (quec_voip_media_event_t *)event_msg.p_data;
                ret = quec_voip_call_media_destroy(voip, call_event);
                if (ret)
                {
                    voip_log("media close fail error");
                }
                break;
            case QUEC_VOIP_EVENT_CFG_SET:
                if (event_msg.p_data == NULL)
                {
                    voip_log("CFG_SET event data is null");
                    break;
                }
                event_pdata = *(quec_voip_event_data_t **)event_msg.p_data;
                if (event_pdata == NULL)
                {
                    voip_log("CFG_SET param is null");
                    break;
                }
                cfg_req = (quec_voip_cfg_req_t *)event_pdata->data;
                if (cfg_req == NULL)
                {
                    voip_log("CFG_SET req is null");
                    break;
                }
                if (voip != NULL && voip->sip_info.is_regist)
                {
                    voip_log("CFG_SET fail:sip already regist");
                    event_pdata->ret = QUEC_VOIP_ERR_SIP_PARAM_ILLEGAL;
                    quec_rtos_semaphore_release(event_pdata->sem);
                    break;
                }
                event_pdata->ret = quec_voip_cfg_set_inner(voip, cfg_req->item, cfg_req->data, cfg_req->len);
                quec_rtos_semaphore_release(event_pdata->sem);
                break;
            case QUEC_VOIP_EVENT_CFG_GET:
                if (event_msg.p_data == NULL)
                {
                    voip_log("CFG_GET event data is null");
                    break;
                }
                event_pdata = *(quec_voip_event_data_t **)event_msg.p_data;
                if (event_pdata == NULL)
                {
                    voip_log("CFG_GET param is null");
                    break;
                }
                cfg_req = (quec_voip_cfg_req_t *)event_pdata->data;
                if (cfg_req == NULL)
                {
                    voip_log("CFG_GET req is null");
                    break;
                }
                event_pdata->ret = quec_voip_cfg_get_inner(voip, cfg_req->item, cfg_req->data, &cfg_req->len);
                quec_rtos_semaphore_release(event_pdata->sem);
                break;
            default:
                voip_log("unknown event");
                break;
        }
            /* The queue message holds a heap copy of the payload made by
           quec_voip_send_manage_event(). event_data / call_event only borrow
           the pointer, nobody else frees it, so release it here. Missing this
           leaked 16 bytes per call-control event and drained the osip pool. */
        if (event_msg.p_data != NULL)
        {
            free(event_msg.p_data);
            event_msg.p_data = NULL;
        }
    }
}

quec_voip_err_e quec_voip_sip_cust_init_value_check(quec_voip_sip_config_t *init_cfg)
{
    if (init_cfg == NULL)
    {
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }

    if ((init_cfg->enable != FALSE && init_cfg->enable != TRUE) || (init_cfg->auto_connect != FALSE && init_cfg->auto_connect != TRUE)
        || (init_cfg->enable_masquerade != FALSE && init_cfg->enable_masquerade != TRUE))
    {
        voip_log("invalid bool cfg en=%d,auto=%d,masq=%d", init_cfg->enable, init_cfg->auto_connect, init_cfg->enable_masquerade);
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }
    voip_log("valid bool cfg en=%d,auto=%d,masq=%d", init_cfg->enable, init_cfg->auto_connect, init_cfg->enable_masquerade);
    if (init_cfg->pdp_cid >= QUEC_VOIP_CID_MAX)
    {
        voip_log("invalid pdp cid=%d", init_cfg->pdp_cid);
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }
    voip_log("valid pdp cid=%d", init_cfg->pdp_cid);
    if (strlen(init_cfg->account) == 0 || strlen(init_cfg->password) == 0)
    {
        voip_log("invalid account or password");
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }

    if (ip_addr_isany(&init_cfg->server.addr) || init_cfg->server.port == 0)
    {
        voip_log("invalid server addr/port");
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }

    if (init_cfg->expiry > 0 && init_cfg->expiry < 30)
    {
        voip_log("invalid expiry=%d", init_cfg->expiry);
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }
voip_log("valid expiry=%d", init_cfg->expiry);
    if (init_cfg->keep_alive_time > 0 && init_cfg->keep_alive_time < 1000)
    {
        voip_log("invalid keepalive=%d", init_cfg->keep_alive_time);
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }
voip_log("valid keepalive=%d", init_cfg->keep_alive_time);
    return QUEC_VOIP_OK;
}
 
static void quec_voip_public_cfg_to_internal(quec_voip_sip_config_t *dst, const quec_voip_config_t *src)
{
    if (dst == NULL || src == NULL)
    {
        return;
    }

    memcpy(&dst->server, &src->server, sizeof(dst->server));
    dst->pdp_cid = src->pdp_cid;
    dst->simid = src->simid;
    memcpy(dst->account, src->account, sizeof(dst->account));
    memcpy(dst->password, src->password, sizeof(dst->password));
    memcpy(dst->agent, src->agent, sizeof(dst->agent));
    dst->expiry = src->expiry;
    dst->keep_alive_time = src->keep_alive_time;
    dst->enable_masquerade = src->ip_rewrite;
    dst->ring_timeout = src->ring_timeout;
    dst->ring_back_timeout = src->ring_back_timeout;
    dst->sip_cb_ctx = src->sip_cb_ctx;
    dst->sip_state_cb = src->sip_state_cb;
    dst->call_cb_ctx = src->call_cb_ctx;
    dst->call_state_cb = src->call_state_cb;
}

static void quec_voip_internal_cfg_to_public(quec_voip_config_t *dst, const quec_voip_sip_config_t *src)
{
    if (dst == NULL || src == NULL)
    {
        return;
    }

    memset(dst, 0, sizeof(*dst));
    memcpy(&dst->server, &src->server, sizeof(dst->server));
    dst->pdp_cid = src->pdp_cid;
    dst->simid = src->simid;
    memcpy(dst->account, src->account, sizeof(dst->account));
    memcpy(dst->password, src->password, sizeof(dst->password));
    memcpy(dst->agent, src->agent, sizeof(dst->agent));
    dst->expiry = src->expiry;
    dst->keep_alive_time = src->keep_alive_time;
    dst->ip_rewrite = src->enable_masquerade;
    dst->ring_timeout = src->ring_timeout;
    dst->ring_back_timeout = src->ring_back_timeout;
    dst->sip_cb_ctx = src->sip_cb_ctx;
    dst->sip_state_cb = src->sip_state_cb;
    dst->call_cb_ctx = src->call_cb_ctx;
    dst->call_state_cb = src->call_state_cb;
}

static quec_voip_err_e quec_voip_apply_runtime_cfg(quec_voip_t *voip)
{
    char                    prog_name[32] = {0};
    int                     optval = 0;
    struct eXosip_t        *sip_context = NULL;
    quec_voip_sip_config_t *cfg = NULL;

    if (voip == NULL)
    {
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }

    sip_context = (struct eXosip_t *)voip->sip_info.sip_context;
    cfg = &voip->sip_info.sip_cfg;
    if (sip_context == NULL)
    {
        return QUEC_VOIP_OK;
    }

    optval = cfg->enable_masquerade ? 1 : 0;
    eXosip_set_option(sip_context, EXOSIP_OPT_AUTO_MASQUERADE_CONTACT, &optval);

    optval = cfg->keep_alive_time ? cfg->keep_alive_time : QUEC_VOIP_SIP_KEEPALIVE_TIME;
    eXosip_set_option(sip_context, EXOSIP_OPT_UDP_KEEP_ALIVE, (void *)&optval);

    if (strlen(cfg->agent) == 0)
    {
        snprintf(prog_name, sizeof(prog_name), "%s (%s)", "QuecVoip", eXosip_get_version());
    }
    else
    {
        snprintf(prog_name, sizeof(prog_name), "%s", cfg->agent);
    }
    eXosip_set_user_agent(sip_context, prog_name);

    return QUEC_VOIP_OK;
}

static BOOL quec_voip_cfg_need_refresh(quec_voip_cfg_item_e item)
{
    switch (item)
    {
        case QUEC_VOIP_CFG_ITEM_SIP_CB_CTX:
        case QUEC_VOIP_CFG_ITEM_SIP_STATE_CB:
        case QUEC_VOIP_CFG_ITEM_CALL_CB_CTX:
        case QUEC_VOIP_CFG_ITEM_CALL_STATE_CB:
        case QUEC_VOIP_CFG_ITEM_LOCAL_IP: /* side effect handled inside cfg_set_inner */
            return FALSE;
        default:
            return TRUE;
    }
}

static quec_voip_err_e quec_voip_cfg_copy_out(void *value, unsigned short *len, const void *src, unsigned short src_len)
{
    if (value == NULL || len == NULL || src == NULL)
    {
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }

    if (*len < src_len)
    {
        *len = src_len;
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }

    memcpy(value, src, src_len);
    *len = src_len;
    return QUEC_VOIP_OK;
}

static quec_voip_err_e quec_voip_cfg_set_inner(quec_voip_t *voip, quec_voip_cfg_item_e item, const void *value, unsigned short len)
{
    quec_voip_sip_config_t  temp_cfg = {0};
    quec_voip_sip_config_t *sip_cfg = NULL;
    unsigned short          temp_u16 = 0;
    unsigned char           temp_u8 = 0;
    BOOL                    temp_bool = FALSE;

    if (voip == NULL || item >= QUEC_VOIP_CFG_ITEM_MAX || value == NULL)
    {
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }

    /* LOCAL_IP may be refreshed by APP even while registered (e.g. DHCP renew) */
    if (voip->sip_info.is_regist && item != QUEC_VOIP_CFG_ITEM_LOCAL_IP)
    {
        voip_log("voip is regist, cfg can not be changed");
        return QUEC_VOIP_ERR_SIP_PARAM_ILLEGAL;
    }

    sip_cfg = &voip->sip_info.sip_cfg;

    switch (item)
    {
        case QUEC_VOIP_CFG_ITEM_ALL:
            if (len != sizeof(quec_voip_config_t))
            {
                return QUEC_VOIP_ERR_INVALID_PARAM;
            }
            memcpy(&temp_cfg, sip_cfg, sizeof(temp_cfg));
            quec_voip_public_cfg_to_internal(&temp_cfg, (const quec_voip_config_t *)value);
            if (quec_voip_sip_cust_init_value_check(&temp_cfg) != QUEC_VOIP_OK)
            {
                return QUEC_VOIP_ERR_INVALID_PARAM;
            }
            memcpy(sip_cfg, &temp_cfg, sizeof(*sip_cfg));
            break;
        case QUEC_VOIP_CFG_ITEM_ENABLE:
            if (len != sizeof(BOOL))
            {
                return QUEC_VOIP_ERR_INVALID_PARAM;
            }
            memcpy(&temp_bool, value, sizeof(temp_bool));
            if (temp_bool != FALSE && temp_bool != TRUE)
            {
                return QUEC_VOIP_ERR_INVALID_PARAM;
            }
            sip_cfg->enable = temp_bool;
            break;
        case QUEC_VOIP_CFG_ITEM_SERVER:
            if (len != sizeof(quec_voip_addr_t))
            {
                return QUEC_VOIP_ERR_INVALID_PARAM;
            }
            memcpy(&sip_cfg->server, value, sizeof(sip_cfg->server));
            break;
        case QUEC_VOIP_CFG_ITEM_PDP_CID:
            if (len != sizeof(unsigned char))
            {
                return QUEC_VOIP_ERR_INVALID_PARAM;
            }
            memcpy(&temp_u8, value, sizeof(temp_u8));
            if (temp_u8 >= QUEC_VOIP_CID_MAX)
            {
                return QUEC_VOIP_ERR_INVALID_PARAM;
            }
            sip_cfg->pdp_cid = temp_u8;
            break;
        case QUEC_VOIP_CFG_ITEM_SIMID:
            if (len != sizeof(unsigned char))
            {
                return QUEC_VOIP_ERR_INVALID_PARAM;
            }
            memcpy(&sip_cfg->simid, value, sizeof(sip_cfg->simid));
            break;
        case QUEC_VOIP_CFG_ITEM_ACCOUNT:
            if (len == 0 || len >= sizeof(sip_cfg->account))
            {
                return QUEC_VOIP_ERR_INVALID_PARAM;
            }
            memset(sip_cfg->account, 0, sizeof(sip_cfg->account));
            memcpy(sip_cfg->account, value, len);
            break;
        case QUEC_VOIP_CFG_ITEM_PASSWORD:
            if (len == 0 || len >= sizeof(sip_cfg->password))
            {
                return QUEC_VOIP_ERR_INVALID_PARAM;
            }
            memset(sip_cfg->password, 0, sizeof(sip_cfg->password));
            memcpy(sip_cfg->password, value, len);
            break;
        case QUEC_VOIP_CFG_ITEM_AGENT:
            if (len >= sizeof(sip_cfg->agent))
            {
                return QUEC_VOIP_ERR_INVALID_PARAM;
            }
            memset(sip_cfg->agent, 0, sizeof(sip_cfg->agent));
            if (len > 0)
            {
                memcpy(sip_cfg->agent, value, len);
            }
            break;
        case QUEC_VOIP_CFG_ITEM_EXPIRY:
            if (len != sizeof(unsigned short))
            {
                return QUEC_VOIP_ERR_INVALID_PARAM;
            }
            memcpy(&temp_u16, value, sizeof(temp_u16));
            if (temp_u16 > 0 && temp_u16 < 30)
            {
                return QUEC_VOIP_ERR_INVALID_PARAM;
            }
            sip_cfg->expiry = temp_u16;
            break;
        case QUEC_VOIP_CFG_ITEM_KEEP_ALIVE:
            if (len != sizeof(unsigned short))
            {
                return QUEC_VOIP_ERR_INVALID_PARAM;
            }
            memcpy(&temp_u16, value, sizeof(temp_u16));
            if (temp_u16 > 0 && temp_u16 < 1000)
            {
                return QUEC_VOIP_ERR_INVALID_PARAM;
            }
            sip_cfg->keep_alive_time = temp_u16;
            break;
        case QUEC_VOIP_CFG_ITEM_IP_REWRITE:
            if (len != sizeof(BOOL))
            {
                return QUEC_VOIP_ERR_INVALID_PARAM;
            }
            memcpy(&temp_bool, value, sizeof(temp_bool));
            if (temp_bool != FALSE && temp_bool != TRUE)
            {
                return QUEC_VOIP_ERR_INVALID_PARAM;
            }
            sip_cfg->enable_masquerade = temp_bool;
            break;
        case QUEC_VOIP_CFG_ITEM_AUTO_CONNECT:
            if (len != sizeof(BOOL))
            {
                return QUEC_VOIP_ERR_INVALID_PARAM;
            }
            memcpy(&temp_bool, value, sizeof(temp_bool));
            if (temp_bool != FALSE && temp_bool != TRUE)
            {
                return QUEC_VOIP_ERR_INVALID_PARAM;
            }
            sip_cfg->auto_connect = temp_bool;
            break;
        case QUEC_VOIP_CFG_ITEM_RING_TIMEOUT:
            if (len != sizeof(unsigned int))
            {
                return QUEC_VOIP_ERR_INVALID_PARAM;
            }
            memcpy(&sip_cfg->ring_timeout, value, sizeof(sip_cfg->ring_timeout));
            break;
        case QUEC_VOIP_CFG_ITEM_RING_BACK_TIMEOUT:
            if (len != sizeof(unsigned int))
            {
                return QUEC_VOIP_ERR_INVALID_PARAM;
            }
            memcpy(&sip_cfg->ring_back_timeout, value, sizeof(sip_cfg->ring_back_timeout));
            break;
        case QUEC_VOIP_CFG_ITEM_SIP_CB_CTX:
            if (len != sizeof(void *))
            {
                return QUEC_VOIP_ERR_INVALID_PARAM;
            }
            memcpy(&sip_cfg->sip_cb_ctx, value, sizeof(sip_cfg->sip_cb_ctx));
            break;
        case QUEC_VOIP_CFG_ITEM_SIP_STATE_CB:
            if (len != sizeof(sip_cfg->sip_state_cb))
            {
                return QUEC_VOIP_ERR_INVALID_PARAM;
            }
            memcpy(&sip_cfg->sip_state_cb, value, sizeof(sip_cfg->sip_state_cb));
            break;
        case QUEC_VOIP_CFG_ITEM_CALL_CB_CTX:
            if (len != sizeof(void *))
            {
                return QUEC_VOIP_ERR_INVALID_PARAM;
            }
            memcpy(&sip_cfg->call_cb_ctx, value, sizeof(sip_cfg->call_cb_ctx));
            break;
        case QUEC_VOIP_CFG_ITEM_CALL_STATE_CB:
            if (len != sizeof(sip_cfg->call_state_cb))
            {
                return QUEC_VOIP_ERR_INVALID_PARAM;
            }
            memcpy(&sip_cfg->call_state_cb, value, sizeof(sip_cfg->call_state_cb));
            break;
        case QUEC_VOIP_CFG_ITEM_LOCAL_IP:
        {
            quec_voip_addr_t local = {0};

            if (len != sizeof(quec_voip_addr_t))
            {
                return QUEC_VOIP_ERR_INVALID_PARAM;
            }
            memcpy(&local, value, sizeof(local));
            if (ip_addr_isany(&local.addr))
            {
                return QUEC_VOIP_ERR_INVALID_PARAM;
            }

            /* only the address is taken, the local SIP port stays as configured */
            ip_addr_copy(voip->sip_info.local.addr, local.addr);
            voip_log("set local ip=%s", ipaddr_ntoa(&voip->sip_info.local.addr));

            /*
             * Not registered yet: try to register now if the service is enabled.
             * Already registered: keep the session, APP is responsible for the
             * re-registration after the address actually changed.
             */
            if (voip->sip_info.is_regist)
            {
                voip_log("local ip updated while regist, re-regist needed");
            }
            else if (sip_cfg->enable)
            {
                quec_voip_network_check(voip);
            }
            return QUEC_VOIP_OK;
        }
        default:
            return QUEC_VOIP_ERR_INVALID_PARAM;
    }

    quec_voip_apply_runtime_cfg(voip);

    if (quec_voip_cfg_need_refresh(item))
    {
        if (sip_cfg->enable)
        {
            quec_voip_network_check(voip);
        }
        else
        {
            quec_voip_unregist(voip);
        }
    }

    return QUEC_VOIP_OK;
}

static quec_voip_err_e quec_voip_cfg_get_inner(quec_voip_t *voip, quec_voip_cfg_item_e item, void *value, unsigned short *len)
{
    quec_voip_config_t cfg = {0};

    if (voip == NULL || item >= QUEC_VOIP_CFG_ITEM_MAX)
    {
        return QUEC_VOIP_ERR_NOT_INIT;
    }

    switch (item)
    {
        case QUEC_VOIP_CFG_ITEM_ALL:
            quec_voip_internal_cfg_to_public(&cfg, &voip->sip_info.sip_cfg);
            return quec_voip_cfg_copy_out(value, len, &cfg, sizeof(cfg));
        case QUEC_VOIP_CFG_ITEM_ENABLE:
            return quec_voip_cfg_copy_out(value, len, &voip->sip_info.sip_cfg.enable, sizeof(voip->sip_info.sip_cfg.enable));
        case QUEC_VOIP_CFG_ITEM_SERVER:
            return quec_voip_cfg_copy_out(value, len, &voip->sip_info.sip_cfg.server, sizeof(voip->sip_info.sip_cfg.server));
        case QUEC_VOIP_CFG_ITEM_PDP_CID:
            return quec_voip_cfg_copy_out(value, len, &voip->sip_info.sip_cfg.pdp_cid, sizeof(voip->sip_info.sip_cfg.pdp_cid));
        case QUEC_VOIP_CFG_ITEM_SIMID:
            return quec_voip_cfg_copy_out(value, len, &voip->sip_info.sip_cfg.simid, sizeof(voip->sip_info.sip_cfg.simid));
        case QUEC_VOIP_CFG_ITEM_ACCOUNT:
            return quec_voip_cfg_copy_out(value, len, voip->sip_info.sip_cfg.account, sizeof(voip->sip_info.sip_cfg.account));
        case QUEC_VOIP_CFG_ITEM_PASSWORD:
            return quec_voip_cfg_copy_out(value, len, voip->sip_info.sip_cfg.password, sizeof(voip->sip_info.sip_cfg.password));
        case QUEC_VOIP_CFG_ITEM_AGENT:
            return quec_voip_cfg_copy_out(value, len, voip->sip_info.sip_cfg.agent, sizeof(voip->sip_info.sip_cfg.agent));
        case QUEC_VOIP_CFG_ITEM_EXPIRY:
            return quec_voip_cfg_copy_out(value, len, &voip->sip_info.sip_cfg.expiry, sizeof(voip->sip_info.sip_cfg.expiry));
        case QUEC_VOIP_CFG_ITEM_KEEP_ALIVE:
            return quec_voip_cfg_copy_out(value, len, &voip->sip_info.sip_cfg.keep_alive_time, sizeof(voip->sip_info.sip_cfg.keep_alive_time));
        case QUEC_VOIP_CFG_ITEM_IP_REWRITE:
            return quec_voip_cfg_copy_out(value, len, &voip->sip_info.sip_cfg.enable_masquerade, sizeof(voip->sip_info.sip_cfg.enable_masquerade));
        case QUEC_VOIP_CFG_ITEM_AUTO_CONNECT:
            return quec_voip_cfg_copy_out(value, len, &voip->sip_info.sip_cfg.auto_connect, sizeof(voip->sip_info.sip_cfg.auto_connect));
        case QUEC_VOIP_CFG_ITEM_RING_TIMEOUT:
            return quec_voip_cfg_copy_out(value, len, &voip->sip_info.sip_cfg.ring_timeout, sizeof(voip->sip_info.sip_cfg.ring_timeout));
        case QUEC_VOIP_CFG_ITEM_RING_BACK_TIMEOUT:
            return quec_voip_cfg_copy_out(value, len, &voip->sip_info.sip_cfg.ring_back_timeout, sizeof(voip->sip_info.sip_cfg.ring_back_timeout));
        case QUEC_VOIP_CFG_ITEM_SIP_CB_CTX:
            return quec_voip_cfg_copy_out(value, len, &voip->sip_info.sip_cfg.sip_cb_ctx, sizeof(voip->sip_info.sip_cfg.sip_cb_ctx));
        case QUEC_VOIP_CFG_ITEM_SIP_STATE_CB:
            return quec_voip_cfg_copy_out(value, len, &voip->sip_info.sip_cfg.sip_state_cb, sizeof(voip->sip_info.sip_cfg.sip_state_cb));
        case QUEC_VOIP_CFG_ITEM_CALL_CB_CTX:
            return quec_voip_cfg_copy_out(value, len, &voip->sip_info.sip_cfg.call_cb_ctx, sizeof(voip->sip_info.sip_cfg.call_cb_ctx));
        case QUEC_VOIP_CFG_ITEM_CALL_STATE_CB:
            return quec_voip_cfg_copy_out(value, len, &voip->sip_info.sip_cfg.call_state_cb, sizeof(voip->sip_info.sip_cfg.call_state_cb));
        case QUEC_VOIP_CFG_ITEM_LOCAL_IP:
        {
            quec_voip_addr_t local = {0};

            ip_addr_copy(local.addr, voip->sip_info.local.addr);
            return quec_voip_cfg_copy_out(value, len, &local, sizeof(local));
        }
        default:
            return QUEC_VOIP_ERR_INVALID_PARAM;
    }
}

/* Synchronize registration expiry time from eXosip to quec_voip */
void quec_voip_sync_register_expiry(int expires_val)
{
    if (quec_voip == NULL || expires_val <= 0)
        return;
    
    /* Update the register check time */
    quec_voip->sip_info.register_check_time = expires_val;
    voip_log("Synced register expiry: %d seconds", expires_val);
}

/* Query whether ip_rewrite (auto masquerade) is enabled, for use by eXosip callbacks */
int quec_voip_is_ip_rewrite_enabled(void)
{
    BOOL       ip_rewrite = FALSE;
    unsigned short len = sizeof(ip_rewrite);

    if (quec_voip == NULL)
        return 0;
    if (quec_voip_cfg_get_inner(quec_voip, QUEC_VOIP_CFG_ITEM_IP_REWRITE, &ip_rewrite, &len) != QUEC_VOIP_OK)
        return 0;
    return ip_rewrite ? 1 : 0;
}

quec_voip_err_e quec_voip_cfg_set(quec_voip_cfg_item_e item, const void *value, unsigned short len)
{
    quec_voip_cfg_req_t     req = {0};
    quec_voip_event_data_t *event = NULL;
    int                     ret = QUEC_VOIP_OK;

    if (quec_voip == NULL)
    {
        return QUEC_VOIP_ERR_NOT_INIT;
    }

    if (item >= QUEC_VOIP_CFG_ITEM_MAX || value == NULL || len > sizeof(req.data))
    {
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }

    req.item = item;
    req.len = len;
    if (len)
    {
        memcpy(req.data, value, len);
    }

    event = quec_voip_creat_event_data((uint8 *)&req, sizeof(req));
    if (event == NULL)
    {
        return QUEC_VOIP_ERR_NO_MEMORY;
    }

    if (quec_voip_send_manage_event(NULL, QUEC_VOIP_EVENT_CFG_SET, (void *)&event, sizeof(quec_voip_event_data_t *)))
    {
        quec_voip_event_data_destroy(event);
        return QUEC_VOIP_ERR_UNKNOWN;
    }

    quec_rtos_semaphore_wait(event->sem, QUEC_WAIT_FOREVER);
    ret = event->ret;
    quec_voip_event_data_destroy(event);
    return ret;
}

quec_voip_err_e quec_voip_cfg_get(quec_voip_cfg_item_e item, void *value, unsigned short *len)
{
    quec_voip_cfg_req_t     req = {0};
    quec_voip_cfg_req_t    *rsp = NULL;
    quec_voip_event_data_t *event = NULL;
    int                     ret = QUEC_VOIP_OK;

    if (quec_voip == NULL)
    {
        return QUEC_VOIP_ERR_NOT_INIT;
    }

    if (item >= QUEC_VOIP_CFG_ITEM_MAX || value == NULL || len == NULL)
    {
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }

    req.item = item;
    req.len = *len;

    event = quec_voip_creat_event_data((uint8 *)&req, sizeof(req));
    if (event == NULL)
    {
        return QUEC_VOIP_ERR_NO_MEMORY;
    }

    if (quec_voip_send_manage_event(NULL, QUEC_VOIP_EVENT_CFG_GET, (void *)&event, sizeof(quec_voip_event_data_t *)))
    {
        quec_voip_event_data_destroy(event);
        return QUEC_VOIP_ERR_UNKNOWN;
    }

    quec_rtos_semaphore_wait(event->sem, QUEC_WAIT_FOREVER);
    ret = event->ret;
    rsp = (quec_voip_cfg_req_t *)event->data;
    if (rsp)
    {
        *len = rsp->len;
        if (ret == QUEC_VOIP_OK)
        {
            memcpy(value, rsp->data, rsp->len);
        }
    }
    quec_voip_event_data_destroy(event);
    return ret;
}

void           *voip_manage_stackPtr = NULL;
quec_voip_err_e quec_voip_init(quec_voip_config_t *cfg)
{
    quec_voip_err_e ret = QUEC_VOIP_OK;
    if (cfg == NULL)
    {
        RTI_LOG("CFG_NULL");
        return QUEC_VOIP_ERR_INVALID_PARAM;
    }

    if (quec_voip_manage_m == NULL)
    {
        if (quec_rtos_queue_create(&quec_voip_manage_m, sizeof(quec_voip_manage_t), QUEC_VOIP_MANAGE_MSG_MAX))
        {
            return QUEC_VOIP_ERR_UNKNOWN;
        }
    }

    if (voip_manmage_ref == NULL)
    {
        ret = OSATaskCreate(&voip_manmage_ref, voip_manage_stackPtr, (10 * 1024), 90, "voip_manage", quec_voip_manage, NULL);
        if (ret)
        {
            voip_manmage_ref = NULL;
            return QUEC_VOIP_ERR_UNKNOWN;
        }
    }

    if (quec_voip == NULL)
    {
        ret = quec_voip_start_init(support_table, sizeof(support_table) / sizeof(quec_voip_media_profile_t));
        if (ret)
        {
            return ret;
        }
    }

    ret = quec_voip_cfg_set(QUEC_VOIP_CFG_ITEM_ALL, cfg, sizeof(*cfg));
    return ret;
}

#endif
