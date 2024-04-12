#pragma once
#ifndef __TOYROUTINE_SYSCALLS_H__
#define __TOYROUTINE_SYSCALLS_H__

#include <sys/types.h>
#include <sys/socket.h>

struct toro;

int co_accept(struct toro*, int fd, struct sockaddr *addr, socklen_t *len);
ssize_t co_read(struct toro*, int , void *, size_t);
ssize_t co_write(struct toro*, int , const void *, size_t);


#endif 