/*
 * Copyright (c) 2016-2020 Belledonne Communications SARL.
 *
 * This file is part of bctoolbox.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ortp_charconv.h"
#include "ortp_def.h"
#include "ortp_list.h"
#include "ortp_logging.h"
#include "ortp_port.h"
#include "ortp_vconnect.h"
#include "../src/utils.h"

#ifdef _MSC_VER
#ifndef access
#define access _access
#endif
#endif

#define bctbx_error(...)

#ifndef MIN
#define MIN(a, b) a <= b ? a : b
#endif

#ifndef NI_MAXHOST
#define NI_MAXHOST      48
#endif

#ifndef NI_MAXSERV
#define NI_MAXSERV      32
#endif

#ifndef NI_NUMERICHOST
#define NI_NUMERICHOST    0x0001    /* Don't try to look up hostname */
#endif

#ifndef NI_NUMERICSERV
#define NI_NUMERICSERV    0x0002  /* Don't convert port number to name */
#endif





char *bctbx_strdup_vprintf(const char *fmt, va_list ap) {
	/* Guess we need no more than 100 bytes. */
	int n, size = 200;
	char *p, *np;
#ifndef _WIN32
	va_list cap; /*copy of our argument list: a va_list cannot be re-used (SIGSEGV on linux 64 bits)*/
#endif
	if ((p = (char *)bctbx_malloc(size)) == NULL) return NULL;
	while (1) {
/* Try to print in the allocated space. */
#ifndef _WIN32
		va_copy(cap, ap);
		n = vsnprintf(p, size, fmt, cap);
		va_end(cap);
#else
		/*this works on 32 bits, luckily*/
		n = vsnprintf(p, size, fmt, ap);
#endif
		/* If that worked, return the string. */
		if (n > -1 && n < size) return p;
		// printf("Reallocing space.\n");
		/* Else try again with more space. */
		if (n > -1)                     /* glibc 2.1 */
			size = n + 1;               /* precisely what is needed */
		else /* glibc 2.0 */ size *= 2; /* twice the old size */
		if ((np = (char *)bctbx_realloc(p, size)) == NULL) {
			free(p);
			return NULL;
		} else {
			p = np;
		}
	}
}

char *bctbx_strdup_printf(const char *fmt, ...) {
	char *ret;
	va_list args;
	va_start(args, fmt);
	ret = bctbx_strdup_vprintf(fmt, args);
	va_end(args);
	return ret;
}

char *bctbx_strcat_vprintf(char *dst, const char *fmt, va_list ap) {
	char *ret;
	size_t dstlen, retlen;

	ret = bctbx_strdup_vprintf(fmt, ap);
	if (!dst) return ret;

	dstlen = strlen(dst);
	retlen = strlen(ret);

	if ((dst = bctbx_realloc(dst, dstlen + retlen + 1)) != NULL) {
		strcat(dst, ret);
		bctbx_free(ret);
		return dst;
	} else {
		bctbx_free(ret);
		return NULL;
	}
}

char *bctbx_strcat_printf(char *dst, const char *fmt, ...) {
	char *ret;
	va_list args;
	va_start(args, fmt);
	ret = bctbx_strcat_vprintf(dst, fmt, args);
	va_end(args);
	return ret;
}



/*
 * this method is an utility method that calls fnctl() on UNIX or
 * ioctlsocket on Win32.
 * int retrun the result of the system method
 */
int bctbx_socket_set_non_blocking(bctbx_socket_t sock) {
#if !defined(_WIN32) && !defined(_WIN32_WCE)
	return fcntl(sock, F_SETFL, O_NONBLOCK);
#else
	unsigned long nonBlock = 1;
	return ioctlsocket(sock, FIONBIO, &nonBlock);
#endif
}


#if 1

ssize_t bctbx_send(bctbx_socket_t socket, const void *buffer, size_t length, int flags) {
	return send(socket, buffer, length, flags);
}

ssize_t bctbx_sendto(bctbx_socket_t socket,
                     const void *message,
                     size_t length,
                     int flags,
                     const struct sockaddr *dest_addr,
                     socklen_t dest_len) {
	return sendto(socket, message, length, flags, dest_addr, dest_len);
}

ssize_t bctbx_recv(bctbx_socket_t socket, void *buffer, size_t length, int flags) {
	return recv(socket, buffer, length, flags);
}

