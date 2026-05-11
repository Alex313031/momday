#ifndef MOMDAY_UTILS_H_
#define MOMDAY_UTILS_H_

#include "framework.h"

// Typedef for accessing undocumented RtlGetNtVersionNumbers in ntdll.dll
typedef void(WINAPI* RtlGetNtVersionNumbers_t)(DWORD* pNtMajorVersion,
                                               DWORD* pNtMinorVersion,
                                               DWORD* pNtBuildNumber);
// Color constants
#define RGB_BLACK   RGB(0, 0, 0)
#define RGB_WHITE   RGB(255, 255, 255)
#define RGB_GREY    RGB(128, 128, 128)
#define RGB_RED     RGB(255, 0, 0)
#define RGB_GREEN   RGB(0, 255, 0)
#define RGB_BLUE    RGB(0, 0, 255)
#define RGB_YELLOW  RGB(255, 255, 0)
#define RGB_CYAN    RGB(0, 255, 255)
#define RGB_MAGENTA RGB(255, 0, 255)

#define RGB_DARKBLUE RGB(0, 0, 128)

// When true, satellite hearts are redrawn widely on WM_RESIZE, otherwise, they adjust their position
// to stay as close (relatively) to their original positions in relation to the main center heart.
static constexpr bool wild_satellites = false;

// Strings printed in UI
extern const std::wstring kMessage1;
extern const std::wstring kMessage2;
const std::wstring kToolTip1();

// Subsequent-click tooltip pool. The click handler walks this array
// in order (not randomly) and re-shows kToolTip1() at the top of each
// cycle - so click 1 always shows "I love...", clicks 2..N+1 show
// kToolTip2[0..N-1], click N+2 wraps back to kToolTip1(), etc.
extern const std::vector<std::wstring> kToolTip2;

// Default desired ant canvas size (NOT the outer window size). wWinMain
// adds the OS chrome and the toolbar's measured height on top of these
// to compute the actual outer window size, so the user always gets a
// CW_WIDTH x CW_HEIGHT ant canvas at startup regardless of how tall the
// menu bar / toolbar end up being.
inline constexpr INT CW_WIDTH  = 800;
inline constexpr INT CW_HEIGHT = 600;

// Min window size
inline constexpr INT CW_MINWIDTH  = 480;
inline constexpr INT CW_MINHEIGHT = 480;

// Child window style
inline constexpr DWORD dwCHILD = WS_CHILD | WS_VISIBLE;

// Minimum common controls version for certain functions, used for fallback codepaths
// See https://learn.microsoft.com/en-us/windows/win32/controls/common-control-versions
inline constexpr DWORD dwComCtl32TargetVer =
    _PACKVERSION(static_cast<DWORD>(5u), static_cast<DWORD>(82u));

extern bool g_debug_mode;

// Maximum number of extra smaller hearts to draw, besides the main large one in the center.
inline constexpr UINT MAX_EXTRA_HEARTS = 9u;
// Min/Max size of satellite hearts
inline constexpr int kSatelliteMinSize = 48;
inline constexpr int kSatelliteMaxSize = 72;

// Heart animation state. The four strokes drawn by DrawHeart all key off
// (g_heart_step / g_heart_max_steps) so they reach their meeting points
// simultaneously. Advance g_heart_step by 1 per TIMER_HEARTS tick.
extern int g_heart_step;
extern const int g_heart_max_steps;

// Phase-2 animation state. Used by elements that should only start
// animating after g_heart_step has reached its max - e.g. the second
// marquee that slides in once the heart and the top marquee are done.
extern int g_subtext_step;
extern const int g_subtext_max_steps;

// Draws a single heart with a 2-px outline in `outlineColor`. See .cc
// for the geometry. Default color is magenta to match the main heart.
void DrawHeart(HWND hWnd, RECT boundingRect, COLORREF outlineColor = RGB_MAGENTA);

// Fills the heart's interior (the closed shape that DrawHeart traces at
// progress == 1.0) with `fillColor`. Call BEFORE DrawHeart so the
// outline lands on top of the fill instead of being covered by it.
void FillHeart(HWND hWnd, RECT boundingRect, COLORREF fillColor);

// Gets the desired font at the specified size (in pixels). Face name
// defaults to Tahoma; `italic` defaults to true to keep the existing
// marquee call sites italic without having to spell it out at every
// call. Caller owns the returned HFONT and must DeleteObject it when
// done. Returns nullptr on failure.
//
// (Param order: size first because in C++ a defaulted parameter can't
// precede a non-defaulted one - so the original "font first, size
// second" wouldn't compile.)
HFONT GetFont(int size, std::wstring font = L"Tahoma", bool italic = true);

// Slides `text` in to horizontally centered, with its top edge anchored
// at `yPos` inside `clientRect`. `progress` is the animation progress in
// [0, 1] (clamped) - caller picks which step counter drives it (e.g.
// g_heart_step for the top marquee, g_subtext_step for the second one
// that waits until phase 2). `slideFromLeft == true` starts the text
// fully off the left edge of clientRect; `false` starts it off the
// right edge.
void DrawMarquee(HWND hWnd,
                 RECT clientRect,
                 int yPos,
                 const std::wstring& text,
                 int fontSize,
                 double progress,
                 bool slideFromLeft = false,
                 std::wstring fontFace = L"Tahoma");

// Draws a classic Win32-style tooltip balloon (yellow background, black
// 1-px border, black text) with its top-left anchored just below and
// to the right of `cursorPos`. The box is clamped so it stays inside
// the client rect. Caller controls when this fires and how long the
// tooltip stays up - this just paints one frame.
void DrawTooltipPopup(HWND hWnd, POINT cursorPos, const std::wstring& text);

// Fills a rect with a solid color. Wraps the CreateSolidBrush + FillRect
// + DeleteObject trio so call sites don't have to repeat all three (and
// can't forget the DeleteObject and leak a GDI brush).
bool FillRectWithColor(HDC hdc, const RECT& rc, COLORREF color);

// Gets the current side by side directory, regardless of where .exe is started from
const std::wstring GetExeDir();

// Helper functions for MessageBoxW
bool InfoBox(HWND hWnd, const std::wstring& title, const std::wstring& message);

bool WarnBox(HWND hWnd, const std::wstring& title, const std::wstring& message);

bool ErrorBox(HWND hWnd, const std::wstring& title, const std::wstring& message);

// Gets version as human readable wstring.
const std::wstring GetVersionString();

// Returns APP_NAME as wstring, for easier usage.
const std::wstring GetAppName();

// Returns true on Windows XP (5.1) or later, false on Windows 2000 (5.0)
// or earlier. Used to gate styles / APIs that exist only on WinXP.
bool IsWindowsXpOrLater();

// For checking system's commctl32.dll
bool IsCommCtrlAtLeast(const DWORD to_compare);

#endif // MOMDAY_UTILS_H_
