// ==WindhawkMod==
// @id              move-window-to-monitor
// @name            Move Window to Monitor
// @description     Moves the active window to a specific monitor using configurable hotkeys (default: Ctrl+Alt+Arrows), and optionally jumps focus + cursor to the next monitor or to the monitor under the cursor
// @version         1.3.0
// @author          TomberWolf
// @github          https://github.com/TomberWolf
// @include         windhawk.exe
// @compilerOptions -luser32 -lgdi32 -lshcore
// @license         MIT
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Move Window to Monitor

Moves the active window to a specific monitor using configurable hotkeys.

## Default Shortcut: Ctrl+Alt+Arrow keys

## Setup

Monitors are numbered automatically, sorted **left to right, then top to bottom**
based on their physical position in your Windows display layout.


**Example for a setup with 3 monitors on the bottom and 1 on top center:**

```
        [ 1 ]
[ 0 ]   [ 2 ]   [ 3 ]
```

Set: UP=1, LEFT=0, DOWN=2, RIGHT=3

Use **-1** to keep automatic geometric detection for a direction.

When the active window is already on the target monitor, the hotkey does nothing.

The **Center window** option centers the window on the target monitor instead
of preserving its relative position. Works independently of **Keep window size**.

## Switch focus to the next monitor (optional)

A separate hotkey (default **Alt+`**, the key above Tab) moves keyboard focus
and the mouse cursor to the **next** monitor. It focuses the topmost window on
that monitor and warps the cursor onto it. With two monitors this simply toggles
between them; with more, it cycles through them in order.

This is independent of the move-window hotkeys and can be disabled in the
settings.

## Focus the monitor under the cursor (optional)

A separate hotkey (default **Alt+Shift+`**) moves keyboard focus to the
topmost window on the monitor where the **mouse cursor** currently is, without
moving the cursor. Use it to make the monitor you're pointing at the active one
(for Start menu placement, new windows, etc.) without clicking.

If there is no focusable window on that monitor, or focus is already there,
the hotkey does nothing. This is independent of the other hotkeys and can be
disabled in the settings.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- modifierKeys: "ctrl_alt"
  $name: Modifier Keys
  $options:
    - ctrl_alt: Ctrl+Alt
    - ctrl_shift: Ctrl+Shift
    - alt_shift: Alt+Shift
    - ctrl_alt_shift: Ctrl+Alt+Shift
- targetUp: -1
  $name: "Target monitor UP (-1 = automatic)"
  $description: Index of the monitor to move to when pressing the UP hotkey
- targetDown: -1
  $name: "Target monitor DOWN (-1 = automatic)"
  $description: Index of the monitor to move to when pressing the DOWN hotkey
- targetLeft: -1
  $name: "Target monitor LEFT (-1 = automatic)"
  $description: Index of the monitor to move to when pressing the LEFT hotkey
- targetRight: -1
  $name: "Target monitor RIGHT (-1 = automatic)"
  $description: Index of the monitor to move to when pressing the RIGHT hotkey
- keepSize: true
  $name: Keep window size when moving
  $description: If disabled, the window is scaled proportionally to the target monitor
- centerOnMove: false
  $name: Center window on target monitor
  $description: If enabled, the window is centered on the target monitor instead of keeping its relative position
- enableFocusSwitch: true
  $name: Enable "switch focus to next monitor" hotkey
  $description: Adds a separate hotkey that moves keyboard focus and the mouse cursor to the next monitor
- focusModifier: "alt"
  $name: Focus-switch modifier
  $options:
    - alt: Alt
    - ctrl: Ctrl
    - ctrl_alt: Ctrl+Alt
    - ctrl_shift: Ctrl+Shift
    - alt_shift: Alt+Shift
    - ctrl_alt_shift: Ctrl+Alt+Shift
    - win: Win
- focusKey: "grave"
  $name: Focus-switch key
  $options:
    - grave: "` ~ (key above Tab)"
    - tab: Tab
    - oem_1: "; (semicolon)"
- focusMoveCursor: true
  $name: Move mouse cursor when switching focus
  $description: Also warps the mouse cursor onto the focused window on the target monitor
- enableFocusCursor: true
  $name: Enable "focus monitor under cursor" hotkey
  $description: Adds a separate hotkey that moves keyboard focus to the topmost window on the monitor where the mouse cursor is (the cursor is not moved)
