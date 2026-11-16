# mini-rtos: a preemptive RTOS kernel for ARM Cortex-M3

A small real-time operating system kernel written from scratch in C and ARM
assembly (about 500 lines of kernel code), with an automated test suite that
runs on the QEMU emulator. No hardware needed.

**Status:** 7/7 kernel tests pass in QEMU. A negative test proves the
priority-inheritance test really detects priority inversion. Tested on QEMU
only, not on physical hardware.

## Features

| Feature | How it works |
|---|---|
| Preemptive scheduler | Highest priority ready task always runs. Round-robin between equal priorities on every 1 ms tick. |
| Context switch | `PendSV` handler in ARM assembly: saves R4-R11 on the task stack, swaps stack pointers, restores the next task. |
| Tick | SysTick at 1 kHz (measured: 3000 ticks in 2.93 s of real time). |
| Delays | `rtos_delay(ticks)` puts the task in a DELAYED state until the tick handler wakes it. |
| Semaphores | Counting semaphores. The token is handed directly to the highest-priority waiter. |
| Mutexes | Ownership with direct hand-off and **priority inheritance** to prevent priority inversion. |
| Queues | Blocking queue of 32-bit items, built from two semaphores. |
| Diagnostics | Stack high-water marks and overflow detection (stack fill pattern). |

Footprint (demo, `-Os`): about 2.7 KB code, 4.7 KB RAM (mostly task stacks).

## Run it

```bash
# Ubuntu / WSL
sudo apt install gcc-arm-none-eabi qemu-system-arm make

make test              # builds and runs the kernel test suite in QEMU
make test-no-inherit   # negative test: must FAIL when inheritance is disabled
make run               # sensor -> queue -> filter demo
```

Expected test output:

```
[PASS] priority_order (B>C>A)
[PASS] delay_50_ticks
[PASS] semaphore_block_and_wake
[PASS] mutex_mutual_exclusion (200 == 200)
      event order: UHM (expected UHM)
[PASS] priority_inheritance
[PASS] queue_order_and_blocking (sum 210)
[PASS] no_stack_overflow
RESULT: 7/7 passed
```

(QEMU may print `Timer with period zero, disabling` at startup. It is harmless:
it is QEMU's SysTick before the board clock is configured.)

## How it works

**Task stacks.** Each task has its own stack. A new task's stack is pre-loaded
with a fake exception frame (xPSR, PC = task function, LR = `rtos_task_exit`,
R0 = argument) so the first context switch can "return" into it.

**Context switch.** `rtos_yield()`, a blocking call or the tick sets the PendSV
bit. PendSV has the lowest interrupt priority, so it runs once no other handler
is active. The hardware has already pushed R0-R3, R12, LR, PC and xPSR onto the
task's stack; the handler pushes R4-R11, stores the stack pointer in the task
control block, calls `rtos_pick_next()`, then loads the next task's registers.

**Scheduling.** `rtos_pick_next()` scans the task table starting after the
current task and chooses the highest priority READY task, so equal priorities
rotate. The idle task (priority 0, `wfi`) is always ready.

**Priority inversion demo (`tests/test_main.c`).** Low-priority task L holds a
mutex; high-priority H wants it; medium-priority M is CPU-bound.
Without inheritance M keeps L off the CPU, so H waits behind M (events: `MUH`).
With inheritance L is boosted to H's priority, finishes first and H runs
before M (events: `UHM`).

## Limitations (honest list)

- Priority inheritance is single-level: it does not handle chains of mutexes or a
  task holding several mutexes at once.
- No timeouts on blocking calls, no recursive mutexes, no task deletion by other tasks.
- Waiters of equal priority are woken in table order, not FIFO.
- Single core, Cortex-M3 only; runs under QEMU (`lm3s6965evb`), untested on real silicon.
- Semihosting (used to end test runs) only works under an emulator/debugger.

## Ideas to extend it

1. Timeouts on `sem_wait` and `queue_receive`.
2. Multi-mutex priority inheritance.
3. A software timer service.
4. Port to a real board (STM32): only `board.c`, the linker script and the clock constant change.

## Interview talking points

- Why is PendSV the lowest priority? (So the switch never preempts another interrupt handler.)
- Why does a new task's stack contain a fake frame? (Exception return pops it.)
- What is priority inversion, and how did you prove your fix works? (The negative test.)
- Why are the critical sections short and done by masking PRIMASK?
- What happens if a task overflows its stack? (Fill pattern check; no MPU here.)
