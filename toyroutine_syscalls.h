#pragma once
#ifndef __TOYROUTINE_SYSCALLS_H__
#define __TOYROUTINE_SYSCALLS_H__

#include <sys/socket.h>
#include <sys/types.h>

struct toro;

int co_accept(struct toro *, int, struct sockaddr *, socklen_t *);
ssize_t co_read(struct toro *, int, void *, size_t);
ssize_t co_write(struct toro *, int, const void *, size_t);
int co_connect(struct toro *, int, const struct sockaddr *, socklen_t);
ssize_t co_sendto(struct toro *, int, const void *, size_t, int,
                  const struct sockaddr *, socklen_t);
ssize_t co_recvfrom(struct toro *, int, void *, size_t, int, struct sockaddr *,
                    socklen_t *);
ssize_t co_send(struct toro *, int, const void *, size_t, int);
ssize_t co_recv(struct toro *, int, void *, size_t, int);

#endif