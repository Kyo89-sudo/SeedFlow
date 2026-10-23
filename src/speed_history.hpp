#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace sf {
struct SpeedSample { std::uint64_t time = 0; std::int64_t down = 0, up = 0; };
class SpeedHistory {
public:
    static constexpr std::size_t capacity = 301;
    static constexpr std::uint64_t window_ms = 300000;
    void append(std::uint64_t time, std::int64_t down, std::int64_t up) {
        SpeedSample value{time, std::max<std::int64_t>(0, down), std::max<std::int64_t>(0, up)};
        if (count_) {
            auto& last = samples_[(next_ + capacity - 1) % capacity];
            if (time < last.time) return;
            if (time == last.time) { last = value; return; }
        }
        samples_[next_] = value;
        next_ = (next_ + 1) % capacity;
        count_ = std::min(count_ + 1, capacity);
    }
    std::size_t size() const { return count_; }
    SpeedSample const& at(std::size_t index) const {
        if (index >= count_) throw std::out_of_range("SpeedHistory index");
        return samples_[(next_ + capacity - count_ + index) % capacity];
    }
private:
    std::array<SpeedSample, capacity> samples_{};
    std::size_t count_ = 0, next_ = 0;
};

// Speed history at three resolutions: 1 s (5 min), 6 s averages (30 min) and 24 s
// averages (2 h). Each level keeps SpeedHistory::capacity samples; the graph picks the
// finest level that covers the requested window.
class UsageHistory {
public:
    static constexpr int levels = 3;
    static constexpr std::uint64_t step_ms[levels] = {1000, 6000, 24000};
    void add(std::uint64_t time, std::int64_t down, std::int64_t up) {
        if (have_last_ && time < last_time_) return;
        have_last_ = true; last_time_ = time;
        rings_[0].append(time, down, up);
        for (int l = 1; l < levels; ++l) {
            auto& b = pending_[l];
            std::uint64_t const slot = time / step_ms[l];
            if (b.count && slot != b.slot) flush(l);
            if (!b.count) b.slot = slot;
            b.down += std::max<std::int64_t>(0, down); b.up += std::max<std::int64_t>(0, up); ++b.count;
        }
    }
    // Finest level whose span covers `window_ms`.
    int level_for(std::uint64_t window_ms) const {
        for (int l = 0; l < levels; ++l)
            if (step_ms[l] * (SpeedHistory::capacity - 1) >= window_ms) return l;
        return levels - 1;
    }
    SpeedHistory const& level(int l) const { return rings_[std::clamp(l, 0, levels - 1)]; }
    bool empty() const { return rings_[0].size() == 0; }
private:
    struct Bucket { std::uint64_t slot = 0; std::int64_t down = 0, up = 0; int count = 0; };
    void flush(int l) {
        auto& b = pending_[l];
        // stamped at the end of its slot, so the newest bucket sits at "now"
        rings_[l].append((b.slot + 1) * step_ms[l], b.down / b.count, b.up / b.count);
        b = Bucket{};
    }
    std::array<SpeedHistory, levels> rings_{};
    std::array<Bucket, levels> pending_{};
    std::uint64_t last_time_ = 0;
    bool have_last_ = false;
};
} // namespace sf