- focusCursorModifier: "alt_shift"
  $name: Focus-under-cursor modifier
  $options:
    - alt: Alt
    - ctrl: Ctrl
    - ctrl_alt: Ctrl+Alt
    - ctrl_shift: Ctrl+Shift
    - alt_shift: Alt+Shift
    - ctrl_alt_shift: Ctrl+Alt+Shift
    - win: Win
- focusCursorKey: "grave"
  $name: Focus-under-cursor key
  $options:
    - grave: "` ~ (key above Tab)"
    - tab: Tab
    - oem_1: "; (semicolon)"
*/
// ==/WindhawkModSettings==

#include <windows.h>
#include <shellscalingapi.h>
#include <vector>
#include <atomic>
#include <algorithm>

#define HOTKEY_UP         1
#define HOTKEY_DOWN       2
#define HOTKEY_LEFT       3
#define HOTKEY_RIGHT      4
#define HOTKEY_FOCUS_NEXT 5
#define HOTKEY_FOCUS_CURSOR 6

// Custom message posted from WhTool_ModSettingsChanged to the hotkey thread
// so that (Un)RegisterHotKey always runs on the thread that owns the window.
#define WM_APP_SETTINGS_CHANGED (WM_APP + 1)

struct ModSettings {
    UINT modifierKeys = MOD_CONTROL | MOD_ALT;
    int  targetUp     = -1;
    int  targetDown   = -1;
    int  targetLeft   = -1;
    int  targetRight  = -1;
    bool keepSize     = true;
    bool centerOnMove = false;

    bool enableFocusSwitch = true;
    UINT focusModifier     = MOD_ALT;
    UINT focusKey          = VK_OEM_3;   // ` ~ key
    bool focusMoveCursor   = true;

    bool enableFocusCursor   = true;
    UINT focusCursorModifier = MOD_ALT | MOD_SHIFT;
    UINT focusCursorKey      = VK_OEM_3;   // ` ~ key
};

static ModSettings       g_settings;
static HANDLE            g_hThread = nullptr;
static HWND              g_hMsgWnd = nullptr;
static std::atomic<bool> g_running{false};

// ── settings ──────────────────────────────────────────────────────────────────

static UINT SettingToMod(const wchar_t* v) {
    if (wcscmp(v, L"ctrl_shift")     == 0) return MOD_CONTROL | MOD_SHIFT;
    if (wcscmp(v, L"alt_shift")      == 0) return MOD_ALT     | MOD_SHIFT;
    if (wcscmp(v, L"ctrl_alt_shift") == 0) return MOD_CONTROL | MOD_ALT | MOD_SHIFT;
    return MOD_CONTROL | MOD_ALT;
}

static UINT FocusSettingToMod(const wchar_t* v) {
    if (wcscmp(v, L"alt")            == 0) return MOD_ALT;
    if (wcscmp(v, L"ctrl")           == 0) return MOD_CONTROL;
    if (wcscmp(v, L"ctrl_alt")       == 0) return MOD_CONTROL | MOD_ALT;
    if (wcscmp(v, L"ctrl_shift")     == 0) return MOD_CONTROL | MOD_SHIFT;
    if (wcscmp(v, L"alt_shift")      == 0) return MOD_ALT     | MOD_SHIFT;
    if (wcscmp(v, L"ctrl_alt_shift") == 0) return MOD_CONTROL | MOD_ALT | MOD_SHIFT;
    if (wcscmp(v, L"win")            == 0) return MOD_WIN;
    return MOD_CONTROL;
}

static UINT FocusSettingToVk(const wchar_t* v) {
    if (wcscmp(v, L"tab")   == 0) return VK_TAB;
    if (wcscmp(v, L"oem_1") == 0) return VK_OEM_1;   // ; :
    return VK_OEM_3;                                 // ` ~
}

