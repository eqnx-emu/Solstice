/**
 * @file src/platform/windows/virtual_display.h
 * @brief SudoVDA virtual display driver backend for Windows.
 */
#pragma once

// standard includes
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// platform includes
#include <windows.h>
#include <winioctl.h>  // CTL_CODE, required by sudovda.h

// lib includes
#include <sudovda/sudovda.h>

// local includes
#include "src/virtual_display_types.h"

namespace platf::vdisplay {
  using virtual_display::driver_status_e;
  using virtual_display::guid_t;
  using virtual_display::mode_t;

  /**
   * @brief Build the SudoVDA add-display IOCTL parameters.
   *
   * The refresh rate is converted to millihertz when below 1000, width and height are rounded down to
   * an even number, and the names are truncated to 13 characters.
   *
   * @param mode The requested display mode.
   * @param guid Monitor identifier.
   * @param name Monitor name reported in the EDID.
   * @param serial Monitor serial number reported in the EDID.
   * @return The IOCTL parameters.
   */
  SUDOVDA::VIRTUAL_DISPLAY_ADD_PARAMS make_add_params(const mode_t &mode, const GUID &guid, std::string_view name, std::string_view serial);

  /**
   * @brief Derive a stable monitor identifier from a client identity.
   *
   * The identifier is the SHA-256 digest of the identity, truncated to 16 bytes and marked as a UUID version 8.
   *
   * @param unique_id The client identity (the paired client's PEM certificate, or its unique id as a fallback).
   *                  Empty identities map to a fixed default identifier.
   * @return A deterministic identifier.
   */
  guid_t guid_from_client_id(std::string_view unique_id);

  /**
   * @brief Check whether the driver protocol is compatible with this build.
   * @param driver The protocol version reported by the driver.
   * @return True when the major versions match and the driver minor is not older than ours.
   */
  bool is_protocol_compatible(const SUDOVDA::SUVDA_PROTOCAL_VERSION &driver);

  /**
   * @brief Get the sleep schedule between retries when opening the driver or looking up a new display name.
   * @return Sleep durations in milliseconds, applied after each failed attempt (the attempt count is size() + 1).
   */
  std::vector<unsigned long> backoff_schedule_ms();

  /**
   * @brief SudoVDA backed virtual display implementation.
   */
  class sudovda_backend_t: public virtual_display::backend_t {
  public:
    sudovda_backend_t() = default;
    sudovda_backend_t(const sudovda_backend_t &) = delete;
    sudovda_backend_t &operator=(const sudovda_backend_t &) = delete;

    /**
     * @brief Stop the ping thread and close the driver handle.
     */
    ~sudovda_backend_t() override;

    /**
     * @brief Open the driver, verify its protocol and start the watchdog ping thread.
     * @return The resulting driver status.
     */
    driver_status_e open() override;

    /**
     * @brief Create a virtual monitor and wait for its GDI name.
     * @param mode The requested display mode.
     * @param guid Stable identifier of the monitor.
     * @param client_serial Serial number reported in the EDID.
     * @return The GDI device name (UTF-8), or nullopt on failure (the monitor is removed on lookup timeout).
     */
    std::optional<std::string> create(const mode_t &mode, const guid_t &guid, std::string_view client_serial) override;

    /**
     * @brief Remove a virtual monitor.
     * @param guid Identifier of the monitor to remove.
     * @return True when the driver removed the monitor.
     */
    bool remove(const guid_t &guid) override;

    /**
     * @brief Select the GPU used by the driver to render the virtual monitors.
     * @param adapter_name DXGI adapter description (UTF-8). Empty is a no-op.
     */
    void set_render_adapter(std::string_view adapter_name);

    /**
     * @brief Get the current driver status.
     * @return The driver status.
     */
    driver_status_e status() const override;

  private:
    /**
     * @brief Close the driver handle and stop the ping thread.
     */
    void close();

    /**
     * @brief Start the watchdog ping thread.
     * @return True when the watchdog was queried successfully.
     */
    bool start_ping_thread();

    HANDLE handle_ = INVALID_HANDLE_VALUE;  ///< Driver handle.
    std::atomic<driver_status_e> status_ = driver_status_e::unknown;  ///< Driver status.
    std::mutex mutex_;  ///< Guards the driver handle.
    std::mutex stop_mutex_;  ///< Guards the ping thread stop flag.
    std::condition_variable stop_cv_;  ///< Wakes the ping thread.
    bool stop_ = false;  ///< Ping thread stop flag.
    std::thread ping_thread_;  ///< Watchdog ping thread.
  };
}  // namespace platf::vdisplay
