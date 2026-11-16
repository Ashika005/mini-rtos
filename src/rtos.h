#ifndef RTOS_H
#define RTOS_H
/*
 * mini-rtos: a small preemptive RTOS kernel for ARM Cortex-M3.
 *
 *  - Fixed-priority preemptive scheduler, round-robin between equal priorities
 *  - Context switch in PendSV (assembly), 1 kHz SysTick tick
 *  - Delays, counting semaphores, mutexes with priority inheritance,
 *    blocking message queues
 *
 * Priority: a HIGHER number means a HIGHER priority. 0 is reserved for idle.
 */
#include <stdint.h>
#include <stddef.h>

#define RTOS_MAX_TASKS 10
#define RTOS_TICK_HZ   1000u

#ifndef RTOS_PRIO_INHERIT
#define RTOS_PRIO_INHERIT 1   /* set to 0 to see priority inversion happen */
#endif

typedef uint32_t tick_t;
typedef void (*task_fn)(void *arg);

typedef enum {
    T_FREE = 0,
    T_CREATING,
    T_READY,     /* ready, or currently running */
    T_DELAYED,
    T_BLOCKED,   /* waiting on a semaphore or mutex */
    T_EXITED
} task_state_t;

typedef struct tcb {
    uint32_t    *sp;          /* saved stack pointer - MUST be first (used by asm) */
    task_state_t state;
    uint8_t      base_prio;   /* priority the task was created with */
    uint8_t      prio;        /* effective priority (may be boosted) */
    tick_t       wake_tick;
    void        *wait_obj;    /* semaphore/mutex this task is blocked on */
    const char  *name;
    uint32_t    *stack_base;
    uint32_t     stack_words;
} tcb_t;

typedef struct { volatile int32_t count; } rtos_sem_t;
typedef struct { tcb_t *owner; } rtos_mutex_t;

typedef struct {
    uint32_t  *buf;
    uint32_t   cap, head, tail;
    rtos_sem_t items;   /* counts filled slots */
    rtos_sem_t spaces;  /* counts free slots   */
} rtos_queue_t;

/* Kernel control */
void  rtos_init(void);
int   rtos_task_create(const char *name, task_fn fn, void *arg, uint8_t prio,
                       uint32_t *stack, uint32_t stack_words);
void  rtos_start(void) __attribute__((noreturn));

/* Time and scheduling */
void   rtos_delay(tick_t ticks);
void   rtos_yield(void);
tick_t rtos_ticks(void);
void   rtos_task_exit(void) __attribute__((noreturn));
tcb_t *rtos_current(void);

/* Counting semaphore */
void rtos_sem_init(rtos_sem_t *s, int32_t initial);
void rtos_sem_wait(rtos_sem_t *s);
void rtos_sem_give(rtos_sem_t *s);

/* Mutex (not recursive). Returns 0 on success, -1 on misuse. */
void rtos_mutex_init(rtos_mutex_t *m);
int  rtos_mutex_lock(rtos_mutex_t *m);
int  rtos_mutex_unlock(rtos_mutex_t *m);

/* Blocking queue of 32-bit items */
void rtos_queue_init(rtos_queue_t *q, uint32_t *storage, uint32_t capacity);
void rtos_queue_send(rtos_queue_t *q, uint32_t item);
uint32_t rtos_queue_receive(rtos_queue_t *q);

/* Diagnostics */
uint32_t rtos_stack_free_words(int task_id);   /* high-water mark: untouched words */
int      rtos_stack_ok(void);                  /* 1 if no task overflowed its stack */
const tcb_t *rtos_task_info(int task_id);

#endif