static void LoadSettings() {
    PCWSTR s = Wh_GetStringSetting(L"modifierKeys");
    g_settings.modifierKeys = SettingToMod(s);
    Wh_FreeStringSetting(s);
    g_settings.targetUp     = Wh_GetIntSetting(L"targetUp");
    g_settings.targetDown   = Wh_GetIntSetting(L"targetDown");
    g_settings.targetLeft   = Wh_GetIntSetting(L"targetLeft");
    g_settings.targetRight  = Wh_GetIntSetting(L"targetRight");
    g_settings.keepSize     = Wh_GetIntSetting(L"keepSize") != 0;
    g_settings.centerOnMove = Wh_GetIntSetting(L"centerOnMove") != 0;

    g_settings.enableFocusSwitch = Wh_GetIntSetting(L"enableFocusSwitch") != 0;
    PCWSTR fm = Wh_GetStringSetting(L"focusModifier");
    g_settings.focusModifier = FocusSettingToMod(fm);
    Wh_FreeStringSetting(fm);
    PCWSTR fk = Wh_GetStringSetting(L"focusKey");
    g_settings.focusKey = FocusSettingToVk(fk);
    Wh_FreeStringSetting(fk);
    g_settings.focusMoveCursor = Wh_GetIntSetting(L"focusMoveCursor") != 0;

    g_settings.enableFocusCursor = Wh_GetIntSetting(L"enableFocusCursor") != 0;
    PCWSTR fcm = Wh_GetStringSetting(L"focusCursorModifier");
    g_settings.focusCursorModifier = FocusSettingToMod(fcm);
    Wh_FreeStringSetting(fcm);
    PCWSTR fck = Wh_GetStringSetting(L"focusCursorKey");
    g_settings.focusCursorKey = FocusSettingToVk(fck);
    Wh_FreeStringSetting(fck);
}

// ── monitor enumeration ───────────────────────────────────────────────────────

struct MonitorInfo {
    HMONITOR hMon;
    RECT     rcWork;
    UINT     dpiX;
    UINT     dpiY;
};

static BOOL CALLBACK MonitorEnumProc(HMONITOR hMon, HDC, LPRECT, LPARAM lParam) {
    auto* list = reinterpret_cast<std::vector<MonitorInfo>*>(lParam);
    MONITORINFO mi = { sizeof(mi) };
    if (!GetMonitorInfo(hMon, &mi)) return TRUE;

    MonitorInfo info;
    info.hMon   = hMon;
    info.rcWork = mi.rcWork;
    info.dpiX   = 96;
    info.dpiY   = 96;

    typedef HRESULT (WINAPI* GetDpiForMonitorFn)(HMONITOR, int, UINT*, UINT*);
    static auto fn = reinterpret_cast<GetDpiForMonitorFn>(
        GetProcAddress(GetModuleHandle(L"shcore.dll"), "GetDpiForMonitor"));
    if (fn) fn(hMon, 0 /*MDT_EFFECTIVE_DPI*/, &info.dpiX, &info.dpiY);

    list->push_back(info);
    return TRUE;
}

static std::vector<MonitorInfo> GetAllMonitors() {
    std::vector<MonitorInfo> list;
    EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc,
                        reinterpret_cast<LPARAM>(&list));
    std::sort(list.begin(), list.end(), [](const MonitorInfo& a, const MonitorInfo& b) {
        if (a.rcWork.left != b.rcWork.left)
            return a.rcWork.left < b.rcWork.left;
        return a.rcWork.top < b.rcWork.top;
    });
    return list;
}

static POINT RectCenter(const RECT& r) {
    return { (r.left + r.right) / 2, (r.top + r.bottom) / 2 };
}

static int FindMonitorIndex(const std::vector<MonitorInfo>& monitors, HMONITOR hMon) {
    for (int i = 0; i < (int)monitors.size(); i++)
        if (monitors[i].hMon == hMon) return i;
    return -1;
}

static void LogAllMonitors(const std::vector<MonitorInfo>& monitors) {
    Wh_Log(L"=== Move Window to Monitor v6: %d monitor(s) detected ===",
           (int)monitors.size());
    Wh_Log(L"Sorted left-to-right, then top-to-bottom:");
    for (int i = 0; i < (int)monitors.size(); i++) {
        const auto& m = monitors[i];
        int w = m.rcWork.right  - m.rcWork.left;
        int h = m.rcWork.bottom - m.rcWork.top;
        Wh_Log(L"  Index %d | pos (%d, %d) | size %dx%d | DPI %u",
               i, m.rcWork.left, m.rcWork.top, w, h, m.dpiX);
    }
    Wh_Log(L"Settings: UP=%d DOWN=%d LEFT=%d RIGHT=%d  keepSize=%d  center=%d",
           g_settings.targetUp, g_settings.targetDown,
           g_settings.targetLeft, g_settings.targetRight,
           (int)g_settings.keepSize, (int)g_settings.centerOnMove);
}

// ── window moving ─────────────────────────────────────────────────────────────

enum class Direction { Up, Down, Left, Right };

