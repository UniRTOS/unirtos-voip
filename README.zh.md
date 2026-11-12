# unirtos-voip

中文 | [English](README.md)

UniRTOS 外部 VoIP 组件，提供 SIP 通话控制和 RTP 语音传输。
完整通话示例见 [unirtos-voip-demos](https://github.com/UniRTOS/unirtos-voip-demos)。

## 功能说明

- SIP/UDP 注册、拨号、接听、拒接、取消和挂断，支持单路双向通话。
- RTP/PCMA（G.711 A-law）语音传输，组件完成 PCM 重采样和音频流适配。
- 支持通话音量、主叫回铃音量和麦克风数字增益设置。
- 提供通话状态、注册结果和媒体诊断接口。

组件负责协议和媒体处理；蜂窝联网、账号、按键、指示灯、ES8311 和来电铃声由 Demo 管理。

## 接入方式

推荐先运行配套 Demo。它已经在 `env_config.json` 中声明组件与官方 SDK 依赖。
进入 Demo 工程目录执行：

```bash
unirtos-cli env-setup
unirtos-cli build
```

接入自己的应用时，参照 Demo 的 `env_config.json` 添加 `unirtos-voip` 依赖，在
CMake 中链接 `unirtos-voip`，并包含 `qcm_voip.h`。应用需要启用音频流和 socket 功能：

```text
CONFIG_QCM_AUDIO_STREAM_FUNC=y
CONFIG_QCM_SOCKET_ADP_FUNC=y
```

SDK 会编译组件并链接到应用固件。组件目录本身不生成可烧录固件，适配不需要修改 SDK。

## 主要接口

| 接口 | 用途 |
| --- | --- |
| `qcm_voip_configure` | 配置服务器和账号 |
| `qcm_voip_start` / `qcm_voip_stop` | 注册 / 停止并注销 |
| `qcm_voip_dial` / `qcm_voip_answer` | 拨号 / 接听 |
| `qcm_voip_hangup` | 取消、拒接或挂断 |
| `qcm_voip_set_audio` | 设置通话音量和上行增益 |
| `qcm_voip_set_ringback_volume` | 设置回铃音量 |
| `qcm_voip_get_status` | 查询当前状态 |

参数和调用约束见 [include/qcm_voip.h](include/qcm_voip.h)。控制接口应在业务任务中串行调用。

## 目录结构

```text
include/        公共接口
port/           UniRTOS、音频、重采样和 SDP 适配
vendor/         SIP/RTP 协议及编解码实现
qcm_voip.c      通话控制
docs/           适配说明和源码来源
CMakeLists.txt  组件构建入口
```

适配细节见 [docs/PORTING.md](docs/PORTING.md)，版权和许可见
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
