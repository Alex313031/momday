/*------------------------------------------
   Mother's Day Win32
   Copyright (c) 2026 Alex313031
  ------------------------------------------*/

#include "main.h"

#include "globals.h"
#include "resource.h"
#include "utils.h"

HWND mainHwnd = nullptr;

HINSTANCE g_hInstance = nullptr;

int cxClient = 0;
int cyClient = 0;

// Tracks whether the last WM_SIZE minimized the window. Set by WM_SIZE on
// SIZE_MINIMIZED, cleared by the next non-minimize WM_SIZE. Used to decide
// whether the just-arrived size event is "we're coming back from a
// minimize" (restart the tick source) vs. a normal resize (no-op).
static bool s_was_minimized = false;

// Current background color. Defaults to blue;
COLORREF g_bkg_color = RGB_DARKBLUE;

bool g_debug_mode   = is_debug;

// Store handles to main icon since commonly used
HICON kMainIcon  = nullptr;
HICON kSmallIcon = nullptr;

// Whether we have commctl32 5.82 (XP/I.E 6.0)
bool can_use_582_controls = false;

// Smaller hearts scattered around the main center heart. Each rect is
// independently random in width and height (kSatelliteMinSize ..
// kSatelliteMaxSize) and randomly placed inside the client area but not
// overlapping the main heart's bounding rect. The layout is persisted
// across WM_PAINTs (so the satellites don't jitter every frame) and only
// re-rolled when the client size or the main heart rect change - that's
// what s_satellites_dirty tracks. WM_SIZE flips it true, WM_PAINT clears
// it after regenerating.
// MAX_EXTRA_HEARTS lives in utils.h since it's a public-facing knob.
static std::vector<RECT> s_satellite_rects;
static bool s_satellites_dirty = true;

// Re-rolls s_satellite_rects with up to MAX_EXTRA_HEARTS non-overlapping
// rects. Each rect's width and height are drawn independently from
// [kSatelliteMinSize, kSatelliteMaxSize], and placement is rejected if
// the rect would intersect mainRect (so satellites never spill into the
// main heart's space). If a rect can't be placed within a bounded number
// of attempts, it's just skipped - useful when the surrounding margin is
// too crowded or the window is too small.
static void RegenerateSatellites(int clientW, int clientH, const RECT& mainRect) {
  s_satellite_rects.clear();
  if (clientW <= kSatelliteMinSize || clientH <= kSatelliteMinSize) {
    return;
  }
  // Seed once. Subsequent calls keep advancing the same stream so each
  // resize gives a different layout instead of always the same pattern.
  static std::mt19937 rng{std::random_device{}()};
  std::uniform_int_distribution<int> sizeDist(kSatelliteMinSize, kSatelliteMaxSize);
  constexpr int kMaxAttemptsPerHeart = 32;
  for (UINT i = 0; i < MAX_EXTRA_HEARTS; ++i) {
    for (int attempt = 0; attempt < kMaxAttemptsPerHeart; ++attempt) {
      const int w = sizeDist(rng);
      const int h = sizeDist(rng);
      if (w >= clientW || h >= clientH) {
        continue;
      }
      std::uniform_int_distribution<int> xDist(0, clientW - w);
      std::uniform_int_distribution<int> yDist(0, clientH - h);
      const int x = xDist(rng);
      const int y = yDist(rng);
      RECT candidate     = {x, y, x + w, y + h};
      RECT discardedIntx = {0, 0, 0, 0};
      // IntersectRect returns FALSE when the two rects don't intersect,
      // which is exactly what we want for "satellite is outside the main
      // heart". The first arg is required even though we don't use it.
      if (!IntersectRect(&discardedIntx, &candidate, &mainRect)) {
        s_satellite_rects.push_back(candidate);
        break;
      }
    }
  }
}

bool RegisterWndClass(HINSTANCE hInstance, LPCWSTR className) {
  if (kMainIcon == nullptr || kSmallIcon == nullptr) {
    return false;
  }
  WNDCLASSEXW wndclass;
  wndclass.cbSize      = sizeof(WNDCLASSEX);
  // CS_HREDRAW | CS_VREDRAW asks the OS to invalidate the entire client area
  // on any width or height change so the heart re-centers and re-renders at
  // the new client size during a resize without us having to InvalidateRect
  // by hand from WM_SIZE.
  wndclass.style       = CS_HREDRAW | CS_VREDRAW;
  wndclass.lpfnWndProc = WindowProc;
  wndclass.cbClsExtra  = 0;
  wndclass.cbWndExtra  = 0;
  wndclass.hInstance   = hInstance;
  wndclass.hIcon       = kMainIcon;
  wndclass.hCursor     = LoadCursorW(nullptr, IDC_ARROW);
  // No stock brush matches our default bg (there's only black / white /
  // grey / null), and we handle erase + paint ourselves - WM_ERASEBKGND
  // returns TRUE and WM_PAINT fills with g_bkg_color. nullptr here skips
  // the OS's pre-fill entirely so there's no flash of a wrong-colored
  // window before our first WM_PAINT.
  wndclass.hbrBackground = nullptr;
  wndclass.lpszMenuName  = MAKEINTRESOURCEW(IDR_MAIN);
  wndclass.lpszClassName = className;
  wndclass.hIconSm       = kSmallIcon;

  // RegisterClassEx returns an ATOM (typedef unsigned short - really a short
  // pointer left over from Win16 days), 0 on failure. The double cast spells
  // out "this is an ATOM-shaped zero" rather than relying on the implicit
  // promotion from int 0.
  if (RegisterClassExW(&wndclass) == static_cast<ATOM>(static_cast<unsigned short>(0))) {
    return false;
  }
  return true;
}

