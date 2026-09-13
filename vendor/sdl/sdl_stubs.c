// AntTweakBarC99 SDL3 vendoring shim: SDL_utils.c's SDL_CreateDeviceName()
// (used for audio/joystick device naming, neither of which this trimmed
// build compiles in - see vendor/sdl/config/SDL_build_config_macos.h)
// unconditionally calls SDL_GetGamepadTypeFromVIDPID(), normally defined in
// the deliberately excluded src/joystick/SDL_joystick.c. This project never
// classifies a VID/PID pair, so a constant stub is enough. Kept here at
// vendor/sdl/ root, not under vendor/sdl/src/, to stay clearly distinct
// from the unmodified upstream tree - see docs/plans/sdl3-backend.md.

#include "SDL_internal.h"
#include <SDL3/SDL_gamepad.h>

SDL_GamepadType SDL_GetGamepadTypeFromVIDPID(Uint16 vendor_id, Uint16 product_id, const char *name, bool forUI)
{
    (void)vendor_id; (void)product_id; (void)name; (void)forUI;
    return SDL_GAMEPAD_TYPE_UNKNOWN;
}
