CROSS   ?= arm-none-eabi-
CC       = $(CROSS)gcc
SIZE     = $(CROSS)size
BUILD    = build
QEMU     = qemu-system-arm -machine lm3s6965evb -cpu cortex-m3 -nographic \
           -semihosting-config enable=on,target=native

CFLAGS   = -mcpu=cortex-m3 -mthumb -Os -g -std=c11 -Wall -Wextra -Werror \
           -ffreestanding -ffunction-sections -fdata-sections -Isrc \
           $(EXTRA_CFLAGS)
LDFLAGS  = -T linker.ld -nostartfiles -nostdlib -Wl,--gc-sections

KERNEL_SRC = src/startup.c src/uart.c src/board.c src/kernel.c

all: $(BUILD)/demo.elf $(BUILD)/tests.elf

$(BUILD)/demo.elf: $(KERNEL_SRC) src/main.c linker.ld | $(BUILD)
	$(CC) $(CFLAGS) $(KERNEL_SRC) src/main.c $(LDFLAGS) -Wl,-Map=$(BUILD)/demo.map -lgcc -o $@
	$(SIZE) $@

$(BUILD)/tests.elf: $(KERNEL_SRC) tests/test_main.c linker.ld | $(BUILD)
	$(CC) $(CFLAGS) $(KERNEL_SRC) tests/test_main.c $(LDFLAGS) -Wl,-Map=$(BUILD)/tests.map -lgcc -o $@

$(BUILD):
	mkdir -p $(BUILD)

run: $(BUILD)/demo.elf
	$(QEMU) -kernel $<

test: $(BUILD)/tests.elf
	./scripts/run_tests.sh

# Proves the inheritance test is meaningful: with inheritance disabled the
# suite MUST fail. (CI runs this as a negative test.)
test-no-inherit:
	$(MAKE) clean
	$(MAKE) EXTRA_CFLAGS=-DRTOS_PRIO_INHERIT=0 $(BUILD)/tests.elf
	./scripts/run_tests.sh --expect-fail

clean:
	rm -rf $(BUILD)

.PHONY: all run test test-no-inherit clean
