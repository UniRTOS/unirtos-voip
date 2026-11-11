/**
 * @file qcm_voip_pcm.c
 * @brief 将 ASR PCMA 媒体回调接入 UniRTOS 双向 PCM 流。
 * @note QuecDuino 的 ES8311 使用 BCLK 倍频，硬件保持 16 kHz，SIP 媒体保持 8 kHz。
 */
#include "qcm_voip_port.h"
#include "qcm_voip_pcm.h"
#include "qcm_voip_resample.h"
#include "qcm_audio.h"
#include "rtp_g711.h"

/** @struct voip_pcm_t
 * @brief 一条单向 PCM 流，滤波历史随流创建和释放。
 */
typedef struct voip_pcm {
    struct voip_pcm *retired_next; /*!< 关闭失败时转交给适配层的回收链。 */
    qosa_aud_handle_t stream; /*!< UniRTOS 音频句柄。 */
    uintptr_t generation;   /*!< 播放流代号，用于单播放流占用校验。 */
    bool record;             /*!< true 为录音，false 为播放。 */
    unsigned capture_gain_percent; /*!< 本条流的上行数字幅度百分比快照。 */
    voip_resample_t filter;   /*!< 当前方向的独立转换状态。 */
} voip_pcm_t;
static unsigned ringback_volume = 1; /*!< 主叫接通前的独立播放档位。 */
static bool ringback_selected; /*!< SIP 状态回调维护，打开流时取得快照。 */
static unsigned call_volume = 6; /*!< 未配置时保持平台原始幅度。 */
static unsigned capture_gain = 100; /*!< 未配置时不放大上行。 */
static qosa_mutex_t pcm_lifecycle;
static voip_pcm_t *retired_streams;
static uintptr_t playback_generation;
static uintptr_t playback_active;
/** @brief 在生命周期锁内重试回收已交还的流；失败时阻止打开新流。
 * @return 0 表示已全部回收，负值表示底层关闭仍失败。
 * @note 队列只在短临界区内修改，音频关闭在临界区外执行。
 */
static int reap_retired(void)
{
    voip_pcm_t *pcm;
    for (;;) {
        qosa_enter_critical();
        pcm = retired_streams;
        if (pcm) retired_streams = pcm->retired_next;
        qosa_exit_critical();
        if (!pcm) return 0;
        if (qcm_aud_stream_close(pcm->stream, true)) {
            qosa_enter_critical();
            pcm->retired_next = retired_streams;
            retired_streams = pcm;
            qosa_exit_critical();
            return -1;
        }
        qosa_enter_critical();
        if (!pcm->record && playback_active == pcm->generation) playback_active = 0;
        qosa_exit_critical();
        free(pcm);
    }
}

/** @brief 保存 PCM 流打开时使用的播放等级及上行数字增益。
 * @param[in] unsigned volume 播放等级 0～11。
 * @param[in] unsigned capture_gain_percent 上行幅度百分比 100～800。
 * @return 0 成功，-1 参数无效；失败不改变原配置。
 * @note 由组件在 SIP 停止时调用；短临界区保护配置快照，不执行硬件操作。
 */
int voip_pcm_set_audio(unsigned volume, unsigned capture_gain_percent)
{
    if (volume > 11U || capture_gain_percent < 100U || capture_gain_percent > 800U) return -1;
    qosa_enter_critical();
    call_volume = volume;
    capture_gain = capture_gain_percent;
    qosa_exit_critical();
    return 0;
}

/** @brief 保存主叫回铃等级，不影响通话及麦克风配置。
 * @param[in] unsigned volume 播放等级 0～11。
 * @return 0 成功，-1 超出范围；失败保留原值。
 */
int voip_pcm_set_ringback_volume(unsigned volume)
{
    if (volume > 11U) return -1;
    qosa_enter_critical(); ringback_volume = volume; qosa_exit_critical();
    return 0;
}

