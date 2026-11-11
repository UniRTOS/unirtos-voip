/*
  eXosip - This is the eXtended osip library.
  Copyright (C) 2001-2020 Aymeric MOIZARD amoizard@antisip.com

  eXosip is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  eXosip is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

  In addition, as a special exception, the copyright holders give
  permission to link the code of portions of this program with the
  OpenSSL library under certain conditions as described in each
  individual source file, and distribute linked combinations
  including the two.
  You must obey the GNU General Public License in all respects
  for all of the code used other than OpenSSL.  If you modify
  file(s) with this exception, you may extend this exception to your
  version of the file(s), but you are not obligated to do so.  If you
  do not wish to do so, delete this exception statement from your
  version.  If you delete this exception statement from all source
  files in the program, then also delete it here.
*/

#ifndef _JPIPE_H_
#define _JPIPE_H_

#ifndef OSIP_MONOTHREAD

/**
 * @file jpipe.h
 * @brief PPL Pipe Handling Routines
 */

/**
 * @defgroup JPIPE Pipe Handling
 * @ingroup PPL
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Structure for storing a pipe descriptor
 * @defvar jpipe_t
 */
typedef struct jpipe_t jpipe_t;

struct jpipe_t {
#if defined(CONFIG_QUEC_VOIP_SUPPORT)
  qosa_flag_t wake; /*!< 任务间合并唤醒事件；业务数据保存在独立 FIFO。 */
#else
  int pipes[2];
#endif /* CONFIG_QUEC_VOIP_SUPPORT */
};

/**
 * Get New pipe pair.
 */
jpipe_t *jpipe(void);

/**
 * Close pipe
 */
int jpipe_close(jpipe_t *apipe);

/**
 * Write in a pipe.
 */
int jpipe_write(jpipe_t *pipe, const void *buf, int count);

/**
 * Read in a pipe.
 */
int jpipe_read(jpipe_t *pipe, void *buf, int count);

/**
 * Get descriptor of reading pipe.
 */
int jpipe_get_read_descr(jpipe_t *pipe);

#if defined(CONFIG_QUEC_VOIP_SUPPORT)
/** @brief 等待并消费唤醒；对象由调用方持有，关闭前需停止所有使用者。
 * @param[in] jpipe_t *apipe 唤醒对象。
 * @param[in] unsigned int timeout_ms 等待毫秒数，0 表示非阻塞。
 * @return 1 收到通知，0 无通知，负值表示参数错误。
 */
int jpipe_wait(jpipe_t *apipe, unsigned int timeout_ms);
/** @brief 以最多 20 ms 的时间片等待网络，内部唤醒不占用 socket。
 * @param[in] jpipe_t *apipe 唤醒对象。
 * @param[in] int nfds 最大网络描述符加一，0 表示没有网络描述符。
 * @param[in,out] fd_set *readfds 可读集合。
 * @param[in,out] fd_set *writefds 可写集合。
 * @param[in,out] fd_set *exceptfds 异常集合。
 * @param[in] const struct timeval *timeout 上层等待预算，可为 NULL。
 * @return 网络 select 结果，内部唤醒或超时返回 0。
 */
int jpipe_select(jpipe_t *apipe, int nfds, fd_set *readfds, fd_set *writefds,
                 fd_set *exceptfds, const struct timeval *timeout);
#endif /* CONFIG_QUEC_VOIP_SUPPORT */


#ifdef __cplusplus
}
#endif
#endif
/** @} */
#endif
