#!/bin/bash
set -e
sudo insmod kernel/vsensor.ko period_ms=${1:-100}
sudo chmod 666 /dev/vsensor0
echo "loaded; see: dmesg | tail, cat /proc/vsensor"
