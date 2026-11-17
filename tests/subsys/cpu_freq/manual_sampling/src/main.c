/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/cpu_freq/cpu_freq.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/ztest.h>

PSTATE_DT_DEFINE(DT_NODELABEL(pstate_0), NULL);
PSTATE_DT_DEFINE(DT_NODELABEL(pstate_1), NULL);
static atomic_t requests;
static atomic_t from_isr;
static atomic_t seen_states;
K_THREAD_STACK_DEFINE(busy_stack, 1024);
static struct k_thread busy_thread;

int cpu_freq_pstate_set(const struct pstate *state)
{
	atomic_inc(&requests);
	atomic_set(&from_isr, k_is_in_isr());
	if (state == PSTATE_DT_GET(DT_NODELABEL(pstate_0))) {
		atomic_or(&seen_states, 1);
	} else if (state == PSTATE_DT_GET(DT_NODELABEL(pstate_1))) {
		atomic_or(&seen_states, 2);
	}
	return 0;
}

static void busy_work(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	k_busy_wait(100000);
}

#if defined(CONFIG_CPU_FREQ_MANUAL_SAMPLING)
static void sample_handler(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	cpu_freq_sample();
}

K_TIMER_DEFINE(sample_timer, sample_handler, NULL);
#endif

ZTEST(cpu_freq_sampling, test_sampling)
{
#if defined(CONFIG_CPU_FREQ_MANUAL_SAMPLING)
	k_sleep(K_MSEC(100));
	zassert_equal(atomic_get(&requests), 0, "manual mode sampled autonomously");
	k_timer_start(&sample_timer, K_MSEC(1), K_MSEC(10));
#endif
	k_sleep(K_MSEC(60));
	zassert_true((atomic_get(&seen_states) & 2) != 0, "idle state not requested");
	k_thread_create(&busy_thread, busy_stack, K_THREAD_STACK_SIZEOF(busy_stack), busy_work,
			NULL, NULL, NULL, K_PRIO_PREEMPT(0), 0, K_NO_WAIT);
	k_sleep(K_MSEC(150));
	zassert_true((atomic_get(&seen_states) & 1) != 0, "busy state not requested");
	zassert_true(atomic_get(&requests) >= 2, "changed state was not requested");
	zassert_equal(atomic_get(&from_isr), 1, "sample did not run in timer context");
#if defined(CONFIG_CPU_FREQ_MANUAL_SAMPLING)
	k_timer_stop(&sample_timer);
#endif
}

ZTEST_SUITE(cpu_freq_sampling, NULL, NULL, NULL, NULL, NULL);
