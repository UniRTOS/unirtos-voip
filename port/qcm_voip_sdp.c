/** @file qcm_voip_sdp.c
 * @brief RFC 3605/8866/3551 的 RTCP、媒体方向及静态 PCMA 默认处理。
 * @note 这是 UniRTOS 私有协议适配层，不改变导入的 oSIP 通用解析器。
 */
#include "qcm_voip_sdp.h"
#include "osip_port.h"

/** @brief 有界解析十进制端口。
 * @param[in] text 输入数字串。
 * @param[out] end 返回数字后的字符位置。
 * @param[out] port 返回 1..65535 的端口。
 * @return 0 成功，-1 格式或范围错误。
 */
static int voip_sdp_port(const char *text, const char **end, unsigned *port)
{
    unsigned value = 0;
    if (!text || *text < '0' || *text > '9') return -1;
    while (*text >= '0' && *text <= '9') {
        unsigned digit = (unsigned)(*text++ - '0');
        if (value > (65535U - digit) / 10U) return -1;
        value = value * 10U + digit;
    }
    if (!value) return -1;
    *port = value; *end = text;
    return 0;
}

int voip_sdp_audio_rtcp(sdp_media_t *media, quec_media_rtcp_attr_t *rtcp)
{
    int i;
    unsigned port;
    const char *end;
    if (!media || !rtcp || voip_sdp_port(media->m_port, &end, &port) || *end) return -1;
    memset(rtcp, 0, sizeof(*rtcp));
    for (i = 0; i < media->a_attributes.nb_elt; ++i) {
        sdp_attribute_t *attr = osip_list_get(&media->a_attributes, i);
        const char *value;
        char net[16], family[8], address[16], extra;
        if (!attr || !attr->a_att_field || osip_strcasecmp(attr->a_att_field, "rtcp")) continue;
        value = attr->a_att_value;
        if (!value) return -1;
        while (*value == ' ' || *value == '\t') ++value;
        if (voip_sdp_port(value, &end, &port)) return -1;
        if (*end && *end != ' ' && *end != '\t') return -1;
        rtcp->port = (unsigned short)port;
        while (*end == ' ' || *end == '\t') ++end;
        if (!*end) return 0;
        if (sscanf(end, "%15s %7s %15s %c", net, family, address, &extra) != 3 ||
            strcmp(net, "IN") || strcmp(family, "IP4") ||
            !ipaddr_aton(address, &rtcp->addr) || !IP_IS_V4(&rtcp->addr)) return -1;
        strcpy(rtcp->net_type, "IN");
        return 0;
    }
    if (port == 65535U) return -1;
    rtcp->port = (unsigned short)(port + 1U);
    return 0;
}

/** @brief 查找一层属性中的媒体方向。
 * @param[in] attrs 属性列表。
 * @param[out] direction 匹配时返回方向。
 * @return true 表示存在有效方向属性。
 */
static bool voip_sdp_find_direction(osip_list_t *attrs, quec_media_direction_e *direction)
{
    int i;
    for (i = 0; i < attrs->nb_elt; ++i) {
        sdp_attribute_t *attr = osip_list_get(attrs, i);
        if (!attr || !attr->a_att_field) continue;
        if (!osip_strcasecmp(attr->a_att_field, "sendrecv")) *direction = QUEC_MEDIA_DIRECTION_SENDRECV;
        else if (!osip_strcasecmp(attr->a_att_field, "sendonly")) *direction = QUEC_MEDIA_DIRECTION_SENDONLY;
        else if (!osip_strcasecmp(attr->a_att_field, "recvonly")) *direction = QUEC_MEDIA_DIRECTION_RECVONLY;
        else if (!osip_strcasecmp(attr->a_att_field, "inactive")) *direction = QUEC_MEDIA_DIRECTION_INACTIVE;
        else continue;
        return true;
    }
    return false;
}

int voip_sdp_direction(sdp_message_t *session, sdp_media_t *media, quec_media_direction_e *direction)
{
    if (!session || !media || !direction) return -1;
    *direction = QUEC_MEDIA_DIRECTION_SENDRECV;
    if (!voip_sdp_find_direction(&media->a_attributes, direction))
        voip_sdp_find_direction(&session->a_attributes, direction);
    return 0;
}

bool voip_sdp_static_pcma(sdp_media_t *media)
{
    int i;
    bool found = false;
    if (!media) return false;
    for (i = 0; i < media->m_payloads.nb_elt; ++i) {
        const char *payload = osip_list_get(&media->m_payloads, i);
        if (payload && !strcmp(payload, "8")) found = true;
    }
    if (!found) return false;
    for (i = 0; i < media->a_attributes.nb_elt; ++i) {
        sdp_attribute_t *attr = osip_list_get(&media->a_attributes, i);
        const char *end;
        unsigned payload;
        if (!attr || !attr->a_att_field || osip_strcasecmp(attr->a_att_field, "rtpmap") || !attr->a_att_value) continue;
        if (!voip_sdp_port(attr->a_att_value, &end, &payload) && payload == 8 && (*end == ' ' || *end == '\t')) return false;
    }
    return true;
}
