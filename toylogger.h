#ifndef _TOYLOGGER_H_
#define _TOYLOGGER_H_

#include <stdio.h>
#include <time.h>
#define TRACE 0
#define DEBUG 1
#define INFO 2
#define WARN 3
#define ERROR 4
#define FATAL 5
static int log_level = TRACE;
#define DO_LOG(level, short, fmt, ...)                                         \
  do {                                                                         \
    if (log_level <= level) {                                                  \
      struct timespec ts;                                                      \
      clock_gettime(CLOCK_REALTIME, &ts);                                      \
      struct tm tm;                                                            \
      localtime_r(&ts.tv_sec, &tm);                                            \
      fprintf(stderr,                                                          \
              "[%02d:%02d:%02d.%06ld|" short "|" __FILE__ ":%d] " fmt "\n",    \
              tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000, __LINE__,   \
              ##__VA_ARGS__);                                                  \
      fflush(stderr);                                                          \
    }                                                                          \
  } while (0)

#ifdef NO_LOG
#define LOG_TRACE(fmt, ...) ((void)0)
#define LOG_DEBUG(fmt, ...) ((void)0)
#define LOG_INFO(fmt, ...) ((void)0)
#define LOG_WARN(fmt, ...) ((void)0)
#define LOG_ERROR(fmt, ...) ((void)0)
#define LOG_FATAL(fmt, ...) ((void)0)
#else
#define LOG_TRACE(fmt, ...) DO_LOG(0, "T", fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) DO_LOG(1, "D", fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) DO_LOG(2, "I", fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) DO_LOG(3, "W", fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) DO_LOG(4, "E", fmt, ##__VA_ARGS__)
#define LOG_FATAL(fmt, ...) DO_LOG(5, "F", fmt, ##__VA_ARGS__)
#endif
#endif
