#include "utils.h"

#include <shlwapi.h>

#include <cmath>

#include "globals.h"
#include "resource.h"

const std::wstring kMessage1 = L"Happy Mother's Day!";
const std::wstring kMessage2 = L"Made with love by ";
const std::wstring kMessage3 = L" - The Frickster";

namespace {
constexpr double kPi = 3.14159265358979323846;
// Sample count for one full top arc (a half circle, 0 -> +/-pi). Higher
// values smooth the curve at the cost of more LineTo segments per frame.
constexpr int kFullArcSamples = 128;
}  // namespace

int g_heart_step = 0;
const int g_heart_max_steps = 100;

// Draws a heart, starting at the top and bottom simultaneously and then meeting in the middle, bounded by
// boundingRect.
//
// Geometry, with W = boundingRect width, H = height, lobe radius r = W/4:
//   - Two top lobes (semicircles) centred at x = W/4 and 3W/4, y = W/4
//   - Top dip / shared start of both top arcs at (W/2, W/4)
//   - Side meeting points at (0, W/4) and (W, W/4)
//   - Bottom point at (W/2, H), shared start of both bottom V-strokes
//
// Progress (g_heart_step / g_heart_max_steps) drives all four strokes off
// the same parameter, so the top arcs and the V lines all arrive at the
// side meeting points simultaneously when progress hits 1.0.
void DrawHeart(HWND hWnd, RECT boundingRect) {
  if (hWnd == nullptr) {
    return;
  }
  HDC hdc = GetDC(hWnd);
  if (hdc == nullptr) {
    return;
  }

  HPEN hPen = CreatePen(PS_SOLID, 2, RGB_MAGENTA);
  if (hPen == nullptr) {
    ReleaseDC(hWnd, hdc);
    return;
  }
  HPEN hOldPen = static_cast<HPEN>(SelectObject(hdc, hPen));

  const double W = static_cast<double>(boundingRect.right - boundingRect.left);
  const double H = static_cast<double>(boundingRect.bottom - boundingRect.top);

  // Need enough room for the lobe geometry (r = W/4) to be meaningful.
  if (W >= 4.0 && H >= 4.0) {
    const double Lx       = static_cast<double>(boundingRect.left);
    const double Ty       = static_cast<double>(boundingRect.top);
    const double r        = W / 4.0;
    const double leftCx   = Lx + W * 0.25;
    const double rightCx  = Lx + W * 0.75;
    // With r = W/4, choosing lobeCy = Ty + W/4 makes the lobe top kiss the
    // top edge of the bounding rect (cy - r == Ty).
    const double lobeCy   = Ty + W * 0.25;
    const double centerX  = Lx + W * 0.5;
    const double bottomY  = Ty + H;

    // Extra sweep past the leftmost / rightmost point of each lobe so the
    // arc curls down before the V takes over. Without this, the arc
    // tangent at the meeting point is vertical while the V line arrives
    // at a diagonal - which shows as a hard kink. Setting the arc
    // tangent slope equal to the V line slope and solving gives
    //   1 + cos(δ) = (k - 1) · sin(δ)   where k = 4H/W
    // → tan(δ/2) = 1 / (k - 1)
    // i.e. the corner is exactly tangent-continuous. For the typical
    // square heart (k = 4) that's δ ≈ 0.6435 rad ≈ 36.87°. Hearts whose
    // bounding rect is wider than ~4× their height degenerate, so we
    // fall back to no extension there and clamp the upper end at 90°.
    double extraSweep = 0.0;
    const double k = 4.0 * H / W;
    if (k > 1.001) {
      extraSweep = 2.0 * std::atan(1.0 / (k - 1.0));
      if (extraSweep > kPi * 0.5) {
        extraSweep = kPi * 0.5;
      }
    }
    const double totalSweep = kPi + extraSweep;

    const double leftMx = leftCx - r * std::cos(extraSweep);
    const double rightMx = rightCx + r * std::cos(extraSweep);
    const double meetY = lobeCy + r * std::sin(extraSweep);

    const int step = std::min(std::max(g_heart_step, 0), g_heart_max_steps);
    const double progress =
        static_cast<double>(step) / static_cast<double>(g_heart_max_steps);

    // Number of polyline segments to draw on each top arc. We sample the
    // full half-circle with kFullArcSamples segments and emit only the
    // first (progress * kFullArcSamples) of them so the leading edge moves
    // smoothly tick-by-tick.
    const int drawSegments =
        std::min(kFullArcSamples,
                 static_cast<int>(std::ceil(progress * kFullArcSamples)));

    if (drawSegments >= 1) {
      std::vector<POINT> pts;
      pts.reserve(drawSegments + 1);

      // Top-left arc. theta sweeps from 0 (top dip, on the +x side of the
      // left lobe centre) to -totalSweep*progress. In screen coords (+y
      // down) negative theta walks the upper half of the circle and then
      // continues past the leftmost point by `extraSweep`, curling down
      // toward the V meeting point so the corner is tangent-continuous.
      for (int i = 0; i <= drawSegments; ++i) {
        const double frac =
            static_cast<double>(i) / static_cast<double>(drawSegments);
        const double theta = -totalSweep * progress * frac;
        POINT p;
        p.x = static_cast<LONG>(std::lround(leftCx + r * std::cos(theta)));
        p.y = static_cast<LONG>(std::lround(lobeCy + r * std::sin(theta)));
        pts.push_back(p);
      }
      Polyline(hdc, pts.data(), static_cast<int>(pts.size()));

      // Top-right arc, mirrored. theta sweeps from pi (shared top dip,
      // -x side of the right lobe) through totalSweep, again walking the
      // upper half-circle and the matching extra curl-down before
      // handing off to the V at the right meeting point.
      pts.clear();
      for (int i = 0; i <= drawSegments; ++i) {
        const double frac =
            static_cast<double>(i) / static_cast<double>(drawSegments);
        const double theta = kPi + totalSweep * progress * frac;
        POINT p;
        p.x = static_cast<LONG>(std::lround(rightCx + r * std::cos(theta)));
        p.y = static_cast<LONG>(std::lround(lobeCy + r * std::sin(theta)));
        pts.push_back(p);
      }
      Polyline(hdc, pts.data(), static_cast<int>(pts.size()));

      // Bottom V-strokes: straight lines from the bottom point growing
      // out toward the side meeting points. Linear interpolation differs
      // in visual speed from the arcs, but they share the same progress,
      // so all four endpoints arrive at the same moment.
      const LONG cx = static_cast<LONG>(std::lround(centerX));
      const LONG by = static_cast<LONG>(std::lround(bottomY));

      const LONG blEndX =
          static_cast<LONG>(std::lround(centerX + (leftMx - centerX) * progress));
      const LONG blEndY =
          static_cast<LONG>(std::lround(bottomY + (meetY - bottomY) * progress));
      MoveToEx(hdc, cx, by, nullptr);
      LineTo(hdc, blEndX, blEndY);

      const LONG brEndX = static_cast<LONG>(
          std::lround(centerX + (rightMx - centerX) * progress));
      const LONG brEndY =
          static_cast<LONG>(std::lround(bottomY + (meetY - bottomY) * progress));
      MoveToEx(hdc, cx, by, nullptr);
      LineTo(hdc, brEndX, brEndY);

      // GDI's "exclusive last pixel" rule means Polyline and LineTo skip
      // their final endpoint, so each of the four strokes is missing the
      // pixel at its leading edge. That shows up as a small gap at the
      // arc/V meeting point once progress hits 1 (and as a one-pixel
      // ghost at the leading edge of each animating stroke in between).
      // Fill the four missed pixels so the strokes actually butt up
      // against each other when they meet.
      const double leftArcEndTheta = -totalSweep * progress;
      SetPixel(hdc,
               static_cast<int>(std::lround(leftCx + r * std::cos(leftArcEndTheta))),
               static_cast<int>(std::lround(lobeCy + r * std::sin(leftArcEndTheta))),
               RGB_MAGENTA);
      const double rightArcEndTheta = kPi + totalSweep * progress;
      SetPixel(hdc,
               static_cast<int>(std::lround(rightCx + r * std::cos(rightArcEndTheta))),
               static_cast<int>(std::lround(lobeCy + r * std::sin(rightArcEndTheta))),
               RGB_MAGENTA);
      SetPixel(hdc, static_cast<int>(blEndX), static_cast<int>(blEndY), RGB_MAGENTA);
      SetPixel(hdc, static_cast<int>(brEndX), static_cast<int>(brEndY), RGB_MAGENTA);
    }
  }

  SelectObject(hdc, hOldPen);
  DeleteObject(hPen);
  ReleaseDC(hWnd, hdc);
}

