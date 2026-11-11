/**
 * @file utltimer.h
 * @brief ASR 来源的 SIP/RTP 代码使用的定时器头文件名兼容入口。
 * @details oSIP、oRTP 源码仍通过此文件名包含平台声明；实际的定时器类型、
 * 接口声明及 UniRTOS 适配统一由 qcm_voip_port.h 和 qcm_voip_port.c 提供。
 * @note 保留旧文件名可减少对导入源码的修改，方便后续与源端对比和同步。
 * 本文件仅转发包含，不定义额外接口或创建定时器；删除前须同步修改所有引用。
 */
#include "qcm_voip_port.h"
