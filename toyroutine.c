#include "toyroutine.h"
#include <assert.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ucontext.h>
#include <ucontext.h>
#include <unistd.h>

#include "toylogger.h"
#include "toyroutine_syscalls.h"

// a coroutine
struct toro {
  ucontext_t *ctx;   // owned
  struct tothr *thr; // as weak_ptr
  int fd;            // the fd that the coroutine is waiting on
};

#define MAX_TOROS 1024
// a thread, scheduling multiple toros (coroutines)
struct tothr {
  int ep_fd;
  int atomic_flag;
  int timer_fd;
  int rr_idx; // round-robin index for scheduling
  int toro_count;
  struct toro *toros[MAX_TOROS];        // owned
  struct toro *waiting_toro[MAX_TOROS]; // as unique_ptr's shadow
};

// delete the toro from the epoll wait list
static void tothr_del_wait(struct tothr *thr, struct toro *t) {
  if (!t) {
    return;
  }
  int fd = t->fd;
  if (fd < 0 || fd >= MAX_TOROS || thr->waiting_toro[fd] == NULL) {
    return;
  }
  epoll_ctl(thr->ep_fd, EPOLL_CTL_DEL, fd, NULL);
  thr->waiting_toro[fd] = NULL;
  t->fd = -1;
}

struct tothr *tothr_create() {
  struct tothr *thr = (struct tothr *)malloc(sizeof(struct tothr));
  thr->ep_fd = epoll_create(1);
  thr->atomic_flag = 0;
  thr->rr_idx = -1;
  thr->toro_count = 0;
  memset(thr->waiting_toro, 0, sizeof(thr->waiting_toro));
  memset(thr->toros, 0, sizeof(thr->toros));
  return thr;
}

void tothr_destroy(struct tothr *thr) {
  LOG_TRACE("tothr_destroy %p, toro count[%d]", thr, thr->toro_count);
  for (int i = 0; i < thr->toro_count; ++i) {
    tothr_del_wait(thr, thr->toros[i]);
    toro_destroy(thr->toros[i]);
    free(thr->toros[i]);
  }
  close(thr->ep_fd);
}

int tothr_toro_count(struct tothr *thr) { return thr->toro_count; }

void tothr_remove_toro(struct tothr *thr, struct toro *t) {
  tothr_del_wait(thr, t);

  while (!__sync_bool_compare_and_swap(&thr->atomic_flag, 0, 1))
    ;
  if (thr->toro_count == 0) {
    thr->atomic_flag = 0;
    return;
  }
  int last = thr->toro_count - 1;

  for (int i = 0; i < thr->toro_count; i++) {
    if (thr->toros[i] == t) {
      if (i < last) {
        thr->toros[i] = thr->toros[last];
        thr->toros[last] = NULL;
      }
      --thr->toro_count;
      break;
    }
  }

  thr->atomic_flag = 0;
}

void tothr_sched(struct tothr *thr) {
  struct epoll_event ev;
  int ret = epoll_wait(thr->ep_fd, &ev, 1, 0);
  while (ret < 0) {
    ret = epoll_wait(thr->ep_fd, &ev, 1, 0);
  }
  if (ret > 0) {
    struct toro *t = (struct toro *)ev.data.ptr;
    LOG_TRACE("sched waited thr[%p] toro[%p]", thr, t);
    tothr_del_wait(thr, t);
    setcontext(t->ctx);
    return;
  }
  while (!__sync_bool_compare_and_swap(&thr->atomic_flag, 0, 1))
    ;
  if (thr->toro_count == 0) {
    thr->atomic_flag = 0;
    return;
  }
  thr->rr_idx = (thr->rr_idx + 1) % thr->toro_count;
  struct toro *next = thr->toros[thr->rr_idx];
  LOG_TRACE("sched rotated thr[%p][%d] toro[%p]", thr, thr->rr_idx, next);
  tothr_del_wait(thr, next);
  thr->atomic_flag = 0;
  LOG_TRACE("next[%p] ctx[%p]", next, next->ctx);
  setcontext(next->ctx);
}

static void tothr_add_wait(struct tothr *thr, struct toro *t, int fd,
                           int events) {
  struct epoll_event ev;
  ev.events = events;
  ev.data.ptr = t;
  assert(t->fd == -1);
  t->fd = fd;
  thr->waiting_toro[fd] = t;
  epoll_ctl(thr->ep_fd, EPOLL_CTL_ADD, fd, &ev);
}

