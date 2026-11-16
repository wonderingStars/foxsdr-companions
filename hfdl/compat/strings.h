/* Windows/MSVC shim for <strings.h>, written for this helper (not upstream code). */
#pragma once
#include <string.h>
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
