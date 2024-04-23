#include <arpa/inet.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "toylogger.h"
#include "toyroutine.h"
#include "toyroutine_syscalls.h"

#ifndef CLIENT_THREAD
#define CLIENT_THREAD 0
#endif
struct tothr *thrs[CLIENT_THREAD + 1];
#if CLIENT_THREAD > 0
pthread_t threads[CLIENT_THREAD];
int evfds[CLIENT_THREAD];
#endif

void client_routine(struct tothr *thr, struct toro *t, void *arg) {
  int fd = (int)(uintptr_t)arg;
  LOG_DEBUG("client_routine [%p][%p] [%d]", thr, t, fd);
  struct sockaddr_in addr;
  socklen_t addrlen = sizeof(addr);
  getpeername(fd, (struct sockaddr *)&addr, &addrlen);
  LOG_INFO("client_routine [%d] connected to [%s:%d]", fd,
           inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));

  int flags = fcntl(fd, F_GETFL);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  char buf[1024];
  while (1) {
    ssize_t n = co_read(t, fd, buf, sizeof(buf));
    if (n >= 0)
      buf[n] = '\0';
    LOG_INFO("%d reads[%lu][%s]", fd, n, buf);
    if (n <= 0) {
      close(fd);
      break;
    }
  }
  toro_destroy(t);
  tothr_sched(thr);
}

#if CLIENT_THREAD > 0
void read_client_fd_routine(struct tothr *thr, struct toro *t, void *arg) {
  int idx = (int)(uintptr_t)arg;
  int evfd = evfds[idx];
  LOG_DEBUG("read_client_fd_routine [%p][%p] idx[%d] evfd[%d]", thr, t, idx,
            evfd);
  uint64_t val;
  while (1) {
    ssize_t n = co_read(t, evfd, &val, sizeof(val));
    if (n < 0) {
      LOG_ERROR("read_client_fd_routine read error");
      break;
    }
    LOG_INFO("read_client_fd_routine read [%lu]", val);
    struct toro *client_toro =
        toro_create(thr, client_routine, (void *)(uintptr_t)val);
    (void)client_toro;
  }

  toro_destroy(t);
  tothr_sched(thr);
}

void *new_thread_client_entry(void *args) {
  int idx = (int)(uintptr_t)args;
  struct tothr *thr = tothr_create();
  thrs[idx + 1] = thr;
  struct toro *t =
      toro_create(thr, read_client_fd_routine, (void *)(uintptr_t)idx);
  (void)t;
  tothr_sched(thr);
  return NULL;
}
#endif

void accept_routine(struct tothr *thr, struct toro *t, void *arg) {
  int fd = (int)(uintptr_t)arg;
  LOG_DEBUG("accept_routine thr[%p][%p][%d]", thr, t, fd);
  fcntl(fd, F_SETFL, O_NONBLOCK);
#if CLIENT_THREAD > 0
  for (int i = 0; i < CLIENT_THREAD; ++i) {
    evfds[i] = eventfd(0, EFD_NONBLOCK);
    if (evfds[i] < 0) {
      perror("eventfd");
      return;
    }
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&threads[i], &attr, new_thread_client_entry,
                   (void *)(uintptr_t)i);
  }
  int idx = 0;
#endif

  while (1) {
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    int new_fd = co_accept(t, fd, (struct sockaddr *)&addr, &addrlen);

    if (new_fd < 0)
      continue;
#if CLIENT_THREAD > 0
    uint64_t val = new_fd;

    co_write(t, evfds[idx], &val, sizeof(val));
    idx = (idx + 1) % CLIENT_THREAD;
#else
    struct toro *client_toro =
        toro_create(thr, client_routine, (void *)(uintptr_t)new_fd);
#endif
  }

  LOG_DEBUG("accept_routine going to die");
  toro_destroy(t);
  tothr_sched(thr);
}

int main(int argc, char **argv) {
  int port = 1025;
  if (argc > 1) {
    port = atoi(argv[1]);
  }
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    perror("socket");
    return 1;
  }
  int reuse = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
    perror("setsockopt");
    return 1;
  }
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0) {
    perror("setsockopt");
    return 1;
  }
  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);
  if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind");
    return 1;
  }
  if (listen(fd, 10) < 0) {
    perror("listen");
    return 1;
  }
  struct tothr *thr = tothr_create();
#ifdef CLIENT_THREAD
  thrs[0] = thr;
#endif
  struct toro *t = toro_create(thr, accept_routine, (void *)(uintptr_t)fd);
  (void)t;
  tothr_sched(thr);
  return 0;
}