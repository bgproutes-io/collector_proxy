#ifndef __TIMERS_H__
#define __TIMERS_H__

/**
 * @file timers.h
 * @brief Lightweight one-shot and periodic task scheduler.
 *
 * Copied and adapted from the collector timer module.  Call
 * Timer_list_process() from the proxy event loop before blocking; its return
 * value is the number of seconds until the next task is due.
 */

#include "common.h"

#define TIMER_PERIODICAL 1
#define TIMER_CLASSIC    2
#define TIMER_NO_TIMEOUT ((uint64_t)INT64_MAX)


struct timernode
{
    struct timernode *prev;
    struct timernode *next;

    uint8_t type;
    int64_t last_tik;
    int64_t timer;
    int64_t remaining;

    void *args;
    int (*callback)(void *arg);
};

typedef struct Timer_list_s
{
    struct timernode *head;
    struct timernode *tail;
    uint32_t count;
} Timer_list_t;

extern Timer_list_t *timers;

#define timernode_infos(X)    ((X)->timer)
#define timernode_next(X)     ((X) ? (X)->next : NULL)
#define timernode_prev(X)     ((X) ? (X)->prev : NULL)
#define timer_list_head(X)    ((X) ? (X)->head : NULL)
#define timer_list_tail(X)    ((X) ? (X)->tail : NULL)
#define timer_list_count(X)   ((X) ? (X)->count : 0U)

#define FOR_ALL_ELEMENTS_TIMERS(list, node, infos)                         \
    (node) = (list)->head;                                                 \
    (node) && (((infos) = timernode_infos(node)), 1);                      \
    (node) = timernode_next(node)

Timer_list_t *Timer_list_new(void);

struct timernode *Timer_list_add_tail(
    Timer_list_t *list,
    int (*callback)(void *info),
    void *arg,
    int sec,
    uint8_t type
);

void Timer_list_free(Timer_list_t *list);
void Timer_list_delete_elem(Timer_list_t *list, struct timernode *node);

/** Return TIMER_NO_TIMEOUT when no task remains. */
uint64_t Timer_list_process(Timer_list_t *list);

void Timer_list_print(Timer_list_t *list);
void Timer_list_remove_peer(Timer_list_t *list, void *data);

int Timer_cancel_job(
    Timer_list_t *list,
    void *data,
    int (*callback)(void *arg)
);

struct timernode *Timer_job_lookup(
    Timer_list_t *list,
    void *data,
    int (*callback)(void *arg)
);

#endif
