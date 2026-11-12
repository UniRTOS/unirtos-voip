# unirtos-voip

[中文](README.zh.md) | English

An external UniRTOS VoIP component providing SIP call control and RTP voice transport.
For a complete application, use [unirtos-voip-demos](https://github.com/UniRTOS/unirtos-voip-demos).

## Features

- SIP/UDP registration, dialing, answering, rejection, cancellation and hangup for one bidirectional call.
- RTP/PCMA (G.711 A-law), PCM resampling and audio stream adaptation.
- Separate call volume, outgoing ringback volume and microphone gain controls.
- Call status, registration results and media diagnostics.

The component handles protocols and media. The demo owns cellular connectivity,
accounts, buttons, LEDs, ES8311 and the incoming ringtone.

## Integration

Start with the companion demo, which declares the component and official SDK in
`env_config.json`. Run these commands from the demo project directory:

```bash
unirtos-cli env-setup
unirtos-cli build
```

For your own application, follow the demo's `env_config.json` to add the
`unirtos-voip` dependency, link the `unirtos-voip` CMake target and include
`qcm_voip.h`. Enable audio streams and sockets in the application configuration:

```text
CONFIG_QCM_AUDIO_STREAM_FUNC=y
CONFIG_QCM_SOCKET_ADP_FUNC=y
```

The SDK compiles the component and links it into the application firmware.
Build the application directory; this library alone does not produce flashable firmware.
No SDK modification is required.

## Main APIs

| API | Purpose |
| --- | --- |
| `qcm_voip_configure` | Configure the server and account |
| `qcm_voip_start` / `qcm_voip_stop` | Register / stop and unregister |
| `qcm_voip_dial` / `qcm_voip_answer` | Dial / answer |
| `qcm_voip_hangup` | Cancel, reject or hang up |
| `qcm_voip_set_audio` | Set call volume and microphone gain |
| `qcm_voip_set_ringback_volume` | Set ringback volume |
| `qcm_voip_get_status` | Query current status |

See [include/qcm_voip.h](include/qcm_voip.h) for parameters and calling constraints.
Serialize control operations in an application task.

## Directory Structure

```text
include/        Public APIs
port/           UniRTOS, audio, resampling and SDP adapters
vendor/         SIP/RTP protocols and codecs
qcm_voip.c      Call control
docs/           Porting notes and source provenance
CMakeLists.txt  Component build entry
```

See [porting notes](docs/PORTING.md) and
[third-party notices](THIRD_PARTY_NOTICES.md) for implementation and licensing details.
