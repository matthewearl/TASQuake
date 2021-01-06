#pragma once
#pragma warning(disable : 4010)
extern "C"
{
#include "../quakedef.h"
}


#define TAS_MIN(a, b)   ((a) < (b) ? (a) : (b))
#define TAS_MAX(a, b)   ((a) > (b) ? (a) : (b))


#ifndef _WIN32

// I think these are the equivalents on Linux...
#define sprintf_s snprintf
#define vsprintf_s vsnprintf
#define sscanf_s sscanf

// These macro definitions break some libstdc++ imports on linux, eg algorithmfwd.h
// Drop the definitions from at this point, and use TAS_MIN and TAS_MAX in C++ code.
#undef max
#undef min

// Redefining rand similarly breaks eg. stl_algo.h
#undef rand

#endif /* _WIN32 */


#include "json.hpp"


#ifndef ARRAYSIZE
#define ARRAYSIZE(a) \
      ((sizeof(a) / sizeof(*(a))) / \
         static_cast<size_t>(!(sizeof(a) % sizeof(*(a)))))
#endif /* ARRAY_SIZE */

