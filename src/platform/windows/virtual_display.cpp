/**
 * @file src/platform/windows/virtual_display.cpp
 * @brief SudoVDA virtual display driver backend for Windows.
 *
 * Adapted from the SudoVDA integration in Apollo (GPL-3.0).
 */
// standard includes
#include <algorithm>
#include <chrono>
#include <cstring>

// platform includes
#include <dxgi.h>
#include <wrl/client.h>

// local includes
#include "src/crypto.h"
#include "src/logging.h"
#include "src/platform/windows/utf_utils.h"
#include "src/platform/windows/virtual_display.h"

using namespace std::literals;

namespace platf::vdisplay {
  namespace {
    constexpr auto monitor_name = "Solstice"sv;  ///< Monitor name reported in the EDID.
    constexpr size_t edid_text_max = 13;  ///< Maximum EDID text length (field is 14 bytes with terminator).

    /**
     * @brief Convert the neutral identifier to a Windows GUID.
     * @param guid The neutral identifier.
     * @return The Windows GUID with the same bytes.
     */
    GUID to_win_guid(const guid_t &guid) {
      GUID out {};
      static_assert(sizeof(GUID) == 16);
      std::memcpy(&out, guid.bytes.data(), sizeof(out));
      return out;
    }

    /**
     * @brief Copy a string into a fixed EDID text field, truncating it.
     * @param dest The destination field.
     * @param src The source text.
     */
    void copy_edid_text(CHAR (&dest)[14], std::string_view src) {
      const auto n = std::min(src.size(), edid_text_max);
      std::memcpy(dest, src.data(), n);
      dest[n] = '\0';
    }
  }  // namespace

  SUDOVDA::VIRTUAL_DISPLAY_ADD_PARAMS make_add_params(const mode_t &mode, const GUID &guid, std::string_view name, std::string_view serial) {
    SUDOVDA::VIRTUAL_DISPLAY_ADD_PARAMS params {};
    params.Width = mode.width & ~1u;
    params.Height = mode.height & ~1u;
    params.RefreshRate = mode.fps < 1000 ? mode.fps * 1000 : mode.fps;
    params.MonitorGuid = guid;
    copy_edid_text(params.DeviceName, name);
    copy_edid_text(params.SerialNumber, serial);
    return params;
  }

  guid_t guid_from_client_id(std::string_view unique_id) {
    guid_t guid;
    if (unique_id.empty()) {
      constexpr auto fixed = "solstice-default"sv;
      static_assert(fixed.size() == 16);
      std::copy(fixed.begin(), fixed.end(), guid.bytes.begin());
      return guid;
    }

    const auto digest = crypto::hash(unique_id);
    std::copy_n(digest.begin(), guid.bytes.size(), guid.bytes.begin());
    guid.bytes[6] = (guid.bytes[6] & 0x0f) | 0x80;  // UUID version 8 (custom)
    guid.bytes[8] = (guid.bytes[8] & 0x3f) | 0x80;  // RFC 4122 variant
    return guid;
  }

  bool is_protocol_compatible(const SUDOVDA::SUVDA_PROTOCAL_VERSION &driver) {
    return SUDOVDA::isProtocolCompatible(driver);
  }

  sudovda_backend_t::~sudovda_backend_t() {
    close();
  }

  void sudovda_backend_t::close() {
    {
      std::lock_guard lg {stop_mutex_};
      stop_ = true;
    }
    stop_cv_.notify_all();
    if (ping_thread_.joinable() && ping_thread_.get_id() != std::this_thread::get_id()) {
      ping_thread_.join();
    }

    std::lock_guard lg {mutex_};
    if (handle_ != INVALID_HANDLE_VALUE) {
      CloseHandle(handle_);
      handle_ = INVALID_HANDLE_VALUE;
    }
  }

  driver_status_e sudovda_backend_t::open() {
    close();
    {
      std::lock_guard lg {stop_mutex_};
      stop_ = false;
    }

    HANDLE handle = INVALID_HANDLE_VALUE;
    for (DWORD retry_interval = 20;; retry_interval *= 2) {
      handle = SUDOVDA::OpenDevice(&SUDOVDA::SUVDA_INTERFACE_GUID);
      if (handle != INVALID_HANDLE_VALUE) {
        break;
      }
      if (retry_interval >= 320) {
        BOOST_LOG(warning) << "SudoVDA: failed to open the virtual display driver"sv;
        status_ = driver_status_e::failed;
        return status_;
      }
      Sleep(retry_interval);
    }

    if (!SUDOVDA::CheckProtocolCompatible(handle)) {
      BOOST_LOG(warning) << "SudoVDA: driver protocol is not compatible with this build"sv;
      CloseHandle(handle);
      status_ = driver_status_e::version_incompatible;
      return status_;
    }

    {
      std::lock_guard lg {mutex_};
      handle_ = handle;
    }
    status_ = driver_status_e::ok;

    if (!start_ping_thread()) {
      BOOST_LOG(warning) << "SudoVDA: failed to query the driver watchdog"sv;
    }
    return status_;
  }

