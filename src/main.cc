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
struct SatelliteHeart {
  RECT rect;
  COLORREF color;
};
static std::vector<SatelliteHeart> s_satellite_rects;
static bool s_satellites_dirty = true;

// Color palette satellites pick from. Kept side-by-side with the
// satellite layout so it's easy to add or swap colors without touching
// the regen function.
static constexpr COLORREF kSatelliteColors[] = {RGB_YELLOW, RGB_CYAN, RGB_MAGENTA};

// Reference frame the satellite layout was rolled against - the main
// heart's centre and side length at regen time. Used in tame mode
// (wild_satellites == false) to scale + translate stored satellite
// rects to track the main heart as the client area resizes, instead of
// re-rolling the layout and flickering the satellites every WM_SIZE.
struct SatelliteFrame {
  int mainCenterX;
  int mainCenterY;
  int mainSide;
};
static SatelliteFrame s_regen_frame = {0, 0, 0};

// Click-tooltip state. WM_LBUTTONDOWN snapshots the cursor position and
// the kToolTip1() text, flips s_tooltip_visible on, and arms a one-shot
// TIMER_TOOLTIP that clears the flag so the tooltip auto-dismisses.
// WM_PAINT draws DrawTooltipPopup last (on top of everything else)
// whenever the flag is set.
constexpr UINT kTooltipDismissMs = 2500;
static bool s_tooltip_visible    = false;
static POINT s_tooltip_pos       = {0, 0};
static std::wstring s_tooltip_text;

