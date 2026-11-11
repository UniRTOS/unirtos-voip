# UniRTOS VoIP 组件

包版本 `1.0.1`。本仓库将已验证的 UniRTOS VoIP 实现整理为独立外部组件，供应用通过 `unirtos-cli` 声明依赖、下载和编译。首版支持 **EG800ZCN_LA**，配套示例为同级 `unirtos-voip-demos`。

本组件仅提供 VoIP 通话能力。保留既有 SIP/RTP 状态机、PCMA 编解码、连续拨号与取消、PCM 流生命周期及音量设置；1.0.1 删除独立录放、单音和固定 PCM 播放自测 API 及其测试统计。

## 功能边界

- SIP/UDP 注册、主动呼叫、被叫接听、拒接、取消呼叫和通话挂断。
- 单个活动通话，服务器使用 IPv4 地址。
- RTP 语音使用 G.711 A-law（PCMA，8 kHz，静态 payload type 8）。
- 模组侧录音和播放采用 16 kHz、单声道、16-bit PCM，通过现有重采样适配网络音频。
- 提供呼叫状态、收发/录音计数、呼叫阶段和注册/来电诊断。
- 通话播放音量、上行数字增益、接通前回铃音量可独立设置。

账号、网络承载启停、按键、LED、来电铃声和 ES8311/PA 由应用及板级适配负责。本组件不会自动注册账号，也不会在上电时自行拨号。

## 必须配套的 SDK

当前本地验证采用 **`1.0.4-voip.1`**，这是本地配套版本，尚非官方发布版本。它必须同时包含匹配的音频头文件、音频实现/归档和 EG800Z 平台底包。

官方原始 `1.0.4` 与已验证源工程的音频结构布局存在差异，不能只复制应用和本组件后直接混用。配套 SDK 必须满足：

1. 在外部组件发现前定义 CMake 变量 `UNIRTOS_VOIP_AUDIO_ABI=1`。
2. 提供 `qcm_aud_stream_open/close/read/write`、匹配的 stream 配置/回调结构，以及已验证的 PCM 格式处理、回调上下文和同步事件修复。
3. 提供 `qosa_audio_set_stream_iface/get_stream_iface`、QOSA OS/time/socket/log API。
4. 在导入本组件前建立 `qcm_audio` 与 `socket_adapter` CMake target。

组件 CMake 会检查板型和配套标记。不应手动伪造标记来跳过兼容检查；标记表示 SDK 发布者已经将头文件、实现和底包配套。

## 在应用中使用

应用的 `env_config.json` 包含以下依赖片段，完整本地验证配置由配套示例提供：

```json
{
  "sdk": {
    "version": "1.0.4-voip.1"
  },
  "libraries": {
    "list": [
      { "name": "unirtos-voip", "version": "1.0.1" }
    ]
  }
}
```

完成组件和 SDK 的 manifest 登记后，在应用目录执行：

```powershell
unirtos-cli env-setup
unirtos-cli build
```

应用 CMake 显式链接 `unirtos-voip`，包含公共头 `qcm_voip.h`。本组件根目录由 SDK 的 `add_subdirectory()` 导入，不作为独立固件工程运行 CMake。

## 公共 API

完整参数和调用契约见 [public/qcm_voip.h](public/qcm_voip.h)。

| API | 用途 |
|---|---|
| `qcm_voip_configure` | 在 SIP 停止时设置服务器、端口和本机账号 |
| `qcm_voip_set_audio` | 设置通话播放音量和上行数字增益 |
| `qcm_voip_set_ringback_volume` | 设置主叫振铃阶段播放音量 |
| `qcm_voip_start` / `qcm_voip_stop` | 使用已激活网络的本地 IPv4 注册，或停止服务 |
| `qcm_voip_dial` | 呼叫指定对端账号 |
| `qcm_voip_answer` / `qcm_voip_hangup` | 接听，或取消/拒接/挂断 |
| `qcm_voip_get_status` | 获取当前状态和媒体计数 |
| `qcm_voip_get_call_diag` / `qcm_voip_get_net_diag` | 获取呼叫建立及网络诊断快照 |

应用必须将阻塞控制操作串行放在一个业务工作任务中，不能在 ISR 或 AT 回调中直接等待执行。AT 命令受理、控制操作返回和异步呼叫状态分别判断；具体用法见配套 demo。

回铃音量只适用于协议能识别的接通前阶段。服务器在 SIP 200 接通后混入的提示音属于已接通媒体，使用通话音量。

## 源码布局与差异

| 路径 | 内容 |
|---|---|
| `public/` | 应用可包含的公共 API |
| `qcm_voip.c` | 有界单通话 API 及控制边界 |
| `port/` | UniRTOS 线程、socket、时钟和 PCM/SDP 适配 |
| `vendor/app/` | 已适配的呼叫管理与 SIP/RTP 集成 |
| `vendor/osip/` | 原始 oSIP/eXosip 及既有适配 |
| `vendor/ortp/` | 原始 oRTP、G.711 及既有适配 |

### 兼容头文件

`port/` 中的以下 5 个头文件保留了 ASR 来源代码使用的旧头文件名。当前导入的 SIP/RTP 源码仍引用这些名称，文件中的 `#include "qcm_voip_port.h"` 将它们接入统一的 UniRTOS 平台适配层，因此这些文件应随组件源码一起提供。保留旧名称便于与原始代码对比及后续同步。

| 文件 | 对应的适配用途 | 当前引用示例 |
|---|---|---|
| `port/utltimer.h` | 定时器类型与接口 | oSIP 的 `osip.h`、oRTP 的 `str_utils.h` |
| `port/utlTime.h` | 时间类型与时间查询接口 | oSIP 的 `osip_port.c` |
| `port/quec_rtos.h` | 任务、信号量、消息队列等 RTOS 类型与接口 | 呼叫模块的 `qosa_voip.h`、oSIP 的 `port_condv.c` |
| `port/lwip_api.h` | SDP 所需的网络接口和地址类型 | SDP 的 `sdp_message.h` |
| `port/quec_cust_feature.h` | 呼叫模块及 SIP 库原有配置头文件的包含入口 | `qosa_voip.c`、eXosip 的 `eXosip2.h` |

平台类型、接口映射和函数声明集中在 `port/qcm_voip_port.h`，需要适配的函数实现位于 `port/qcm_voip_port.c`，组件编译宏由根目录的 `CMakeLists.txt` 提供。每个兼容头文件开头都有中文说明，可直接打开查看其来源、用途和引用关系。后续若合并这些入口，需要一并更新所有旧头文件名的引用，并重新验证构建。

发布快照仅包含当前通话组件。版本 `1.0.1` 对应标签 `v1.0.1`，供 CLI manifest 引用；本地开发历史和旧自测代码不属于发布分支。

构建产物和本机验证工具不随组件源码提交。CLI 下载、编译和板上通话是不同验证层次，实际执行结果以工作区的验证说明为准。

## 来源与版权

原始来源及既有移植边界见 [PORTING.txt](PORTING.txt)。[source_manifest.json](source_manifest.json) 保留历史源工程的路径映射和原始 ASR 文件校验值，不是当前适配文件的校验清单。

第三方源文件的原有声明和许可证随源码保留，见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

`port/qcm_voip_pcm.c` 是实际通话使用的 PCM 采集/播放与 PCMA 转换接口，必须参与构建；它不是独立 PCM 测试 demo。