#define TORO_STACK_SIZE (1024 * 1024)
struct toro *toro_create(struct tothr *thr, toro_entry entry, void *args) {
  struct toro *t = (struct toro *)malloc(sizeof(struct toro));
  t->thr = thr;
  t->fd = -1;
  t->ctx = malloc(sizeof(ucontext_t));
//  getcontext(t->ctx);
  memset(t->ctx, 0, sizeof(ucontext_t));
  t->ctx->uc_link = NULL;
  t->ctx->uc_stack.ss_sp = malloc(TORO_STACK_SIZE);
  t->ctx->uc_stack.ss_size = TORO_STACK_SIZE;
  while (!__sync_bool_compare_and_swap(&thr->atomic_flag, 0, 1))
    ;
  LOG_TRACE("create toro, thr[%p][%d] toro[%p]", thr, thr->toro_count, t);
  thr->toros[thr->toro_count++] = t;
  if (thr->rr_idx == -1) {
    thr->rr_idx = 0;
  }
  makecontext(t->ctx, (void (*)(void))entry, 2, t, args);

  thr->atomic_flag = 0;
  return t;
}

void toro_destroy(struct toro *t) {
  struct tothr *thr = t->thr;
  LOG_TRACE("toro destroy, tothr[%p] toro[%p]", thr, t);
  tothr_remove_toro(thr, t);
  free(t->ctx->uc_stack.ss_sp);
  free(t->ctx);
  free(t);

  tothr_sched(thr);
}

void toro_queue(struct toro *tr, toro_entry entry, void *arg) {
  getcontext(tr->ctx);
  tr->ctx->uc_link = NULL;
  tr->ctx->uc_stack.ss_sp = malloc(TORO_STACK_SIZE);
  tr->ctx->uc_stack.ss_size = TORO_STACK_SIZE;
  makecontext(tr->ctx, (void (*)(void))entry, 2, tr, arg);
}

void toro_yield(struct toro *t) { tothr_sched(t->thr); }

/// ----------------- syscalls -----------------
int co_socket(struct toro *, int domain, int type, int protocol);

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

int co_connect(struct toro *t, int fd, const struct sockaddr *addr,
               socklen_t len) {
  getcontext(t->ctx);
  LOG_TRACE("co_connect [%p][%d]", t, fd);
  int ret = connect(fd, addr, len);
  if (ret == 0) {
    return 0;
  }
  tothr_add_wait(t->thr, t, fd, EPOLLOUT);
  toro_yield(t);
  return -1;
}

ssize_t co_sendto(struct toro *t, int fd, const void *buf, size_t len,
                  int flags, const struct sockaddr *addr, socklen_t addrlen) {
  getcontext(t->ctx);
  ssize_t n = sendto(fd, buf, len, flags, addr, addrlen);
  if (n >= 0) {
    return n;
  }
  tothr_add_wait(t->thr, t, fd, EPOLLOUT);
  toro_yield(t);
  return -1;
}

ssize_t co_recvfrom(struct toro *t, int fd, void *buf, size_t len, int flags,
                    struct sockaddr *addr, socklen_t *addrlen) {
  getcontext(t->ctx);
  ssize_t n = recvfrom(fd, buf, len, flags, addr, addrlen);
  if (n >= 0) {
    return n;
  }
  tothr_add_wait(t->thr, t, fd, EPOLLIN);
  toro_yield(t);
  return -1;
}

ssize_t co_send(struct toro *t, int fd, const void *buf, size_t len,
                int flags) {
  getcontext(t->ctx);
  ssize_t n = send(fd, buf, len, flags);
  if (n >= 0) {
    return n;
  }
  tothr_add_wait(t->thr, t, fd, EPOLLOUT);
  toro_yield(t);
  return -1;
}

ssize_t co_recv(struct toro *t, int fd, void *buf, size_t len, int flags) {
  getcontext(t->ctx);
  ssize_t n = recv(fd, buf, len, flags);
  if (n >= 0) {
    return n;
  }
  tothr_add_wait(t->thr, t, fd, EPOLLIN);
  toro_yield(t);
  return -1;
}
