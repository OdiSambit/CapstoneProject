CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pthread
.PHONY: all kernel apps clean
all: kernel apps
kernel:
	$(MAKE) -C kernel
apps: monitor logger_daemon
monitor: app/monitor.cpp lib/libsensor.hpp kernel/vsensor.h
	$(CXX) $(CXXFLAGS) $< -o $@
logger_daemon: app/logger_daemon.cpp lib/libsensor.hpp kernel/vsensor.h
	$(CXX) $(CXXFLAGS) $< -o $@
clean:
	$(MAKE) -C kernel clean
	rm -f monitor logger_daemon vsensor.log