bool InitWindow(HINSTANCE hInstance, LPCWSTR className, LPCWSTR title, int iCmdShow) {
  static constexpr DWORD exStyle = WS_EX_OVERLAPPEDWINDOW;
  static constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX |
                                 WS_MAXIMIZEBOX | WS_SIZEBOX;

  // Create main window
  mainHwnd = CreateWindowExW(exStyle, className, title, style, CW_USEDEFAULT, CW_USEDEFAULT,
                             CW_WIDTH, CW_HEIGHT, nullptr, nullptr, hInstance, nullptr);

  if (mainHwnd == nullptr) {
    return false;
  }
  ShowWindow(mainHwnd, iCmdShow);
  if (!UpdateWindow(mainHwnd)) {
    return false;
  }
  return true;
}

int APIENTRY wWinMain(HINSTANCE hInstance,
                      HINSTANCE hPrevInstance,
                      LPWSTR lpCmdLine,
                      int iCmdShow) {
  UNREFERENCED_PARAMETER(hPrevInstance);
  g_hInstance = hInstance;
  // Initialize common controls
  INITCOMMONCONTROLSEX icex;
  icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
  icex.dwICC  = ICC_STANDARD_CLASSES;
  InitCommonControlsEx(&icex);
  // Now that comctl32 is initialized, probe its version once for callers
  // that need to gate v5.82+ behavior (notably the status-bar tooltip
  // TOOLINFO size that Win2000's v5.81 doesn't accept).
  can_use_582_controls = IsCommCtrlAtLeast(dwComCtl32TargetVer);

  static const std::wstring name   = GetAppName();
  static const LPCWSTR appTitle    = name.c_str();
  static const LPCWSTR szClassName = MAIN_WNDCLASS;

  kMainIcon  = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_MAIN));
  kSmallIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_SMALL));

  // Register our window class.
  if (!RegisterWndClass(g_hInstance, szClassName)) {
    ErrorBox(nullptr, L"RegisterClassEx Error", L"This program requires Windows NT!");
    return 1;
  }

  // Open our window now
  if (!InitWindow(g_hInstance, szClassName, appTitle, iCmdShow)) {
    return 4;
  }

  HACCEL hAccel = LoadAcceleratorsW(hInstance, MAKEINTRESOURCEW(IDR_MAIN));
  if (hAccel == nullptr) {
    return 5;
  }

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    if (!TranslateAcceleratorW(mainHwnd, hAccel, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
  if (hAccel != nullptr) {
    DestroyAcceleratorTable(hAccel);
  }
  return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_CREATE:
      if (mainHwnd == nullptr) {
        mainHwnd = hWnd; // Prevent race condition in InitApp
      }
      InitApp(hWnd);
      break;
    case WM_TIMER:
      if (wParam == TIMER_HEARTS) {
        // Drive the DrawHeart animation: each tick bumps the shared
        // progress counter and asks for a repaint. Stop invalidating once
        // we've reached the meeting point so we're not doing pointless
        // work for a heart that's already fully drawn.
        if (g_heart_step < g_heart_max_steps) {
          ++g_heart_step;
          InvalidateRect(hWnd, nullptr, FALSE);
        }
      }
      break;
    case WM_APP_AUTOPLAY:
      break;
    case WM_ERASEBKGND:
      // Returning TRUE tells Windows we have handled background erasing
      // ourselves, suppressing the default white fill. We do our own filling
      // in WM_PAINT so the two operations don't race or double-paint.
      return TRUE;
    case WM_GETMINMAXINFO: {
      LPMINMAXINFO pMinMaxInfo = reinterpret_cast<LPMINMAXINFO>(lParam);;
      pMinMaxInfo->ptMinTrackSize.x = 200;
      pMinMaxInfo->ptMinTrackSize.y = 200;
      const int MAXWIDTH            = GetSystemMetrics(SM_CXMAXIMIZED);
      const int MAXHEIGHT           = GetSystemMetrics(SM_CYMAXIMIZED);
      pMinMaxInfo->ptMaxTrackSize.x = MAXWIDTH;
      pMinMaxInfo->ptMaxTrackSize.y = MAXHEIGHT;
      break;
    }
    case WM_PAINT: {
      // WM_ERASEBKGND returned TRUE so Windows skipped its bg fill - we
      // paint g_bkg_color over the dirty region ourselves to keep the
      // canvas blue, then layer the heart on top via DrawHeart (which
      // owns its own DC).
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hWnd, &ps);
      FillRectWithColor(hdc, ps.rcPaint, g_bkg_color);
      EndPaint(hWnd, &ps);

      RECT client;
      GetClientRect(hWnd, &client);
      // Heart is a centered square sized to ~75% of the smaller client
      // dimension so it scales with the window without crowding the edges.
      const LONG side = static_cast<LONG>(std::min(client.right, client.bottom) * 0.75f);
      if (side >= 8) {
        RECT heartRect;
        heartRect.left   = (client.right - side) / 2;
        heartRect.top    = (client.bottom - side) / 2;
        heartRect.right  = heartRect.left + side;
        heartRect.bottom = heartRect.top + side;
        DrawHeart(hWnd, heartRect);

        // Lazy regen: WM_SIZE marked us dirty (or this is the first
        // paint). Recompute the satellite layout against the current
        // client rect and the just-computed main heart rect, then draw
        // each satellite in the same animation step as the main heart.
        if (s_satellites_dirty) {
          RegenerateSatellites(client.right, client.bottom, heartRect);
          s_satellites_dirty = false;
        }
        for (const RECT& r : s_satellite_rects) {
          DrawHeart(hWnd, r);
        }
      }
      break;
    }
    case WM_SIZE: {
      // cxClient / cyClient represent the ants canvas area, not the parent's
      // client area - neither the toolbar (top) nor the status bar (bottom)
      // is drawable space. cyClient gets the status bar height subtracted
      // below once the bar has self-sized for this WM_SIZE.
      cxClient = LOWORD(lParam);
      cyClient = HIWORD(lParam);
      if (wParam == SIZE_MINIMIZED) {
        s_was_minimized = true;
        break;
      } else {
      }
      if (cyClient < 0) {
        cyClient = 0;
      }
      // Client size changed - the main heart rect (and the surrounding
      // free area) move, so the satellite layout has to be rolled fresh
      // on the next WM_PAINT.
      s_satellites_dirty = true;
      break;
    }
    case WM_COMMAND: {
      const int command = LOWORD(wParam);
      switch (command) {
        case IDM_EXIT:
          ShutDownApp();
          break;
        case IDM_ABOUT:
          PlaySoundW(L"SystemNotification", nullptr, SND_ALIAS | SND_ASYNC);
          DialogBoxW(g_hInstance, MAKEINTRESOURCEW(IDD_ABOUTDLG), hWnd, AboutDlgProc);
          break;
        case IDM_HELP:
          LaunchHelp(hWnd);
          break;
        default:
          return DefWindowProcW(hWnd, message, wParam, lParam);
      }
    } break;
    case WM_HELP:
      LaunchHelp(hWnd);
      break;
    case WM_CLOSE:
      ShutDownApp();
      break;
    case WM_QUERYENDSESSION:
      return TRUE;
    case WM_DESTROY:
      KillTimer(hWnd, TIMER_HEARTS);
      PostQuitMessage(0);
      break;
    case WM_NCDESTROY:
      mainHwnd = nullptr;
      break;
    default:
      return DefWindowProcW(hWnd, message, wParam, lParam);
  }
  return 0;
}

