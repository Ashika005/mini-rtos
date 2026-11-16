/*
 * Kernel test suite. Runs inside QEMU as a normal RTOS task, prints PASS/FAIL
 * per test and exits QEMU with status 0 only if everything passed.
 */
#include <stdint.h>
#include "rtos.h"
#include "uart.h"
#include "semihost.h"
#include "board.h"

#define HSTACK_WORDS 256
static uint32_t helper_stack[4][HSTACK_WORDS] __attribute__((aligned(8)));
static uint32_t runner_stack[512] __attribute__((aligned(8)));

static int g_total, g_failed;

static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static void report(const char *name, int ok)
{
    g_total++;
    if (!ok) g_failed++;
    kprintf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
}

/* ---- 1. strict priority order ---- */
static char order[8];
static volatile int order_n;
static void rec_task(void *arg) { order[order_n++] = (char)(uintptr_t)arg; }

static void test_priority_order(void)
{
    order_n = 0;
    rtos_task_create("A", rec_task, (void *)'A', 3, helper_stack[0], HSTACK_WORDS);
    rtos_task_create("B", rec_task, (void *)'B', 5, helper_stack[1], HSTACK_WORDS);
    rtos_task_create("C", rec_task, (void *)'C', 4, helper_stack[2], HSTACK_WORDS);
    rtos_delay(5);   /* let them run; runner outranks them, so they only run while we sleep */
    order[order_n] = 0;
    report("priority_order (B>C>A)", str_eq(order, "BCA"));
}

/* ---- 2. delay is tick-accurate ---- */
static void test_delay(void)
{
    tick_t t0 = rtos_ticks();
    rtos_delay(50);
    tick_t dt = rtos_ticks() - t0;
    report("delay_50_ticks", dt >= 50 && dt <= 52);
}

/* ---- 3. semaphore blocks and wakes ---- */
static rtos_sem_t sem;
static volatile int sem_count;
static void consumer(void *arg)
{
    (void)arg;
    for (int i = 0; i < 5; i++) { rtos_sem_wait(&sem); sem_count++; }
}

static void test_semaphore(void)
{
    sem_count = 0;
    rtos_sem_init(&sem, 0);
    rtos_task_create("cons", consumer, NULL, 6, helper_stack[0], HSTACK_WORDS);
    rtos_delay(2);
    int blocked_ok = (sem_count == 0);          /* must be blocked, count is 0 */
    for (int i = 0; i < 5; i++) { rtos_delay(3); rtos_sem_give(&sem); }
    rtos_delay(5);
    report("semaphore_block_and_wake", blocked_ok && sem_count == 5);
}

/* ---- 4. mutex gives mutual exclusion ---- */
static rtos_mutex_t mtx;
static volatile int shared, done_count;
static void incrementer(void *arg)
{
    (void)arg;
    for (int i = 0; i < 100; i++) {
        rtos_mutex_lock(&mtx);
        int tmp = shared;
        rtos_yield();          /* force the other task to try to enter the critical section */
        shared = tmp + 1;
        rtos_mutex_unlock(&mtx);
    }
    done_count++;
}

static void test_mutex(void)
{
    shared = 0; done_count = 0;
    rtos_mutex_init(&mtx);
    rtos_task_create("inc1", incrementer, NULL, 4, helper_stack[0], HSTACK_WORDS);
    rtos_task_create("inc2", incrementer, NULL, 4, helper_stack[1], HSTACK_WORDS);
    for (int i = 0; i < 400 && done_count < 2; i++) rtos_delay(5);
    report("mutex_mutual_exclusion (200 == 200)", done_count == 2 && shared == 200);
}

/* ---- 5. priority inheritance prevents priority inversion ----
 * L (low) holds the lock. H (high) wants it. M (medium) is CPU-bound.
 * Without inheritance M starves L, so H is stuck behind M: events come out
 * as "MUH". With inheritance L is boosted above M and finishes first: "UHM". */
static rtos_mutex_t pi_mtx;
static char events[8];
static volatile int ev_n;
static void task_L(void *a)
{
    (void)a;
    rtos_mutex_lock(&pi_mtx);
    tick_t t0 = rtos_ticks();
    while (rtos_ticks() - t0 < 40) { }     /* hold the lock while "working" */
    events[ev_n++] = 'U';                  /* about to unlock */
    rtos_mutex_unlock(&pi_mtx);
}
static void task_H(void *a)
{
    (void)a;
    rtos_delay(10);
    rtos_mutex_lock(&pi_mtx);
    events[ev_n++] = 'H';
    rtos_mutex_unlock(&pi_mtx);
}
static void task_M(void *a)
{
    (void)a;
    rtos_delay(20);
    tick_t t0 = rtos_ticks();
    while (rtos_ticks() - t0 < 50) { }     /* CPU hog */
    events[ev_n++] = 'M';
}

static void test_priority_inheritance(void)
{
    ev_n = 0;
    rtos_mutex_init(&pi_mtx);
    rtos_task_create("L", task_L, NULL, 2, helper_stack[0], HSTACK_WORDS);
    rtos_task_create("H", task_H, NULL, 8, helper_stack[1], HSTACK_WORDS);
    rtos_task_create("M", task_M, NULL, 5, helper_stack[2], HSTACK_WORDS);
    rtos_delay(150);
    events[ev_n] = 0;
    kprintf("      event order: %s (expected UHM)\n", events);
    report("priority_inheritance", str_eq(events, "UHM"));
}

/* ---- 6. blocking queue keeps order across a full buffer ---- */
static rtos_queue_t q;
static uint32_t q_store[4];
static void qproducer(void *a)
{
    (void)a;
    for (uint32_t i = 1; i <= 20; i++) rtos_queue_send(&q, i);   /* blocks when full */
}

static void test_queue(void)
{
    rtos_queue_init(&q, q_store, 4);
    rtos_task_create("prod", qproducer, NULL, 6, helper_stack[0], HSTACK_WORDS);
    uint32_t sum = 0, expect = 1; int in_order = 1;
    for (int i = 0; i < 20; i++) {
        uint32_t v = rtos_queue_receive(&q);
        if (v != expect++) in_order = 0;
        sum += v;
    }
    report("queue_order_and_blocking (sum 210)", in_order && sum == 210);
}

/* ---- 7. no stack overflow anywhere ---- */
static void test_stacks(void)
{
    report("no_stack_overflow", rtos_stack_ok());
}

static void runner(void *arg)
{
    (void)arg;
    kprintf("mini-rtos kernel tests (inherit=%d)\n", RTOS_PRIO_INHERIT);
    test_priority_order();
    test_delay();
    test_semaphore();
    test_mutex();
    test_priority_inheritance();
    test_queue();
    test_stacks();
    kprintf("RESULT: %d/%d passed\n", g_total - g_failed, g_total);
    semihost_exit(g_failed ? 1 : 0);
}

int main(void)
{
    board_init();
    rtos_init();
    rtos_task_create("runner", runner, NULL, 10, runner_stack, 512);
    rtos_start();
}
