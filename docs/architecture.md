# Architecture
```
 monitor (CLI)      logger_daemon (reader thread -> queue -> logger thread)
        \                 /
         libsensor.hpp (RAII C++ wrapper, poll + ioctl)
                  |
        /dev/vsensor0   ---- user/kernel boundary (syscalls)
                  |
 vsensor.ko: file_operations {open, release, read, poll, ioctl, fasync}
   hrtimer callback ("interrupt") -> ring buffer (256 entries) -> wait queue wake-up
   /proc/vsensor statistics
```
## Design decisions
- **hrtimer as interrupt source**: models a periodic hardware interrupt; the callback runs in atomic context and cannot sleep.
- **Spinlock (irqsave) on ring buffer + stats**: shared between the timer callback (atomic) and process context; a mutex is illegal in the callback.
- **Mutex on configuration (ioctl)**: process context only; `hrtimer_cancel()` may wait for the callback, so it must not be called under the spinlock.
- **Wait queue + poll**: readers sleep instead of busy-waiting; `O_NONBLOCK` returns `-EAGAIN`.
- **Overflow policy**: drop newest and count it (visible in stats).
- **fasync/SIGIO**: asynchronous notification when threshold is crossed.
- **User-space producer/consumer**: the daemon decouples slow disk I/O from reading so the kernel buffer does not overflow.