  bool sudovda_backend_t::start_ping_thread() {
    SUDOVDA::VIRTUAL_DISPLAY_GET_WATCHDOG_OUT watchdog {};
    {
      std::lock_guard lg {mutex_};
      if (handle_ == INVALID_HANDLE_VALUE || !SUDOVDA::GetWatchdogTimeout(handle_, watchdog)) {
        return false;
      }
    }

    BOOST_LOG(debug) << "SudoVDA: watchdog timeout "sv << watchdog.Timeout << "s"sv;
    if (watchdog.Timeout == 0) {
      return true;
    }

    const auto interval = std::chrono::milliseconds {watchdog.Timeout * 1000 / 3};
    ping_thread_ = std::thread([this, interval]() {
      int fail_count = 0;
      for (;;) {
        {
          std::unique_lock lg {stop_mutex_};
          if (stop_cv_.wait_for(lg, interval, [this]() {
                return stop_;
              })) {
            return;
          }
        }

        std::lock_guard lg {mutex_};
        if (handle_ == INVALID_HANDLE_VALUE) {
          return;
        }
        if (SUDOVDA::PingDriver(handle_)) {
          fail_count = 0;
          continue;
        }
        if (++fail_count > 3) {
          BOOST_LOG(warning) << "SudoVDA: watchdog ping failed, dropping the driver connection"sv;
          CloseHandle(handle_);
          handle_ = INVALID_HANDLE_VALUE;
          status_ = driver_status_e::watchdog_failed;
          return;
        }
      }
    });
    return true;
  }

  std::optional<std::string> sudovda_backend_t::create(const mode_t &mode, const guid_t &guid, std::string_view client_serial) {
    const auto win_guid = to_win_guid(guid);
    const auto params = make_add_params(mode, win_guid, monitor_name, client_serial);

    SUDOVDA::VIRTUAL_DISPLAY_ADD_OUT output {};
    {
      std::lock_guard lg {mutex_};
      if (handle_ == INVALID_HANDLE_VALUE) {
        return std::nullopt;
      }
      if (!SUDOVDA::AddVirtualDisplay(handle_, params.Width, params.Height, params.RefreshRate, win_guid, params.DeviceName, params.SerialNumber, output)) {
        BOOST_LOG(warning) << "SudoVDA: failed to add the virtual display"sv;
        return std::nullopt;
      }
    }

    wchar_t device_name[CCHDEVICENAME] {};
    for (DWORD retry_interval = 20;; retry_interval *= 2) {
      if (SUDOVDA::GetAddedDisplayName(output, device_name)) {
        break;
      }
      if (retry_interval >= 320) {
        BOOST_LOG(warning) << "SudoVDA: cannot get the name of the new virtual display, removing it"sv;
        remove(guid);
        return std::nullopt;
      }
      Sleep(retry_interval);
    }

    auto name = utf_utils::to_utf8(device_name);
    BOOST_LOG(info) << "SudoVDA: virtual display "sv << name << " created ("sv << params.Width << 'x' << params.Height << '@' << params.RefreshRate << " mHz)"sv;
    return name;
  }

  bool sudovda_backend_t::remove(const guid_t &guid) {
    std::lock_guard lg {mutex_};
    if (handle_ == INVALID_HANDLE_VALUE) {
      return false;
    }
    return SUDOVDA::RemoveVirtualDisplay(handle_, to_win_guid(guid));
  }

  void sudovda_backend_t::set_render_adapter(std::string_view adapter_name) {
    if (adapter_name.empty()) {
      return;
    }

    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
      return;
    }

    const auto wanted = utf_utils::from_utf8(std::string {adapter_name});
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    for (UINT i = 0; SUCCEEDED(factory->EnumAdapters(i, &adapter)); ++i) {
      DXGI_ADAPTER_DESC desc;
      if (FAILED(adapter->GetDesc(&desc)) || std::wstring_view {desc.Description} != wanted) {
        continue;
      }

      std::lock_guard lg {mutex_};
      if (handle_ != INVALID_HANDLE_VALUE && SUDOVDA::SetRenderAdapter(handle_, desc.AdapterLuid)) {
        return;
      }
    }
    BOOST_LOG(warning) << "SudoVDA: could not set render adapter "sv << adapter_name;
  }

  driver_status_e sudovda_backend_t::status() const {
    return status_;
  }
}  // namespace platf::vdisplay
