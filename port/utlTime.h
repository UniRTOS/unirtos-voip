/**
 * @file utlTime.h
 * @brief ASR 来源的 oSIP 代码使用的时间头文件名兼容入口。
 * @details osip_port.c 保留了此头文件的引用；时间类型及 gettimeofday、
 * clock_gettime 等接口的映射统一由 qcm_voip_port.h 提供，实现在 qcm_voip_port.c。
 * @note 保留旧文件名可减少对导入源码的修改，方便后续与源端对比和同步。
 * 本文件仅转发包含，不单独实现计时功能；删除前须同步修改所有引用。
 */
#include "qcm_voip_port.h"