HFONT GetFont(std::wstring font, int size) {
}

bool FillRectWithColor(HDC hdc, const RECT& rc, COLORREF color) {
  bool ok = true;
  if (hdc == nullptr) {
    return false;
  }
  HBRUSH hBrush = CreateSolidBrush(color);
  if (hBrush == nullptr) {
    return false;
  }
  if (!FillRect(hdc, &rc, hBrush)) {
    ok = false;
  }
  DeleteObject(hBrush);
  return ok;
}

const std::wstring GetExeDir() {
  wchar_t exe_path[MAX_PATH];
  HMODULE this_app = GetModuleHandleW(nullptr);
  if (!this_app) {
    return std::wstring();
  }
  DWORD got_path = GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
  if (got_path == 0 || got_path >= MAX_PATH) {
    return std::wstring();
  }

  // Find the last backslash to get the directory
  std::wstring fullPath(exe_path);
  size_t lastSlash = fullPath.find_last_of(L"\\/");
  std::wstring retval;
  if (lastSlash != std::wstring::npos) {
    retval = fullPath.substr(0, lastSlash + 1); // Include trailing slash
  } else {
    retval = fullPath;
  }
  return retval;
}

// MessageBoxW with MB_OK can be dismissed several ways the user considers
// equivalent: clicking OK (IDOK), clicking the X close button (IDCANCEL),
// or pressing Esc (IDCANCEL). All of those mean "the box showed and the
// user dismissed it" - which is what these helpers want to report as
// success. Only a 0 return means the box failed to display in the first
// place (bad hWnd, OOM, no desktop access, etc.); that's the real false.
// `hWnd ? hWnd : mainHwnd` falls back to the main window when the caller
// passed null - useful from helpers that don't have an hWnd of their own.
bool InfoBox(HWND hWnd, const std::wstring& title, const std::wstring& message) {
  return MessageBoxW(hWnd ? hWnd : mainHwnd, message.c_str(), title.c_str(),
                     MB_OK | MB_ICONINFORMATION) != 0;
}

