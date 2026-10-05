# vsensor-hub: Virtual Interrupt-Driven Sensor (Linux Kernel Module + C++ Stack)

A Linux character device driver (C) that simulates a memory-mapped, interrupt-driven temperature sensor, with a C++ library, CLI and multithreaded logging daemon on top. No physical hardware needed.

## Architecture
```
 monitor (CLI)      logger_daemon (reader thread -> queue -> logger thread)
        \                 /
         libsensor.hpp  (C++ RAII wrapper)
                  |
        /dev/vsensor0  (user/kernel boundary)
                  |
 vsensor.ko: hrtimer "IRQ" -> ring buffer -> wait queue -> read/poll/ioctl/fasync, /proc/vsensor
```
Details: [docs/architecture.md](docs/architecture.md), [docs/register_map.md](docs/register_map.md)

## Features
- Char device `/dev/vsensor0` (`alloc_chrdev_region`, `cdev`, `class_create`, `device_create`)
- hrtimer-driven simulated interrupt generating samples (15-35 C, pseudo-random)
- Kernel ring buffer with overflow/drop accounting
- Blocking / non-blocking `read`, `poll`/`select` support, SIGIO via `fasync` on threshold alert
- `ioctl` control: start, stop, rate, threshold, stats, clear
- Spinlock (IRQ-safe) + mutex synchronization, documented rationale
- `/proc/vsensor` live statistics; module parameter `period_ms`
- C++17 library, CLI and 2-thread producer/consumer logger daemon

## Prerequisites
Linux (tested design for kernel 5.x-6.x; version guards included), `gcc`, `g++`, `make`, kernel headers.
```bash
sudo apt install build-essential linux-headers-$(uname -r)    # Debian/Ubuntu
```
Note: use a VM or native Linux. WSL2 default kernels usually lack headers; Secure Boot may block unsigned modules.

## Build, load, run
```bash
make                       # builds kernel/vsensor.ko, ./monitor, ./logger_daemon
./scripts/load_module.sh   # insmod + permissions (optional arg: period ms)
./monitor rate 100
./monitor thresh 30000     # alert above 30.000 C
./monitor start
./monitor read 10          # print 10 samples
./monitor stats
cat /proc/vsensor
./logger_daemon out.csv    # Ctrl+C to stop; CSV log written
./scripts/unload_module.sh
```

## Sample output
```
t=1234.567s temp=24.31C
t=1234.667s temp=31.02C ALERT
produced=120 consumed=120 dropped=0 alerts=14
```

## Testing
`./tests/run_tests.sh` (module loaded): read count, stats, ring-buffer overflow accounting. Check `dmesg` for kernel messages.

## Repository layout
```
kernel/  vsensor.c vsensor.h Makefile     lib/  libsensor.hpp
app/     monitor.cpp logger_daemon.cpp    scripts/  tests/  docs/
```

## Limitations / future work
- Single device instance; simulated data only
- Add `mmap` of the ring buffer and `read()` vs `mmap` benchmark
- Move sample work to a tasklet/workqueue (bottom half); add device-tree/platform-driver binding

## Author / License
Your Name, Roll No. — GPL-2.0 (kernel module), MIT for user space.