/** @brief 按当前 SIP 状态选择下一条播放流的等级。
 * @param[in] bool enabled true 为主叫振铃，false 为正常通话。
 * @return 无；只更新短临界区中的状态，不调用硬件。
 */
void voip_pcm_use_ringback(bool enabled)
{
    qosa_enter_critical(); ringback_selected = enabled; qosa_exit_critical();
}

/** @brief 在媒体线程创建前初始化 PCM 打开/关闭互斥锁。
 * @return 0 成功，其余为底层错误码。
 */
int voip_pcm_init(void)
{
    return pcm_lifecycle ? 0 : qosa_mutex_create(&pcm_lifecycle);
}

/** @brief 创建 16 kHz 硬件音频流，对上层提供 8 kHz 单声道媒体。
 * @param[out] void **out 成功时返回本层拥有的音频句柄。
 * @param[in] int rate 上层采样率，必须为 8000 Hz。
 * @param[in] unsigned channels 声道数，必须为 1。
 * @param[in] int record 非零表示录音。
 * @return 0 成功，负值表示参数、资源或底层打开失败。
 */
int voip_pcm_open(void **out, int rate, unsigned channels, int record)
{
    voip_pcm_t *pcm;
    qosa_aud_stream_cfg_t cfg = {0};
    unsigned volume;
    int ret;
    if (!out || rate != 8000 || channels != 1) return -1;
    *out = NULL;
    pcm = calloc(1, sizeof(*pcm));
    if (!pcm) return -1;
    pcm->record = !!record;
    qosa_enter_critical();
    pcm->capture_gain_percent = capture_gain;
    volume = ringback_selected ? ringback_volume : call_volume;
    qosa_exit_critical();
    cfg.is_record = pcm->record;
    cfg.samprate = 16000;
    cfg.channels = 1;
    cfg.sync_mode = false;
    cfg.format = QOSA_AUD_FMT_PCM;
    cfg.route = QOSA_AUD_ROUTE_NEAR;
    if (!pcm_lifecycle || qosa_mutex_lock(pcm_lifecycle, QOSA_WAIT_FOREVER)) {
        free(pcm);
        return -1;
    }
    if (reap_retired() || (!pcm->record && playback_active)) {
        qosa_mutex_unlock(pcm_lifecycle);
        free(pcm);
        return -1;
    }
    if (!pcm->record) {
        qosa_enter_critical();
        if (++playback_generation == 0) ++playback_generation;
        pcm->generation = playback_generation;
        playback_active = pcm->generation;
        qosa_exit_critical();
    }
    // 来电铃声会修改全局等级；每次打开恢复当前回铃/通话阶段的独立等级。
    ret = pcm->record ? QOSA_AUD_SUCCESS : qosa_aud_set_volume((qosa_aud_vol_level_e)volume);
    if (ret == QOSA_AUD_SUCCESS) ret = qcm_aud_stream_open(&cfg, &pcm->stream);
    if (ret != QOSA_AUD_SUCCESS && !pcm->record) {
        qosa_enter_critical(); playback_active = 0; qosa_exit_critical();
    }
    qosa_mutex_unlock(pcm_lifecycle);
    if (ret != QOSA_AUD_SUCCESS) {
        free(pcm);
        return -1;
    }
    *out = pcm;
    return 0;
}

/** @brief 媒体线程退出后关闭单向流，关闭成功才释放句柄。
 * @param[in] void *handle 本层创建的有效句柄。
 * @return 0 成功，负值表示关闭失败。
 */
int voip_pcm_close(void *handle)
{
    voip_pcm_t *pcm = handle;
    int ret;
    if (!pcm || !pcm_lifecycle || qosa_mutex_lock(pcm_lifecycle, QOSA_WAIT_FOREVER)) return -1;
    qosa_enter_critical();
    if (!pcm->record && playback_active == pcm->generation) playback_active = 0;
    qosa_exit_critical();
    ret = qcm_aud_stream_close(pcm->stream, true);
    qosa_mutex_unlock(pcm_lifecycle);
    if (ret) return -1;
    free(pcm);
    return 0;
}