static void MoveWindowToMonitor(HWND hwnd, const MonitorInfo& src, const MonitorInfo& dst) {
    bool wasMaximized = IsZoomed(hwnd);
    // Restore synchronously: ShowWindowAsync only posts the request, so
    // GetWindowRect below would still read the maximized geometry and
    // SetWindowPos would be applied to a still-maximized window (which Windows
    // refuses to relocate). ShowWindow blocks until the restore is applied.
    if (wasMaximized) ShowWindow(hwnd, SW_RESTORE);

    RECT rcWin = {};
    GetWindowRect(hwnd, &rcWin);

    int ww   = rcWin.right  - rcWin.left;
    int wh   = rcWin.bottom - rcWin.top;
    int srcW = src.rcWork.right  - src.rcWork.left;
    int srcH = src.rcWork.bottom - src.rcWork.top;
    int dstW = dst.rcWork.right  - dst.rcWork.left;
    int dstH = dst.rcWork.bottom - dst.rcWork.top;

    float relX = srcW > 0 ? (float)(rcWin.left - src.rcWork.left) / srcW : 0.0f;
    float relY = srcH > 0 ? (float)(rcWin.top  - src.rcWork.top)  / srcH : 0.0f;

    int newW, newH;
    if (g_settings.keepSize) {
        float dpiScaleX = (dst.dpiX > 0 && src.dpiX > 0)
                          ? (float)dst.dpiX / src.dpiX : 1.0f;
        float dpiScaleY = (dst.dpiY > 0 && src.dpiY > 0)
                          ? (float)dst.dpiY / src.dpiY : 1.0f;
        newW = (int)(ww * dpiScaleX);
        newH = (int)(wh * dpiScaleY);
    } else {
        newW = (int)(ww * (float)dstW / srcW);
        newH = (int)(wh * (float)dstH / srcH);
    }

    int newX, newY;
    if (g_settings.centerOnMove) {
        newX = dst.rcWork.left + (dstW - newW) / 2;
        newY = dst.rcWork.top  + (dstH - newH) / 2;
    } else {
        newX = dst.rcWork.left + (int)(relX * dstW);
        newY = dst.rcWork.top  + (int)(relY * dstH);
    }

    if (newX + newW > dst.rcWork.right)  newX = dst.rcWork.right  - newW;
    if (newY + newH > dst.rcWork.bottom) newY = dst.rcWork.bottom - newH;
    if (newX < dst.rcWork.left)          newX = dst.rcWork.left;
    if (newY < dst.rcWork.top)           newY = dst.rcWork.top;

    BOOL ok = SetWindowPos(hwnd, nullptr, newX, newY, newW, newH,
                           SWP_NOZORDER | SWP_NOACTIVATE);
    Wh_Log(L"  Move: maximized=%d  to (%d,%d) size %dx%d  SetWindowPos=%d (err %u)",
           (int)wasMaximized, newX, newY, newW, newH, (int)ok, GetLastError());

    if (wasMaximized) ShowWindow(hwnd, SW_MAXIMIZE);
}

static void MoveActiveWindowInDirection(Direction dir) {
    HWND hwnd = GetForegroundWindow();
    Wh_Log(L"Hotkey dir=%d  foreground hwnd=%p  zoomed=%d",
           (int)dir, hwnd, hwnd ? (int)IsZoomed(hwnd) : -1);
    if (!hwnd) { Wh_Log(L"  No foreground window"); return; }

    HMONITOR hCurrent = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO miCurrent = { sizeof(miCurrent) };
    if (!GetMonitorInfo(hCurrent, &miCurrent)) { Wh_Log(L"  GetMonitorInfo failed"); return; }

    auto monitors = GetAllMonitors();
    int  curIdx   = FindMonitorIndex(monitors, hCurrent);
    if (curIdx < 0) { Wh_Log(L"  Current monitor not in list"); return; }

    int fixedIndex = -1;
    switch (dir) {
        case Direction::Up:    fixedIndex = g_settings.targetUp;    break;
        case Direction::Down:  fixedIndex = g_settings.targetDown;  break;
        case Direction::Left:  fixedIndex = g_settings.targetLeft;  break;
        case Direction::Right: fixedIndex = g_settings.targetRight; break;
    }

    int dstIdx = -1;

    if (fixedIndex >= 0 && fixedIndex < (int)monitors.size()) {
        if (fixedIndex != curIdx)
            dstIdx = fixedIndex;
    }

    if (dstIdx < 0 && fixedIndex < 0) {
        POINT curCenter = RectCenter(monitors[curIdx].rcWork);
        int   bestScore = INT_MAX;

        for (int i = 0; i < (int)monitors.size(); i++) {
            if (i == curIdx) continue;
            POINT c  = RectCenter(monitors[i].rcWork);
            int   dx = c.x - curCenter.x;
            int   dy = c.y - curCenter.y;
            bool  candidate = false;
            int   primary   = 0;

            switch (dir) {
                case Direction::Up:
                    candidate = (dy < 0) && (abs(dy) >= abs(dx)); primary = -dy; break;
                case Direction::Down:
                    candidate = (dy > 0) && (abs(dy) >= abs(dx)); primary = dy;  break;
                case Direction::Left:
                    candidate = (dx < 0) && (abs(dx) >= abs(dy)); primary = -dx; break;
                case Direction::Right:
                    candidate = (dx > 0) && (abs(dx) >= abs(dy)); primary = dx;  break;
            }

            if (candidate && primary < bestScore) {
                bestScore = primary;
                dstIdx    = i;
            }
        }
    }

    Wh_Log(L"  curIdx=%d  fixedIndex=%d  dstIdx=%d", curIdx, fixedIndex, dstIdx);
    if (dstIdx < 0) { Wh_Log(L"  No destination (already there / out of range / no neighbor)"); return; }
    MoveWindowToMonitor(hwnd, monitors[curIdx], monitors[dstIdx]);
}