// Middle-drag-to-resize state. We can't use the WM_NCLBUTTONDOWN trick
// (which drops the OS into its modal sizing loop) because that loop
// only exits on a *left* mouse-up. Instead we capture the mouse on
// WM_MBUTTONDOWN, anchor the opposite corner, and drive SetWindowPos
// directly from WM_MOUSEMOVE until WM_MBUTTONUP / WM_CAPTURECHANGED.
// (Right-click is left free for a future popup menu.)
static bool s_resizing                 = false;
static POINT s_resize_start_screen     = {0, 0};
static RECT s_resize_start_window      = {0, 0, 0, 0};
static WPARAM s_resize_corner          = HTBOTTOMRIGHT;
// Smallest window we'll let the right-drag resize produce. Mirrors the
// floor in WM_GETMINMAXINFO so manual dragging can't undercut it.
constexpr int kMinResizeWindowSide     = 200;

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
  std::uniform_int_distribution<size_t> colorDist(
      0, (sizeof(kSatelliteColors) / sizeof(kSatelliteColors[0])) - 1);
  constexpr int kMaxAttemptsPerHeart = 32;
  // Keep at least this many pixels between every satellite rect and the
  // client edges. Hearts that touch the edge get visually clipped on
  // the side facing the edge (one half of a lobe disappears) which
  // looks broken; a 1-px margin keeps the full outline visible.
  constexpr int kSatelliteEdgeMargin = 1;
  for (UINT i = 0; i < MAX_EXTRA_HEARTS; ++i) {
    for (int attempt = 0; attempt < kMaxAttemptsPerHeart; ++attempt) {
      const int w = sizeDist(rng);
      const int h = sizeDist(rng);
      // Need w + 2*margin pixels horizontally (and same vertically) so
      // that there's at least one valid placement with the requested
      // edge gap on both sides.
      if (w + 2 * kSatelliteEdgeMargin > clientW ||
          h + 2 * kSatelliteEdgeMargin > clientH) {
        continue;
      }
      std::uniform_int_distribution<int> xDist(
          kSatelliteEdgeMargin, clientW - w - kSatelliteEdgeMargin);
      std::uniform_int_distribution<int> yDist(
          kSatelliteEdgeMargin, clientH - h - kSatelliteEdgeMargin);
      const int x = xDist(rng);
      const int y = yDist(rng);
      RECT candidate     = {x, y, x + w, y + h};
      RECT discardedIntx = {0, 0, 0, 0};
      // IntersectRect returns FALSE when the two rects don't intersect,
      // which is exactly what we want for "satellite is outside the main
      // heart". The first arg is required even though we don't use it.
      if (!IntersectRect(&discardedIntx, &candidate, &mainRect)) {
        // Pick the color now (alongside the rect) so it stays stable
        // across paints - drawing a fresh random color each WM_PAINT
        // would make the satellites flicker between hues every frame.
        SatelliteHeart sh;
        sh.rect  = candidate;
        sh.color = kSatelliteColors[colorDist(rng)];
        s_satellite_rects.push_back(sh);
        break;
      }
    }
  }
  // Snapshot the main heart's frame this layout was rolled against.
  // Tame-mode resize handling reads this back to transform the stored
  // satellite rects on the fly instead of re-rolling them.
  s_regen_frame.mainCenterX = (mainRect.left + mainRect.right) / 2;
  s_regen_frame.mainCenterY = (mainRect.top + mainRect.bottom) / 2;
  s_regen_frame.mainSide    = mainRect.right - mainRect.left;
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
        // Two-phase animation: the heart (and the top marquee that
        // shares its counter) animates first, then the bottom marquee
        // takes over via g_subtext_step. Each tick advances whichever
        // counter is still unfinished and only invalidates if something
        // actually changed - a heart that's fully drawn AND a subtext
        // that's fully slid in means there's nothing left to redraw.
        bool advanced = false;
        if (g_heart_step < g_heart_max_steps) {
          ++g_heart_step;
          advanced = true;
        } else if (g_subtext_step < g_subtext_max_steps) {
          ++g_subtext_step;
          advanced = true;
        }
        if (advanced) {
          InvalidateRect(hWnd, nullptr, FALSE);
        }
      } else if (wParam == TIMER_TOOLTIP) {
        // One-shot dismiss: kill the timer so it can't refire, clear
        // the visibility flag, and trigger a repaint to wipe the
        // tooltip from the window.
        KillTimer(hWnd, TIMER_TOOLTIP);
        s_tooltip_visible = false;
        InvalidateRect(hWnd, nullptr, FALSE);
      }
      break;
    case WM_LBUTTONDOWN: {
      // Cycle: index 0 -> kToolTip1() ("I love..."), indices 1..N ->
      // kToolTip2[idx-1]. Wraps modulo (1 + kToolTip2.size()) so the
      // first click after every wrap is always the "I love..." again,
      // and the rest are walked in array order (not random).
      static size_t s_tooltip_cycle = 0;
      const size_t cycleLen = 1 + kToolTip2.size();
      const size_t idx     = s_tooltip_cycle % cycleLen;
      // GET_X_LPARAM / GET_Y_LPARAM are signed because clicks on the
      // window edge during a capture-drag can briefly report negative
      // client coords; the tooltip clamping in DrawTooltipPopup
      // handles those.
      s_tooltip_pos.x   = GET_X_LPARAM(lParam);
      s_tooltip_pos.y   = GET_Y_LPARAM(lParam);
      s_tooltip_text    = (idx == 0) ? kToolTip1() : kToolTip2[idx - 1];
      s_tooltip_visible = true;
      ++s_tooltip_cycle;
      // Reset the dismiss timer on every click so a fresh click
      // restarts the 2.5-s window instead of being dismissed by a
      // stale fire from the previous click.
      KillTimer(hWnd, TIMER_TOOLTIP);
      SetTimer(hWnd, TIMER_TOOLTIP, kTooltipDismissMs, nullptr);
      InvalidateRect(hWnd, nullptr, FALSE);
      break;
    }
    case WM_MBUTTONDOWN: {
      // Start a middle-drag resize: pick the corner nearest the cursor
      // (so the opposite corner stays anchored), snapshot the cursor
      // and window in screen coords, take the mouse capture so we
      // keep getting MOUSEMOVE messages even when the cursor leaves
      // the client area, and flip s_resizing on. WM_MOUSEMOVE does
      // the actual resize, WM_MBUTTONUP / WM_CAPTURECHANGED end it.
      const int px = GET_X_LPARAM(lParam);
      const int py = GET_Y_LPARAM(lParam);
      RECT client;
      GetClientRect(hWnd, &client);
      const int midX = (client.left + client.right) / 2;
      const int midY = (client.top + client.bottom) / 2;
      if (px < midX && py < midY) {
        s_resize_corner = HTTOPLEFT;
      } else if (px >= midX && py < midY) {
        s_resize_corner = HTTOPRIGHT;
      } else if (px < midX && py >= midY) {
        s_resize_corner = HTBOTTOMLEFT;
      } else {
        s_resize_corner = HTBOTTOMRIGHT;
      }
      GetCursorPos(&s_resize_start_screen);
      GetWindowRect(hWnd, &s_resize_start_window);
      SetCapture(hWnd);
      s_resizing = true;
      break;
    }
    case WM_MOUSEMOVE: {
      if (s_resizing) {
        // Compute the new window rect by moving only the dragged
        // corner's two edges by the screen-space cursor delta. Then
        // clamp width/height against the minimum, anchoring the
        // *opposite* edge so the anchor side doesn't drift when we
        // bottom out.
        POINT cur;
        GetCursorPos(&cur);
        const int dx = cur.x - s_resize_start_screen.x;
        const int dy = cur.y - s_resize_start_screen.y;
        RECT r = s_resize_start_window;
        switch (s_resize_corner) {
          case HTTOPLEFT:
            r.left += dx;
            r.top += dy;
            break;
          case HTTOPRIGHT:
            r.right += dx;
            r.top += dy;
            break;
          case HTBOTTOMLEFT:
            r.left += dx;
            r.bottom += dy;
            break;
          case HTBOTTOMRIGHT:
          default:
            r.right += dx;
            r.bottom += dy;
            break;
        }
        if (r.right - r.left < kMinResizeWindowSide) {
          if (s_resize_corner == HTTOPLEFT || s_resize_corner == HTBOTTOMLEFT) {
            r.left = r.right - kMinResizeWindowSide;
          } else {
            r.right = r.left + kMinResizeWindowSide;
          }
        }
        if (r.bottom - r.top < kMinResizeWindowSide) {
          if (s_resize_corner == HTTOPLEFT || s_resize_corner == HTTOPRIGHT) {
            r.top = r.bottom - kMinResizeWindowSide;
          } else {
            r.bottom = r.top + kMinResizeWindowSide;
          }
        }
        SetWindowPos(hWnd, nullptr, r.left, r.top,
                     r.right - r.left, r.bottom - r.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
      }
      break;
    }
    case WM_MBUTTONUP:
      if (s_resizing) {
        ReleaseCapture();  // triggers WM_CAPTURECHANGED, which clears s_resizing
      }
      break;
    case WM_CAPTURECHANGED:
      // Fired when capture is released for any reason (our own
      // ReleaseCapture, an alt-tab, another window stealing it, etc.).
      // Clearing here is the single point of truth for ending a drag.
      s_resizing = false;
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
      pMinMaxInfo->ptMinTrackSize.x = CW_MINWIDTH;
      pMinMaxInfo->ptMinTrackSize.y = CW_MINHEIGHT;
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
        // Once the outline is fully traced, fill the interior with red
        // so the heart "comes to life" at the climax. Has to happen
        // before DrawHeart so the magenta outline lands on top of the
        // red fill instead of being covered by it.
        if (g_heart_step >= g_heart_max_steps) {
          FillHeart(hWnd, heartRect, RGB_RED);
        }
        DrawHeart(hWnd, heartRect);

        // Lazy regen: WM_SIZE marked us dirty (or this is the first
        // paint). Recompute the satellite layout against the current
        // client rect and the just-computed main heart rect, then draw
        // each satellite in the same animation step as the main heart.
        if (s_satellites_dirty) {
          RegenerateSatellites(client.right, client.bottom, heartRect);
          s_satellites_dirty = false;
        }
        // In wild mode the stored rect is already correct (WM_SIZE
        // marked us dirty and we just re-rolled). In tame mode the
        // stored rect was rolled against a possibly older main-heart
        // frame, so map each rect from that frame into the current
        // one: scale by mainSide ratio, translate by the centre
        // delta. Net effect is "the whole composition zooms with the
        // window".
        const int curCenterX = (heartRect.left + heartRect.right) / 2;
        const int curCenterY = (heartRect.top + heartRect.bottom) / 2;
        const int curSide    = heartRect.right - heartRect.left;
        for (const SatelliteHeart& s : s_satellite_rects) {
          RECT r = s.rect;
          if (!wild_satellites && s_regen_frame.mainSide > 0 && curSide > 0) {
            const double scale = static_cast<double>(curSide) /
                                 static_cast<double>(s_regen_frame.mainSide);
            const int origCx = (s.rect.left + s.rect.right) / 2;
            const int origCy = (s.rect.top + s.rect.bottom) / 2;
            const int origW  = s.rect.right - s.rect.left;
            const int origH  = s.rect.bottom - s.rect.top;
            const int newCx = curCenterX + static_cast<int>(
                std::lround((origCx - s_regen_frame.mainCenterX) * scale));
            const int newCy = curCenterY + static_cast<int>(
                std::lround((origCy - s_regen_frame.mainCenterY) * scale));
            const int newW = std::max(
                1, static_cast<int>(std::lround(origW * scale)));
            const int newH = std::max(
                1, static_cast<int>(std::lround(origH * scale)));
            r.left   = newCx - newW / 2;
            r.top    = newCy - newH / 2;
            r.right  = r.left + newW;
            r.bottom = r.top + newH;
          }
          DrawHeart(hWnd, r, s.color);
        }

        // Marquees: kMessage1 in the top margin (72-px), kMessage2 in
        // the lower third (36-px). kMessage1 shares the heart's
        // progress so it lands centered the moment the heart finishes
        // tracing; kMessage2 keys off the phase-2 counter so it stays
        // off-screen until the heart and the top marquee are done.
        constexpr int kMarqueeFontSize    = 72;
        constexpr int kMarqueeTopPad      = 8;
        constexpr int kSubMarqueeFontSize = 24;
        const double heartProgress =
            static_cast<double>(g_heart_step) / static_cast<double>(g_heart_max_steps);
        DrawMarquee(hWnd, client, kMarqueeTopPad, kMessage1, kMarqueeFontSize,
                    heartProgress);

        // Center kMessage2 vertically inside the bottom-third band so
        // it sits clear of the main heart and centers nicely as the
        // window resizes.
        const int lowerThirdTop = static_cast<int>(client.bottom * 2.0f / 3.0f);
        const int subY =
            lowerThirdTop + ((client.bottom - lowerThirdTop) - kSubMarqueeFontSize) / 2;
        const double subProgress = static_cast<double>(g_subtext_step) /
                                   static_cast<double>(g_subtext_max_steps);
        DrawMarquee(hWnd, client, subY, kMessage2, kSubMarqueeFontSize, subProgress,
                    /*slideFromLeft=*/true, L"Lucida Console");
      }
      // Tooltip stays on top of the hearts and marquees - draw it last.
      // (Outside the `side >= 8` block so it still shows even on a
      // window too tiny to render the heart.)
      if (s_tooltip_visible) {
        DrawTooltipPopup(hWnd, s_tooltip_pos, s_tooltip_text);
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
      // Client size changed. In wild mode we re-roll the whole layout
      // (which is what makes the hearts pop in/out as you drag a
      // resize). In tame mode we leave the stored rects alone and let
      // WM_PAINT transform them against the new main heart so the
      // composition just zooms/translates with the window.
      if (wild_satellites) {
        s_satellites_dirty = true;
      }
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
      KillTimer(hWnd, TIMER_TOOLTIP);
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
