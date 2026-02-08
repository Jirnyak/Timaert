#pragma once

#include <algorithm>
#include <chrono>
#include <array>
#include <cstring>

namespace debug {

struct SystemTiming {
    char name[32]{};
    double time_us = 0.0;      // Current frame time in microseconds
    double avg_time_us = 0.0;  // Rolling average
    double max_time_us = 0.0;  // Max in recent history
    double min_time_us = 1e9;  // Min in recent history
    int sample_count = 0;

    std::chrono::high_resolution_clock::time_point start_time;

    void set_name(const char* n) {
        std::strncpy(name, n, sizeof(name) - 1);
    }

    void begin() {
        start_time = std::chrono::high_resolution_clock::now();
    }

    void end() {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);
        time_us = static_cast<double>(duration.count()) / 1000.0;

        // Update stats with exponential moving average
        constexpr double alpha = 0.1;
        if (sample_count == 0) {
            avg_time_us = time_us;
        } else {
            avg_time_us = alpha * time_us + (1.0 - alpha) * avg_time_us;
        }

        max_time_us = std::max(time_us, max_time_us);
        min_time_us = std::min(time_us, min_time_us);

        sample_count++;

        // Reset min/max periodically
        if (sample_count % 300 == 0) {
            max_time_us = time_us;
            min_time_us = time_us;
        }
    }
};

class SystemProfiler {
public:
    static constexpr std::size_t MAX_SYSTEMS = 32;

    void begin(const char* system_name) {
        auto* timing = get_or_create(system_name);
        if (timing)
            timing->begin();
    }

    void end(const char* system_name) {
        auto* timing = find(system_name);
        if (timing)
            timing->end();
    }

    [[nodiscard]] const SystemTiming* timings() const {
        return timings_.data();
    }
    [[nodiscard]] std::size_t timing_count() const {
        return timing_count_;
    }

    void reset() {
        for (std::size_t i = 0; i < timing_count_; ++i) {
            timings_[i].avg_time_us = 0;
            timings_[i].max_time_us = 0;
            timings_[i].min_time_us = 1e9;
            timings_[i].sample_count = 0;
        }
    }

    [[nodiscard]] double total_frame_time_us() const {
        double total = 0;
        for (std::size_t i = 0; i < timing_count_; ++i) {
            total += timings_[i].time_us;
        }
        return total;
    }

private:
    std::array<SystemTiming, MAX_SYSTEMS> timings_{};
    std::size_t timing_count_ = 0;

    SystemTiming* find(const char* name) {
        for (std::size_t i = 0; i < timing_count_; ++i) {
            if (std::strcmp(timings_[i].name, name) == 0) {
                return &timings_[i];
            }
        }
        return nullptr;
    }

    SystemTiming* get_or_create(const char* name) {
        auto* existing = find(name);
        if (existing)
            return existing;

        if (timing_count_ >= MAX_SYSTEMS)
            return nullptr;

        auto* timing = &timings_[timing_count_++];
        timing->set_name(name);
        return timing;
    }
};

// RAII helper for profiling scopes
class ScopedProfile {
public:
    ScopedProfile(SystemProfiler& profiler, const char* name) : profiler_(profiler), name_(name) {
        profiler_.begin(name_);
    }

    ~ScopedProfile() {
        profiler_.end(name_);
    }

    ScopedProfile(const ScopedProfile&) = delete;
    ScopedProfile& operator=(const ScopedProfile&) = delete;
    ScopedProfile(ScopedProfile&&) = delete;
    ScopedProfile& operator=(ScopedProfile&&) = delete;

private:
    SystemProfiler& profiler_;
    const char* name_;
};

#define PROFILE_SCOPE(profiler, name) debug::ScopedProfile _profile_##__LINE__(profiler, name)

}  // namespace debug
