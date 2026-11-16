#include "rtos.h"

/* Cortex-M system registers */
#define SCB_ICSR   (*(volatile uint32_t *)0xE000ED04u)
#define SCB_SHPR3  (*(volatile uint32_t *)0xE000ED20u)
#define SYST_CSR   (*(volatile uint32_t *)0xE000E010u)
#define SYST_RVR   (*(volatile uint32_t *)0xE000E014u)
#define SYST_CVR   (*(volatile uint32_t *)0xE000E018u)
#define ICSR_PENDSVSET (1u << 28)

/* QEMU's lm3s6965evb runs the core/SysTick at this rate (verified in tests/) */
#ifndef RTOS_CPU_HZ
#define RTOS_CPU_HZ 12000000u
#endif

#define STACK_FILL       0xA5A5A5A5u
#define IDLE_STACK_WORDS 64u

/* Used by the assembly in PendSV/SVC: keep names and layout stable. */
tcb_t *volatile rtos_cur_tcb;
static tcb_t tcbs[RTOS_MAX_TASKS];
static volatile tick_t g_ticks;
static volatile int g_started;   /* scheduler running? (PendSV is only valid after start) */
static uint32_t idle_stack[IDLE_STACK_WORDS] __attribute__((aligned(8)));

/* ---------- helpers ---------- */

static inline uint32_t irq_save(void)
{
    uint32_t primask;
    __asm volatile("mrs %0, primask\n\tcpsid i" : "=r"(primask) : : "memory");
    return primask;
}

static inline void irq_restore(uint32_t primask)
{
    __asm volatile("msr primask, %0" : : "r"(primask) : "memory");
}

/* Ask for a context switch. PendSV has the lowest priority, so it runs as soon
 * as interrupts are unmasked and no other handler is active. */
static inline void pend_switch(void)
{
    if (!g_started) return;      /* before rtos_start() there is no context to save */
    SCB_ICSR = ICSR_PENDSVSET;
    __asm volatile("dsb\n\tisb" : : : "memory");
}

static void idle_task(void *arg)
{
    (void)arg;
    for (;;) __asm volatile("wfi");
}

/* ---------- task creation ---------- */

static int create_at(int idx, const char *name, task_fn fn, void *arg, uint8_t prio,
                     uint32_t *stack, uint32_t words)
{
    tcb_t *t = &tcbs[idx];

    for (uint32_t i = 0; i < words; i++) stack[i] = STACK_FILL;

    /* Top of stack, 8-byte aligned (AAPCS requirement). */
    uint32_t *sp = (uint32_t *)((uint32_t)(stack + words) & ~7u);

    /* Hardware-stacked exception frame (popped on exception return). */
    *--sp = 0x01000000u;          /* xPSR: Thumb bit set        */
    *--sp = (uint32_t)fn;         /* PC: task entry             */
    *--sp = (uint32_t)rtos_task_exit; /* LR: if fn returns, exit */
    *--sp = 0;                    /* R12 */
    *--sp = 0;                    /* R3  */
    *--sp = 0;                    /* R2  */
    *--sp = 0;                    /* R1  */
    *--sp = (uint32_t)arg;        /* R0: task argument          */
    /* Software-saved registers R4-R11 (restored by PendSV). */
    for (int i = 0; i < 8; i++) *--sp = 0;

    t->sp = sp;
    t->base_prio = prio;
    t->prio = prio;
    t->wait_obj = NULL;
    t->name = name;
    t->stack_base = stack;
    t->stack_words = words;
    t->wake_tick = 0;
    return idx;
}

void rtos_init(void)
{
    for (int i = 0; i < RTOS_MAX_TASKS; i++) tcbs[i].state = T_FREE;
    create_at(0, "idle", idle_task, NULL, 0, idle_stack, IDLE_STACK_WORDS);
    tcbs[0].state = T_READY;
    rtos_cur_tcb = &tcbs[0];
}

int rtos_task_create(const char *name, task_fn fn, void *arg, uint8_t prio,
                     uint32_t *stack, uint32_t stack_words)
{
    if (!fn || !stack || stack_words < 48 || prio == 0) return -1;

    /* Reserve a slot (slot 0 is idle). Finished tasks' slots are reused. */
    int idx = -1;
    uint32_t p = irq_save();
    for (int i = 1; i < RTOS_MAX_TASKS; i++) {
        if (tcbs[i].state == T_FREE || tcbs[i].state == T_EXITED) {
            tcbs[i].state = T_CREATING;
            idx = i;
            break;
        }
    }
    irq_restore(p);
    if (idx < 0) return -1;

    create_at(idx, name, fn, arg, prio, stack, stack_words);

    p = irq_save();
    tcbs[idx].state = T_READY;
    pend_switch();           /* a higher-priority new task preempts immediately */
    irq_restore(p);
    return idx;
}