// ── focus + cursor switching ────────────────────────────────────────────────────

// A window worth focusing: visible, not minimized, not cloaked (skips UWP ghost
// windows), not a tool window, and has a title. Mirrors Alt+Tab eligibility.
static bool IsFocusableWindow(HWND hwnd) {
    if (!IsWindowVisible(hwnd)) return false;
    if (IsIconic(hwnd))         return false;

    typedef HRESULT (WINAPI* DwmGetWindowAttributeFn)(HWND, DWORD, PVOID, DWORD);
    static auto pDwm = reinterpret_cast<DwmGetWindowAttributeFn>(
        GetProcAddress(LoadLibrary(L"dwmapi.dll"), "DwmGetWindowAttribute"));
    if (pDwm) {
        int cloaked = 0;
        if (SUCCEEDED(pDwm(hwnd, 14 /*DWMWA_CLOAKED*/, &cloaked, sizeof(cloaked))) &&
            cloaked) {
            return false;
        }
    }

    if (GetWindowLongPtr(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return false;
    if (GetWindowTextLength(hwnd) == 0)                         return false;
    return true;
}

struct FindTopWindowData {
    HMONITOR target;
    HWND     result;
};

static BOOL CALLBACK FindTopWindowProc(HWND hwnd, LPARAM lParam) {
    auto* d = reinterpret_cast<FindTopWindowData*>(lParam);
    if (!IsFocusableWindow(hwnd)) return TRUE;
    if (MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) != d->target) return TRUE;
    d->result = hwnd;
    return FALSE;  // EnumWindows is top-to-bottom Z-order; first match is topmost
}

static HWND FindTopWindowOnMonitor(HMONITOR target) {
    FindTopWindowData d{ target, nullptr };
    EnumWindows(FindTopWindowProc, reinterpret_cast<LPARAM>(&d));
    return d.result;
}

// SetForegroundWindow is subject to the foreground lock; from a hotkey it
// usually works, but fall back to the AttachThreadInput trick if it doesn't.
static void ForceSetForegroundWindow(HWND hwnd) {
    if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
    if (SetForegroundWindow(hwnd)) return;

    HWND  fg        = GetForegroundWindow();
    DWORD fgThread  = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    DWORD ourThread = GetCurrentThreadId();
    if (fgThread && fgThread != ourThread) {
        AttachThreadInput(ourThread, fgThread, TRUE);
        BringWindowToTop(hwnd);
        SetForegroundWindow(hwnd);
        AttachThreadInput(ourThread, fgThread, FALSE);
    } else {
        BringWindowToTop(hwnd);
        SetForegroundWindow(hwnd);
    }
}

static void FocusNextMonitor() {
    auto monitors = GetAllMonitors();
    if (monitors.size() < 2) { Wh_Log(L"FocusNext: need >= 2 monitors"); return; }

    // "Current" monitor: where focus is, falling back to the cursor.
    HWND     fg   = GetForegroundWindow();
    HMONITOR hCur = fg ? MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST) : nullptr;
    if (!hCur) {
        POINT pt; GetCursorPos(&pt);
        hCur = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    }

    int curIdx = FindMonitorIndex(monitors, hCur);
    if (curIdx < 0) curIdx = 0;
    int nextIdx = (curIdx + 1) % (int)monitors.size();
    const MonitorInfo& dst = monitors[nextIdx];

    HWND target = FindTopWindowOnMonitor(dst.hMon);
    Wh_Log(L"FocusNext: curIdx=%d -> nextIdx=%d  target hwnd=%p",
           curIdx, nextIdx, target);

    POINT dest = RectCenter(dst.rcWork);
    if (target) {
        ForceSetForegroundWindow(target);
        RECT rc;
        if (GetWindowRect(target, &rc)) dest = RectCenter(rc);
    }

    // Keep the cursor inside the destination monitor's work area.
    if (dest.x < dst.rcWork.left)   dest.x = dst.rcWork.left;
    if (dest.x > dst.rcWork.right)  dest.x = dst.rcWork.right  - 1;
    if (dest.y < dst.rcWork.top)    dest.y = dst.rcWork.top;
    if (dest.y > dst.rcWork.bottom) dest.y = dst.rcWork.bottom - 1;

    if (g_settings.focusMoveCursor) SetCursorPos(dest.x, dest.y);
}

