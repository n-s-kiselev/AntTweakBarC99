// sfml_unity.mm - single-file compilation of the vendored SFML 3.1.0
// System+Window module sources under src/SFML/, so the examples link
// against a built-in SFML instead of requiring one to be installed
// system-wide. Same pattern as vendor/glfw/glfw_unity.c, but - unlike
// vendor/sdl/ (SDL3's private headers aren't multi-inclusion-safe, so
// that backend is compiled per-file and archived instead, see
// docs/plans/sdl3-backend.md) - a real compile spike confirmed SFML's own
// source is unity-build-safe (see docs/plans/sfml3-backend.md Step 1).
//
// Platform selection: SFML's own include/SFML/Config.hpp detects the
// platform purely from compiler-predefined macros (__APPLE__/_WIN32/
// __linux__), unlike SDL3 - no -D flags or project-authored config header
// needed here (see build_sfml() in nob.c). Currently only the macOS file
// list is validated; Linux/Windows sections are not yet present - see
// docs/plans/sfml3-backend.md.
//
// Compiled as Objective-C++ (-std=c++17, no -fobjc-arc: the macOS backend
// files use manual retain/release, not ARC - confirmed by the same
// compile spike, the opposite of vendor/sdl/'s Cocoa files).

// -- System module (platform-independent + Unix) --
#include "src/SFML/System/Clock.cpp"
#include "src/SFML/System/Err.cpp"
#include "src/SFML/System/Sleep.cpp"
#include "src/SFML/System/String.cpp"
#include "src/SFML/System/TimeoutWithPredicate.cpp"
#include "src/SFML/System/Utils.cpp"
#include "src/SFML/System/Version.cpp"
#include "src/SFML/System/FileInputStream.cpp"
#include "src/SFML/System/MemoryInputStream.cpp"
#include "src/SFML/System/Unix/SleepImpl.cpp"

// -- Window module: common sources (every platform) --
#include "src/SFML/Window/Clipboard.cpp"
#include "src/SFML/Window/Context.cpp"
#include "src/SFML/Window/Cursor.cpp"
#include "src/SFML/Window/GlContext.cpp"
#include "src/SFML/Window/GlResource.cpp"
#include "src/SFML/Window/Joystick.cpp"
#include "src/SFML/Window/JoystickManager.cpp"
#include "src/SFML/Window/Keyboard.cpp"
#include "src/SFML/Window/Mouse.cpp"
#include "src/SFML/Window/Touch.cpp"
#include "src/SFML/Window/Sensor.cpp"
#include "src/SFML/Window/SensorManager.cpp"
#include "src/SFML/Window/VideoMode.cpp"
#include "src/SFML/Window/Vulkan.cpp"
#include "src/SFML/Window/Window.cpp"
#include "src/SFML/Window/WindowBase.cpp"
#include "src/SFML/Window/WindowImpl.cpp"

// -- Window module: macOS/Cocoa backend --
#if defined(__APPLE__)
#include "src/SFML/Window/macOS/cg_sf_conversion.mm"
#include "src/SFML/Window/macOS/CursorImpl.mm"
#include "src/SFML/Window/macOS/ClipboardImpl.mm"
#include "src/SFML/Window/macOS/InputImpl.mm"
#include "src/SFML/Window/macOS/HIDInputManager.mm"
#include "src/SFML/Window/macOS/HIDJoystickManager.cpp"
#include "src/SFML/Window/macOS/JoystickImpl.cpp"
#include "src/SFML/Window/macOS/NSImage+raw.mm"
#include "src/SFML/Window/macOS/SensorImpl.cpp"
#include "src/SFML/Window/macOS/SFApplication.m"
#include "src/SFML/Window/macOS/SFApplicationDelegate.m"
#include "src/SFML/Window/macOS/SFContext.mm"
#include "src/SFML/Window/macOS/SFKeyboardModifiersHelper.mm"
#include "src/SFML/Window/macOS/SFOpenGLView.mm"
#include "src/SFML/Window/macOS/SFOpenGLView+keyboard.mm"
#include "src/SFML/Window/macOS/SFOpenGLView+mouse.mm"
#include "src/SFML/Window/macOS/SFSilentResponder.m"
#include "src/SFML/Window/macOS/SFWindow.m"
#include "src/SFML/Window/macOS/SFWindowController.mm"
#include "src/SFML/Window/macOS/SFViewController.mm"
#include "src/SFML/Window/macOS/VideoModeImpl.cpp"
#include "src/SFML/Window/macOS/WindowImplCocoa.mm"
#include "src/SFML/Window/macOS/AutoreleasePoolWrapper.mm"
#endif
