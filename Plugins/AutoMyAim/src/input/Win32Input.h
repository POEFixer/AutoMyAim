// Win32Input.h - cursor and click input for Auto My Aim and Pick My Loot (the
// two copies are identical).
//
// World-to-screen coordinates from the host are relative to the game window's
// client area (the overlay covers exactly that area). SendInput needs absolute
// desktop coordinates, so every call converts through ClientToScreen first:
// the original plugins assumed the game sat at the desktop origin and aimed
// wrongly in windowed mode or on a second monitor.
#pragma once

#include <Windows.h>

namespace PfInput {

// Client point of the game window -> desktop point. Without a window the point
// is passed through (borderless fullscreen at the origin behaves the same).
inline POINT ClientToDesktop(HWND game, int x, int y) {
    POINT p{x, y};
    if (game) ::ClientToScreen(game, &p);
    return p;
}

inline void NormalizeAbsolute(POINT desk, LONG& nx, LONG& ny) {
    const int vsX = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vsY = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vsW = ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vsH = ::GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (vsW <= 1) vsW = ::GetSystemMetrics(SM_CXSCREEN);
    if (vsH <= 1) vsH = ::GetSystemMetrics(SM_CYSCREEN);
    const long long dw = vsW > 1 ? vsW - 1 : 1;
    const long long dh = vsH > 1 ? vsH - 1 : 1;
    nx = static_cast<LONG>((static_cast<long long>(desk.x - vsX) * 65535 + dw / 2) / dw);
    ny = static_cast<LONG>((static_cast<long long>(desk.y - vsY) * 65535 + dh / 2) / dh);
}

// Move the cursor to a desktop point.
inline void MoveCursorDesktop(POINT desk) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    NormalizeAbsolute(desk, in.mi.dx, in.mi.dy);
    in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    ::SendInput(1, &in, sizeof(INPUT));
}

// Move the cursor to a point of the game's client area.
inline void MoveCursorClient(HWND game, int x, int y) {
    MoveCursorDesktop(ClientToDesktop(game, x, y));
}

// Left click AT a point of the game's client area: both button events carry the
// absolute coordinate, so the click lands there even if something moved the
// cursor in between (the host's hardware click does the same).
inline void LeftClickClient(HWND game, int x, int y) {
    INPUT in[2]{};
    LONG nx = 0, ny = 0;
    NormalizeAbsolute(ClientToDesktop(game, x, y), nx, ny);
    const DWORD move = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    for (INPUT& i : in) {
        i.type = INPUT_MOUSE;
        i.mi.dx = nx;
        i.mi.dy = ny;
    }
    in[0].mi.dwFlags = move | MOUSEEVENTF_LEFTDOWN;
    in[1].mi.dwFlags = move | MOUSEEVENTF_LEFTUP;
    ::SendInput(2, in, sizeof(INPUT));
}

inline bool GetCursorDesktop(POINT& out) {
    return ::GetCursorPos(&out) != FALSE;
}

inline bool IsKeyDown(int vk) {
    if (vk <= 0 || vk > 0xFE) return false;
    return (::GetAsyncKeyState(vk) & 0x8000) != 0;
}

}  // namespace PfInput
