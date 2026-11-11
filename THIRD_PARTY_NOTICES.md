# 第三方源码说明

本仓库从已适配的 UniRTOS VoIP 组件整理而来，保留既有源文件和版权声明。拆包不变更第三方代码的许可证。

| 路径 | 来源与声明位置 |
|---|---|
| `vendor/app/` | ASR VoIP 呼叫管理集成，保留各文件原有声明 |
| `vendor/osip/` | oSIP/eXosip 源码，保留各文件原有声明 |
| `vendor/osip/exosip/COPYING` | 源包附带的 GNU General Public License 文本 |
| `vendor/ortp/` | oRTP/G.711 源码，保留各文件原有声明 |

各文件的实际版权头和附带许可证是对应文件的依据。本说明仅记录现有材料，不为所有文件另行授予统一许可证，也不沿用 LVGL 示例的许可证。

历史源路径和原始校验值位于 `source_manifest.json`。其中 `target` 的 `components/qcm_voip/` 前缀对应本仓库根目录；校验值是原始 ASR 来源记录，不应当作当前已适配文件的哈希。
