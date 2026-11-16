# mini-rtos

[![ci](https://github.com/Ashika005/mini-rtos/actions/workflows/ci.yml/badge.svg)](https://github.com/Ashika005/mini-rtos/actions/workflows/ci.yml)
![Arch](https://img.shields.io/badge/ARM-Cortex--M3-0091BD)
![Language](https://img.shields.io/badge/Language-C%20%2B%20ARM%20asm-informational)
![Tests](https://img.shields.io/badge/tests-7%2F7%20in%20QEMU-brightgreen)

**A preemptive RTOS kernel for ARM Cortex-M3, written from scratch in C and ARM assembly.**
Priority scheduling, semaphores, mutexes with **priority inheritance**, blocking queues and
stack-overflow detection, in under 400 lines of kernel code. It is tested automatically on every
commit in the QEMU emulator, so no hardware is needed.

> Tested on QEMU (`lm3s6965evb`) only, not on physical hardware.

## What makes it interesting

- **Real context switch.** A hand-written `PendSV` handler in ARM assembly saves and restores
  R4-R11 and swaps stack pointers.
- **Priority inversion, reproduced and fixed.** A test creates the classic inversion
  (low task holds a lock, high task waits, medium task hogs the CPU), shows it breaking
  without inheritance, and shows it fixed with inheritance.
- **The negative test is part of CI.** CI also builds with inheritance *disabled* and requires
  the test to fail, so the passing test is proven to catch the bug.
- **Everything runs in CI.** Each push builds the kernel and runs the whole suite on QEMU.

## Demo

A sensor task samples a simulated ADC at 40 Hz and sends values through a queue to a filter task
that flags spikes against a moving average. A heartbeat task and a mutex-protected console run
alongside it. Output from `make run`:

```
mini-rtos demo: sensor -> queue -> filter
[175] sample #8 value=504 avg=512
[400] sample #17 value=637 avg=510  <-- ALERT: spike
[500] heartbeat
[825] sample #34 value=623 avg=516  <-- ALERT: spike
[1000] heartbeat

--- stack high-water marks (free words) ---
sensor: 230 of 256 free
filter: 218 of 256 free
heartbeat: 234 of 256 free
monitor: 228 of 256 free
stack overflow check: OK
```

## Tests

`make test` runs 7 tests inside QEMU:

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

(QEMU prints `Timer with period zero, disabling` at reset. It is harmless: it is QEMU's SysTick
before the board clock is set up.)

### Priority inversion, in one picture

```
Without inheritance                    With inheritance (this kernel)

H  (high)    ....wait for lock.......  H  ....wait....run
M  (medium)  ......RUN RUN RUN.......  M  ...............run
L  (low)     run(lock)..starved......  L  run(lock)-boosted-unlock

events:  M U H   <- H is stuck         events:  U H M   <- H runs first
         behind M
```

## Features

| Feature | How it works |
|---|---|
| Preemptive scheduler | Highest-priority ready task always runs. Round-robin between equal priorities on each 1 ms tick. |
| Context switch | `PendSV` handler in ARM assembly: saves R4-R11 on the task stack, swaps stack pointers, restores the next task. |
| Tick | SysTick at 1 kHz (measured: 3000 ticks in 2.93 s of real time). |
| Delays | `rtos_delay(ticks)` puts the task in a DELAYED state until the tick handler wakes it. |
| Semaphores | Counting semaphores. The token is handed directly to the highest-priority waiter. |
| Mutexes | Ownership with direct hand-off and **priority inheritance**. |
| Queues | Blocking queue of 32-bit items, built from two semaphores. |
| Diagnostics | Stack high-water marks and overflow detection (stack fill pattern). |

Footprint (demo, `-Os`): about 2.7 KB code and 4.7 KB RAM, mostly task stacks.

## Quick start

```bash
# Ubuntu / WSL
sudo apt install gcc-arm-none-eabi qemu-system-arm make

make test              # build and run the kernel test suite in QEMU
make test-no-inherit   # negative test: must FAIL when inheritance is disabled
make run               # sensor -> queue -> filter demo
```

## How it works

**Task stacks.** Each task has its own stack. A new task's stack is pre-loaded with a fake
exception frame (xPSR, PC = task function, LR = `rtos_task_exit`, R0 = argument) so the first
context switch can "return" into it.

**Context switch.** `rtos_yield()`, a blocking call or the tick sets the PendSV bit. PendSV has
the lowest interrupt priority, so it runs only when no other handler is active. The hardware has
already pushed R0-R3, R12, LR, PC and xPSR onto the task's stack. The handler pushes R4-R11,
stores the stack pointer in the task control block, calls `rtos_pick_next()` and loads the next
task's registers.

**Scheduling.** `rtos_pick_next()` scans the task table starting after the current task and picks
the highest-priority READY task, so equal priorities rotate. The idle task (priority 0, `wfi`) is
always ready.

**Critical sections.** Short sections that touch scheduler state are protected by masking
`PRIMASK`.

## Project layout

```
src/        kernel.c (scheduler, sync primitives), startup.c, uart.c, board.c, main.c (demo)
tests/      test_main.c (the 7-test suite, runs as an RTOS task)
scripts/    run_tests.sh (runs QEMU and checks the exit status)
.github/    ci.yml (build, test, negative test on every push)
```

## Limitations

- Priority inheritance is single-level: no mutex chains, and no task holding several mutexes at once.
- No timeouts on blocking calls, no recursive mutexes, no task deletion by other tasks.
- Waiters of equal priority are woken in table order, not FIFO.
- Single core, Cortex-M3 only; runs under QEMU, untested on real silicon.
- Semihosting (used to end test runs) only works under an emulator or debugger.

## Roadmap

1. Timeouts on `sem_wait` and `queue_receive`.
2. Multi-mutex priority inheritance.
3. A software timer service.
4. Port to a real board (e.g. STM32): only `board.c`, the linker script and the clock constant change.

## Author

Ashika Krishnamurthy, final-year B.Tech (Electronics & Instrumentation), Puducherry Technological University.
