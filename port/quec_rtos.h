/**
 * @file quec_rtos.h
 * @brief ASR 来源的呼叫模块和 oSIP 代码使用的 RTOS 头文件名兼容入口。
 * @details 原有任务、信号量、消息队列等类型和接口，通过 qcm_voip_port.h
 * 映射到 UniRTOS；需要函数实现的适配由 qcm_voip_port.c 提供。
 * @note 保留旧文件名可减少对导入源码的修改，方便后续与源端对比和同步。
 * 本文件仅转发包含，不创建任务或其他系统资源；删除前须同步修改所有引用。
 */
#include "qcm_voip_port.h"