bool WarnBox(HWND hWnd, const std::wstring& title, const std::wstring& message) {
  return MessageBoxW(hWnd ? hWnd : mainHwnd, message.c_str(), title.c_str(),
                     MB_OK | MB_ICONWARNING) != 0;
}

bool ErrorBox(HWND hWnd, const std::wstring& title, const std::wstring& message) {
  return MessageBoxW(hWnd ? hWnd : mainHwnd, message.c_str(), title.c_str(),
                     MB_OK | MB_ICONERROR) != 0;
}

const std::wstring GetVersionString() {
  // VERSION_STRING is a narrow C string literal built by stringize macros,
  // so we can't feed it straight to std::wstring. Build the wide form
  // directly from the same integer macros (single source of truth in
  // version.h) - std::to_wstring keeps it standards-clean across MinGW
  // and MSVC alike.
  return std::to_wstring(MAJOR_VERSION) + L"." + std::to_wstring(MINOR_VERSION) + L"." +
         std::to_wstring(BUILD_VERSION);
}

const std::wstring GetAppName() {
  const std::wstring app_name = std::wstring(APP_NAME);
  return app_name;
}

static bool GetRawNtVersion(UINT* major, UINT* minor, UINT* build) {
  HMODULE hNtDll = GetModuleHandleW(L"ntdll.dll");
  if (hNtDll == nullptr) {
    return false;
  }
  const RtlGetNtVersionNumbers_t pfnRtlGetNtVersionNumbers =
      reinterpret_cast<RtlGetNtVersionNumbers_t>(GetProcAddress(hNtDll, "RtlGetNtVersionNumbers"));
  if (pfnRtlGetNtVersionNumbers == nullptr) {
    return false;
  }
  DWORD majorVer = 0;
  DWORD minorVer = 0;
  DWORD buildVer = 0;
  pfnRtlGetNtVersionNumbers(&majorVer, &minorVer, &buildVer);
  if (majorVer == 0) {
    return false; // Should never be zero
  }
  // RtlGetNtVersionNumbers packs the build-type flag into the top 4 bits
  // of the build number: 0xC0000000 = checked (debug) build, 0xF0000000 =
  // free (release) build. Bit Mask them off so callers see the same plain
  // build number the OS reports everywhere else (e.g. 2600 on XP SP3,
  // 7601 on Win7 SP1, 19045 on a recent Win10) instead of the raw
  // 0xF0000A28 = 4026534440 mess.
  const DWORD cleanBuildVer = buildVer & 0x0FFFFFFFu;
  // Out-params are individually optional - skip the assignment if a caller
  // passed nullptr (e.g. they only care about the major version).
  if (major != nullptr) {
    *major = static_cast<unsigned int>(majorVer);
  }
  if (minor != nullptr) {
    *minor = static_cast<unsigned int>(minorVer);
  }
  if (build != nullptr) {
    *build = static_cast<unsigned int>(cleanBuildVer);
  }
  return true;
}

