// Header-only C++ wrapper (RAII) over /dev/vsensor0
#pragma once
#include "../kernel/vsensor.h"
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdexcept>
#include <string>
#include <vector>
#include <cerrno>
#include <cstring>

class Sensor {
    int fd_;
public:
    explicit Sensor(const std::string& path = "/dev/vsensor0", bool nonblock = false)
        : fd_(::open(path.c_str(), O_RDONLY | (nonblock ? O_NONBLOCK : 0))) {
        if (fd_ < 0) throw std::runtime_error("open " + path + ": " + std::strerror(errno));
    }
    ~Sensor() { if (fd_ >= 0) ::close(fd_); }
    Sensor(const Sensor&) = delete;
    Sensor& operator=(const Sensor&) = delete;

    int fd() const { return fd_; }
    void start() { ctl(VS_IOC_START, nullptr); }
    void stop()  { ctl(VS_IOC_STOP, nullptr); }
    void clear() { ctl(VS_IOC_CLEAR, nullptr); }
    void setRateMs(unsigned ms) { __u32 v = ms; ctl(VS_IOC_SET_RATE, &v); }
    void setThreshold(int milliC) { __s32 v = milliC; ctl(VS_IOC_SET_THRESH, &v); }
    vs_stats stats() { vs_stats s{}; ctl(VS_IOC_GET_STATS, &s); return s; }

    // Blocks up to timeout_ms (-1 = forever). Returns samples read (empty on timeout).
    std::vector<vs_sample> read(size_t max, int timeout_ms = -1) {
        pollfd p{fd_, POLLIN, 0};
        int r = ::poll(&p, 1, timeout_ms);
        if (r <= 0) return {};
        std::vector<vs_sample> v(max);
        ssize_t n = ::read(fd_, v.data(), max * sizeof(vs_sample));
        if (n < 0) { if (errno == EAGAIN || errno == EINTR) return {}; throw std::runtime_error(std::strerror(errno)); }
        v.resize(n / sizeof(vs_sample));
        return v;
    }
private:
    void ctl(unsigned long cmd, void* arg) {
        if (::ioctl(fd_, cmd, arg) < 0) throw std::runtime_error(std::string("ioctl: ") + std::strerror(errno));
    }
};