ssize_t bctbx_recvfrom(
    bctbx_socket_t socket, void *buffer, size_t length, int flags, struct sockaddr *address, socklen_t *address_len) {
	return recvfrom(socket, buffer, length, flags, address, address_len);
}

ssize_t bctbx_read(int fd, void *buf, size_t nbytes) {
	return read(fd, buf, nbytes);
}

ssize_t bctbx_write(int fd, const void *buf, size_t nbytes) {
	return write(fd, buf, nbytes);
}

#endif

static char allocated_by_bctbx_magic[10] = "bctbx";

static struct addrinfo *_bctbx_alloc_addrinfo(int ai_family, int socktype, int proto) {
	struct addrinfo *ai = (struct addrinfo *)bctbx_malloc0(sizeof(struct addrinfo) + sizeof(struct sockaddr_storage));
	ai->ai_family = ai_family;
	ai->ai_socktype = socktype;
	ai->ai_protocol = proto;
	ai->ai_addrlen = AF_INET6 ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);
	ai->ai_addr = (struct sockaddr *)(((unsigned char *)ai) + sizeof(struct addrinfo));
	ai->ai_canonname = allocated_by_bctbx_magic; /*this is the way we will recognize our own allocated addrinfo
	                                                structures in bctbx_freeaddrinfo()*/
	return ai;
}

static struct addrinfo *convert_to_v4mapped(const struct addrinfo *ai) {
	struct addrinfo *res = NULL;
	const struct addrinfo *it;
	struct addrinfo *v4m = NULL;
	struct addrinfo *last = NULL;

	for (it = ai; it != NULL; it = it->ai_next) {
		struct sockaddr_in6 *sin6;
		struct sockaddr_in *sin;
		v4m = _bctbx_alloc_addrinfo(AF_INET6, it->ai_socktype, it->ai_protocol);
		v4m->ai_flags |= AI_V4MAPPED;
		sin6 = (struct sockaddr_in6 *)v4m->ai_addr;
		sin = (struct sockaddr_in *)it->ai_addr;
		sin6->sin6_family = AF_INET6;
		((uint8_t *)&sin6->sin6_addr)[10] = 0xff;
		((uint8_t *)&sin6->sin6_addr)[11] = 0xff;
		memcpy(((uint8_t *)&sin6->sin6_addr) + 12, &sin->sin_addr, 4);
		sin6->sin6_port = sin->sin_port;
		if (last) {
			last->ai_next = v4m;
		} else {
			res = v4m;
		}
		last = v4m;
	}
	return res;
}



int bctbx_getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res) {
	int result = getaddrinfo(node, service, hints, res);
#if __APPLE__
	if (*res && (*res)->ai_family == AF_INET6) {
		struct sockaddr_in6 *sockaddr = (struct sockaddr_in6 *)(*res)->ai_addr;
		if (sockaddr->sin6_port == 0 && service) {
			int possible_port = atoi(service);
			if (possible_port > 0 && possible_port <= 65535) {
				bctbx_message("Apple nat64 getaddrinfo bug, fixing port to [%i]", possible_port);
				sockaddr->sin6_port = htons(possible_port);
			}
		}
	}
#endif
	return result;
}

int bctbx_socket_close(bctbx_socket_t sock) {
	return close(sock);
}

int bctbx_getnameinfo(const struct sockaddr *addr,
                      socklen_t addrlen,
                      char *host,
                      socklen_t hostlen,
                      char *serv,
                      socklen_t servlen,
                      int flags) {


  struct sockaddr_in *fromsa = (struct sockaddr_in *) addr;
  char *tmp;
  
  tmp = inet_ntoa(fromsa->sin_addr);
  
  if (tmp == NULL) {
 //   OSIP_TRACE(osip_trace(__FILE__, __LINE__, OSIP_ERROR, NULL, "[eXosip] [getnameinfo] failure [?]\n"));
    snprintf(host, hostlen, "127.0.0.1");
    return -1;
  }
  
  snprintf(host, hostlen, "%s", tmp);
  return 0;
}

static void _bctbx_addrinfo_to_ip_address_error(int err, char *ip, size_t ip_size) {
	//bctbx_error("getnameinfo() error: %s", gai_strerror(err));
	//strncpy(ip, "<bug!!>", ip_size);
}
#define IN6_IS_ADDR_V4MAPPED(a)     ip6_addr_isipv4mappedipv6((ip6_addr_t*)(a))

