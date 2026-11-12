# Porting notes

## Source provenance

The original source is `ASR/3601/dev/temp/mike/R06_VOIP_1331597/quectel/voip`.
The existing UniRTOS port was the adaptation reference. [source_manifest.json](source_manifest.json)
records the original ASR file paths and hashes, not hashes of the current port.
Original notices remain in `vendor/`; see [third-party notices](../THIRD_PARTY_NOTICES.md).

## Official SDK boundary

The component uses the official SDK selected by the demo's `env_config.json`.
All compatibility changes are in this repository and the demo:

- The PCM stream uses the official `qosa_aud_stream_cfg_t`; reserved `options`
  stays zero. No local `route` field or alternate SDK header is used.
- The SDK links its own `libqcm_audio.a` and socket implementation. CMake checks
  required Kconfig features, without requiring `UNIRTOS_VOIP_AUDIO_ABI` or a
  synthetic imported audio target.
- `qcm_voip_uptime_ms()` derives monotonic milliseconds from the public
  `qosa_get_system_tick_cnt()` API and `CONFIG_QOSA_SYS_TICK_PER_SECOND`.
  It extends tick rollover before conversion. The active application polls
  frequently enough to observe rollover. Timers do not depend on RTC time changes.
- The demo keeps asynchronous ringtone writes in its control task and handles
  write failures/stalls there; it does not depend on callback `ctx` forwarding
  from the older SDK audio archive.
- SDK source, audio archives and the EG800Z base firmware are not replaced.

## Preserved call behavior

SIP/RTP state machines and cancellation/re-dial handling remain in `vendor/`.
PCM stream ownership and close-failure recovery remain in `port/qcm_voip_pcm.c`.
The 63-tap Q15 resampler, PCMA codec, volume selection and capture gain are retained.
The demo owns accounts, bearer activation, GPIO, ES8311 and ringtone policy.

Legacy include names (`utltimer.h`, `utlTime.h`, `quec_rtos.h`, `lwip_api.h`,
`quec_cust_feature.h`) forward to `qcm_voip_port.h` because imported files still
include them. They are compatibility boundaries, not empty unused placeholders.

Build logs, firmware and host validation scripts belong outside the published
repositories. Compile/link/package checks do not replace board acceptance after
changing the underlying SDK binaries.

## Tone PCM volume (2026-11-12)

Tone levels are applied to PCM before `qcm_aud_stream_write`, with the SDK set to
unity (6). Level 1 is 1/32 (-30.1 dB), versus the old platform level 1 at 1/6
(-15.6 dB). This deliberately makes low tone levels quieter; speech and microphone
gain defaults are unchanged. The component checks SIP tone/speech selection per
frame and protects partial-write retries from in-place SDK sample changes.

An observed caller log while the callee was not answered had `state=3`, `role=1`,
`sip=200`, `rec=1`, `play=1`. The optional answered-tone guard now handles this
path entirely inside the component. The demo enables it, while the component's
default remains off for other consumers. It classifies 20 ms of decoded PCMA at
8 kHz using fixed-point Goertzel/least-squares projection (425, 450, 440+480 Hz).
At least 94% explained energy selects tone volume; non-tone frames use the
original call curve immediately. After 300 ms of active non-tone audio the
detector stops for that stream; new streams reset. There is no heap allocation
per frame, media buffering delay or SIP state change. Initial short packets
may use the quiet level until a complete 20 ms window is available.

This heuristic is not a reliable remote-answer signal. Other frequencies,
music, distorted tones or tone-like speech can be misclassified. Disable it
using the demo macro/API when unsuitable. Diagnostics are current values only
and are returned by explicit query; there are no additional automatic URCs.
SDK source, prebuilt archives and base firmware are not modified.
Frequency templates reference the upstream
[Asterisk indications sample](https://github.com/asterisk/asterisk/blob/master/configs/samples/indications.conf.sample).
The actual server audio still requires board verification.
