#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#include "toyroutine.h"
#include "toyroutine_syscalls.h"

#include "toylogger.h"

void client_routine(struct toro *t, void *arg) {
  int fd = (int)(uintptr_t)arg;
  LOG_DEBUG("client_routine [%p] [%d]", t, fd);
  fcntl(fd, F_SETFL, O_NONBLOCK);
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
}

#define MAX_THREADS 15
struct tothr *thrs[MAX_THREADS + 1];
pthread_t threads[MAX_THREADS];
int thread_cnt = 0;

void *new_thread_client_entry(void *args) {
  struct tothr *thr = args;
  while (tothr_toro_count(thr) > 0) {
    tothr_sched(thr);
  }
  tothr_destroy(thr);
  return NULL;
}

void accept_routine(struct toro *t, void *arg) {
  int fd = (int)(uintptr_t)arg;
  LOG_DEBUG("accept_routine [%p][%d]", t, fd);
  fcntl(fd, F_SETFL, O_NONBLOCK);
  while (1) {

    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    int new_fd = co_accept(t, fd, (struct sockaddr *)&addr, &addrlen);

    if (new_fd < 0)
      continue;

    struct tothr *thr = thrs[thread_cnt];
    LOG_INFO("new connection %d, toro_cnt[%d]", new_fd, tothr_toro_count(thr));

    int new_thr = tothr_toro_count(thr) == 2;
    if (new_thr) {
      thr = tothr_create();
      LOG_DEBUG("toro_count 2, thread_cnt[%d]", thread_cnt);
      thrs[thread_cnt + 1] = thr;
    }

    struct toro *client_toro =
        toro_create(thr, client_routine, (void *)(uintptr_t)new_fd);
    if (new_thr) {
      pthread_create(&threads[thread_cnt], NULL, new_thread_client_entry, thr);
      pthread_detach(threads[thread_cnt]);
      ++thread_cnt;
    }
  }

  LOG_DEBUG("accept_routine going to die");
  toro_destroy(t);
}

void exit_func() {
  //
  (void)0;
}

int main(int argc, char **argv) {
  int port = 1025;
  if (argc > 1) {
    port = atoi(argv[1]);
  }
  atexit(exit_func);
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
  thrs[0] = thr;
  struct toro *t = toro_create(thr, accept_routine, (void *)(uintptr_t)fd);
  tothr_sched(thr);

  LOG_INFO("go to sleep");
  sleep(99999);
}