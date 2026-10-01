// AntTweakBarC99 SDL3 vendoring: trimmed Windows build config.
//
// This is a project-authored replacement for upstream's
// include/build_config/SDL_build_config_windows.h - not a copy of it. Mirrors
// SDL_build_config_macos.h's policy: enable only what this project's
// examples need (Windows video, OpenGL/WGL context creation, events,
// threads, timers, filesystem) and explicitly disable every subsystem
// AntTweakBarC99 has no use for (audio, camera, joystick, haptic, sensor,
// GPU, dialog, process, power) via SDL's own SDL_*_DISABLED config macros -
// see docs/plans/sdl3-backend.md Step 4 for the compile spike that validated
// this exact set on MinGW.
//
// SDL_VIDEO_RENDER_OGL and the render/software + render/opengl sources are
// NOT optional here despite being unused by this project, same reasoning as
// the macOS header: SDL_internal.h force-defines SDL_VIDEO_RENDER_SW
// whenever the render subsystem isn't fully disabled, and SDL_video.c's
// legacy SDL_GetWindowSurface() path hard-depends on it. Same reasoning for
// SDL_TRAY_DUMMY (SDL_windowsevents.c references tray functions
// unconditionally); the dummy backend is a no-op.

#ifndef SDL_build_config_h_
#define SDL_build_config_h_

#include <SDL3/SDL_platform_defines.h>

#define HAVE_STDARG_H 1
#define HAVE_STDDEF_H 1
#define HAVE_LIBC 1

/* Useful headers */
#define HAVE_FLOAT_H 1
#define HAVE_LIMITS_H 1
#define HAVE_MATH_H 1
#define HAVE_SIGNAL_H 1
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_WCHAR_H 1
#define HAVE_STDINT_H 1

/* C library functions (MinGW's runtime provides all of these) */
#define HAVE_MALLOC 1
#define HAVE_ABS 1
#define HAVE_MEMSET 1
#define HAVE_MEMCPY 1
#define HAVE_MEMMOVE 1
#define HAVE_MEMCMP 1
#define HAVE_STRLEN 1
#define HAVE__STRREV 1
#define HAVE_STRCHR 1
#define HAVE_STRRCHR 1
#define HAVE_STRSTR 1
#define HAVE_STRTOL 1
#define HAVE_STRTOUL 1
#define HAVE_STRTOLL 1
#define HAVE_STRTOULL 1
#define HAVE_STRTOD 1
#define HAVE_ATOI 1
#define HAVE_ATOF 1
#define HAVE_STRCMP 1
#define HAVE_STRNCMP 1
#define HAVE_STRPBRK 1
#define HAVE_VSNPRINTF 1
#define HAVE_VSSCANF 1
#define HAVE_ACOS 1
#define HAVE_ACOSF 1
#define HAVE_ASIN 1
#define HAVE_ASINF 1
#define HAVE_ATAN 1
#define HAVE_ATANF 1
#define HAVE_ATAN2 1
#define HAVE_ATAN2F 1
#define HAVE_CEIL 1
#define HAVE_CEILF 1
#define HAVE__COPYSIGN 1
#define HAVE_COS 1
#define HAVE_COSF 1
#define HAVE_EXP 1
#define HAVE_EXPF 1
#define HAVE_FABS 1
#define HAVE_FABSF 1
#define HAVE_FLOOR 1
#define HAVE_FLOORF 1
#define HAVE_FMOD 1
#define HAVE_FMODF 1
#define HAVE_ISINF 1
#define HAVE_ISINF_FLOAT_MACRO 1
#define HAVE_ISNAN 1
#define HAVE_ISNAN_FLOAT_MACRO 1
#define HAVE_LOG 1
#define HAVE_LOGF 1
#define HAVE_LOG10 1
#define HAVE_LOG10F 1
#define HAVE_LROUND 1
#define HAVE_LROUNDF 1
#define HAVE_POW 1
#define HAVE_POWF 1
#define HAVE_ROUND 1
#define HAVE_ROUNDF 1
#define HAVE_SCALBN 1
#define HAVE_SCALBNF 1
#define HAVE_SIN 1
#define HAVE_SINF 1
#define HAVE_SQRT 1
#define HAVE_SQRTF 1
#define HAVE_TAN 1
#define HAVE_TANF 1
#define HAVE_TRUNC 1
#define HAVE_TRUNCF 1

/* Subsystems this project does not need */
#define SDL_AUDIO_DISABLED 1
#define SDL_CAMERA_DISABLED 1
#define SDL_JOYSTICK_DISABLED 1
#define SDL_HAPTIC_DISABLED 1
#define SDL_SENSOR_DISABLED 1
#define SDL_GPU_DISABLED 1
#define SDL_DIALOG_DISABLED 1
#define SDL_PROCESS_DISABLED 1
#define SDL_POWER_DISABLED 1

#define SDL_LOADSO_WINDOWS 1
#define SDL_THREAD_GENERIC_COND_SUFFIX 1
#define SDL_THREAD_GENERIC_RWLOCK_SUFFIX 1
#define SDL_THREAD_WINDOWS 1
#define SDL_TIME_WINDOWS 1
#define SDL_TIMER_WINDOWS 1

#define SDL_VIDEO_DRIVER_WINDOWS 1
#define SDL_VIDEO_OPENGL 1
#define SDL_VIDEO_OPENGL_WGL 1
#define SDL_VIDEO_RENDER_OGL 1

#define SDL_TRAY_DUMMY 1

#define SDL_FILESYSTEM_WINDOWS 1
#define SDL_FSOPS_WINDOWS 1

#endif