// Focus the topmost window on the monitor under the mouse cursor, without
// moving the cursor. Does nothing if focus is already on that monitor or if
// there is no focusable window there.
static void FocusMonitorUnderCursor() {
    POINT pt;
    if (!GetCursorPos(&pt)) { Wh_Log(L"FocusCursor: GetCursorPos failed"); return; }
    HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);

    HWND fg = GetForegroundWindow();
    if (fg && MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST) == hMon) {
        Wh_Log(L"FocusCursor: already focused on cursor's monitor");
        return;
    }

    HWND target = FindTopWindowOnMonitor(hMon);
    Wh_Log(L"FocusCursor: cursor (%d,%d)  target hwnd=%p", pt.x, pt.y, target);
    if (!target) { Wh_Log(L"FocusCursor: no focusable window on monitor"); return; }

    ForceSetForegroundWindow(target);
}

// ── hotkey thread ─────────────────────────────────────────────────────────────

static void RegisterHotkeys(HWND hwnd) {
    UINT mod = g_settings.modifierKeys | MOD_NOREPEAT;
    struct { int id; UINT vk; const wchar_t* name; } keys[] = {
        { HOTKEY_UP,    VK_UP,    L"UP"    },
        { HOTKEY_DOWN,  VK_DOWN,  L"DOWN"  },
        { HOTKEY_LEFT,  VK_LEFT,  L"LEFT"  },
        { HOTKEY_RIGHT, VK_RIGHT, L"RIGHT" },
    };
    for (auto& k : keys) {
        if (RegisterHotKey(hwnd, k.id, mod, k.vk)) {
            Wh_Log(L"RegisterHotKey %s: OK", k.name);
        } else {
            Wh_Log(L"RegisterHotKey %s: FAILED (err %u) -- hotkey is taken by "
                   L"another app", k.name, GetLastError());
        }
    }

    if (g_settings.enableFocusSwitch) {
        UINT fmod = g_settings.focusModifier | MOD_NOREPEAT;
        if (RegisterHotKey(hwnd, HOTKEY_FOCUS_NEXT, fmod, g_settings.focusKey)) {
            Wh_Log(L"RegisterHotKey FOCUS_NEXT: OK");
        } else {
            Wh_Log(L"RegisterHotKey FOCUS_NEXT: FAILED (err %u) -- hotkey is taken "
                   L"by another app", GetLastError());
        }
    }

    if (g_settings.enableFocusCursor) {
        UINT fcmod = g_settings.focusCursorModifier | MOD_NOREPEAT;
        if (RegisterHotKey(hwnd, HOTKEY_FOCUS_CURSOR, fcmod, g_settings.focusCursorKey)) {
            Wh_Log(L"RegisterHotKey FOCUS_CURSOR: OK");
        } else {
            Wh_Log(L"RegisterHotKey FOCUS_CURSOR: FAILED (err %u) -- hotkey is taken "
                   L"by another app", GetLastError());
        }
    }
}

static void UnregisterHotkeys(HWND hwnd) {
    UnregisterHotKey(hwnd, HOTKEY_UP);
    UnregisterHotKey(hwnd, HOTKEY_DOWN);
    UnregisterHotKey(hwnd, HOTKEY_LEFT);
    UnregisterHotKey(hwnd, HOTKEY_RIGHT);
    UnregisterHotKey(hwnd, HOTKEY_FOCUS_NEXT);
    UnregisterHotKey(hwnd, HOTKEY_FOCUS_CURSOR);
}

