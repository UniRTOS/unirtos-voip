/** @file qcm_voip_platform.h
 * @brief Shared timing adapter for applications using the official UniRTOS SDK.
 */
#ifndef QCM_VOIP_PLATFORM_H
#define QCM_VOIP_PLATFORM_H
#include <stdint.h>

/** @brief Read monotonic milliseconds derived from the SDK system tick counter.
 * @return Low 32 bits of elapsed milliseconds; use unsigned subtraction for intervals.
 * @note Thread-safe, independent of network/RTC wall-clock adjustments. Tick rollover
 * is extended by the component; call at least once per hardware tick-counter wrap.
 */
uint32_t qcm_voip_uptime_ms(void);
#endif
