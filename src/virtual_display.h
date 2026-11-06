/**
 * @file src/virtual_display.h
 * @brief Declarations for the virtual display session lifecycle.
 */
#pragma once

// standard includes
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

// local includes
#include "config.h"
#include "virtual_display_types.h"

namespace rtsp_stream {
  struct launch_session_t;
}

namespace virtual_display {
  /**
   * @brief Why `begin()` is called.
   */
  enum class start_e {
    launch,  ///< A new app is being launched.
    resume  ///< A client resumes the app that is already running.
  };

  /**
   * @brief Decide whether a session should stream from a virtual display.
   *
   * The global `disabled` policy turns the feature off, even for apps that set "enabled".
   * Otherwise an app that sets "enabled" always uses it and one that sets "disabled" never does,
   * and an app without an override uses it only when the global policy is `always`.
   *
   * @param global The global virtual display policy.
   * @param app_value The app's "virtual-display" value: "enabled", "disabled", or empty to follow the global policy.
   * @return `true` when the session should use a virtual display.
   */
  bool should_use(config::video_t::virtual_display_e global, std::string_view app_value);

  /**
   * @brief Derive the stable identity of a paired client.
   *
   * The PEM certificate of the paired client is used when the session has one, because Moonlight clients send a
   * fixed `uniqueid`. The `uniqueid` is only a fallback for sessions without a certificate.
   *
   * @param session The launch session.
   * @return The identity string fed into the monitor identifier.
   */
  std::string client_identity(const rtsp_stream::launch_session_t &session);

  /**
   * @brief Create the platform backend and clean up after a crash.
   *
   * On Windows the backend is SudoVDA. The driver is opened (and `config::video.adapter_name` selected as its
   * render adapter) only when `state_file` exists, because crash recovery needs it. Otherwise opening is deferred
   * to the first `begin()` that wants a virtual display, so that no driver handle or watchdog ping thread exists
   * while the feature is unused. Other platforms get a backend whose status is always `failed`.
   *
   * When `state_file` records a virtual display that was still active when Sunshine stopped (a crash), that monitor
   * is removed if it is still alive, then re-created briefly so that the display configuration persisted by
   * `display_device` can be reverted against it, and removed again. Never throws; the status is logged.
   *
   * @param state_file File recording the active virtual display, written by `begin()` and deleted by `end()`.
   */
  void init(const std::filesystem::path &state_file);

  /**
   * @brief Start streaming from a virtual display when the session asks for one.
   *
   * Creates a virtual display with the client's requested mode, points `config::video.output_name` at it,
   * forces HDR off and configures it as the primary display (`disabled`, `verify_only` and `ensure_active` are
   * upgraded to `ensure_primary`; `ensure_primary` and `ensure_only_display` are kept). When a virtual display is
   * already active (resume), no new display is created and only the new mode is applied.
   *
   * The configuration is applied in the calling thread and retried for a few seconds while a new monitor
   * enumerates. When it still fails, the virtual display is torn down as in `end()` and `false` is returned.
   *
   * A resume creates a virtual display only when the running app was launched with one, so that an app that fell
   * back to the physical display at launch is not hidden behind a new, empty virtual display.
   *
   * When the driver is not usable, it is (re-)opened first (at most once every few seconds), which opens it lazily
   * and recovers from a driver installed after Sunshine started and from a watchdog failure. An active display whose
   * driver connection was lost is torn down; a launch then creates a new one, a resume does not.
   *
   * A teardown deferred by `end()` that is due but has not run yet runs first.
   *
   * @param session The launch session. Its `enable_hdr` is cleared when the virtual display is used.
   * @param app_value The app's "virtual-display" value.
   * @param start Whether this is a launch or a resume.
   * An active virtual display that the session no longer wants is torn down as in `end()` before returning `false`.
   *
   * @return `true` when the virtual display is used and the display was configured;
   *         `false` when the caller should configure the physical display as usual.
   */
  bool begin(rtsp_stream::launch_session_t &session, std::string_view app_value, start_e start);

  /**
   * @brief Stop using the virtual display.
   *
   * Reverts the display configuration immediately (falling back to the scheduled revert when that fails),
   * then removes the virtual display and restores the original
   * `config::video.output_name`. Does nothing when no virtual display is active.
   *
   * While stream sessions are running (see `session_started()`) the teardown is deferred until the last of them
   * has stopped, so that the display never disappears and `config::video.output_name` never changes under a
   * running capture thread. `active()` stays true until the teardown has run.
   */
  void end();

  /**
   * @brief Restore the display after an app or the last stream stopped.
   *
   * Decides under one lock: an active virtual display is ended as in `end()`, otherwise the physical display
   * configuration is reverted with `display_device::revert_configuration()`.
   *
   * @param keep_virtual_display Keep an active virtual display (and do nothing) instead of ending it, for when the
   *        last stream stopped but the app still runs and may be resumed.
   */
  void end_or_revert(bool keep_virtual_display = false);

  /**
   * @brief Record that a stream session is about to start its capture threads.
   *
   * Must be balanced by one `session_stopped()` once the session's threads have been joined.
   */
  void session_started();

  /**
   * @brief Record that a stream session stopped and its capture threads have been joined.
   *
   * When this was the last running session and `end()` deferred a teardown, the teardown is handed to a
   * background worker and this function returns without waiting for it, so that a slow display revert never runs
   * inside the caller (the stream join, which is guarded by a hang watchdog).
   */
  void session_stopped();

  /**
   * @brief Wait for a pending background teardown and stop the worker.
   *
   * A deferred teardown that is still due runs before this returns. Teardowns due later run in the thread that
   * makes them due. Call it once the stream sessions are gone and before the display device layer shuts down.
   */
  void shutdown();

  /**
   * @brief Report whether a virtual display is in use.
   * @return `true` between a successful `begin()` and `end()`.
   */
  bool active();

#ifdef SUNSHINE_TESTS
  /**
   * @brief Replace the backend and drop any active session state.
   * @param backend Backend used by later calls, or nullptr for none.
   */
  void test_install_backend(std::unique_ptr<backend_t> backend);

  /**
   * @brief Run `init()` with the given backend instead of the platform backend.
   * @param backend Backend used by later calls.
   * @param state_file File recording the active virtual display.
   */
  void test_init(std::unique_ptr<backend_t> backend, const std::filesystem::path &state_file);

  /**
   * @brief Wait until a teardown handed to the background worker has run.
   */
  void test_wait_for_teardown();

  /**
   * @brief Override how long `begin()` retries applying the display configuration.
   * @param window Retry window, normally a few seconds.
   */
  void test_set_apply_window(std::chrono::milliseconds window);

  /**
   * @brief Get the number of stream sessions recorded as running.
   * @return The session count.
   */
  int test_session_count();

  /**
   * @brief Keep the teardown worker from running requested teardowns, to make races reproducible.
   *
   * A held worker still runs a requested teardown when it is stopped (`shutdown()`).
   *
   * @param hold True to hold the worker, false to release it.
   */
  void test_hold_teardown_worker(bool hold);
#endif
}  // namespace virtual_display
