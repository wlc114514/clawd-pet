#pragma once
#include <stdlib.h>

// The version this build is. Bump it (and mac/Info.plist) before tagging a
// release; CI checks that the three agree.
#ifndef CLAWD_VERSION   // (tests can pretend to be another one)
#define CLAWD_VERSION "1.1.0"
#endif

// Is version a ("v1.2.3" or "1.2.3") newer than b?
static inline int version_newer(const char *a, const char *b)
{
    if (*a == 'v') a++;
    if (*b == 'v') b++;
    for (int i = 0; i < 3; i++) {
        char *ea, *eb;
        long x = strtol(a, &ea, 10), y = strtol(b, &eb, 10);
        if (x != y) return x > y;
        a = *ea == '.' ? ea + 1 : ea;
        b = *eb == '.' ? eb + 1 : eb;
    }
    return 0;
}
