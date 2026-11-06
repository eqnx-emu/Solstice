/**
 * @file src/speedtest.cpp
 * @brief Definitions for the connection capacity test (speed test) support.
 */
// standard includes
#include <algorithm>
#include <atomic>
#include <charconv>
#include <random>
#include <vector>

// local includes
#include "speedtest.h"

namespace speedtest {
  namespace {
    std::atomic_flag busy = ATOMIC_FLAG_INIT;  ///< Set while a speed test owns the slot.

    /**
     * @brief Get the shared pseudorandom pool, generating it on first use.
     *
     * @return The pool.
     */
    const std::vector<char> &pool() {
      static const std::vector<char> data = [] {
        std::vector<char> bytes(pool_size);
        std::mt19937_64 engine {std::random_device {}()};
        for (std::size_t i = 0; i < bytes.size(); i += sizeof(std::uint64_t)) {
          const auto word = engine();
          std::copy_n(reinterpret_cast<const char *>(&word), sizeof(word), bytes.begin() + i);
        }
        return bytes;
      }();
      return data;
    }
  }  // namespace

  std::optional<int> parse_seconds(std::string_view value) {
    int seconds = 0;
    const auto *end = value.data() + value.size();
    const auto [ptr, ec] = std::from_chars(value.data(), end, seconds);
    if (value.empty() || ec != std::errc {} || ptr != end || seconds < min_seconds || seconds > max_seconds) {
      return std::nullopt;
    }
    return seconds;
  }

  bool try_acquire() {
    return !busy.test_and_set(std::memory_order_acquire);
  }

  void release() {
    busy.clear(std::memory_order_release);
  }

  std::span<const char> block(std::size_t index) {
    const auto offset = (index * block_size) % pool_size;
    return std::span<const char> {pool()}.subspan(offset, block_size);
  }

  admission_t admit(std::string_view seconds_value, int active_sessions) {
    admission_t result;
    const auto seconds = parse_seconds(seconds_value);
    if (!seconds) {
      result.status = 400;
    } else if (active_sessions > 0) {
      result.status = 409;
    } else if (!try_acquire()) {
      result.status = 429;
    } else {
      result.seconds = *seconds;
      result.slot = slot_t {true};
    }
    return result;
  }
}  // namespace speedtest