struct addrinfo *bctbx_addrinfo_sort(struct addrinfo *ais) {
	bctbx_list_t *v6 = NULL;
	bctbx_list_t *v4_mapped = NULL;
	bctbx_list_t *v4 = NULL;
	bctbx_list_t *it = NULL;
	struct addrinfo *res0 = NULL;
	struct addrinfo *res = NULL;
	struct addrinfo *ai = NULL;

	// sort by type
	for (ai = ais; ai != NULL;) {
		struct addrinfo *next = ai->ai_next;
		struct sockaddr_in6 *sock_in6 = (struct sockaddr_in6 *)ai->ai_addr;
		if (ai->ai_family == AF_INET6) {
			if (IN6_IS_ADDR_V4MAPPED(&sock_in6->sin6_addr)) {
				v4_mapped = bctbx_list_prepend(v4_mapped, ai);
			} else {
				v6 = bctbx_list_prepend(v6, ai);
			}
		} else {
			v4 = bctbx_list_prepend(v4, ai);
		}

		ai->ai_next = NULL;
		ai = next;
	}
	v6 = bctbx_list_concat(v6, v4_mapped);
	v6 = bctbx_list_concat(v6, v4);

	for (it = v6; it != NULL; it = it->next) {
		if (res0 == NULL) {
			res0 = res = (struct addrinfo *)it->data;
		} else {
			res->ai_next = (struct addrinfo *)it->data;
			res = res->ai_next;
		}
	}
	if (res) res->ai_next = NULL;

	bctbx_list_free(v6);

	return res0;
}
int bctbx_addrinfo_to_ip_address(const struct addrinfo *ai, char *ip, size_t ip_size, int *port) {
	char serv[16];
	int err = bctbx_getnameinfo(ai->ai_addr, (socklen_t)ai->ai_addrlen, ip, (socklen_t)ip_size, serv,
	                            (socklen_t)sizeof(serv), NI_NUMERICHOST | NI_NUMERICSERV);
	if (err != 0) _bctbx_addrinfo_to_ip_address_error(err, ip, ip_size);
	if (port) *port = atoi(serv);
	return 0;
}

int bctbx_addrinfo_to_printable_ip_address(const struct addrinfo *ai, char *printable_ip, size_t printable_ip_size) {
	char ip[64];
	char serv[16];
	int err = bctbx_getnameinfo(ai->ai_addr, (socklen_t)ai->ai_addrlen, ip, sizeof(ip), serv, sizeof(serv),
	                            NI_NUMERICHOST | NI_NUMERICSERV);
	if (err != 0) _bctbx_addrinfo_to_ip_address_error(err, ip, sizeof(ip));
	if (ai->ai_family == AF_INET) snprintf(printable_ip, printable_ip_size, "%s:%s", ip, serv);
	else if (ai->ai_family == AF_INET6) snprintf(printable_ip, printable_ip_size, "[%s]:%s", ip, serv);
	return 0;
}

int bctbx_sockaddr_to_ip_address(const struct sockaddr *sa, socklen_t salen, char *ip, size_t ip_size, int *port) {
	struct addrinfo ai = {0};
	ai.ai_addr = (struct sockaddr *)sa;
	ai.ai_addrlen = salen;
	ai.ai_family = sa->sa_family;
	return bctbx_addrinfo_to_ip_address(&ai, ip, ip_size, port);
}

int bctbx_sockaddr_to_printable_ip_address(struct sockaddr *sa,
                                           socklen_t salen,
                                           char *printable_ip,
                                           size_t printable_ip_size) {
	if ((sa->sa_family == 0) || (salen == 0)) {
		snprintf(printable_ip, printable_ip_size, "no-addr");
		return 0;
	} else {
		struct addrinfo ai = {0};
		ai.ai_addr = sa;
		ai.ai_addrlen = salen;
		ai.ai_family = sa->sa_family;
		return bctbx_addrinfo_to_printable_ip_address(&ai, printable_ip, printable_ip_size);
	}
}