/* ---------- scheduler ---------- */

/* Called from PendSV with interrupts masked. Picks the highest-priority READY
 * task; among equal priorities it prefers the one after the current task in
 * table order, which gives round-robin time slicing. */
void rtos_pick_next(void);
void rtos_pick_next(void)
{
    int cur = (int)(rtos_cur_tcb - tcbs);
    tcb_t *best = NULL;
    for (int i = 1; i <= RTOS_MAX_TASKS; i++) {
        tcb_t *t = &tcbs[(cur + i) % RTOS_MAX_TASKS];
        if (t->state == T_READY && (best == NULL || t->prio > best->prio)) best = t;
    }
    rtos_cur_tcb = best;   /* idle is always READY, so best is never NULL */
}

tick_t rtos_ticks(void) { return g_ticks; }
tcb_t *rtos_current(void) { return rtos_cur_tcb; }

void SysTick_Handler(void)
{
    g_ticks++;
    for (int i = 0; i < RTOS_MAX_TASKS; i++) {
        tcb_t *t = &tcbs[i];
        if (t->state == T_DELAYED && (int32_t)(g_ticks - t->wake_tick) >= 0)
            t->state = T_READY;
    }
    pend_switch();   /* time slice / wake-ups may change who should run */
}

/*
 * Context switch. On exception entry the hardware already pushed
 * R0-R3, R12, LR, PC, xPSR onto the task's stack (PSP). We push R4-R11,
 * save PSP in the TCB, choose the next task, then pop its R4-R11 and
 * return - the hardware pops the rest of ITS frame.
 */
__attribute__((naked)) void PendSV_Handler(void)
{
    __asm volatile(
        "mrs   r0, psp                \n"
        "stmdb r0!, {r4-r11}          \n"
        "ldr   r1, =rtos_cur_tcb      \n"
        "ldr   r2, [r1]               \n"
        "str   r0, [r2]               \n"   /* tcb->sp = r0 */
        "push  {r1, lr}               \n"
        "cpsid i                      \n"
        "bl    rtos_pick_next         \n"
        "cpsie i                      \n"
        "pop   {r1, lr}               \n"
        "ldr   r2, [r1]               \n"
        "ldr   r0, [r2]               \n"   /* r0 = next->sp */
        "ldmia r0!, {r4-r11}          \n"
        "msr   psp, r0                \n"
        "bx    lr                     \n"
        ".ltorg                       \n");
}

/* Starts the first task: restore its context and "return" into it on PSP. */
__attribute__((naked)) void SVC_Handler(void)
{
    __asm volatile(
        "ldr   r1, =rtos_cur_tcb      \n"
        "ldr   r2, [r1]               \n"
        "ldr   r0, [r2]               \n"
        "ldmia r0!, {r4-r11}          \n"
        "msr   psp, r0                \n"
        "isb                          \n"
        "ldr   r3, =0xE000E010        \n"   /* start SysTick: clk=cpu, int, enable */
        "mov   r2, #7                 \n"
        "str   r2, [r3]               \n"
        "ldr   lr, =0xFFFFFFFD        \n"   /* return to thread mode, use PSP */
        "bx    lr                     \n"
        ".ltorg                       \n");
}

void rtos_start(void)
{
    /* PendSV lowest priority, SysTick just above it. */
    SCB_SHPR3 = (0xFFu << 16) | (0xFEu << 24);
    SYST_RVR = (RTOS_CPU_HZ / RTOS_TICK_HZ) - 1u;
    SYST_CVR = 0;

    rtos_pick_next();                  /* choose the first task to run */
    g_started = 1;
    __asm volatile("svc 0");           /* SVC_Handler launches it */
    for (;;) { }
}

/* ---------- time ---------- */

void rtos_yield(void)
{
    uint32_t p = irq_save();
    pend_switch();
    irq_restore(p);
}

void rtos_delay(tick_t ticks)
{
    if (ticks == 0) { rtos_yield(); return; }
    uint32_t p = irq_save();
    rtos_cur_tcb->wake_tick = g_ticks + ticks;
    rtos_cur_tcb->state = T_DELAYED;
    pend_switch();
    irq_restore(p);        /* PendSV fires here and switches away */
}

void rtos_task_exit(void)
{
    uint32_t p = irq_save();
    rtos_cur_tcb->state = T_EXITED;
    pend_switch();
    irq_restore(p);
    for (;;) { }
}

/* ---------- blocking primitives ---------- */

