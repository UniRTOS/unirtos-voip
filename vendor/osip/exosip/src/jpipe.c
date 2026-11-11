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

#include "eXosip2.h"

#ifndef OSIP_MONOTHREAD

#include "jpipe.h"

#ifdef HAVE_UNISTD_H
#include "unistd.h"
#endif

#if defined(CONFIG_QUEC_VOIP_SUPPORT)

/** 唤醒事件位；多个生产者的重复通知合并为一个待处理提示。 */
#define JPIPE_WAKE_BIT (1U)
/** SIP 网络 select 的单次等待上限，单位微秒。 */
#define JPIPE_POLL_US (20000L)

/** @brief 创建不依赖 SIM、PDP 或 TCP 回环的任务唤醒对象。
 * @return 成功返回对象，内存或事件标志创建失败返回 NULL。
 * @note 创建者持有所有权；关闭前必须停止全部访问任务。
 */
jpipe_t *jpipe(void)
{
    jpipe_t *apipe = osip_malloc(sizeof(*apipe));
    int rc = 0;
    if (apipe == NULL)
    {
        return NULL;
    }
    qosa_memset(apipe, 0, sizeof(*apipe));
    rc = qosa_flag_create(&apipe->wake);
    if (rc != 0)
    {
        osip_free(apipe);
        return NULL;
    }
    return apipe;
}

/** @brief 释放已停止使用的任务唤醒对象。
 * @param[in] jpipe_t *apipe 待释放对象。
 * @return 0 成功；参数或事件对象释放失败返回 oSIP 错误。
 */
int jpipe_close(jpipe_t *apipe)
{
    int rc = 0;
    if (apipe == NULL)
    {
        return OSIP_BADPARAMETER;
    }
    rc = qosa_flag_delete(apipe->wake);
    if (rc != 0)
    {
        return OSIP_UNDEFINED_ERROR;
    }
    osip_free(apipe);
    return OSIP_SUCCESS;
}

/** @brief 合并唤醒通知；实际 SIP 事件仍由原 FIFO 保存。
 * @param[in] jpipe_t *apipe 有效唤醒对象。
 * @param[in] const void *buf 非空提示数据；不保留或复制其内容。
 * @param[in] int count 提示长度。
 * @return 成功返回 count，参数或设置失败返回 oSIP 错误。
 * @note 仅任务上下文调用，不将此接口用作普通字节管道。
 */
int jpipe_write(jpipe_t *apipe, const void *buf, int count)
{
    int rc = 0;
    if (apipe == NULL || buf == NULL || count <= 0)
    {
        return OSIP_BADPARAMETER;
    }
    rc = qosa_flag_set(apipe->wake, JPIPE_WAKE_BIT);
    return rc == 0 ? count : OSIP_UNDEFINED_ERROR;
}

/** @brief 等待并消费一次合并唤醒，不丢弃 FIFO 中的业务事件。
 * @param[in] jpipe_t *apipe 有效唤醒对象。
 * @param[in] unsigned int timeout_ms 等待时间，单位毫秒，0 表示不等待。
 * @return 1 表示收到通知，0 表示未收到，负值表示参数错误。
 */
int jpipe_wait(jpipe_t *apipe, unsigned int timeout_ms)
{
    qosa_uint32_t flags = 0;
    if (apipe == NULL)
    {
        return OSIP_BADPARAMETER;
    }
    flags = qosa_flag_wait(apipe->wake, JPIPE_WAKE_BIT,
                           QOSA_FLAG_CLEAR_ENABLE, QOSA_FLAG_WAIT_ANY, timeout_ms);
    return flags == JPIPE_WAKE_BIT ? 1 : 0;
}

/** @brief 非阻塞消费一次唤醒提示。
 * @param[in] jpipe_t *apipe 有效唤醒对象。
 * @param[out] void *buf 至少一个字节的提示缓冲区。
 * @param[in] int count 缓冲区字节容量。
 * @return 1 表示读出提示，0 表示没有提示，负值为参数错误。
 */
int jpipe_read(jpipe_t *apipe, void *buf, int count)
{
    int ready = 0;
    if (apipe == NULL || buf == NULL || count <= 0)
    {
        return OSIP_BADPARAMETER;
    }
    ready = jpipe_wait(apipe, 0U);
    if (ready == 1)
    {
        ((char *)buf)[0] = 'w';
    }
    return ready;
}

/** @brief 返回无效描述符；RTOS 唤醒对象不能加入 socket fd_set。
 * @param[in] jpipe_t *apipe 唤醒对象。
 * @return 固定为 -1；调用方使用 jpipe_wait 或 jpipe_select。
 */
int jpipe_get_read_descr(jpipe_t *apipe)
{
    (void)apipe;
    return -1;
}