static struct addrinfo *
_bctbx_name_to_addrinfo(int family, int socktype, const char *ipaddress, int port, int numeric_only) {
	struct addrinfo *res = NULL;
	struct addrinfo hints = {0};
	char serv[10];
	int err;

	snprintf(serv, sizeof(serv), "%i", port);
	hints.ai_family = family;
	if (numeric_only) hints.ai_flags = AI_NUMERICSERV | AI_NUMERICHOST;
	hints.ai_socktype = socktype;

	if (family == AF_INET6) {
		hints.ai_flags |= AI_V4MAPPED;
		hints.ai_flags |= AI_ALL;
	}
	err = bctbx_getaddrinfo(ipaddress, serv, &hints, &res);
    CPUartLogPrintf("%s,%d,err=%p,res=%p",__FUNCTION__,__LINE__,err,res);
	if (err != 0) {
		if (!numeric_only || err != EAI_NONAME)
			bctbx_error("%s(%s): getaddrinfo failed: %s", __FUNCTION__, ipaddress, gai_strerror(err));
		return NULL;
	}
	// sort result
	if (res) res = bctbx_addrinfo_sort(res);

	return res;
}

struct addrinfo *bctbx_name_to_addrinfo(int family, int socktype, const char *name, int port) {
	struct addrinfo *res = NULL;
	if (res == NULL) {
		res = _bctbx_name_to_addrinfo(family, socktype, name, port, FALSE);
	}
	return res;
}

struct addrinfo *bctbx_ip_address_to_addrinfo(int family, int socktype, const char *name, int port) {
	struct addrinfo *res = _bctbx_name_to_addrinfo(family, socktype, name, port, TRUE);
#if __APPLE__
	/*required for nat64 on apple platform*/
	if (res) {
		/*fine, we are sure that name was an ip address, give a chance to get its nat64 form*/
		bctbx_freeaddrinfo(res);
		res = bctbx_name_to_addrinfo(family, SOCK_STREAM, name, port);
	}
#endif
	return res;
}

#ifndef IN6_GET_ADDR_V4MAPPED
#define IN6_GET_ADDR_V4MAPPED(sin6_addr) *(unsigned int *)((unsigned char *)(sin6_addr) + 12)
#endif

void bctbx_sockaddr_remove_v4_mapping(const struct sockaddr *v6, struct sockaddr *result, socklen_t *result_len) {
	if (v6->sa_family == AF_INET6) {
		struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)v6;

		if (IN6_IS_ADDR_V4MAPPED(&in6->sin6_addr)) {
			struct sockaddr_in *in = (struct sockaddr_in *)result;
			result->sa_family = AF_INET;
			in->sin_addr.s_addr = IN6_GET_ADDR_V4MAPPED(&in6->sin6_addr);
			in->sin_port = in6->sin6_port;
			*result_len = sizeof(struct sockaddr_in);
		} else {
			if (v6 != result) memcpy(result, v6, sizeof(struct sockaddr_in6));
			*result_len = sizeof(struct sockaddr_in6);
		}
	} else {
		*result_len = sizeof(struct sockaddr_in);
		if (v6 != result) memcpy(result, v6, sizeof(struct sockaddr_in));
	}
}

void bctbx_sockaddr_remove_nat64_mapping(const struct sockaddr *v6, struct sockaddr *result, socklen_t *result_len) {
	if (v6->sa_family == AF_INET6) {
		struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)v6;

		if (memcmp(in6->sin6_addr.s6_addr,"\x00\x64\xff\x9b",4)==0) {
			struct sockaddr_in *in = (struct sockaddr_in *)result;
			result->sa_family = AF_INET;
			in->sin_addr.s_addr = IN6_GET_ADDR_V4MAPPED(&in6->sin6_addr);
			in->sin_port = in6->sin6_port;
			*result_len = sizeof(struct sockaddr_in);
			return;
		}
	}
	/* it was not a NAT64 address: just copy the source address as is, whatever it is.*/
	*result_len = v6->sa_family == AF_INET6 ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);
	if (v6 != result) memcpy(result, v6, *result_len);
}

void bctbx_sockaddr_ipv6_to_ipv4(const struct sockaddr *v6, struct sockaddr *result, socklen_t *result_len) {
	bctbx_sockaddr_remove_v4_mapping(v6, result, result_len);
}

