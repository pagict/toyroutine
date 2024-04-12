#include "toyroutine.h"
#include "toylogger.h"
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ucontext.h>
#include <ucontext.h>
#include <unistd.h>

struct toro {
  ucontext_t *ctx;
  int fd;

  struct tothr *thr;
};

#define MAX_TOROS 1024
struct tothr {
  int ep_fd;
  //   int cur_ctx;

  struct toro *waiting_toro[MAX_TOROS];
  int rr_idx;
  int toro_count;
  struct toro *toros[MAX_TOROS];
};

struct tothr *tothr_create() {
  struct tothr *thr = malloc(sizeof(struct tothr));
  thr->ep_fd = epoll_create(1);
  thr->rr_idx = -1;
  thr->toro_count = 0;
  memset(thr->waiting_toro, 0, sizeof(thr->waiting_toro));
  memset(thr->toros, 0, sizeof(thr->toros));
  return thr;
}

int tothr_toro_count(struct tothr *thr) { return thr->toro_count; }

void tothr_destroy(struct tothr *thr) {
  // TODO: free toros
  close(thr->ep_fd);
  free(thr);
}

static void tothr_del_wait(struct tothr *thr, struct toro *t, int fd) {
  if (fd < 0 || fd >= MAX_TOROS || thr->waiting_toro[fd] == NULL) {
    return;
  }
  epoll_ctl(thr->ep_fd, EPOLL_CTL_DEL, fd, NULL);
  thr->waiting_toro[fd] = NULL;
}

void tothr_sched(struct tothr *thr) {
  struct epoll_event ev;
  int ret = epoll_wait(thr->ep_fd, &ev, 1, 0);
  if (ret > 0) {
    struct toro *t = ev.data.ptr;
    tothr_del_wait(thr, t, t->fd);
    t->fd = -1;
    setcontext(t->ctx);
  }
  thr->rr_idx = (thr->rr_idx + 1) % thr->toro_count;
  struct toro *next = thr->toros[thr->rr_idx];
  tothr_del_wait(thr, next, next->fd);
  setcontext(next->ctx);
}

static void tothr_add_wait(struct tothr *thr, struct toro *t, int fd,
                           int events) {
  struct epoll_event ev;
  ev.events = events;
  ev.data.ptr = t;
  t->fd = fd;
  thr->waiting_toro[fd] = t;
  epoll_ctl(thr->ep_fd, EPOLL_CTL_ADD, fd, &ev);
}

struct toro *toro_create(struct tothr *thr) {
  struct toro *t = malloc(sizeof(struct toro));
  t->ctx = NULL;
  t->fd = -1;
  t->thr = thr;
  thr->toros[thr->toro_count++] = t;
  if (thr->rr_idx == -1) {
    thr->rr_idx = 0;
  }
  return t;
}

void toro_destroy(struct toro *t) {
  free(t->ctx);
  free(t);
}

void toro_queue(struct toro *tr, toro_entry entry, void *arg) {
  tr->ctx = malloc(sizeof(ucontext_t));
  tr->ctx->uc_link = NULL;
  tr->ctx->uc_stack.ss_sp = malloc(1024 * 1024);
  tr->ctx->uc_stack.ss_size = 1024 * 1024;
  makecontext(tr->ctx, (void (*)(void))entry, 2, tr, arg);
}

void toro_yield(struct toro *t) { tothr_sched(t->thr); }

int co_accept(struct toro *t, int fd, struct sockaddr *addr, socklen_t *len) {
  getcontext(t->ctx);
  LOG_TRACE("co_accept [%p][%d]", t, fd);
  int new_fd = accept(fd, addr, len);
  if (new_fd >= 0) {
    return new_fd;
  }
  tothr_add_wait(t->thr, t, fd, EPOLLIN);
  toro_yield(t);
  return -1;
}

ssize_t co_read(struct toro *t, int fd, void *buf, size_t len) {
  getcontext(t->ctx);
  LOG_TRACE("co_read [%p][%d]", t, fd);
  ssize_t n = read(fd, buf, len);
  if (n >= 0) {
    return n;
  }
  tothr_add_wait(t->thr, t, fd, EPOLLIN);
  toro_yield(t);
  return -1;
}

ssize_t co_write(struct toro *t, int fd, const void *buf, size_t len) {
  getcontext(t->ctx);
  ssize_t n = write(fd, buf, len);
  if (n >= 0) {
    return n;
  }
  tothr_add_wait(t->thr, t, fd, EPOLLOUT);
  toro_yield(t);
  return -1;
}