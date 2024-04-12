#include <fcntl.h>
#include <netinet/in.h>
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
    LOG_DEBUG("read %ld bytes", n);
    if (n <= 0) {
      close(fd);
      break;
    }
    LOG_INFO("%d reads[%lu][%s]", fd, n, buf);
  }
}

struct tothr *thr;
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
    LOG_INFO("new connection %d", new_fd);

    struct toro *client_toro = toro_create(thr);
    toro_queue(client_toro, client_routine, (void *)(uintptr_t)new_fd);
  }
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
  thr = tothr_create();
  struct toro *t = toro_create(thr);
  toro_queue(t, accept_routine, (void *)(uintptr_t)fd);
  tothr_sched(thr);
  sleep(99999);
}