void bctbx_sockaddr_ipv4_to_ipv6(const struct sockaddr *v4, struct sockaddr *result, socklen_t *result_len) {
	if (v4->sa_family == AF_INET) {
		struct addrinfo *v4m;
		struct addrinfo ai = {0};
		struct sockaddr_in6 *v6 = (struct sockaddr_in6 *)result;
		ai.ai_addr = (struct sockaddr *)v4;
		ai.ai_addrlen = sizeof(struct sockaddr_in);
		ai.ai_family = v4->sa_family;
		v4m = convert_to_v4mapped(&ai);
		*result_len = sizeof(struct sockaddr_in6);
		memcpy(v6, v4m->ai_addr, *result_len);
		bctbx_freeaddrinfo(v4m);
	}
}

char *bctbx_replace(char *str, char c, char n) {
	char *s = str;

	while (*s) {
		if (*s == c) {
			*s = n;
		}
		++s;
	}
	return str;
}

char *bctbx_concat(const char *str, ...) {
	va_list ap;
	size_t allocated = 100;
	char *result = (char *)malloc(allocated);

	if (result != NULL) {
		char *newp;
		char *wp;
		const char *s;

		va_start(ap, str);

		wp = result;
		for (s = str; s != NULL; s = va_arg(ap, const char *)) {
			size_t len = strlen(s);

			/* Resize the allocated memory if necessary.  */
			if (wp + len + 1 > result + allocated) {
				intptr_t current_offset = wp - result;
				allocated = (allocated + len) * 2;
				newp = (char *)realloc(result, allocated);
				if (newp == NULL) {
					free(result);
					return NULL;
				}
				wp = newp + current_offset;
				result = newp;
			}
			memcpy(wp, s, len);
			wp += len;
		}

		/* Terminate the result string.  */
		*wp++ = '\0';

		/* Resize memory to the optimal size.  */
		newp = realloc(result, wp - result);
		if (newp != NULL) result = newp;

		va_end(ap);
	}

	return result;
}

bool_t bctbx_sockaddr_equals(const struct sockaddr *sa, const struct sockaddr *sb) {

	if (sa->sa_family != sb->sa_family) return FALSE;

	if (sa->sa_family == AF_INET) {
		if ((((struct sockaddr_in *)sa)->sin_addr.s_addr != ((struct sockaddr_in *)sb)->sin_addr.s_addr ||
		     ((struct sockaddr_in *)sa)->sin_port != ((struct sockaddr_in *)sb)->sin_port))
			return FALSE;
	} else if (sa->sa_family == AF_INET6) {
		if (memcmp(&((struct sockaddr_in6 *)sa)->sin6_addr, &((struct sockaddr_in6 *)sb)->sin6_addr,
		           sizeof(struct in6_addr)) != 0 ||
		    ((struct sockaddr_in6 *)sa)->sin6_port != ((struct sockaddr_in6 *)sb)->sin6_port)
			return FALSE;
	} else {
		bctbx_warning("Cannot compare family type [%d]", sa->sa_family);
		return FALSE;
	}
	return TRUE;
}

static const char *ai_family_to_string(int af) {
	switch (af) {
		case AF_INET:
			return "AF_INET";
		case AF_INET6:
			return "AF_INET6";
		case AF_UNSPEC:
			return "AF_UNSPEC";
		default:
			return "invalid address family";
	}
}

