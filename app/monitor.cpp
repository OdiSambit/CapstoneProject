// CLI: monitor <start|stop|clear|rate MS|thresh MC|stats|read N|watch>
#include "../lib/libsensor.hpp"
#include <cstdio>
#include <cstdlib>
#include <csignal>

static volatile sig_atomic_t quit = 0;
static void on_int(int) { quit = 1; }

static void usage() {
    puts("usage: monitor <start|stop|clear|rate <ms>|thresh <milliC>|stats|read <N>|watch>");
}

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 1; }
    std::string c = argv[1];
    try {
        Sensor s;
        if (c == "start") s.start();
        else if (c == "stop") s.stop();
        else if (c == "clear") s.clear();
        else if (c == "rate" && argc > 2) s.setRateMs(atoi(argv[2]));
        else if (c == "thresh" && argc > 2) s.setThreshold(atoi(argv[2]));
        else if (c == "stats") {
            auto st = s.stats();
            printf("produced=%llu consumed=%llu dropped=%llu alerts=%llu\n",
                   (unsigned long long)st.produced, (unsigned long long)st.consumed,
                   (unsigned long long)st.dropped, (unsigned long long)st.alerts);
        } else if (c == "read" || c == "watch") {
            size_t want = (c == "read" && argc > 2) ? (size_t)atoi(argv[2]) : (size_t)-1, got = 0;
            signal(SIGINT, on_int);
            while (!quit && got < want) {
                for (auto& v : s.read(8, 500)) {
                    printf("t=%.3fs temp=%.2fC %s\n", v.ts_ns / 1e9, v.value / 1000.0, v.alert ? "ALERT" : "");
                    if (++got >= want) break;
                }
            }
        } else { usage(); return 1; }
    } catch (const std::exception& e) { fprintf(stderr, "error: %s\n", e.what()); return 2; }
    return 0;
}
