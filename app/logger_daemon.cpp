// Producer/consumer logger: reader thread (poll+read) -> queue -> logger thread (file)
#include "../lib/libsensor.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <deque>
#include <mutex>
#include <thread>

static std::atomic<bool> quit{false};
static void on_sig(int) { quit = true; }

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "vsensor.log";
    signal(SIGINT, on_sig); signal(SIGTERM, on_sig);
    try {
        Sensor s;
        s.start();
        std::deque<vs_sample> q; std::mutex m; std::condition_variable cv;

        std::thread reader([&] {
            while (!quit) {
                auto v = s.read(16, 200);
                if (v.empty()) continue;
                { std::lock_guard<std::mutex> g(m); q.insert(q.end(), v.begin(), v.end()); }
                cv.notify_one();
            }
            cv.notify_all();
        });
        std::thread logger([&] {
            FILE* f = fopen(path, "w");
            if (!f) { perror("fopen"); quit = true; return; }
            fprintf(f, "timestamp_s,temp_c,alert\n");
            while (true) {
                std::unique_lock<std::mutex> lk(m);
                cv.wait_for(lk, std::chrono::milliseconds(300), [&] { return !q.empty() || quit; });
                std::deque<vs_sample> batch; batch.swap(q); lk.unlock();
                for (auto& v : batch) fprintf(f, "%.6f,%.3f,%u\n", v.ts_ns / 1e9, v.value / 1000.0, v.alert);
                fflush(f);
                if (quit && batch.empty()) break;
            }
            fclose(f);
        });
        reader.join(); logger.join();
        s.stop();
        auto st = s.stats();
        printf("done: produced=%llu consumed=%llu dropped=%llu alerts=%llu\n",
               (unsigned long long)st.produced, (unsigned long long)st.consumed,
               (unsigned long long)st.dropped, (unsigned long long)st.alerts);
    } catch (const std::exception& e) { fprintf(stderr, "error: %s\n", e.what()); return 2; }
    return 0;
}