static int get_local_ip_for_with_connect(int type, const char *dest, int port, char *result, size_t result_len) {
	int err, tmp;
	struct addrinfo hints;
	struct addrinfo *res = NULL;
	struct sockaddr_storage addr;
	struct sockaddr *p_addr = (struct sockaddr *)&addr;
	bctbx_socket_t sock;
	socklen_t s;
	char port_str[6] = {0};

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = type;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;

	snprintf(port_str, sizeof(port_str), "%i", port);

	err = bctbx_getaddrinfo(dest, port_str, &hints, &res);
	if (err != 0) {
		bctbx_error("get_local_ip_for_with_connect(): getaddrinfo() error for %s: %s", dest, gai_strerror(err));
		return -1;
	}
	if (res == NULL) {
		bctbx_error("bug: getaddrinfo returned nothing.");
		return -1;
	}
	sock = socket(res->ai_family, SOCK_DGRAM, 0);
	if (sock == (bctbx_socket_t)-1) {
		bctbx_error("get_local_ip_for_with_connect() could not create [%s] socket: %s",
		            ai_family_to_string(res->ai_family), getSocketError());
		return -1;
	}
	tmp = 1;
	err = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (SOCKET_OPTION_VALUE)&tmp, sizeof(int));
	if (err == -1) bctbx_warning("Error in setsockopt: %s", getSocketError());
	err = connect(sock, res->ai_addr, (int)res->ai_addrlen);
	if (err == -1) {
		/* The network isn't reachable. We don't display the error as it is the case that we want to check in normal
		 * operation. */
		if (getSocketErrorCode() != BCTBX_ENETUNREACH && getSocketErrorCode() != BCTBX_EHOSTUNREACH &&
		    getSocketErrorCode() != BCTBX_EPROTOTYPE)
			bctbx_error("Error in connect: %s", getSocketError());
		bctbx_freeaddrinfo(res);
		bctbx_socket_close(sock);
		return -1;
	}
	bctbx_freeaddrinfo(res);
	res = NULL;
	s = sizeof(addr);
	err = getsockname(sock, (struct sockaddr *)&addr, &s);
	if (err != 0) {
		bctbx_error("get_local_ip_for_with_connect(): Error in getsockname: %s", getSocketError());
		bctbx_socket_close(sock);
		return -1;
	}
	if (p_addr->sa_family == AF_INET) {
		struct sockaddr_in *p_sin = (struct sockaddr_in *)p_addr;
		if (p_sin->sin_addr.s_addr == 0) {
			bctbx_error("get_local_ip_for_with_connect(): getsockname() returned INADDR_ANY.");
			bctbx_socket_close(sock);
			return -1;
		}
	}
	bctbx_sockaddr_remove_v4_mapping((struct sockaddr *)&addr, (struct sockaddr *)&addr, &s);
	err = bctbx_getnameinfo((struct sockaddr *)&addr, s, result, (socklen_t)result_len, NULL, 0, NI_NUMERICHOST);
	if (err != 0) bctbx_error("getnameinfo error: %s", gai_strerror(err));
	/* Avoid ipv6 link-local addresses */
	if ((p_addr->sa_family == AF_INET6) && (strchr(result, '%') != NULL)) {
		strcpy(result, "::1");
		bctbx_socket_close(sock);
		return -1;
	}
	bctbx_socket_close(sock);
	return 0;
}

int bctbx_get_local_ip_for(int type, const char *dest, int port, char *result, size_t result_len) {
	strncpy(result, (type == AF_INET) ? "127.0.0.1" : "::1", result_len);

	if (dest == NULL) {
		if (type == AF_INET) dest = "87.98.157.38"; /* A public IP address */
		else dest = "2a00:1450:8002::68";
	}
	if (port == 0) port = 5060;
	return get_local_ip_for_with_connect(type, dest, port, result, result_len);
}

void _bctbx_freeaddrinfo(struct addrinfo *res) {
	struct addrinfo *it, *next_it;
	for (it = res; it != NULL; it = next_it) {
		next_it = it->ai_next;
		bctbx_free(it);
	}
}

void bctbx_freeaddrinfo(struct addrinfo *res) {
	struct addrinfo *it;
	struct addrinfo *previt = NULL;
	struct addrinfo *beginit = res;
	bool_t looking_for_allocated_by_bctbx = (res->ai_canonname == allocated_by_bctbx_magic) ? TRUE : FALSE;

	for (it = res; it != NULL; it = it->ai_next) {
		if ((looking_for_allocated_by_bctbx == TRUE) && (it->ai_canonname != allocated_by_bctbx_magic)) {
			if (previt) {
				previt->ai_next = NULL;
				_bctbx_freeaddrinfo(beginit);
				looking_for_allocated_by_bctbx = FALSE;
				beginit = it;
			}
		} else if ((looking_for_allocated_by_bctbx == FALSE) && (it->ai_canonname == allocated_by_bctbx_magic)) {
			if (previt) {
				previt->ai_next = NULL;
				freeaddrinfo(beginit);
				looking_for_allocated_by_bctbx = TRUE;
				beginit = it;
			}
		}
		previt = it;
	}
	if (looking_for_allocated_by_bctbx == TRUE) _bctbx_freeaddrinfo(beginit);
	else freeaddrinfo(beginit);
}

