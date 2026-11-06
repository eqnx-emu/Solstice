/**
 * @file src/speedtest.h
 * @brief Declarations for the connection capacity test (speed test) support.
 */
#pragma once

// standard includes
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

namespace speedtest {
  constexpr std::size_t block_size = 64 * 1024;  ///< Size in bytes of each chunk written to the client.
  constexpr std::size_t pool_size = 1024 * 1024;  ///< Size in bytes of the shared pseudorandom buffer.
  constexpr int min_seconds = 1;  ///< Shortest accepted test duration, in seconds.
  constexpr int max_seconds = 10;  ///< Longest accepted test duration, in seconds.

  /**
   * @brief Parse and validate the `seconds` query value.
   *
   * @param value Raw query value.
   * @return The duration when it is an integer within [min_seconds, max_seconds], otherwise nullopt.
   */
  std::optional<int> parse_seconds(std::string_view value);

  /**
   * @brief Try to claim the single concurrent speed test slot.
   *
   * @return true when the slot was free and is now owned by the caller.
   */
  bool try_acquire();

  /**
   * @brief Release the speed test slot claimed by a successful try_acquire().
   */
  void release();

  /**
   * @brief Get a block of incompressible pseudorandom bytes.
   * @details The 1 MiB pool is generated once and reused; blocks are slices of it selected by index.
   *
   * @param index Sequence number of the block within the stream.
   * @return A view of block_size bytes that stays valid for the lifetime of the process.
   */
  std::span<const char> block(std::size_t index);

  /**
   * @brief RAII owner of the speed test slot; releases it on every exit path.
   */
  class slot_t {
  public:
    slot_t() = default;

    /**
     * @brief Take ownership of an already acquired slot.
     *
     * @param owned Whether the slot is held.
     */
    explicit slot_t(bool owned):
        owned_ {owned} {
    }

    slot_t(const slot_t &) = delete;
    slot_t &operator=(const slot_t &) = delete;

    /**
     * @brief Move the slot ownership out of another guard.
     *
     * @param other Guard that gives up ownership.
     */
    slot_t(slot_t &&other) noexcept:
        owned_ {other.owned_} {
      other.owned_ = false;
    }

    /**
     * @brief Move-assign, releasing any slot currently held.
     *
     * @param other Guard that gives up ownership.
     * @return This guard.
     */
    slot_t &operator=(slot_t &&other) noexcept {
      if (this != &other) {
        reset();
        owned_ = other.owned_;
        other.owned_ = false;
      }
      return *this;
    }

    ~slot_t() {
      reset();
    }

    /**
     * @brief Release the slot now if it is held.
     */
    void reset() {
      if (owned_) {
        owned_ = false;
        release();
      }
    }

    /**
     * @brief Check whether the guard holds the slot.
     *
     * @return true when the slot is held.
     */
    bool owns() const {
      return owned_;
    }

  private:
    bool owned_ = false;  ///< Whether this guard currently holds the slot.
  };

  /**
   * @brief Outcome of deciding whether a speed test request may run.
   */
  struct admission_t {
    int status = 200;  ///< HTTP status: 200 accepted, 400 bad duration, 409 stream active, 429 busy.
    int seconds = 0;  ///< Validated duration when accepted.
    slot_t slot;  ///< Held slot when accepted; releases on destruction.
  };

  /**
   * @brief Decide whether a speed test request may run.
   * @details Checks are made in order: duration (400), active streaming session (409), concurrency (429).
   *
   * @param seconds_value Raw `seconds` query value.
   * @param active_sessions Number of active streaming sessions.
   * @return The decision, holding the slot when the status is 200.
   */
  admission_t admit(std::string_view seconds_value, int active_sessions);
}  // namespace speedtest
