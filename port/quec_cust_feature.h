/**
 * @file quec_cust_feature.h
 * @brief ASR 来源的呼叫模块及 SIP 库使用的配置头文件名兼容入口。
 * @details 原源码保留了此头文件名；此处仅通过 qcm_voip_port.h 引入统一的
 * 平台适配声明，不新增功能开关。组件编译开关由 CMakeLists.txt 中的编译定义提供。
 * @note 保留旧文件名可减少对导入源码的修改，方便后续与源端对比和同步。
 * 本文件仅转发包含，不保存账号或测试配置；删除前须同步修改所有引用。
 */
#include "qcm_voip_port.h"
