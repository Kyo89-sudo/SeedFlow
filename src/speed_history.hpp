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
} // namespace sf
