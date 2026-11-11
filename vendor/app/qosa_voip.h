#ifndef _QUEC_VOIP_H_
#define _QUEC_VOIP_H_

//#include "osa_def.h"
//#include "osa_q_sys.h"
//#include "osa_log.h"
//#include "osa_proj_config.h"
//#include "quec_pro_config.h"
#include "quec_rtos.h"
#include <ortp/ortp.h>
int quec_voip_call_op(unsigned char call_id,BOOL accept);
/* account: called party account buffer, len: valid string length without '\0'. */
int quec_voip_make_call(void *account, unsigned char len);
int quec_voip_make_hold(unsigned char call_id);
/* refer_to: called party address to transfer to, len: valid string length without '\0'. */
int quec_voip_send_refer(void *refer_to, unsigned char len);

#define QUEC_VOIP_CID_MAX            11
#define QUEC_VOIP_PORT_RANGE         32
#define QUEC_VOIP_PORT_DYNAMIC       0xC000
#define QUEC_VOIP_SIP_KEEPALIVE_TIME 30000
#define QUEC_VOIP_SIP_EXPIRY         100
#define QUEC_VOIP_GET_RANDOM_PORT    ((osip_build_random_number() & 0xffff) | QUEC_VOIP_PORT_DYNAMIC)

typedef struct regparam_t
{
    int regid;
    int expiry;
    int auth;
} regparam_t;

typedef enum
{
    QUEC_VOIP_CALL_IDLE = 0,  // no call
    QUEC_VOIP_CALL_HOLD = 1,
    QUEC_VOIP_CALL_ORIGINAL = 2,
    QUEC_VOIP_CALL_CONNECT = 3,
    QUEC_VOIP_CALL_INCOMING = 4,
    QUEC_VOIP_CALL_WAITING = 5,
    QUEC_VOIP_CALL_END = 6,
    QUEC_VOIP_CALL_ALERTING = 7,
} quec_voip_call_state_e;

#define QUEC_VOIP_ERR_BASE 0x8403
typedef enum
{
    QUEC_VOIP_OK = 0,
    QUEC_VOIP_ERR_UNKNOWN = QUEC_VOIP_ERR_BASE,
    QUEC_VOIP_ERR_NOT_INIT,
    QUEC_VOIP_ERR_NETWORK_NOT_REGIST,
    QUEC_VOIP_ERR_INVALID_PARAM,
    QUEC_VOIP_ERR_NO_MEMORY,
    QUEC_VOIP_ERR_NETIF_FAILED,
    QUEC_VOIP_ERR_SIP_PDP_OP_FAILED,
    QUEC_VOIP_ERR_SIP_PDP_NOT_DEF,
    QUEC_VOIP_ERR_IP_GET_FAILED,
    QUEC_VOIP_ERR_IP_TYPE_NOT_MATCH,
    QUEC_VOIP_ERR_TIMEOUT,
    QUEC_VOIP_ERR_SEM_FAILED,
    QUEC_VOIP_ERR_SIP_REGIST_FAILED,
    QUEC_VOIP_ERR_SIP_REGIST_REJECTED,
    QUEC_VOIP_ERR_SIP_PARAM_ILLEGAL,
} quec_voip_err_e;



typedef RtpSessionMode quec_media_mode_e;
typedef RtpProfile     quec_voip_rtp_profile_t;
typedef PayloadType    quec_voip_rtp_payload_t;
typedef struct
{
    int (*media_open)(void **fd, int sample_rate, unsigned char chn, unsigned char flag, void *argv);
    int (*media_close)(void *fd);
    int (*media_write)(void *fd, void *data, unsigned short len, quec_voip_rtp_payload_t *pt);
    int (*media_read)(void *fd, void *data, unsigned short len, quec_voip_rtp_payload_t *pt);
    void *fd_read;
    void *fd_write;
} quec_voip_media_profile_cb_t;

typedef struct
{
    ip_addr_t      addr;
    unsigned short port;
} quec_voip_addr_t;

typedef struct
{
    quec_voip_rtp_profile_t *profile;
    quec_voip_rtp_payload_t *pref_pt;
    quec_voip_addr_t         rtp_remote;
    quec_voip_addr_t         rtp_local;
    quec_voip_addr_t         rtcp_remote;
    quec_voip_addr_t         rtcp_local;
} quec_voip_media_t;




typedef struct{
    unsigned char index;
    char      user_name[32];
    quec_voip_call_state_e state;
}quec_voip_call_t;

