#!/bin/bash
# Functional tests. Requires the module loaded. Run from repo root.
set -e
fail() { echo "FAIL: $1"; exit 1; }
./monitor stop; ./monitor clear; ./monitor rate 10; ./monitor thresh 30000
./monitor start
out=$(./monitor read 20)
[ "$(echo "$out" | wc -l)" -eq 20 ] || fail "read 20 samples"
echo "PASS read"
./monitor stats | grep -q produced || fail "stats"
echo "PASS stats"
# Overflow test: no reader for 3 s at 1 ms period -> 255-entry buffer must overflow
./monitor stop; ./monitor clear; ./monitor rate 1; ./monitor start; sleep 3
d=$(./monitor stats | sed 's/.*dropped=\([0-9]*\).*/\1/')
[ "$d" -gt 0 ] || fail "overflow accounting"
echo "PASS overflow (dropped=$d)"
./monitor stop
cat /proc/vsensor
echo "ALL TESTS PASSED"
