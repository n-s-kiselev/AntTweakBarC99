// AntTweakBarC99 SDL3 vendoring: per-platform config dispatch.
//
// Project-authored replacement for upstream's
// include/build_config/SDL_build_config.h. src/SDL_internal.h does
// #include "SDL_build_config.h" (quoted, so this file - reached first via
// nob.c's include-path ordering - wins over the unused upstream copy still
// present under vendor/sdl/include/build_config/ for provenance).
//
// See docs/plans/sdl3-backend.md for which platforms have been validated.

#ifndef SDL_build_config_h_

#include <SDL3/SDL_platform_defines.h>

#if defined(SDL_PLATFORM_MACOS)
#include "SDL_build_config_macos.h"
#elif defined(SDL_PLATFORM_LINUX)
#error "vendor/sdl/config/SDL_build_config_linux.h not implemented yet - see docs/plans/sdl3-backend.md Step 4"
#elif defined(SDL_PLATFORM_WIN32)
#error "vendor/sdl/config/SDL_build_config_windows.h not implemented yet - see docs/plans/sdl3-backend.md Step 4"
#else
#error "AntTweakBarC99's vendored SDL3 build has not been configured for this platform"
#endif

#endif /* SDL_build_config_h_ */