/* Highest-priority task blocked on obj (ties: lowest table index). */
static tcb_t *highest_waiter(void *obj)
{
    tcb_t *best = NULL;
    for (int i = 0; i < RTOS_MAX_TASKS; i++) {
        tcb_t *t = &tcbs[i];
        if (t->state == T_BLOCKED && t->wait_obj == obj &&
            (best == NULL || t->prio > best->prio))
            best = t;
    }
    return best;
}

void rtos_sem_init(rtos_sem_t *s, int32_t initial) { s->count = initial; }

void rtos_sem_wait(rtos_sem_t *s)
{
    uint32_t p = irq_save();
    if (s->count > 0) {
        s->count--;
        irq_restore(p);
        return;
    }
    rtos_cur_tcb->state = T_BLOCKED;
    rtos_cur_tcb->wait_obj = s;
    pend_switch();
    irq_restore(p);   /* we resume here once rtos_sem_give() hands us the token */
}

void rtos_sem_give(rtos_sem_t *s)
{
    uint32_t p = irq_save();
    tcb_t *w = highest_waiter(s);
    if (w) {            /* hand the token directly to the waiter */
        w->state = T_READY;
        w->wait_obj = NULL;
        pend_switch();  /* waiter may outrank us */
    } else {
        s->count++;
    }
    irq_restore(p);
}

void rtos_mutex_init(rtos_mutex_t *m) { m->owner = NULL; }

int rtos_mutex_lock(rtos_mutex_t *m)
{
    uint32_t p = irq_save();
    tcb_t *cur = rtos_cur_tcb;
    if (m->owner == cur) { irq_restore(p); return -1; }   /* not recursive */
    if (m->owner == NULL) {
        m->owner = cur;
        irq_restore(p);
        return 0;
    }
#if RTOS_PRIO_INHERIT
    /* Priority inheritance: lift the owner to our priority so a medium-priority
     * task cannot starve it while we wait (priority inversion). */
    if (m->owner->prio < cur->prio) m->owner->prio = cur->prio;
#endif
    cur->state = T_BLOCKED;
    cur->wait_obj = m;
    pend_switch();
    irq_restore(p);   /* ownership is handed to us by rtos_mutex_unlock() */
    return 0;
}

int rtos_mutex_unlock(rtos_mutex_t *m)
{
    uint32_t p = irq_save();
    tcb_t *cur = rtos_cur_tcb;
    if (m->owner != cur) { irq_restore(p); return -1; }
    cur->prio = cur->base_prio;        /* drop any inherited priority */
    tcb_t *w = highest_waiter(m);
    if (w) {
        m->owner = w;                  /* direct hand-off */
        w->state = T_READY;
        w->wait_obj = NULL;
    } else {
        m->owner = NULL;
    }
    pend_switch();
    irq_restore(p);
    return 0;
}

/* ---------- queue (built on two semaphores) ---------- */

void rtos_queue_init(rtos_queue_t *q, uint32_t *storage, uint32_t capacity)
{
    q->buf = storage;
    q->cap = capacity;
    q->head = q->tail = 0;
    rtos_sem_init(&q->items, 0);
    rtos_sem_init(&q->spaces, (int32_t)capacity);
}

void rtos_queue_send(rtos_queue_t *q, uint32_t item)
{
    rtos_sem_wait(&q->spaces);          /* block while full */
    uint32_t p = irq_save();
    q->buf[q->head] = item;
    q->head = (q->head + 1) % q->cap;
    irq_restore(p);
    rtos_sem_give(&q->items);
}

uint32_t rtos_queue_receive(rtos_queue_t *q)
{
    rtos_sem_wait(&q->items);           /* block while empty */
    uint32_t p = irq_save();
    uint32_t item = q->buf[q->tail];
    q->tail = (q->tail + 1) % q->cap;
    irq_restore(p);
    rtos_sem_give(&q->spaces);
    return item;
}

/* ---------- diagnostics ---------- */

uint32_t rtos_stack_free_words(int task_id)
{
    const tcb_t *t = &tcbs[task_id];
    uint32_t n = 0;
    while (n < t->stack_words && t->stack_base[n] == STACK_FILL) n++;
    return n;
}

int rtos_stack_ok(void)
{
    for (int i = 0; i < RTOS_MAX_TASKS; i++) {
        if (tcbs[i].state != T_FREE && tcbs[i].state != T_CREATING &&
            tcbs[i].stack_base[0] != STACK_FILL)
            return 0;   /* lowest word overwritten => overflow */
    }
    return 1;
}

const tcb_t *rtos_task_info(int task_id) { return &tcbs[task_id]; }
