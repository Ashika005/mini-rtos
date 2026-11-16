/*
 * Demo application: a tiny sensor pipeline running on mini-rtos.
 *
 *   sensor (prio 4) --queue--> filter (prio 5) --> console (shared, mutex-protected)
 *   heartbeat (prio 2)         monitor (prio 6): prints stack usage, then ends the run
 *
 * Build and run:  make run
 */
#include <stdint.h>
#include "rtos.h"
#include "uart.h"
#include "board.h"
#include "semihost.h"

#define STACK_WORDS 256
static uint32_t stk_sensor[STACK_WORDS]    __attribute__((aligned(8)));
static uint32_t stk_filter[STACK_WORDS]    __attribute__((aligned(8)));
static uint32_t stk_heartbeat[STACK_WORDS] __attribute__((aligned(8)));
static uint32_t stk_monitor[STACK_WORDS]   __attribute__((aligned(8)));

static rtos_queue_t  sample_q;
static uint32_t      sample_store[8];
static rtos_mutex_t  console_lock;       /* protects the UART so lines never interleave */
static int           ids[4];

#define LOG(...) do { rtos_mutex_lock(&console_lock); kprintf(__VA_ARGS__); \
                      rtos_mutex_unlock(&console_lock); } while (0)

/* Simulated ADC: noisy value around 500 with an occasional spike. */
static uint32_t lfsr = 0xACE1u;
static uint32_t fake_adc(uint32_t n)
{
    lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xB400u);
    uint32_t v = 500u + (lfsr & 0x1Fu);
    if (n % 17u == 16u) v += 120u;          /* injected fault */
    return v;
}

static void sensor_task(void *arg)
{
    (void)arg;
    for (uint32_t n = 0; ; n++) {
        rtos_queue_send(&sample_q, fake_adc(n));
        rtos_delay(25);                      /* 40 Hz sampling */
    }
}

static void filter_task(void *arg)
{
    (void)arg;
    uint32_t win[4] = {500, 500, 500, 500};
    uint32_t idx = 0, n = 0;
    for (;;) {
        uint32_t v = rtos_queue_receive(&sample_q);
        uint32_t avg = (win[0] + win[1] + win[2] + win[3]) / 4u;
        int alert = (v > avg + 60u);         /* spike detection against moving average */
        win[idx] = v; idx = (idx + 1u) & 3u;
        n++;
        if (alert || n % 8u == 0u)
            LOG("[%u] sample #%u value=%u avg=%u%s\n", rtos_ticks(), n, v, avg,
                alert ? "  <-- ALERT: spike" : "");
    }
}

static void heartbeat_task(void *arg)
{
    (void)arg;
    for (;;) {
        rtos_delay(500);
        LOG("[%u] heartbeat\n", rtos_ticks());
    }
}

static void monitor_task(void *arg)
{
    (void)arg;
    rtos_delay(1500);
    rtos_mutex_lock(&console_lock);
    kprintf("\n--- stack high-water marks (free words) ---\n");
    const char *names[4] = {"sensor", "filter", "heartbeat", "monitor"};
    for (int i = 0; i < 4; i++)
        kprintf("%s: %u of %u free\n", names[i], rtos_stack_free_words(ids[i]), STACK_WORDS);
    kprintf("stack overflow check: %s\n", rtos_stack_ok() ? "OK" : "FAILED");
    rtos_mutex_unlock(&console_lock);
    semihost_exit(0);
}

int main(void)
{
    board_init();
    rtos_init();
    rtos_mutex_init(&console_lock);
    rtos_queue_init(&sample_q, sample_store, 8);
    kprintf("mini-rtos demo: sensor -> queue -> filter\n");

    ids[0] = rtos_task_create("sensor",    sensor_task,    NULL, 4, stk_sensor,    STACK_WORDS);
    ids[1] = rtos_task_create("filter",    filter_task,    NULL, 5, stk_filter,    STACK_WORDS);
    ids[2] = rtos_task_create("heartbeat", heartbeat_task, NULL, 2, stk_heartbeat, STACK_WORDS);
    ids[3] = rtos_task_create("monitor",   monitor_task,   NULL, 6, stk_monitor,   STACK_WORDS);
    rtos_start();
}
