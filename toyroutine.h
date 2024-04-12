#pragma once
#ifndef __TOYROUTINE_H__
#define __TOYROUTINE_H__

struct tothr;
struct toro;

typedef void (*toro_entry)(struct toro *, void *);

struct tothr *tothr_create();
void tothr_destroy(struct tothr *);
void tothr_sched(struct tothr *);
int tothr_toro_count(struct tothr *);

struct toro *toro_create(struct tothr *);
void toro_destroy(struct toro *);

void toro_queue(struct toro *, toro_entry, void *);
void toro_yield(struct toro *);

#endif