static LRESULT CALLBACK HotkeyWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_HOTKEY) {
        switch ((int)wParam) {
            case HOTKEY_UP:    MoveActiveWindowInDirection(Direction::Up);    break;
            case HOTKEY_DOWN:  MoveActiveWindowInDirection(Direction::Down);  break;
            case HOTKEY_LEFT:  MoveActiveWindowInDirection(Direction::Left);  break;
            case HOTKEY_RIGHT: MoveActiveWindowInDirection(Direction::Right); break;
            case HOTKEY_FOCUS_NEXT:   FocusNextMonitor();        break;
            case HOTKEY_FOCUS_CURSOR: FocusMonitorUnderCursor(); break;
        }
        return 0;
    }

    if (msg == WM_APP_SETTINGS_CHANGED) {
        // (Un)RegisterHotKey must run on the thread that owns the window
        UnregisterHotkeys(hwnd);
        LoadSettings();
        auto monitors = GetAllMonitors();
        LogAllMonitors(monitors);
        RegisterHotkeys(hwnd);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static void HotkeyThreadProc() {
    const wchar_t CLASS_NAME[] = L"WH_MoveToMonitor_MsgWnd";
    WNDCLASSEX wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = HotkeyWndProc;
    wc.hInstance     = GetModuleHandle(nullptr);
    wc.lpszClassName = CLASS_NAME;
    RegisterClassEx(&wc);

    HWND hwnd = CreateWindowEx(0, CLASS_NAME, nullptr, 0, 0, 0, 0, 0,
                               HWND_MESSAGE, nullptr, GetModuleHandle(nullptr), nullptr);
    if (!hwnd) { Wh_Log(L"Failed to create message window"); return; }

    g_hMsgWnd = hwnd;
    RegisterHotkeys(hwnd);

    MSG msg;
    while (g_running && GetMessage(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnregisterHotkeys(hwnd);
    DestroyWindow(hwnd);
    g_hMsgWnd = nullptr;
    UnregisterClass(CLASS_NAME, GetModuleHandle(nullptr));
}

static DWORD WINAPI HotkeyThreadEntry(LPVOID) {
    HotkeyThreadProc();
    return 0;
}

// ── Tool mod callbacks (renamed as required by the tool mod pattern) ──────────

BOOL WhTool_ModInit() {
    Wh_Log(L"MoveWindowToMonitor v6: Init");
    LoadSettings();
    auto monitors = GetAllMonitors();
    LogAllMonitors(monitors);
    g_running = true;
    g_hThread = CreateThread(nullptr, 0, HotkeyThreadEntry, nullptr, 0, nullptr);
    return g_hThread != nullptr;
}

void WhTool_ModSettingsChanged() {
    Wh_Log(L"MoveWindowToMonitor: Settings changed, notifying hotkey thread");
    if (g_hMsgWnd) PostMessage(g_hMsgWnd, WM_APP_SETTINGS_CHANGED, 0, 0);
}

void WhTool_ModUninit() {
    g_running = false;
    if (g_hMsgWnd) PostMessage(g_hMsgWnd, WM_QUIT, 0, 0);
    if (g_hThread) {
        WaitForSingleObject(g_hThread, 3000);
        CloseHandle(g_hThread);
        g_hThread = nullptr;
    }
}

////////////////////////////////////////////////////////////////////////////////
// Windhawk tool mod implementation for mods which don't need to inject to other
// processes or hook other functions. Context:
// https://github.com/ramensoftware/windhawk/wiki/Mods-as-tools:-Running-mods-in-a-dedicated-process
//
// The mod will load and run in a dedicated windhawk.exe process.

bool g_isToolModProcessLauncher;
HANDLE g_toolModProcessMutex;

void WINAPI EntryPoint_Hook() {
    Wh_Log(L">");
    ExitThread(0);
}

BOOL Wh_ModInit() {
    DWORD sessionId;
    if (ProcessIdToSessionId(GetCurrentProcessId(), &sessionId) &&
        sessionId == 0) {
        return FALSE;
    }

    bool isExcluded = false;
    bool isToolModProcess = false;
    bool isCurrentToolModProcess = false;
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLine(), &argc);
    if (!argv) {
        Wh_Log(L"CommandLineToArgvW failed");
        return FALSE;
    }

    for (int i = 1; i < argc; i++) {
        if (wcscmp(argv[i], L"-service") == 0 ||
            wcscmp(argv[i], L"-service-start") == 0 ||
            wcscmp(argv[i], L"-service-stop") == 0) {
            isExcluded = true;
            break;
        }
    }

    for (int i = 1; i < argc - 1; i++) {
        if (wcscmp(argv[i], L"-tool-mod") == 0) {
            isToolModProcess = true;
            if (wcscmp(argv[i + 1], WH_MOD_ID) == 0) {
                isCurrentToolModProcess = true;
            }
            break;
        }
    }

    LocalFree(argv);

    if (isExcluded) {
        return FALSE;
    }

    if (isCurrentToolModProcess) {
        g_toolModProcessMutex =
            CreateMutex(nullptr, TRUE, L"windhawk-tool-mod_" WH_MOD_ID);
        if (!g_toolModProcessMutex) {
            Wh_Log(L"CreateMutex failed");
            ExitProcess(1);
        }

        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            Wh_Log(L"Tool mod already running (%s)", WH_MOD_ID);
            ExitProcess(1);
        }

        if (!WhTool_ModInit()) {
            ExitProcess(1);
        }

        IMAGE_DOS_HEADER* dosHeader =
            (IMAGE_DOS_HEADER*)GetModuleHandle(nullptr);
        IMAGE_NT_HEADERS* ntHeaders =
            (IMAGE_NT_HEADERS*)((BYTE*)dosHeader + dosHeader->e_lfanew);

        DWORD entryPointRVA = ntHeaders->OptionalHeader.AddressOfEntryPoint;
        void* entryPoint = (BYTE*)dosHeader + entryPointRVA;

        Wh_SetFunctionHook(entryPoint, (void*)EntryPoint_Hook, nullptr);
        return TRUE;
    }

    if (isToolModProcess) {
        return FALSE;
    }

    g_isToolModProcessLauncher = true;
    return TRUE;
}

void Wh_ModAfterInit() {
    if (!g_isToolModProcessLauncher) {
        return;
    }

    WCHAR currentProcessPath[MAX_PATH];
    switch (GetModuleFileName(nullptr, currentProcessPath,
                              ARRAYSIZE(currentProcessPath))) {
        case 0:
        case ARRAYSIZE(currentProcessPath):
            Wh_Log(L"GetModuleFileName failed");
            return;
    }

    WCHAR commandLine[MAX_PATH + 2 +
                      (sizeof(L" -tool-mod \"" WH_MOD_ID "\"") / sizeof(WCHAR)) - 1];
    swprintf_s(commandLine, L"\"%s\" -tool-mod \"%s\"", currentProcessPath,
               WH_MOD_ID);

    HMODULE kernelModule = GetModuleHandle(L"kernelbase.dll");
    if (!kernelModule) {
        kernelModule = GetModuleHandle(L"kernel32.dll");
        if (!kernelModule) {
            Wh_Log(L"No kernelbase.dll/kernel32.dll");
            return;
        }
    }

    using CreateProcessInternalW_t = BOOL(WINAPI*)(
        HANDLE hUserToken, LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
        LPSECURITY_ATTRIBUTES lpProcessAttributes,
        LPSECURITY_ATTRIBUTES lpThreadAttributes, WINBOOL bInheritHandles,
        DWORD dwCreationFlags, LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory,
        LPSTARTUPINFOW lpStartupInfo,
        LPPROCESS_INFORMATION lpProcessInformation,
        PHANDLE hRestrictedUserToken);
    CreateProcessInternalW_t pCreateProcessInternalW =
        (CreateProcessInternalW_t)GetProcAddress(kernelModule,
                                                 "CreateProcessInternalW");
    if (!pCreateProcessInternalW) {
        Wh_Log(L"No CreateProcessInternalW");
        return;
    }

    STARTUPINFO si{
        .cb = sizeof(STARTUPINFO),
        .dwFlags = STARTF_FORCEOFFFEEDBACK,
    };
    PROCESS_INFORMATION pi;
    if (!pCreateProcessInternalW(nullptr, currentProcessPath, commandLine,
                                 nullptr, nullptr, FALSE, NORMAL_PRIORITY_CLASS,
                                 nullptr, nullptr, &si, &pi, nullptr)) {
        Wh_Log(L"CreateProcess failed");
        return;
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

void Wh_ModSettingsChanged() {
    if (g_isToolModProcessLauncher) {
        return;
    }
    WhTool_ModSettingsChanged();
}

void Wh_ModUninit() {
    if (g_isToolModProcessLauncher) {
        return;
    }
    WhTool_ModUninit();
    ExitProcess(0);
}
