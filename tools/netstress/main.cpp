/**
 * @file tools/netstress/main.cpp
 * @brief Borderless fullscreen window of random noise and a moving square, used to saturate the stream bitrate.
 * @details Launched by the synthetic "Solstice Network Test" app. Random noise is incompressible, so the encoder
 * spends its whole bitrate budget, which lets clients check the connection against the bitrate they selected.
 * Usage: `solstice-netstress.exe [--fps N]`. The frame rate is `--fps`, else `SUNSHINE_CLIENT_FPS`, else 60, clamped to 10-240. Esc or closing the window exits.
 */
// standard includes
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <string>
#include <thread>
#include <vector>

// local includes
#include "fps.h"

// platform includes
// clang-format off
#include <windows.h>  // must precede shellapi.h
#include <shellapi.h>
#include <timeapi.h>
// clang-format on

namespace {
  /**
   * @brief Resolve the frame rate from `--fps N`, then `SUNSHINE_CLIENT_FPS`, then the default.
   *
   * @return The frame rate in frames per second.
   */
  int parse_fps() {
    std::string cli;
    int argc = 0;
    if (LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc); argv) {
      for (int i = 1; i + 1 < argc; ++i) {
        if (std::wcscmp(argv[i], L"--fps") == 0) {
          cli.clear();
          for (const wchar_t *c = argv[i + 1]; *c; ++c) {
            cli.push_back(*c < 128 ? static_cast<char>(*c) : '?');  // non-ASCII makes the value invalid
          }
        }
      }
      LocalFree(argv);
    }
    const char *env = std::getenv("SUNSHINE_CLIENT_FPS");
    return netstress::resolve_fps(cli, env ? env : "");
  }

  /**
   * @brief Opt in to per-monitor DPI awareness so the window is never bitmap-scaled (which would smooth the noise).
   * @details Uses per-monitor v2 when available and falls back to system DPI awareness.
   */
  void enable_dpi_awareness() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    using set_context_fn = BOOL(WINAPI *)(HANDLE);
    if (auto set_context = reinterpret_cast<set_context_fn>(reinterpret_cast<void *>(GetProcAddress(user32, "SetProcessDpiAwarenessContext")))) {
      const auto per_monitor_aware_v2 = reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4));
      if (set_context(per_monitor_aware_v2)) {
        return;
      }
    }
    SetProcessDPIAware();
  }

  bool running = true;  ///< Cleared when the window should exit.

  /**
   * @brief Window procedure: exits on Esc or close.
   *
   * @param hwnd Window handle.
   * @param msg Message identifier.
   * @param wparam First message parameter.
   * @param lparam Second message parameter.
   * @return Message result.
   */
  LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
      case WM_KEYDOWN:
        if (wparam == VK_ESCAPE) {
          running = false;
        }
        return 0;
      case WM_CLOSE:
      case WM_DESTROY:
        running = false;
        return 0;
      case WM_ERASEBKGND:
        return 1;
      default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
  }
}  // namespace

/**
 * @brief Program entry point.
 *
 * @param instance Module instance handle.
 * @return Process exit code.
 */
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  const int fps = parse_fps();
  enable_dpi_awareness();  // before any metrics are read
  // Primary display: when the virtual display is in use, display_device makes it the primary one.
  const int width = GetSystemMetrics(SM_CXSCREEN);
  const int height = GetSystemMetrics(SM_CYSCREEN);

  WNDCLASSW wc {};
  wc.lpfnWndProc = window_proc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.lpszClassName = L"SolsticeNetStress";
  if (!RegisterClassW(&wc)) {
    return 1;
  }

  HWND hwnd = CreateWindowExW(WS_EX_TOPMOST, wc.lpszClassName, L"Solstice Network Test", WS_POPUP | WS_VISIBLE, 0, 0, width, height, nullptr, nullptr, instance, nullptr);
  if (!hwnd) {
    return 1;
  }
  ShowCursor(FALSE);

  BITMAPINFO bmi {};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = width;
  bmi.bmiHeader.biHeight = -height;  // top-down
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  void *bits = nullptr;
  HDC window_dc = GetDC(hwnd);
  HDC memory_dc = CreateCompatibleDC(window_dc);
  HBITMAP dib = CreateDIBSection(window_dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!dib || !bits) {
    return 1;
  }
  HGDIOBJ previous_bitmap = SelectObject(memory_dc, dib);
  SetForegroundWindow(hwnd);

  auto *pixels = static_cast<std::uint64_t *>(bits);
  const std::size_t pair_count = static_cast<std::size_t>(width) * height / 2;
  std::uint64_t state = 0x9E3779B97F4A7C15ULL ^ static_cast<std::uint64_t>(GetTickCount64());

  const int square = height / 6;
  int x = 0;
  int y = height / 3;
  int dx = std::max(4, width / 240);
  int dy = std::max(3, height / 270);

  const auto frame_time = std::chrono::nanoseconds {1'000'000'000LL / fps};
  auto next_frame = std::chrono::steady_clock::now();

  timeBeginPeriod(1);  // 1 ms timer resolution so sleep_until can hold high --fps values
  while (running) {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        running = false;
      }
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    if (!running) {
      break;
    }

    // xorshift64 noise over the whole frame: incompressible for the encoder
    for (std::size_t i = 0; i < pair_count; ++i) {
      state ^= state << 13;
      state ^= state >> 7;
      state ^= state << 17;
      pixels[i] = state;
    }

    // moving square, bouncing off the screen edges
    x += dx;
    y += dy;
    if (x < 0 || x + square > width) {
      dx = -dx;
      x += 2 * dx;
    }
    if (y < 0 || y + square > height) {
      dy = -dy;
      y += 2 * dy;
    }
    RECT rect {x, y, x + square, y + square};
    FillRect(memory_dc, &rect, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));

    BitBlt(window_dc, 0, 0, width, height, memory_dc, 0, 0, SRCCOPY);

    next_frame += frame_time;
    const auto now = std::chrono::steady_clock::now();
    if (next_frame > now) {
      std::this_thread::sleep_until(next_frame);
    } else {
      next_frame = now;  // fell behind, do not try to catch up
    }
  }

  timeEndPeriod(1);
  ShowCursor(TRUE);
  SelectObject(memory_dc, previous_bitmap);
  DeleteObject(dib);
  DeleteDC(memory_dc);
  ReleaseDC(hwnd, window_dc);
  DestroyWindow(hwnd);
  return 0;
}
