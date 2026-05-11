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

// Large and small main application icons
extern HICON kMainIcon;
extern HICON kSmallIcon;

// Default desired ant canvas size (NOT the outer window size). wWinMain
// adds the OS chrome and the toolbar's measured height on top of these
// to compute the actual outer window size, so the user always gets a
// CW_WIDTH x CW_HEIGHT ant canvas at startup regardless of how tall the
// menu bar / toolbar end up being.
inline constexpr INT CW_WIDTH  = 800;
inline constexpr INT CW_HEIGHT = 600;

// Child window style
inline constexpr DWORD dwCHILD = WS_CHILD | WS_VISIBLE;

// Minimum common controls version for certain functions, used for fallback codepaths
// See https://learn.microsoft.com/en-us/windows/win32/controls/common-control-versions
inline constexpr DWORD dwComCtl32TargetVer =
    _PACKVERSION(static_cast<DWORD>(5u), static_cast<DWORD>(82u));

extern bool g_debug_mode;

// Draws a single heart, see .cc
void DrawHeart(HWND hWnd, RECT boundingRect);

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