/** @brief 有界等待网络描述符，兼顾不属于 socket 的内部唤醒。
 * @param[in] jpipe_t *apipe 内部唤醒对象。
 * @param[in] int nfds 最大网络描述符加一；0 表示没有网络描述符。
 * @param[in,out] fd_set *readfds 可读描述符集合。
 * @param[in,out] fd_set *writefds 可写描述符集合。
 * @param[in,out] fd_set *exceptfds 异常描述符集合。
 * @param[in] const struct timeval *timeout 上层等待预算，NULL 表示不限时。
 * @return 网络 select 的结果；纯内部唤醒或超时返回 0。
 * @note 每次最多等待 20 ms；无 socket 时由 RTOS 等待，避免空 select 忙转。
 */
int jpipe_select(jpipe_t *apipe, int nfds, fd_set *readfds, fd_set *writefds,
                 fd_set *exceptfds, const struct timeval *timeout)
{
    struct timeval slice = {0, JPIPE_POLL_US};
    int ready = 0;
    if (apipe == NULL || nfds < 0 ||
        (timeout != NULL && (timeout->tv_sec < 0 || timeout->tv_usec < 0)))
    {
        return -1;
    }
    if (timeout != NULL && timeout->tv_sec == 0 && timeout->tv_usec < slice.tv_usec)
    {
        slice.tv_usec = timeout->tv_usec;
    }
    ready = jpipe_wait(apipe, 0U);
    if (ready == 1)
    {
        slice.tv_usec = 0;
    }
    if (nfds == 0)
    {
        if (slice.tv_usec != 0)
        {
            (void)jpipe_wait(apipe, (unsigned int)((slice.tv_usec + 999L) / 1000L));
        }
        return 0;
    }
    return select(nfds, readfds, writefds, exceptfds, &slice);
}

#else /* CONFIG_QUEC_VOIP_SUPPORT */

#if 0//!defined(WIN32) && !defined(__arc__) && !defined(__HELIOS_OS__)

#include "fcntl.h"

jpipe_t *jpipe() {
  jpipe_t *my_pipe = (jpipe_t *) osip_malloc(sizeof(jpipe_t));

  if (my_pipe == NULL)
    return NULL;

  if (0 != pipe(my_pipe->pipes)) {
    osip_free(my_pipe);
    return NULL;
  }

  if (fcntl(my_pipe->pipes[1], F_SETFL, O_NONBLOCK) == -1) {
    /* failed for some reason... */
    OSIP_TRACE(osip_trace(__FILE__, __LINE__, OSIP_ERROR, NULL, "[eXosip] cannot set O_NONBLOCK to the pipe[1]\n"));
  }

  return my_pipe;
}

int jpipe_close(jpipe_t *apipe) {
  if (apipe == NULL)
    return OSIP_BADPARAMETER;

  _eXosip_closesocket(apipe->pipes[0]);
  _eXosip_closesocket(apipe->pipes[1]);
  osip_free(apipe);
  return OSIP_SUCCESS;
}

/**
 * Write in a pipe.
 */
int jpipe_write(jpipe_t *apipe, const void *buf, int count) {
  if (apipe == NULL)
    return OSIP_BADPARAMETER;

  return (int) write(apipe->pipes[1], buf, count);
}

/**
 * Read in a pipe.
 */
int jpipe_read(jpipe_t *apipe, void *buf, int count) {
  if (apipe == NULL)
    return OSIP_BADPARAMETER;

  return (int) read(apipe->pipes[0], buf, count);
}

/**
 * Get descriptor of reading pipe.
 */
int jpipe_get_read_descr(jpipe_t *apipe) {
  if (apipe == NULL)
    return OSIP_BADPARAMETER;

  return apipe->pipes[0];
}

#else

int setNonBlocking(SOCKET_TYPE fd) {
  int flags;

  /* If they have O_NONBLOCK, use the Posix way to do it */
#if defined(O_NONBLOCK)

  /* Fixme: O_NONBLOCK is defined but broken on SunOS 4.1.x and AIX 3.2.5. */
  if (-1 == (flags = fcntl(fd, F_GETFL, 0)))
    flags = 0;

  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#else
  /* Otherwise, use the old way of doing it */
  flags = 1;
  return ioctlsocket(fd, FIONBIO, &flags);
#endif
}