bool InitApp(HWND hWnd) {
  if (hWnd == nullptr) {
    return false;
  }
  // ~33 fps tick rate. With g_heart_max_steps == 100 the heart finishes
  // drawing in ~3 seconds. The timer is torn down in WM_DESTROY.
  if (SetTimer(hWnd, TIMER_HEARTS, 30, nullptr) == 0) {
    return false;
  }
  return true;
}

void ShutDownApp() {
  DestroyWindow(mainHwnd);
}

bool LaunchHelp(HWND hWnd) {
  bool success = false;
  if (InfoBox(hWnd, L"Help32", L"No help yet...")) {
    success = true;
  }
  return success;
}

INT_PTR CALLBACK AboutDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
  UNREFERENCED_PARAMETER(lParam);
  switch (message) {
    case WM_INITDIALOG:
      // Set icon in titlebar of about dialog
      static const HICON kAboutIcon = LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_ABOUT));
      SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)kAboutIcon);
      SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)kAboutIcon);
      return TRUE;
    case WM_CLOSE:
      EndDialog(hDlg, TRUE);
      return TRUE;
    case WM_COMMAND:
      if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
        EndDialog(hDlg, LOWORD(wParam));
        return TRUE;
      }
      break;
    default:
      break;
  }
  return FALSE;
}
