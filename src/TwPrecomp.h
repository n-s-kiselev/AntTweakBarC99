//  ---------------------------------------------------------------------------
//
//  @file       TwPrecomp.h
//  @brief      Precompiled header
//  @author     Philippe Decaudin
//  @license    This file is part of the AntTweakBar library.
//              For conditions of distribution and use, see License.txt
//
//  note:       Private header
//
//  ---------------------------------------------------------------------------


#if !defined ANT_TW_PRECOMP_INCLUDED
#define ANT_TW_PRECOMP_INCLUDED


#if defined _MSC_VER
#   pragma warning(disable: 4514)   // unreferenced inline function has been removed
#   pragma warning(disable: 4710)   // function not inlined
#   pragma warning(disable: 4786)   // template name truncated
#   pragma warning(disable: 4530)   // exceptions not handled
#   define _CRT_SECURE_NO_DEPRECATE // visual 8 secure crt warning
#endif

#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <float.h>
#include <string.h>
#include <stdlib.h>
#include <memory.h>
#include <ctype.h>

// min/max: TwBar.c/TwMgr.c used to get these from <algorithm> transitively
// (via <vector>/<map>/... plus `using namespace std;`) now that those C++
// headers are gone. #ifndef-guarded so a platform header that already
// defines them as macros (e.g. Windows' windows.h, included below, unless
// the includer defines NOMINMAX first) keeps taking precedence, unchanged
// from before this file dropped its C++ standard-library includes.
#ifndef min
#   define min(a, b) ((a)<(b) ? (a) : (b))
#endif
#ifndef max
#   define max(a, b) ((a)>(b) ? (a) : (b))
#endif

#include "sds.h" // replaces std::string for the library's own internal string storage

#if defined(_UNIX)
#   define ANT_UNIX
#   include <X11/cursorfont.h>
#   define GLX_GLXEXT_LEGACY
#   include <GL/glx.h>
#   include <X11/Xatom.h>
#   include <unistd.h>
#   include <malloc.h>
#   undef _WIN32
#   undef WIN32
#   undef _WIN64
#   undef WIN64
#   undef _WINDOWS
#   undef ANT_WINDOWS
#   undef ANT_OSX
#elif defined(_MACOSX)
#   define ANT_OSX
#   include <unistd.h>
#   undef _WIN32
#   undef WIN32
#   undef _WIN64
#   undef WIN64
#   undef _WINDOWS
#   undef ANT_WINDOWS
#   undef ANT_UNIX
#elif defined(_WINDOWS) || defined(WIN32) || defined(WIN64) || defined(_WIN32) || defined(_WIN64)
#   define ANT_WINDOWS
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN // Exclude rarely-used stuff from Windows headers
#   endif
#   include <windows.h>
#   include <shellapi.h>
#endif

#if !defined(ANT_OGL_HEADER_INCLUDED)
#   if defined(ANT_OSX)
#   	include <OpenGL/gl.h>
#   else
#	    include <GL/gl.h>  // must be included after windows.h
#   endif
#   define  ANT_OGL_HEADER_INCLUDED
#endif

#endif  // !defined ANT_TW_PRECOMP_INCLUDED
