#include <assert.h>
#include <errno.h>

#include "timers.h"

static int one_shot_calls;
static int periodic_calls;

static int one_shot(void *arg)
{
    assert(arg == NULL);
    one_shot_calls++;
    return 0;
}

static int periodic(void *arg)
{
    assert(arg == &periodic_calls);
    periodic_calls++;
    return periodic_calls == 3;
}

int main(void)
{
    Timer_list_t *list = Timer_list_new();
    assert(list);
    assert(Timer_list_process(list) == TIMER_NO_TIMEOUT);

    assert(Timer_list_add_tail(list, one_shot, NULL, 0,
                               TIMER_CLASSIC));
    assert(Timer_list_process(list) == TIMER_NO_TIMEOUT);
    assert(one_shot_calls == 1);
    assert(timer_list_count(list) == 0);

    struct timernode *job = Timer_list_add_tail(
        list, periodic, &periodic_calls, 0, TIMER_PERIODICAL);
    assert(job);
    assert(Timer_job_lookup(list, &periodic_calls, periodic) == job);
    assert(Timer_list_process(list) == 0);
    assert(Timer_list_process(list) == 0);
    assert(Timer_list_process(list) == TIMER_NO_TIMEOUT);
    assert(periodic_calls == 3);

    int first_arg;
    int second_arg;
    assert(Timer_list_add_tail(list, one_shot, &first_arg, 10,
                               TIMER_CLASSIC));
    assert(Timer_list_add_tail(list, one_shot, &first_arg, 20,
                               TIMER_CLASSIC));
    assert(Timer_list_add_tail(list, one_shot, &second_arg, 30,
                               TIMER_CLASSIC));
    assert(Timer_cancel_job(list, &first_arg, one_shot) == 2);
    assert(timer_list_count(list) == 1);
    Timer_list_remove_peer(list, &second_arg);
    assert(timer_list_count(list) == 0);

    errno = 0;
    assert(Timer_list_add_tail(list, one_shot, NULL, -1,
                               TIMER_CLASSIC) == NULL);
    assert(errno == EINVAL);

    Timer_list_free(list);
    return 0;
}
