#include <errno.h>
#define _POSIX_C_SOURCE 200809L
#include <time.h>

#include "../include/timers.h"

Timer_list_t *timers = NULL;

static int64_t Timer_now(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
        return -1;
    return now.tv_sec;
}

static struct timernode *timernode_new(void)
{
    return calloc(1, sizeof(struct timernode));
}

Timer_list_t *Timer_list_new(void)
{
    return calloc(1, sizeof(Timer_list_t));
}

struct timernode *Timer_list_add_tail(Timer_list_t *list,
                                      int (*callback)(void *info),
                                      void *arg, int sec, uint8_t type)
{
    if (!list || !callback || sec < 0 ||
        (type != TIMER_CLASSIC && type != TIMER_PERIODICAL)) {
        errno = EINVAL;
        return NULL;
    }

    int64_t now = Timer_now();
    if (now < 0)
        return NULL;

    struct timernode *node = timernode_new();
    if (!node)
        return NULL;

    node->last_tik = now;
    node->remaining = sec;
    node->timer = sec;
    node->callback = callback;
    node->args = arg;
    node->type = type;
    node->prev = list->tail;

    if (list->tail)
        list->tail->next = node;
    else
        list->head = node;
    list->tail = node;
    list->count++;
    return node;
}

void Timer_list_free(Timer_list_t *list)
{
    if (!list)
        return;

    struct timernode *node = list->head;
    while (node) {
        struct timernode *next = node->next;
        free(node);
        node = next;
    }
    free(list);
}

void Timer_list_delete_elem(Timer_list_t *list, struct timernode *node)
{
    if (!list || !node)
        return;

    if (node->prev)
        node->prev->next = node->next;
    else
        list->head = node->next;

    if (node->next)
        node->next->prev = node->prev;
    else
        list->tail = node->prev;

    if (list->count)
        list->count--;
    free(node);
}

uint64_t Timer_list_process(Timer_list_t *list)
{
    if (!list || !list->head)
        return TIMER_NO_TIMEOUT;

    int64_t now = Timer_now();
    if (now < 0)
        return TIMER_NO_TIMEOUT;

    int64_t minimum = INT64_MAX;
    struct timernode *node = list->head;
    while (node) {
        struct timernode *next = node->next;
        int64_t elapsed = now - node->last_tik;
        if (elapsed < 0)
            elapsed = 0;

        node->remaining = elapsed >= node->remaining
            ? 0
            : node->remaining - elapsed;
        node->last_tik = now;

        if (node->remaining == 0) {
            int result = node->callback(node->args);
            if (node->type == TIMER_PERIODICAL && result == 0) {
                node->remaining = node->timer;
                minimum = MIN(minimum, node->remaining);
            } else {
                Timer_list_delete_elem(list, node);
            }
        } else {
            minimum = MIN(minimum, node->remaining);
        }
        node = next;
    }

    return list->head ? (uint64_t)minimum : TIMER_NO_TIMEOUT;
}

void Timer_list_print(Timer_list_t *list)
{
    if (!list) {
        printf("\n");
        return;
    }

    struct timernode *node;
    int64_t interval;
    for (FOR_ALL_ELEMENTS_TIMERS(list, node, interval))
        printf(" (%ld) ", (long)interval);
    printf("\n");
}

void Timer_list_remove_peer(Timer_list_t *list, void *data)
{
    if (!list)
        return;

    struct timernode *node = list->head;
    while (node) {
        struct timernode *next = node->next;
        if (node->args == data)
            Timer_list_delete_elem(list, node);
        node = next;
    }
}

int Timer_cancel_job(Timer_list_t *list, void *data,
                     int (*callback)(void *arg))
{
    if (!list)
        return 0;

    int cancelled = 0;
    struct timernode *node = list->head;
    while (node) {
        struct timernode *next = node->next;
        if (node->args == data && node->callback == callback) {
            Timer_list_delete_elem(list, node);
            cancelled++;
        }
        node = next;
    }
    return cancelled;
}

struct timernode *Timer_job_lookup(Timer_list_t *list, void *data,
                                   int (*callback)(void *arg))
{
    if (!list)
        return NULL;

    for (struct timernode *node = list->head; node; node = node->next) {
        if (node->args == data && node->callback == callback)
            return node;
    }
    return NULL;
}