/** @brief 交还音频句柄，关闭失败时由适配层保留并在下次打开前重试。
 * @param[in] void *handle 有效句柄；无论返回值如何，调用者都必须丢弃该句柄。
 * @return 0 表示已关闭；负值表示已接管句柄，底层资源等待回收。
 * @note 用于 ASR 媒体销毁回调的关闭失败回收。
 */
int voip_pcm_dispose(void *handle)
{
    voip_pcm_t *pcm = handle;
    if (!pcm) return -1;
    if (!voip_pcm_close(pcm)) return 0;
    qosa_enter_critical();
    pcm->retired_next = retired_streams;
    retired_streams = pcm;
    qosa_exit_critical();
    return -1;
}

/** @brief 读取 16 kHz PCM，低通抽取并编码为最多 20 ms 的 PCMA。
 * @param[in] void *handle 录音句柄。
 * @param[out] uint8_t *data PCMA 输出缓冲。
 * @param[in] unsigned size 输出容量，单位字节。
 * @return 实际 PCMA 字节数；0 暂无数据；负值表示录音失败。
 */
int voip_pcm_read_alaw(void *handle, uint8_t *data, unsigned size)
{
    voip_pcm_t *pcm = handle;
    int16_t input[320];
    int16_t output[160];
    unsigned count = size > 160 ? 160 : size;
    size_t used;
    unsigned i;
    int bytes;
    if (!pcm || !pcm->record || !data || !count) return -1;
    bytes = qcm_aud_stream_read(pcm->stream, (uint8_t *)input, count * 4U);
    if (!bytes) {
        qosa_task_sleep_ms(20);
        bytes = qcm_aud_stream_read(pcm->stream, (uint8_t *)input, count * 4U);
    }
    // UniRTOS 错误码可能为正数，必须排除越界和非完整 PCM 采样。
    if (bytes < 0 || bytes > (int)(count * 4U) || (bytes & 1)) return -1;
    used = voip_resample_down(&pcm->filter, input, (unsigned)bytes / 2U, output);
    for (i = 0; i < used; ++i) {
        int32_t sample = (int32_t)output[i] * (int32_t)pcm->capture_gain_percent / 100;
        // 在 PCMA 编码前放大上行，饱和处理避免整数回绕。
        if (sample > 32767) sample = 32767;
        else if (sample < -32768) sample = -32768;
        data[i] = linear2alaw((int16_t)sample);
    }
    return (int)used;
}

/** @brief 解码 PCMA 并插值到 16 kHz，处理非阻塞音频写入的短写。
 * @param[in] void *handle 播放句柄。
 * @param[in] const uint8_t *data 输入 PCMA 数据。
 * @param[in] unsigned size 输入字节数。
 * @return 成功消费的 PCMA 字节数；负值表示写入失败或超过 100 ms 预算。
 */
int voip_pcm_write_alaw(void *handle, const uint8_t *data, unsigned size)
{
    voip_pcm_t *pcm = handle;
    int16_t input[160];
    int16_t output[320];
    unsigned used = 0;
    uint32_t start = qosa_get_running_time();
    if (!pcm || pcm->record || !data) return -1;
    while (used < size) {
        unsigned count = size - used > 160 ? 160 : size - used;
        unsigned offset = 0;
        unsigned i;
        unsigned bytes;
        for (i = 0; i < count; ++i) input[i] = alaw2linear(data[used + i]);
        bytes = (unsigned)voip_resample_up(&pcm->filter, input, count, output) * 2U;
        while (offset < bytes) {
            int written = qcm_aud_stream_write(pcm->stream, (uint8_t *)output + offset, bytes - offset);
            if (written < 0 || written > (int)(bytes - offset) || (written & 1)) return -1;
            offset += (unsigned)written;
            if (!written) {
                if ((uint32_t)(qosa_get_running_time() - start) >= 100U) return -1;
                qosa_task_sleep_ms(10);
            }
        }
        used += count;
    }
    return (int)used;
}