jpipe_t *jpipe() {
  int s = 0;
  int timeout = 0;
  struct sockaddr_in raddr;
  int j;
  socklen_t len;

  jpipe_t *my_pipe = (jpipe_t *) osip_malloc(sizeof(jpipe_t));

  if (my_pipe == NULL)
    return NULL;

  s = (int) socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

  if (s < 0) {
    osip_free(my_pipe);
    return NULL;
  }

  my_pipe->pipes[1] = (int) socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

  if (my_pipe->pipes[1] < 0) {
    _eXosip_closesocket(s);
    osip_free(my_pipe);
    return NULL;
  }
#if defined(CONFIG_QUEC_VOIP_SUPPORT)
  raddr.sin_addr.s_addr = usr_inet_addr("127.0.0.1");
#else
  raddr.sin_addr.s_addr = inet_addr("127.0.0.1");
#endif
  raddr.sin_family = AF_INET;
  raddr.sin_port = 0;

  if (bind(s, (struct sockaddr *) &raddr, sizeof(raddr)) < 0) {
    OSIP_TRACE(osip_trace(__FILE__, __LINE__, OSIP_ERROR, NULL, "[eXosip] failed to bind a local socket (port=0), aborting\n"));
    _eXosip_closesocket(s);
    _eXosip_closesocket(my_pipe->pipes[1]);
    osip_free(my_pipe);
    return NULL;
  }

  /* get port */
  len = sizeof(raddr);
  j = getsockname(s, (struct sockaddr *) &raddr, &len);

  if (j != 0) {
    OSIP_TRACE(osip_trace(__FILE__, __LINE__, OSIP_ERROR, NULL, "[eXosip] failed to getsockname on a local socket, aborting\n"));
    _eXosip_closesocket(s);
    _eXosip_closesocket(my_pipe->pipes[1]);
    osip_free(my_pipe);
    return NULL;
  }

  j = listen(s, 1);

  if (j != 0) {
    OSIP_TRACE(osip_trace(__FILE__, __LINE__, OSIP_ERROR, NULL, "[eXosip] failed to listen on a local socket, aborting\n"));
    _eXosip_closesocket(s);
    _eXosip_closesocket(my_pipe->pipes[1]);
    osip_free(my_pipe);
    return NULL;
  }

  j = setsockopt(my_pipe->pipes[1], SOL_SOCKET, SO_RCVTIMEO, (const char *) &timeout, sizeof(timeout));
#if defined(__arc__)

  if (j != 0) {
    /* failed for some reason... */
    OSIP_TRACE(osip_trace(__FILE__, __LINE__, OSIP_ERROR, NULL, "[eXosip] cannot set O_NONBLOCK to the file desciptor\n"));
    _eXosip_closesocket(s);
    _eXosip_closesocket(my_pipe->pipes[1]);
    osip_free(my_pipe);
    return NULL;
  }

#elif !defined(_WIN32_WCE)

  if (j != NO_ERROR) {
    /* failed for some reason... */
    OSIP_TRACE(osip_trace(__FILE__, __LINE__, OSIP_ERROR, NULL, "[eXosip] cannot set O_NONBLOCK to the file desciptor\n"));
    _eXosip_closesocket(s);
    _eXosip_closesocket(my_pipe->pipes[1]);
    osip_free(my_pipe);
    return NULL;
  }

#endif

  connect(my_pipe->pipes[1], (struct sockaddr *) &raddr, sizeof(raddr));

  my_pipe->pipes[0] = (int) accept(s, NULL, NULL);

  if (my_pipe->pipes[0] < 0) {
    OSIP_TRACE(osip_trace(__FILE__, __LINE__, OSIP_ERROR, NULL, "[eXosip] failed to call accept\n"));
    _eXosip_closesocket(s);
    _eXosip_closesocket(my_pipe->pipes[1]);
    osip_free(my_pipe);
    return NULL;
  }

  /* set the socket to non-blocking to avoid a deadly embrace problem. */
  setNonBlocking(my_pipe->pipes[1]);

  _eXosip_closesocket(s);

  return my_pipe;
}

int jpipe_close(jpipe_t *apipe) {
  if (apipe == NULL)
    return OSIP_BADPARAMETER;

  _eXosip_closesocket(apipe->pipes[0]);
  _eXosip_closesocket(apipe->pipes[1]);
  osip_free(apipe);
  return OSIP_SUCCESS;
}

/**
 * Write in a pipe.
 */
int jpipe_write(jpipe_t *apipe, const void *buf, int count) {
  if (apipe == NULL)
    return OSIP_BADPARAMETER;
  OSIP_TRACE(osip_trace(__FUNCTION__, __LINE__, OSIP_INFO3, NULL, "S=%d,data=%s,len=%d", apipe->pipes[1],buf,count));
  return send(apipe->pipes[1], buf, count, 0);
}

/**
 * Read in a pipe.
 */
int jpipe_read(jpipe_t *apipe, void *buf, int count) {
  if (apipe == NULL)
    return OSIP_BADPARAMETER;
  OSIP_TRACE(osip_trace(__FUNCTION__, __LINE__, OSIP_INFO3, NULL, "S=%d,data=%s,len=%d", apipe->pipes[0],buf,count));
  return recv(apipe->pipes[0], buf, count, 0 /* MSG_DONTWAIT */); /* BUG?? */
}

/**
 * Get descriptor of reading pipe.
 */
int jpipe_get_read_descr(jpipe_t *apipe) {
  if (apipe == NULL)
    return OSIP_BADPARAMETER;

  return (int) apipe->pipes[0]; /* on windows, this is a conversion from SOCKET to int... doesn't seems to be an issue */
}

#endif

#endif /* CONFIG_QUEC_VOIP_SUPPORT */

#endif
