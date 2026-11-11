/**
 * @file lwip_api.h
 * @brief ASR 来源的 SDP 代码使用的网络头文件名兼容入口。
 * @details sdp_message.h 保留了此头文件的引用；通过 qcm_voip_port.h 引入
 * UniRTOS 网络接口、地址类型及相关适配，不在此提供独立的 lwIP 实现。
 * @note 保留旧文件名可减少对导入源码的修改，方便后续与源端对比和同步。
 * 本文件仅转发包含，不初始化网络或创建套接字；删除前须同步修改所有引用。
 */
#include "qcm_voip_port.h"