bool IsWindowsXpOrLater() {
  UINT major = 0;
  UINT minor = 0;
  // Use the raw NT version: can't be spoofed by the manifest-driven shim that
  // GetVersionExW / RtlGetVersion go through, anything higher than 5.0 returns true.
  if (GetRawNtVersion(&major, &minor, nullptr)) {
    return major > 5u || (major == 5u && minor >= 1u);
  }
  return false; // Safe fallback, assume Win 2K
}

static DWORD GetCommCtrlVersion() {
  static const wchar_t* kComCtl32Dll = L"comctl32.dll";
  // Resolve the system comctl32.dll path explicitly. GetSystemDirectoryW
  // returns 0 on failure, or >= MAX_PATH if our buffer was too small (in
  // which case it reports the required size). Either is fatal for us -
  // bail rather than fall through with an empty path that would let
  // LoadLibraryW search the standard DLL order and silently bypass the
  // "explicitly use the system one" intent.
  wchar_t systemDir[MAX_PATH];
  const UINT length = GetSystemDirectoryW(systemDir, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) {
    return 0x0;
  }
  const std::wstring comctl32_path = std::wstring(systemDir) + L"\\" + kComCtl32Dll;

  HMODULE hComCtl32Dll = LoadLibraryW(comctl32_path.c_str());
  if (hComCtl32Dll == nullptr) {
    return 0x0;
  }

  DWORD dwVersion = 0x0;
  DLLGETVERSIONPROC pDllGetVersion =
      reinterpret_cast<DLLGETVERSIONPROC>(GetProcAddress(hComCtl32Dll, "DllGetVersion"));
  if (pDllGetVersion == nullptr) {
    return 0x0;
  } else {
    DLLVERSIONINFO dvi = {sizeof(dvi)};
    const HRESULT hr   = pDllGetVersion(&dvi);
    if (hr == S_OK) {
      dwVersion = _PACKVERSION(dvi.dwMajorVersion, dvi.dwMinorVersion);
    }
  }
  FreeLibrary(hComCtl32Dll);
  return dwVersion;
}

bool IsCommCtrlAtLeast(const DWORD to_compare) {
  const DWORD kCommCtrlVer = GetCommCtrlVersion();
  return kCommCtrlVer >= to_compare;
}
