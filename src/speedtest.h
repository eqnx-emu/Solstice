/**
 * @file src/speedtest.h
 * @brief Declarations for the connection capacity test (speed test) support.
 */
#pragma once

// standard includes
#include <chrono>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace speedtest {
  constexpr std::size_t block_size = 64 * 1024;  ///< Size in bytes of each chunk written to the client.
  constexpr std::size_t pool_size = 1024 * 1024;  ///< Size in bytes of the shared pseudorandom buffer.
  constexpr int min_seconds = 1;  ///< Shortest accepted test duration, in seconds.
  constexpr int max_seconds = 10;  ///< Longest accepted test duration, in seconds.
  constexpr std::size_t queue_depth = 3;  ///< Blocks kept queued ahead so a single round trip does not cap throughput.
  constexpr std::chrono::seconds stall_grace {10};  ///< Time past the deadline that queued blocks and the terminator get before a stalled stream is cut off.
  constexpr std::string_view chunk_trailer {"\r\n"};  ///< Bytes that close the data of a chunk.
  constexpr std::string_view last_chunk {"0\r\n\r\n"};  ///< Terminating zero-size chunk with an empty trailer section.

  /**
   * @brief Next action of the speed test stream.
   */
  enum class step_e {
    send_block,  ///< Queue another data block.
    wait,  ///< Wait for a queued chunk to complete.
    finish  ///< Queue the terminating chunk.
  };

  /**
   * @brief Format the size line that starts a chunk of `Transfer-Encoding: chunked`.
   *
   * @param size Number of data bytes in the chunk.
   * @return The size in lowercase hexadecimal followed by CRLF.
   */
  std::string chunk_header(std::size_t size);

  /**
   * @brief Estimate how long sending some bytes takes at the rate measured so far.
   *
   * @param queued_bytes Bytes that still have to be sent.
   * @param completed_bytes Bytes whose send has completed since the stream started.
   * @param elapsed Time since the stream started.
   * @return The estimate, or zero while no rate has been measured.
   */
  std::chrono::nanoseconds drain_time(std::size_t queued_bytes, std::size_t completed_bytes, std::chrono::nanoseconds elapsed);

  /**
   * @brief Decide what the stream does next.
   * @details No block is queued once the deadline is reached or would be passed before the queued blocks and the
   * new one drain (`drain`), or while stopping. The terminator is queued once nothing is in flight anymore.
   *
   * @param now Current time.
   * @param deadline Time at which the test ends.
   * @param in_flight Chunks queued whose send has not completed.
   * @param drain Estimated time to send the queued blocks plus one more (see drain_time()).
   * @param stopping Whether the server is shutting down.
   * @return The next step.
   */
  step_e next_step(std::chrono::steady_clock::time_point now, std::chrono::steady_clock::time_point deadline, std::size_t in_flight, std::chrono::nanoseconds drain, bool stopping);

  /**
   * @brief Get the time at which a stream that has not finished is cut off.
   *
   * @param deadline Time at which the test ends.
   * @return The deadline plus stall_grace.
   */
  std::chrono::steady_clock::time_point cutoff_time(std::chrono::steady_clock::time_point deadline);

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
   * @details Checks are made in order: duration (400), active streaming session or display configuration in
   * progress (409), concurrency (429).
   *
   * @param seconds_value Raw `seconds` query value.
   * @param active_sessions Number of active streaming sessions.
   * @param display_configuring Whether a virtual display is being set up, which stalls the HTTPS thread.
   * @return The decision, holding the slot when the status is 200.
   */
  admission_t admit(std::string_view seconds_value, int active_sessions, bool display_configuring = false);
}  // namespace speedtest