typedef struct
{
    quec_voip_rtp_payload_t     *payload;
    quec_voip_media_profile_cb_t cb;
} quec_voip_media_profile_t;

typedef enum
{
    QUEC_VOIP_STREAM_DIR_RX = 0,
    QUEC_VOIP_STREAM_DIR_TX = 1,
} quec_voip_stream_dir_e;


typedef void (*quec_voip_audio_stream_cb_t)(
    void                          *ctx,
    int                            call_id,
    quec_voip_stream_dir_e         direction,
    const void                    *data,
    unsigned short                 len,
    const quec_voip_rtp_payload_t *pt
);



int quec_voip_send_unregist(void);

/*
 * APP filled local IPv4 for SIP registration.
 * APP gets the address from its own bearer (CH390/WiFi/other) and hands it over
 * here, so VOIP no longer depends on a specific netif or carrier mode.
 * ip: dotted decimal string, e.g. "192.168.6.118". Only the address is updated,
 * the local SIP port is left untouched.
 */
int quec_voip_set_local_ip(const char *ip);
quec_voip_err_e quec_voip_start(BOOL auto_regist);
quec_voip_err_e quec_voip_stop(void);
quec_voip_err_e quec_voip_sip_stop(void);

/*
 * External MO call entry: validate the callee account and enqueue the outgoing
 * call request so session creation runs in the unified manage thread.
 * account points to the callee SIP account string buffer, and len is the valid
 * byte count excluding the trailing '\0'.
 */
int quec_voip_make_call(void *account, unsigned char len);

typedef struct
{
    quec_voip_addr_t server;
    unsigned char    pdp_cid;
    unsigned char    simid;
    char             account[32];
    char             password[32];
    char             agent[32];
    unsigned short   expiry;
    unsigned short   keep_alive_time;
    BOOL             ip_rewrite;
    unsigned int     ring_timeout;
    unsigned int     ring_back_timeout;
    void             *sip_cb_ctx;
    void (*sip_state_cb)(void *ctx, BOOL state);
    void             *call_cb_ctx;
    void (*call_state_cb)(void *ctx, quec_voip_call_t *call_info);
} quec_voip_config_t;

typedef enum
{
    QUEC_VOIP_CFG_ITEM_ALL = 0,
    QUEC_VOIP_CFG_ITEM_ENABLE,
    QUEC_VOIP_CFG_ITEM_SERVER,
    QUEC_VOIP_CFG_ITEM_PDP_CID,
    QUEC_VOIP_CFG_ITEM_SIMID,
    QUEC_VOIP_CFG_ITEM_ACCOUNT,
    QUEC_VOIP_CFG_ITEM_PASSWORD,
    QUEC_VOIP_CFG_ITEM_AGENT,
    QUEC_VOIP_CFG_ITEM_EXPIRY,
    QUEC_VOIP_CFG_ITEM_KEEP_ALIVE,
    QUEC_VOIP_CFG_ITEM_IP_REWRITE,
    QUEC_VOIP_CFG_ITEM_AUTO_CONNECT,
    QUEC_VOIP_CFG_ITEM_RING_TIMEOUT,
    QUEC_VOIP_CFG_ITEM_RING_BACK_TIMEOUT,
    QUEC_VOIP_CFG_ITEM_SIP_CB_CTX,
    QUEC_VOIP_CFG_ITEM_SIP_STATE_CB,
    QUEC_VOIP_CFG_ITEM_CALL_CB_CTX,
    QUEC_VOIP_CFG_ITEM_CALL_STATE_CB,
    QUEC_VOIP_CFG_ITEM_LOCAL_IP, /* local IPv4 filled by APP, carrier-mode independent */
    QUEC_VOIP_CFG_ITEM_MAX,
} quec_voip_cfg_item_e;

quec_voip_err_e quec_voip_init(quec_voip_config_t *cfg);
quec_voip_err_e quec_voip_cfg_set(quec_voip_cfg_item_e item, const void *value, unsigned short len);
quec_voip_err_e quec_voip_cfg_get(quec_voip_cfg_item_e item, void *value, unsigned short *len);

/*
 * Standard RTP Payload Type mapping table.
 * Maps MIME type (case-insensitive) to standard RTP payload type numbers (0-96).
 */
typedef struct
{
    const char *mime_type;
    int         payload_type;
} quec_rtp_standard_pt_map_t;

#endif /* #ifndef _INTERNAL_H_ */
