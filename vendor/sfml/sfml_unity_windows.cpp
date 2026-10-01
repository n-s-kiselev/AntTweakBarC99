// sfml_unity_windows.cpp - Windows counterpart of sfml_unity.mm (see that
// file's own header comment for the general rationale: single-translation-
// unit build of the vendored SFML 3.1.0 System+Window module sources,
// validated unity-build-safe by the Step 1 compile spike in
// docs/plans/sfml3-backend.md).
//
// A separate .cpp file, not a shared file selected by extension: MinGW GCC
// has no Objective-C++ front end at all (confirmed directly - both
// `cc -x objective-c++` and `c++` on a trivial .mm file fail with
// "Objective-C++ compiler not installed on this system"), so sfml_unity.mm
// itself cannot be handed to it even for a plain-C++, no-Cocoa-code build.
// nob.c's SFML_SRC picks this file on Windows instead (see build_sfml()'s
// own comment) - see docs/plans/sfml3-backend.md Step 4/5 for the Windows
// validation this file is part of.
//
// Platform selection inside SFML's own headers: include/SFML/Config.hpp
// detects the platform purely from compiler-predefined macros (_WIN32),
// same as the macOS build - no -D flags or project-authored config header
// needed here either.

// -- System module (platform-independent + Windows) --
#include "src/SFML/System/Clock.cpp"
#include "src/SFML/System/Err.cpp"
#include "src/SFML/System/Sleep.cpp"
#include "src/SFML/System/String.cpp"
#include "src/SFML/System/TimeoutWithPredicate.cpp"
#include "src/SFML/System/Utils.cpp"
#include "src/SFML/System/Version.cpp"
#include "src/SFML/System/FileInputStream.cpp"
#include "src/SFML/System/MemoryInputStream.cpp"
#include "src/SFML/System/Win32/SleepImpl.cpp"

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

// -- Window module: Win32 backend --
#include "src/SFML/Window/Win32/ClipboardImpl.cpp"
#include "src/SFML/Window/Win32/CursorImpl.cpp"
#include "src/SFML/Window/Win32/InputImpl.cpp"
#include "src/SFML/Window/Win32/JoystickImpl.cpp"
#include "src/SFML/Window/Win32/SensorImpl.cpp"
#include "src/SFML/Window/Win32/VideoModeImpl.cpp"
// Unlike macOS (Vulkan.cpp's own #if defines SFML_VULKAN_IMPLEMENTATION_NOT_AVAILABLE
// there, needing no impl file at all - confirmed by reading Vulkan.cpp
// directly), Windows takes Vulkan.cpp's "#if defined(SFML_SYSTEM_WINDOWS)"
// branch, which #includes <SFML/Window/VulkanImpl.hpp> unconditionally -
// so VulkanImplWin32.cpp must be linked in even though this project has no
// Vulkan use of its own (Vulkan::isAvailable()/getFunction() still need a
// real definition to link, matching the "only what's needed" policy's own
// exception for SDL3's render/opengl+render/software above).
#include "src/SFML/Window/Win32/VulkanImplWin32.cpp"
#include "src/SFML/Window/Win32/WglContext.cpp"
#include "src/SFML/Window/Win32/WindowImplWin32.cpp"
