#!/usr/bin/env bash
# Runs the kernel test firmware in QEMU and checks the exit status.
#   ./scripts/run_tests.sh               -> expects all tests to pass
#   ./scripts/run_tests.sh --expect-fail -> expects at least one failure
set -u
ELF="build/tests.elf"
OUT="build/test_output.txt"

timeout 30 qemu-system-arm -machine lm3s6965evb -cpu cortex-m3 -nographic \
    -semihosting-config enable=on,target=native -kernel "$ELF" > "$OUT" 2>&1
status=$?
cat "$OUT"

if [ "${1:-}" = "--expect-fail" ]; then
    if [ $status -eq 1 ] && grep -q "FAIL\] priority_inheritance" "$OUT"; then
        echo "OK: priority-inheritance test fails when inheritance is disabled (as it should)"
        exit 0
    fi
    echo "UNEXPECTED: negative test did not fail as expected (status=$status)"
    exit 1
fi

if [ $status -eq 0 ] && grep -q "RESULT:" "$OUT"; then
    exit 0
fi
echo "TESTS FAILED (qemu exit status $status)"
exit 1
