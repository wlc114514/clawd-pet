/*
 * Windows side of the platform layer in platform.h. Everything the pet core
 * needs from the OS lives here, so the core itself stays portable.
 */
#include "platform.h"

#ifdef _WIN32

DWORD plat_idle_ms(void)
{
    LASTINPUTINFO li = { sizeof li };
    GetLastInputInfo(&li);
    return GetTickCount() - li.dwTime;
}

ULONGLONG plat_ticks_ms(void)
{
    return GetTickCount64();
}

void plat_cursor(POINT *out)
{
    GetCursorPos(out);
}

void plat_screen_rects(POINT pt, RECT *work, RECT *frame)
{
    MONITORINFO mi = { sizeof mi };
    if (GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi)) {
        *work = mi.rcWork;
        *frame = mi.rcMonitor;
    } else {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, work, 0);
        *frame = *work;
    }
}

void plat_work_area(RECT *work, RECT *frame)
{
    POINT origin = { 0, 0 };
    plat_screen_rects(origin, work, frame);
}

#endif
