#define NOB_IMPLEMENTATION
#include "vendor/nob/nob.h"

#define SRC_FOLDER           "src/"
#define INCLUDE_FOLDER       "include/"
#define BUILD_FOLDER         "build/"
#define BUILD_STATIC_FOLDER  "build/static/"
#define BUILD_SHARED_FOLDER  "build/shared/"
// Single object holding glad.o + TwOpenGL.o + TwOpenGLCore.o with every GLAD
// symbol made local - see prelink_renderers().
#define RENDERERS_OBJ_NAME   "TwRenderers.o"
#define TEST_BUILD_FOLDER    "build/tests/"
// Every build artifact - including the final libraries and the copy of the
// public header consuming code would build against - lives under
// BUILD_FOLDER, keeping the repository root free of anything but source.
#define LIB_FOLDER            BUILD_FOLDER "lib/"
#define BUILD_INCLUDE_FOLDER  BUILD_FOLDER "include/"
#define NOB_HEADER           "vendor/nob/nob.h"

// Named libAntTweakBarC99, not libAntTweakBarGLFW3, to avoid colliding with
// the sibling AntTweakBarGLFW3 fork's own build of the (still C++) library
// of the same name - this is the C99 rewrite's distinct artifact name.
#define LIB_STATIC LIB_FOLDER "libAntTweakBarC99.a"

#if defined(_WIN32)
#define LIB_SHARED LIB_FOLDER "libAntTweakBarC99.dll"
#define LIB_IMPORT LIB_FOLDER "libAntTweakBarC99.dll.a"
#elif defined(__APPLE__)
#define LIB_SHARED LIB_FOLDER "libAntTweakBarC99.dylib"
#else
#define LIB_SHARED        LIB_FOLDER "libAntTweakBarC99.so"
#define LIB_SHARED_SONAME LIB_FOLDER "libAntTweakBarC99.so.1"
#define LIB_SHARED_SONAME_NAME "libAntTweakBarC99.so.1"
#endif

// Which windowing/event backend an example is built against - threaded
// through example_output_folder()/example_executable_path()/build_example()/
// build_examples() below instead of a per-call bool now that there are
// three (see docs/plans/sfml3-backend.md).
typedef enum {
    BACKEND_GLFW,
    BACKEND_SDL,
    BACKEND_SFML,
    BACKEND_RAYLIB,
} Backend;

static const char *backend_name(Backend backend)
{
    switch (backend) {
    case BACKEND_GLFW:   return "GLFW3";
    case BACKEND_SDL:    return "SDL3";
    case BACKEND_SFML:   return "SFML3";
    case BACKEND_RAYLIB: return "raylib";
    }
    return "";
}

// raylib statically contains its own copy of GLAD (rcore.c pulls it in via
// rlgl.h), with the same global glad_gl*/GLAD_GL_* symbol names this project's
// own vendor/glad uses. That used to force the raylib examples to link the
// shared library; prelink_renderers() now makes the library's GLAD private, so
// the two copies coexist and raylib links statically like every other backend.

#define EXAMPLES_FOLDER        "examples/"
#define EXAMPLES_GLFW_FOLDER   EXAMPLES_FOLDER "glfw/"
#define EXAMPLES_SDL_FOLDER    EXAMPLES_FOLDER "sdl/"
#define EXAMPLES_SFML_FOLDER   EXAMPLES_FOLDER "sfml/"
#define EXAMPLES_RAYLIB_FOLDER EXAMPLES_FOLDER "raylib/"
#define EXAMPLES_BUILD_FOLDER "build/examples/"
// Split by link mode AND backend, not just a shared EXAMPLES_BUILD_FOLDER,
// so switching between `-examples-glfw`/`-examples-sdl`/`-examples-sfml` and
// plain/`-dynamic` always rebuilds and never overwrites another
// combination's binaries: build_needed() only compares mtimes against a
// fixed output path, so any two of these six combinations sharing one
// executable path could otherwise look "up to date" against the wrong
// combination's binary left over from a previous run - each combination
// now gets its own folder.
#define EXAMPLES_STATIC_GLFW_FOLDER EXAMPLES_BUILD_FOLDER "static-glfw/"
#define EXAMPLES_STATIC_SDL_FOLDER  EXAMPLES_BUILD_FOLDER "static-sdl/"
#define EXAMPLES_STATIC_SFML_FOLDER EXAMPLES_BUILD_FOLDER "static-sfml/"
#define EXAMPLES_SHARED_GLFW_FOLDER EXAMPLES_BUILD_FOLDER "shared-glfw/"
#define EXAMPLES_SHARED_SDL_FOLDER  EXAMPLES_BUILD_FOLDER "shared-sdl/"
#define EXAMPLES_SHARED_SFML_FOLDER EXAMPLES_BUILD_FOLDER "shared-sfml/"
#define EXAMPLES_STATIC_RAYLIB_FOLDER EXAMPLES_BUILD_FOLDER "static-raylib/"
#define EXAMPLES_SHARED_RAYLIB_FOLDER EXAMPLES_BUILD_FOLDER "shared-raylib/"

// sds (Simple Dynamic Strings, vendored from https://github.com/antirez/sds,
// BSD-2-Clause) replaces std::string for the library's own internal string
// storage as part of the C99 rewrite - see docs/plans/sds-string-migration.md.
// Needed only by the library itself, not by examples.
#define SDS_INCLUDE "vendor/sds/"
#define SDS_SRC     "vendor/sds/sds.c"

// GLAD is needed by the library itself (TwOpenGLCore.cpp's Core Profile
// renderer includes <glad/glad.h> - a fork-specific change from stock
// upstream, which used GLEW/gl3.h) as well as by every example, so it's
// compiled twice: once per library object-set (see common_sources below)
// and once more for the examples (see build_glad_for_examples()).
#define GLAD_INCLUDE  "vendor/glad/include/"
#define GLAD_SRC      "vendor/glad/src/glad.c"
#define GLAD_OBJ      EXAMPLES_BUILD_FOLDER "glad.o"

// GLFW3 is vendored (unity build, see vendor/glfw/glfw_unity.c, already
// present in this repo and written in anticipation of this function - its
// own header comment names append_glfw_flags() by name) so examples need no
// system GLFW3 install on any platform. The library itself does not link
// GLFW at all (TwEventGLFW.c only needs the private MiniGLFW.h constants).
#define GLFW_INCLUDE  "vendor/glfw/include/"
#define GLFW_SRC      "vendor/glfw/glfw_unity.c"
#define GLFW_OBJ      EXAMPLES_BUILD_FOLDER "glfw.o"

// SDL3 is vendored the same way GLFW3 is (see vendor/sdl/, examples need no
// system SDL3 install), but built differently: SDL3's private headers are
// not safe to concatenate into one translation unit the way GLFW's are
// (see docs/plans/sdl3-backend.md), so instead of one glfw_unity.c-style
// object, sdl_sources[] below lists the exact upstream files needed and
// each is compiled to its own object (build_sdl(), mirroring build_object()/
// build_static_archive()'s pattern from the library build above), then
// archived into SDL_LIB. vendor/sdl/config/ holds this project's own,
// trimmed SDL_build_config.h (video+events+GL only - audio/joystick/
// haptic/sensor/camera/GPU/dialog/process compiled out via SDL's own
// SDL_*_DISABLED macros) - not upstream's include/build_config/, which is
// still vendored verbatim for provenance but otherwise unused.
#define SDL_INCLUDE        "vendor/sdl/include/"
#define SDL_CONFIG_INCLUDE "vendor/sdl/config/"
#define SDL_SRC_ROOT       "vendor/sdl/src/"
#define SDL_STUB_SRC       "vendor/sdl/sdl_stubs.c"
#define SDL_OBJ_FOLDER     EXAMPLES_BUILD_FOLDER "sdl_obj/"
#define SDL_LIB            EXAMPLES_BUILD_FOLDER "libsdl3_vendored.a"

// SFML3 is vendored too (vendor/sfml/, System+Window modules only - no
// Graphics/Audio/Network, see docs/plans/sfml3-backend.md), and - unlike
// SDL3 - a compile spike found its sources ARE unity-build-safe, so this
// is a single glfw_unity.c-style object again, not an archive. Unlike
// both GLFW and SDL3, SFML needs no platform -D flags or project-authored
// config header at all: include/SFML/Config.hpp detects the platform from
// compiler-predefined macros on its own.
#define SFML_INCLUDE  "vendor/sfml/include/"
// MinGW GCC has no Objective-C++ front end at all (confirmed directly - see
// docs/plans/sfml3-backend.md Step 4/5), so the Windows build uses a
// separate, plain-C++ unity file instead of sfml_unity.mm.
#if defined(_WIN32)
#define SFML_SRC "vendor/sfml/sfml_unity_windows.cpp"
#else
#define SFML_SRC "vendor/sfml/sfml_unity.mm"
#endif
#define SFML_OBJ      EXAMPLES_BUILD_FOLDER "sfml.o"

// raylib is vendored as upstream's src/ tree (vendor/raylib/src/, plus its
// LICENSE and README), so examples need no system raylib install. Built the
// way upstream's own Makefile does for PLATFORM_DESKTOP: one object per
// module, archived into libraylib.a. Only src/ is vendored - upstream's
// examples/, projects/, tools/, logo/ and cmake/ trees are build tooling and
// sample code this project does not use (they are ~80% of the distribution).
//
// raylib brings its own GLFW (src/external/glfw, compiled by rglfw.c), so a
// raylib example must NOT also link vendor/glfw's GLFW_OBJ, nor the examples'
// own GLAD_OBJ - raylib's rcore.o already provides both.
#define RAYLIB_SRC_FOLDER "vendor/raylib/src/"
#define RAYLIB_OBJ_FOLDER EXAMPLES_BUILD_FOLDER "raylib_obj/"
#define RAYLIB_LIB        EXAMPLES_BUILD_FOLDER "libraylib_vendored.a"

static const char *raylib_sources[] = {
    RAYLIB_SRC_FOLDER "rcore.c",
    RAYLIB_SRC_FOLDER "rglfw.c",
    RAYLIB_SRC_FOLDER "rshapes.c",
    RAYLIB_SRC_FOLDER "rtextures.c",
    RAYLIB_SRC_FOLDER "rtext.c",
    RAYLIB_SRC_FOLDER "rmodels.c",
    RAYLIB_SRC_FOLDER "raudio.c",
};

#if defined(_WIN32)
#define EXE_EXT ".exe"
#else
#define EXE_EXT ""
#endif

// This project builds these GLFW3+glad examples only, named for what they
// demonstrate rather than a legacy toolkit/version (dropped the "Tw" prefix
// and any GLFW-version-looking suffix). See
// docs/plans/examples-consolidation.md for the full survey and per-file
// mapping of the legacy GLFW2
// (TwSimpleGLFW.c/TwSimpleGLFW2.c/TwMultiCubesGLFW.c/TwParticlesGLFW.c/
// TwQuadGLFW.c/TwStripGLFW.c/TwTriangleGLFW.c/TwSpongeGLFW.cpp) and GLUT
// (TwSimpleGLUT.c/TwDualGLUT.c/TwString.cpp) sources some of these were
// ported from: all have been removed from examples/, their unique content
// preserved by porting to GLFW3+C99, except TwQuadGLFW.c, dropped outright
// as a redundant subset of Shapes.c's superset demo. The SDL/SFML examples
// and the untouched legacy DirectX9/10/11 examples were already removed -
// vendored FreeGLUT (formerly external/freeglut/, removed entirely) had no
// buildable source for Linux/macOS anyway (headers + prebuilt Windows DLLs
// only), and DirectX/SDL/SFML are out of scope for this GLFW3/Core-Profile-
// focused project (see docs/plans/nob-build-system.md).
static const char *glfw_examples[] = {
    EXAMPLES_GLFW_FOLDER "SimpleGL21_glfw.c",
    EXAMPLES_GLFW_FOLDER "SimpleGL33_glfw.c",
    EXAMPLES_GLFW_FOLDER "SimpleGL41_glfw.c",
    EXAMPLES_GLFW_FOLDER "Shapes_glfw.c",
    EXAMPLES_GLFW_FOLDER "MultiCubes_glfw.c",
    EXAMPLES_GLFW_FOLDER "Particles_glfw.c",
    EXAMPLES_GLFW_FOLDER "Strip_glfw.c",
    EXAMPLES_GLFW_FOLDER "Triangle_glfw.c",
    EXAMPLES_GLFW_FOLDER "Sponge_glfw.c",
    EXAMPLES_GLFW_FOLDER "String_glfw.c",
    EXAMPLES_GLFW_FOLDER "MultiWindow_glfw.c",
    EXAMPLES_GLFW_FOLDER "Advanced_c99_glfw.c",
    EXAMPLES_GLFW_FOLDER "Advanced_cpp_glfw.cpp",
};

// SDL3 ports of the examples above - see docs/plans/sdl3-backend.md Step 5.
static const char *sdl_examples[] = {
    EXAMPLES_SDL_FOLDER "SimpleGL21_sdl.c",
    EXAMPLES_SDL_FOLDER "SimpleGL33_sdl.c",
    EXAMPLES_SDL_FOLDER "SimpleGL41_sdl.c",
    EXAMPLES_SDL_FOLDER "Shapes_sdl.c",
    EXAMPLES_SDL_FOLDER "MultiCubes_sdl.c",
    EXAMPLES_SDL_FOLDER "Particles_sdl.c",
    EXAMPLES_SDL_FOLDER "Strip_sdl.c",
    EXAMPLES_SDL_FOLDER "Triangle_sdl.c",
    EXAMPLES_SDL_FOLDER "Sponge_sdl.c",
    EXAMPLES_SDL_FOLDER "String_sdl.c",
    EXAMPLES_SDL_FOLDER "MultiWindow_sdl.c",
    EXAMPLES_SDL_FOLDER "Advanced_c99_sdl.c",
    EXAMPLES_SDL_FOLDER "Advanced_cpp_sdl.cpp",
};

// SFML3 ports of the examples above - see docs/plans/sfml3-backend.md.
// All entries are .cpp: SFML has no C API at all (unlike GLFW/SDL3), so
// every SFML example must be C++, even the ones ported from a plain-C99
// GLFW3/SDL3 original.
static const char *sfml_examples[] = {
    EXAMPLES_SFML_FOLDER "SimpleGL21_sfml.cpp",
    EXAMPLES_SFML_FOLDER "SimpleGL33_sfml.cpp",
    EXAMPLES_SFML_FOLDER "SimpleGL41_sfml.cpp",
    EXAMPLES_SFML_FOLDER "Shapes_sfml.cpp",
    EXAMPLES_SFML_FOLDER "MultiCubes_sfml.cpp",
    EXAMPLES_SFML_FOLDER "Particles_sfml.cpp",
    EXAMPLES_SFML_FOLDER "Strip_sfml.cpp",
    EXAMPLES_SFML_FOLDER "Triangle_sfml.cpp",
    EXAMPLES_SFML_FOLDER "Sponge_sfml.cpp",
    EXAMPLES_SFML_FOLDER "String_sfml.cpp",
    EXAMPLES_SFML_FOLDER "MultiWindow_sfml.cpp",
    EXAMPLES_SFML_FOLDER "Advanced_c99_sfml.cpp",
    EXAMPLES_SFML_FOLDER "Advanced_cpp_sfml.cpp",
};

// raylib port. Only one example so far - this backend is new; the remaining
// twelve follow once its shape is settled.
static const char *raylib_examples[] = {
    EXAMPLES_RAYLIB_FOLDER "Simple_raylib.c",
};

// The exact upstream vendor/sdl/src/ files needed for a working
// OpenGL+events SDL3 build, validated by compiling and running a real
// SDL_Init/CreateWindow/GL_CreateContext/PollEvent/GL_SwapWindow test
// program (see docs/plans/sdl3-backend.md Step 1 for macOS, Step 4 for
// Windows). Paths are relative to SDL_SRC_ROOT. Audio/camera/joystick/
// haptic/sensor/GPU/dialog/process are deliberately absent (disabled via
// vendor/sdl/config/'s SDL_*_DISABLED defines) - render/opengl+
// render/software and tray/dummy are present even though unused because
// SDL_internal.h/SDL_video.c hard-depend on them whenever render/tray
// aren't fully disabled (see vendor/sdl/config/SDL_build_config_macos.h's
// own header comment).
//
// Split into a platform-neutral list plus one list per platform (mirroring
// sfml_sources_common/sfml_sources_macos below): the previous single list
// mixed in macOS-only backend files (Cocoa video, pthread threading, dlopen
// loadso, BSD locale/url) that have no meaning on Windows.
static const char *sdl_sources_common[] = {
    "atomic/SDL_atomic.c", "atomic/SDL_spinlock.c",
    "cpuinfo/SDL_cpuinfo.c",
    "dynapi/SDL_dynapi.c",
    "events/imKStoUCS.c", "events/SDL_categories.c", "events/SDL_clipboardevents.c",
    "events/SDL_displayevents.c", "events/SDL_dropevents.c", "events/SDL_events.c",
    "events/SDL_eventwatch.c", "events/SDL_keyboard.c", "events/SDL_keymap.c",
    "events/SDL_keysym_to_keycode.c", "events/SDL_keysym_to_scancode.c", "events/SDL_mouse.c",
    "events/SDL_pen.c", "events/SDL_quit.c", "events/SDL_scancode_tables.c",
    "events/SDL_touch.c", "events/SDL_windowevents.c",
    "filesystem/SDL_filesystem.c",
    "io/generic/SDL_asyncio_generic.c", "io/SDL_asyncio.c", "io/SDL_iostream.c",
    "libm/e_atan2.c", "libm/e_exp.c", "libm/e_fmod.c", "libm/e_log.c", "libm/e_log10.c",
    "libm/e_pow.c", "libm/e_rem_pio2.c", "libm/e_sqrt.c", "libm/k_cos.c", "libm/k_rem_pio2.c",
    "libm/k_sin.c", "libm/k_tan.c", "libm/s_atan.c", "libm/s_copysign.c", "libm/s_cos.c",
    "libm/s_fabs.c", "libm/s_floor.c", "libm/s_isinf.c", "libm/s_isinff.c", "libm/s_isnan.c",
    "libm/s_isnanf.c", "libm/s_modf.c", "libm/s_scalbn.c", "libm/s_sin.c", "libm/s_tan.c",
    "locale/SDL_locale.c",
    "main/SDL_main_callbacks.c",
    "misc/SDL_url.c",
    "render/opengl/SDL_render_gl.c", "render/opengl/SDL_shaders_gl.c",
    "render/SDL_render_unsupported.c", "render/SDL_render.c", "render/SDL_yuv_sw.c",
    "render/software/SDL_blendfillrect.c", "render/software/SDL_blendline.c",
    "render/software/SDL_blendpoint.c", "render/software/SDL_drawline.c",
    "render/software/SDL_drawpoint.c", "render/software/SDL_render_sw.c", "render/software/SDL_triangle.c",
    "SDL_assert.c", "SDL_error.c", "SDL_guid.c", "SDL_hashtable.c", "SDL_hints.c",
    "SDL_list.c", "SDL_log.c", "SDL_properties.c", "SDL_utils.c", "SDL.c",
    "stdlib/SDL_crc16.c", "stdlib/SDL_crc32.c", "stdlib/SDL_getenv.c", "stdlib/SDL_iconv.c",
    "stdlib/SDL_malloc.c", "stdlib/SDL_memcpy.c", "stdlib/SDL_memmove.c", "stdlib/SDL_memset.c",
    "stdlib/SDL_murmur3.c", "stdlib/SDL_qsort.c", "stdlib/SDL_random.c", "stdlib/SDL_stdlib.c",
    "stdlib/SDL_string.c", "stdlib/SDL_strtokr.c",
    "thread/SDL_thread.c",
    "time/SDL_time.c",
    "timer/SDL_timer.c",
    "tray/dummy/SDL_tray.c", "tray/SDL_tray_utils.c",
    "video/SDL_blit_0.c", "video/SDL_blit_1.c", "video/SDL_blit_A.c", "video/SDL_blit_auto.c",
    "video/SDL_blit_copy.c", "video/SDL_blit_N.c", "video/SDL_blit_slow.c", "video/SDL_blit.c",
    "video/SDL_bmp.c", "video/SDL_clipboard.c", "video/SDL_egl.c", "video/SDL_fillrect.c",
    "video/SDL_pixels.c", "video/SDL_rect.c", "video/SDL_RLEaccel.c", "video/SDL_rotate.c",
    "video/SDL_stb.c", "video/SDL_stretch.c", "video/SDL_surface.c", "video/SDL_video.c",
    "video/SDL_vulkan_utils.c", "video/SDL_yuv.c",
    "video/yuv2rgb/yuv_rgb_std.c",
};

static const char *sdl_sources_macos[] = {
    "filesystem/cocoa/SDL_sysfilesystem.m", "filesystem/posix/SDL_sysfsops.c",
    "loadso/dlopen/SDL_sysloadso.c",
    "locale/macos/SDL_syslocale.m",
    "misc/macos/SDL_sysurl.m",
    "thread/pthread/SDL_syscond.c", "thread/pthread/SDL_sysmutex.c", "thread/pthread/SDL_sysrwlock.c",
    "thread/pthread/SDL_syssem.c", "thread/pthread/SDL_systhread.c", "thread/pthread/SDL_systls.c",
    "time/unix/SDL_systime.c",
    "timer/unix/SDL_systimer.c",
    "video/cocoa/SDL_cocoaclipboard.m", "video/cocoa/SDL_cocoaevents.m", "video/cocoa/SDL_cocoakeyboard.m",
    "video/cocoa/SDL_cocoamessagebox.m", "video/cocoa/SDL_cocoamodes.m", "video/cocoa/SDL_cocoamouse.m",
    "video/cocoa/SDL_cocoaopengl.m", "video/cocoa/SDL_cocoapen.m", "video/cocoa/SDL_cocoashape.m",
    "video/cocoa/SDL_cocoavideo.m", "video/cocoa/SDL_cocoawindow.m",
};

// Windows equivalents of sdl_sources_macos[] above - see
// docs/plans/sdl3-backend.md Step 4 for the compile spike that validated
// this exact file list on MinGW. core/windows/SDL_windows.c provides
// WIN_SetError()/WIN_StringToUTF8() and similar helpers video/windows/*.c
// calls into; SDL_windowsrawinput.c/SDL_windowsgameinput.cpp/SDL_hid.c/
// SDL_xinput.c/SDL_windowsvulkan.c are joystick/Vulkan-only and deliberately
// absent, matching the "only what's needed" policy already applied to
// SDL_JOYSTICK_DISABLED/no-Vulkan on every other platform.
static const char *sdl_sources_win32[] = {
    "core/windows/SDL_hid.c", "core/windows/SDL_windows.c",
    "filesystem/windows/SDL_sysfilesystem.c", "filesystem/windows/SDL_sysfsops.c",
    "loadso/windows/SDL_sysloadso.c",
    "locale/windows/SDL_syslocale.c",
    "misc/windows/SDL_sysurl.c",
    // SDL_THREAD_GENERIC_COND_SUFFIX/RWLOCK_SUFFIX (see config header): the
    // real Windows CV/SRW-lock-based implementations below both fall back to
    // these generic ones for mutex kinds they don't natively support -
    // confirmed by reading SDL_syscond_cv.c/SDL_sysrwlock_srw.c directly,
    // both #include "../generic/SDL_sys{cond,rwlock}_c.h" and reference
    // SDL_CreateCondition_generic()/SDL_CreateRWLock_generic() etc.
    "thread/generic/SDL_syscond.c", "thread/generic/SDL_sysrwlock.c",
    "thread/windows/SDL_syscond_cv.c", "thread/windows/SDL_sysmutex.c",
    "thread/windows/SDL_sysrwlock_srw.c",
    "thread/windows/SDL_syssem.c", "thread/windows/SDL_systhread.c", "thread/windows/SDL_systls.c",
    "time/windows/SDL_systime.c",
    "timer/windows/SDL_systimer.c",
    "video/windows/SDL_windowsclipboard.c", "video/windows/SDL_windowsevents.c",
    "video/windows/SDL_windowsframebuffer.c", "video/windows/SDL_windowskeyboard.c",
    "video/windows/SDL_windowsmessagebox.c", "video/windows/SDL_windowsmodes.c",
    "video/windows/SDL_windowsmouse.c", "video/windows/SDL_windowsopengl.c",
    // SDL_windowsrawinput.c: real, non-joystick-gated raw mouse/keyboard
    // input plumbing SDL_windowsvideo.c/mouse.c call into directly (confirmed
    // by reading the file - its "#if !XBOX" branch, not a joystick-only
    // path, is what's linked here since this isn't an Xbox target).
    "video/windows/SDL_windowsrawinput.c",
    // SDL_windowsgameinput.cpp is C++ but, with HAVE_GAMEINPUT_H left
    // undefined by this project's trimmed config (no vendored GameInput SDK
    // header), compiles only its "#else" no-op stub branch (confirmed by
    // reading the file) - needed anyway because SDL_windowsvideo.c calls
    // WIN_InitGameInput()/WIN_QuitGameInput()/WIN_UpdateGameInput()
    // unconditionally.
    "video/windows/SDL_windowsgameinput.cpp",
    "video/windows/SDL_windowsshape.c", "video/windows/SDL_windowsvideo.c",
    "video/windows/SDL_windowswindow.c",
    // yuv_rgb_sse.c: SDL_yuv.c's software YUV->RGB conversion path always
    // compiles in the SSE2 variant on x86/x86_64 (see its own "sse2" target
    // attribute, unconditional - not gated on SDL_VIDEO_RENDER_SW or any
    // config macro), so this is needed on any x86_64 platform, not something
    // Windows-specific - the macOS list above didn't need it only because
    // that spike ran on Apple Silicon (arm64). Revisit if/when Linux/x86_64
    // validation (Step 4) happens.
    "video/yuv2rgb/yuv_rgb_sse.c",
};

// Sources common to every platform, matching src/Makefile's SRC_COMMON.
// TwPrecomp.cpp is deliberately excluded: it is just "#include
// \"TwPrecomp.h\"" (an MSVC precompiled-header trigger stub with no other
// content), already excluded by this repo's own src/Makefile.
static const char *common_sources[] = {
    GLAD_SRC,
    SDS_SRC,
    SRC_FOLDER "TwColors.c",
    SRC_FOLDER "TwFonts.c",
    SRC_FOLDER "TwOpenGL.c",
    SRC_FOLDER "TwOpenGLCore.c",
    SRC_FOLDER "TwBar.c",
    SRC_FOLDER "TwMgr.c",
    SRC_FOLDER "TwEventGLFW.c",
};

// TwEventGLUT.c/TwEventSDL.c/TwEventSDL12.c/TwEventSDL13.c/TwEventSFML.cpp
// (and TwEventX11.c, formerly the sole platform_sources entry on
// non-Windows/non-macOS) were translators for toolkits other than GLFW3.
// Deleted outright (not ported) now that the core library formally
// hard-depends on GLFW3 and TwEventGLFW.c is the only event backend that
// serves any remaining purpose - see docs/plans/c99-rewrite.md Step 7.
// Their private stand-in headers (MiniGLUT.h/MiniSDL12.h/MiniSDL13.h/
// MiniSFML16.h) were deleted alongside them; MiniGLFW.h stays, still used
// by the kept TwEventGLFW.c.

static void collect_sources(Nob_File_Paths *sources)
{
    for (size_t i = 0; i < NOB_ARRAY_LEN(common_sources); ++i) {
        nob_da_append(sources, common_sources[i]);
    }
}

// Unlike nob_file_exists() (POSIX access(), which follows symlinks), this reports true for a
// symlink whose target is missing too - access() alone can't see it, which left a dangling
// symlink undeletable and silently blocking its parent directory's removal (see
// delete_if_exists below - this repo hit exactly that with a Linux-only build symlink synced in
// via Dropbox, dangling on a macOS build). Deliberately not nob_get_file_type(): that logs an
// [ERROR] on a genuinely-missing path, which delete_if_exists is routinely called against as an
// expected, silent no-op.
static bool path_exists_or_dangling_symlink(const char *path)
{
#if defined(_WIN32)
    return nob_file_exists(path); // this codebase never creates such symlinks on Windows
#else
    struct stat st;
    return lstat(path, &st) == 0;
#endif
}

static bool delete_if_exists(const char *path)
{
    if (path_exists_or_dangling_symlink(path)) return nob_delete_file(path);
    return true;
}

// Deletes every path in objects - used once a set of intermediate .o files
// has been folded into a final archive/shared library/executable and is no
// longer needed, so they don't linger on disk as stale build output.
static bool delete_objects(Nob_File_Paths *objects)
{
    bool ok = true;
    for (size_t i = 0; i < objects->count; ++i) {
        ok = delete_if_exists(objects->items[i]) && ok;
    }
    return ok;
}

// Deletes every regular file directly inside folder (not recursive), so a
// stale build output left over from a since-renamed/removed source doesn't
// block clean() from removing the folder itself.
static bool clear_directory(const char *folder)
{
    if (!nob_file_exists(folder)) return true;

    Nob_File_Paths children = {0};
    if (!nob_read_entire_dir(folder, &children)) return false;

    bool ok = true;
    for (size_t i = 0; i < children.count; ++i) {
        const char *name = children.items[i];
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        ok = delete_if_exists(nob_temp_sprintf("%s%s", folder, name)) && ok;
    }
    return ok;
}

static bool build_needed(const char *output, const char **inputs, size_t inputs_count)
{
    int result = nob_needs_rebuild(output, inputs, inputs_count);
    if (result < 0) exit(1);
    return result > 0;
}

static bool collect_regular_file(Nob_Walk_Entry entry)
{
    Nob_File_Paths *paths = (Nob_File_Paths *)entry.data;

    if (entry.type == NOB_FILE_REGULAR) {
        nob_da_append(paths, nob_temp_strdup(entry.path));
    }

    return true;
}

static bool collect_tree_files(Nob_File_Paths *paths, const char *root)
{
    return nob_walk_dir(root, collect_regular_file, .data = paths);
}

static void add_common_build_deps(Nob_File_Paths *paths, const char *nob_exe)
{
    nob_da_append(paths, "nob.c");
    nob_da_append(paths, nob_exe);
    nob_da_append(paths, NOB_HEADER);
}

static bool make_dirs(void)
{
    return nob_mkdir_if_not_exists(BUILD_FOLDER)
        && nob_mkdir_if_not_exists(BUILD_STATIC_FOLDER)
        && nob_mkdir_if_not_exists(BUILD_SHARED_FOLDER)
        && nob_mkdir_if_not_exists(LIB_FOLDER)
        && nob_mkdir_if_not_exists(BUILD_INCLUDE_FOLDER);
}

static const char *object_path(const char *folder, const char *source)
{
    char *base = nob_temp_strdup(nob_path_name(source));
    char *dot = strrchr(base, '.');
    if (dot) *dot = '\0';
    return nob_temp_sprintf("%s%s.o", folder, base);
}

static bool is_cpp_source(const char *source)
{
    return nob_sv_ends_with_cstr(nob_sv_from_cstr(source), ".cpp");
}

// Vendored SDL3's Cocoa backend files (see sdl_sources[]) are Objective-C;
// clang infers this from the .m extension alone, but they still need ARC
// (unlike this project's own code, which is plain C99 everywhere - see
// docs/plans/sdl3-backend.md's compile-spike notes on why ARC was used).
static bool is_objc_source(const char *source)
{
    return nob_sv_ends_with_cstr(nob_sv_from_cstr(source), ".m");
}

// sds.c (vendor/sds/, see docs/plans/sds-string-migration.md) is pure C99.
// Compiling it as C++ fails outright: sds.h's SDS_HDR_VAR macro and several
// s_malloc/s_realloc call sites rely on C99's implicit void*->T* conversion,
// valid and idiomatic C, but ill-formed in C++ - and this is vendored,
// unmodified upstream code (AGENTS.md SS4), not something to patch with
// defensive casts the way this project's own new C99 files already are.
// Compile it as plain C on every platform (is_sds_source's own special case
// below still matters for that reason - is_cpp_source's own default would
// already say "cc" for a .c file, but is_sds_source is kept as an explicit,
// separately-reasoned check rather than folded away).
static bool is_sds_source(const char *source)
{
    return strcmp(source, SDS_SRC) == 0;
}

// glad.c (vendor/glad/, generated code) casts the void* dlsym()/
// GetProcAddress() returns to each GL function's pointer-to-function type -
// the standard, portable way to load GL entry points, but strict ISO C
// forbids object-pointer-to-function-pointer conversions, so -pedantic
// flags every single one of them (-Wpedantic). Harmless (every compiler/OS
// this project targets treats the two pointer kinds identically) and not
// something to fix by patching vendored, unmodified upstream code (AGENTS.md
// SS4) - -pedantic is simply the wrong tool for generated loader code like
// this. -Wall/-Wextra still apply, so a genuine bug in this file would
// still be caught.
static bool is_glad_source(const char *source)
{
    return strcmp(source, GLAD_SRC) == 0;
}

// The three sources that form the library's GLAD cluster, prelinked into a
// single archive member by prelink_renderers() below: glad.c defines the ~1124
// glad_*/GLAD_*/gladLoad*/GLVersion globals, and TwOpenGL.c (56 references) and
// TwOpenGLCore.c (49) are the only objects in the whole library that use them.
// Nothing else does, and their only reference back into the library is
// TwSetLastError(), so these three - and only these three - can be merged and
// have their GLAD symbols made local.
static bool is_renderer_source(const char *source)
{
    return is_glad_source(source)
        || strcmp(source, SRC_FOLDER "TwOpenGL.c") == 0
        || strcmp(source, SRC_FOLDER "TwOpenGLCore.c") == 0;
}

static const char *compiler_for_source(const char *source)
{
    if (is_sds_source(source)) return "cc";
    // TwPrecomp.h (TwBar.c/TwMgr.c's shared header) used to pull in
    // Foundation/AppKit on macOS for native Cocoa cursor code, forcing every
    // source through Objective-C++ (-x objective-c++) on that platform,
    // even plain .c helpers with no Objective-C content of their own. The
    // C99 rewrite deleted that native cursor code entirely (TwSetCursorCallback()
    // is now the only cursor-shape mechanism everywhere) and confirmed
    // (by grep, not assumption) that neither TwBar.c nor TwMgr.c reference
    // any Objective-C/Cocoa symbol anymore - so macOS no longer needs any
    // special-casing here at all.
    return is_cpp_source(source) ? "c++" : "cc";
}

static void append_platform_defines(Nob_Cmd *cmd)
{
#if defined(_WIN32)
    nob_cmd_append(cmd, "-D_WIN32");
#elif defined(__APPLE__)
    nob_cmd_append(cmd, "-D_MACOSX");
#else
    nob_cmd_append(cmd, "-D_UNIX");
#endif
}

static bool build_object(const char *source, const char *folder, const char *tw_define,
                          Nob_File_Paths *common_deps)
{
    const char *output = object_path(folder, source);

    Nob_File_Paths inputs = {0};
    nob_da_append(&inputs, source);
    for (size_t i = 0; i < common_deps->count; ++i) {
        nob_da_append(&inputs, common_deps->items[i]);
    }

    if (!build_needed(output, inputs.items, inputs.count)) {
        nob_log(NOB_INFO, "%s is up to date", output);
        return true;
    }

    Nob_Cmd cmd = {0};
    const char *compiler = compiler_for_source(source);
    nob_cmd_append(&cmd, compiler);
    // Matches src/Makefile's CPPCFG: unconditional -fPIC (not just for the
    // shared object set - harmless for the static archive, and matches this
    // repo's own established convention). No -I GLFW_INCLUDE: the library
    // itself does not include any real GLFW header - TwEventGLFW.c uses the
    // private MiniGLFW.h stand-in, and TwBar.c's clipboard/timing code no
    // longer touches GLFW at all (see
    // docs/plans/self-contained-windows-dll.md).
    nob_cmd_append(&cmd, "-Wall", "-Wextra", "-O3", "-fno-strict-aliasing", "-fPIC",
                        "-I" INCLUDE_FOLDER, "-I" GLAD_INCLUDE, "-I" SDS_INCLUDE, tw_define);
    // The whole library is real C99 now (Clusters 1-4 + Step 7 complete -
    // every common_sources entry compiles with "cc", not "c++"; nothing
    // left in this build needs a C++ standard at all) - request it
    // explicitly rather than relying on the compiler's own default C
    // dialect. -std=c99 is meaningless (and would be rejected as
    // conflicting) for a genuine C++ compile, which can't happen here in
    // practice (is_cpp_source has nothing left to say "c++" to in
    // common_sources) but guard on `compiler` anyway rather than assume.
    if (strcmp(compiler, "cc") == 0) {
        nob_cmd_append(&cmd, "-std=c99");
        if (!is_glad_source(source)) nob_cmd_append(&cmd, "-pedantic");
    }
    append_platform_defines(&cmd);
    nob_cmd_append(&cmd, "-c", source, "-o", output);
    return nob_cmd_run(&cmd);
}

// Merges the three is_renderer_source() objects into one relocatable object
// with every GLAD symbol demoted from global to local, and reports its path in
// *output. The library then carries a genuinely private GLAD, as README.md and
// docs/plans/self-contained-windows-dll.md already describe.
//
// Why this is needed: without it the library exports ~1124 glad_*/GLAD_*/
// gladLoad*/GLVersion globals - accidentally, since on POSIX TW_API expands to
// nothing (include/AntTweakBar.h) and nothing here ever set -fvisibility.
// Anything else in the link that embeds its own GLAD then collides with them;
// raylib (vendor/raylib/, whose rcore.o carries a whole glad2 copy) is the
// case that forced the issue. Note -fvisibility=hidden is NOT an alternative:
// it would hide the public Tw* API too, and it does not affect an archive's
// symbol table at all.
//
// The three objects are a self-contained cluster - see is_renderer_source() -
// so TwSetLastError() is the only symbol left undefined here, resolved against
// TwMgr.o at final link.
static bool prelink_renderers(const char *folder, Nob_File_Paths *renderers,
                              const char *nob_exe, const char **output)
{
    *output = nob_temp_sprintf("%s%s", folder, RENDERERS_OBJ_NAME);

    Nob_File_Paths inputs = {0};
    for (size_t i = 0; i < renderers->count; ++i) nob_da_append(&inputs, renderers->items[i]);
    add_common_build_deps(&inputs, nob_exe);

    if (!build_needed(*output, inputs.items, inputs.count)) {
        nob_log(NOB_INFO, "%s is up to date", *output);
        return true;
    }

    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "ld", "-r", "-o", *output);
    for (size_t i = 0; i < renderers->count; ++i) nob_cmd_append(&cmd, renderers->items[i]);
#if defined(__APPLE__)
    // ld64 documents wildcard support for -unexported_symbol, and demotes a
    // matched global to private-extern; because -keep_private_externs is NOT
    // passed, `ld -r` then takes it the rest of the way down to a plain local
    // symbol. Mach-O prefixes C identifiers with an underscore.
    nob_cmd_append(&cmd, "-unexported_symbol", "_glad_*");
    nob_cmd_append(&cmd, "-unexported_symbol", "_GLAD_*");
    nob_cmd_append(&cmd, "-unexported_symbol", "_gladLoad*");
    nob_cmd_append(&cmd, "-unexported_symbol", "_GLVersion");
#endif
    if (!nob_cmd_run(&cmd)) return false;

#if !defined(__APPLE__)
    // GNU ld cannot localize while merging, so objcopy does it afterwards.
    // It ships with the same binutils as `ar`, which this build already
    // requires. ELF and 64-bit PE carry no leading underscore; 32-bit MinGW
    // does, so both spellings are passed there - objcopy silently ignores a
    // pattern that matches nothing.
    Nob_Cmd localize = {0};
    nob_cmd_append(&localize, "objcopy", "--wildcard");
    nob_cmd_append(&localize, "--localize-symbol", "glad_*");
    nob_cmd_append(&localize, "--localize-symbol", "GLAD_*");
    nob_cmd_append(&localize, "--localize-symbol", "gladLoad*");
    nob_cmd_append(&localize, "--localize-symbol", "GLVersion");
#if defined(_WIN32)
    nob_cmd_append(&localize, "--localize-symbol", "_glad_*");
    nob_cmd_append(&localize, "--localize-symbol", "_GLAD_*");
    nob_cmd_append(&localize, "--localize-symbol", "_gladLoad*");
    nob_cmd_append(&localize, "--localize-symbol", "_GLVersion");
#endif
    nob_cmd_append(&localize, *output);
    if (!nob_cmd_run(&localize)) return false;
#endif

    return true;
}

static bool build_static_archive(Nob_File_Paths *objects, const char *nob_exe)
{
    Nob_File_Paths inputs = {0};
    for (size_t i = 0; i < objects->count; ++i) nob_da_append(&inputs, objects->items[i]);
    add_common_build_deps(&inputs, nob_exe);

    if (!build_needed(LIB_STATIC, inputs.items, inputs.count)) {
        nob_log(NOB_INFO, "%s is up to date", LIB_STATIC);
        return true;
    }

    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "ar", "rcs", LIB_STATIC);
    for (size_t i = 0; i < objects->count; ++i) nob_cmd_append(&cmd, objects->items[i]);
    return nob_cmd_run(&cmd);
}

static void append_shared_link_flags(Nob_Cmd *cmd)
{
#if defined(_WIN32)
    nob_cmd_append(cmd, "-shared", "-o", LIB_SHARED, "-Wl,--out-implib," LIB_IMPORT);
#elif defined(__APPLE__)
    nob_cmd_append(cmd, "-dynamiclib", "-Wl,-undefined", "-Wl,dynamic_lookup", "-o", LIB_SHARED);
#else
    nob_cmd_append(cmd, "-shared", "-Wl,-soname," LIB_SHARED_SONAME_NAME, "-o", LIB_SHARED);
#endif
}

static void append_shared_link_libs(Nob_Cmd *cmd)
{
#if defined(_WIN32)
    nob_cmd_append(cmd, "-lopengl32", "-lgdi32", "-luser32", "-lkernel32", "-lm", "-ldinput8", "-ldxguid");
#elif defined(__APPLE__)
    nob_cmd_append(cmd, "-framework", "OpenGL", "-framework", "AppKit");
#else
    // No -lX11/-lXext/-lXxf86vm: the library's own code never calls X11
    // directly (it does not link GLFW at all - see GLFW_OBJ's comment
    // above), and GLFW's X11 extension libraries (Xxf86vm included) are
    // dlopen'd by GLFW itself at runtime, not hard link-time dependencies
    // - see append_glfw_libs() below.
    nob_cmd_append(cmd, "-lGL", "-lpthread", "-lm");
#endif
}

static bool link_shared_library(Nob_File_Paths *objects, const char *nob_exe)
{
    Nob_File_Paths inputs = {0};
    for (size_t i = 0; i < objects->count; ++i) nob_da_append(&inputs, objects->items[i]);
    add_common_build_deps(&inputs, nob_exe);

    if (!build_needed(LIB_SHARED, inputs.items, inputs.count)) {
        nob_log(NOB_INFO, "%s is up to date", LIB_SHARED);
        return true;
    }

    Nob_Cmd cmd = {0};
    // "cc", not "c++": the library became pure C99 in an earlier session
    // (same reasoning as build_example()'s own "cc", not "c++" fix - see
    // git log --oneline -- nob.c for "build C99 examples with cc, not c++").
    // Nothing left in shared_objects needs the C++ driver at link time.
    nob_cmd_append(&cmd, "cc");
    append_shared_link_flags(&cmd);
    for (size_t i = 0; i < objects->count; ++i) nob_cmd_append(&cmd, objects->items[i]);
    append_shared_link_libs(&cmd);
    if (!nob_cmd_run(&cmd)) return false;

#if !defined(_WIN32) && !defined(__APPLE__)
    if (!delete_if_exists(LIB_SHARED_SONAME)) return false;
    Nob_Cmd ln = {0};
    nob_cmd_append(&ln, "ln", "-sf", nob_path_name(LIB_SHARED), LIB_SHARED_SONAME);
    if (!nob_cmd_run(&ln)) return false;
#endif

    return true;
}

static bool build_all(const char *nob_exe)
{
    if (!make_dirs()) return false;

    Nob_File_Paths sources = {0};
    collect_sources(&sources);

    Nob_File_Paths common_deps = {0};
    if (!collect_tree_files(&common_deps, SRC_FOLDER)) return false;
    if (!collect_tree_files(&common_deps, INCLUDE_FOLDER)) return false;
    if (!collect_tree_files(&common_deps, GLAD_INCLUDE)) return false;
    if (!collect_tree_files(&common_deps, SDS_INCLUDE)) return false;
    add_common_build_deps(&common_deps, nob_exe);

    // *_objects are what gets archived/linked; the renderer sources are held
    // back in *_renderers and enter as the single prelinked TwRenderers.o
    // below, so the raw glad.o/TwOpenGL.o/TwOpenGLCore.o never reach the
    // archive with their GLAD symbols still global. *_scratch is everything
    // produced, for the cleanup at the end.
    Nob_File_Paths static_objects = {0}, static_renderers = {0}, static_scratch = {0};
    Nob_File_Paths shared_objects = {0}, shared_renderers = {0}, shared_scratch = {0};

    for (size_t i = 0; i < sources.count; ++i) {
        bool renderer = is_renderer_source(sources.items[i]);

        if (!build_object(sources.items[i], BUILD_STATIC_FOLDER, "-DTW_STATIC", &common_deps)) return false;
        const char *static_obj = object_path(BUILD_STATIC_FOLDER, sources.items[i]);
        nob_da_append(&static_scratch, static_obj);
        nob_da_append(renderer ? &static_renderers : &static_objects, static_obj);

        if (!build_object(sources.items[i], BUILD_SHARED_FOLDER, "-DTW_EXPORTS", &common_deps)) return false;
        const char *shared_obj = object_path(BUILD_SHARED_FOLDER, sources.items[i]);
        nob_da_append(&shared_scratch, shared_obj);
        nob_da_append(renderer ? &shared_renderers : &shared_objects, shared_obj);
    }

    const char *static_renderers_obj = NULL, *shared_renderers_obj = NULL;
    if (!prelink_renderers(BUILD_STATIC_FOLDER, &static_renderers, nob_exe, &static_renderers_obj)) return false;
    nob_da_append(&static_objects, static_renderers_obj);
    nob_da_append(&static_scratch, static_renderers_obj);
    if (!prelink_renderers(BUILD_SHARED_FOLDER, &shared_renderers, nob_exe, &shared_renderers_obj)) return false;
    nob_da_append(&shared_objects, shared_renderers_obj);
    nob_da_append(&shared_scratch, shared_renderers_obj);

    if (!build_static_archive(&static_objects, nob_exe)) return false;
    if (!link_shared_library(&shared_objects, nob_exe)) return false;

    // LIB_STATIC/LIB_SHARED above already contain everything these
    // intermediate .o files provided, so remove them (and the now-empty
    // per-object-set folders) rather than leave stale build output behind.
    // Trade-off: build_object()'s build_needed() sees a missing .o as
    // needing a rebuild, so every subsequent `./nob` always recompiles
    // every source from scratch - there is no longer an incremental/no-op
    // `./nob` re-run once this cleanup runs.
    if (!delete_objects(&static_scratch)) return false;
    if (!delete_objects(&shared_scratch)) return false;
    // clear_directory() first, not just the delete_objects() above: sweeps up anything else
    // that ended up in these folders (a stray .DS_Store, an orphaned .o left over from a
    // since-renamed/removed source) so it doesn't silently block removing the folder itself -
    // the same reason clean() below already clears every folder it removes.
    if (!clear_directory(BUILD_STATIC_FOLDER)) return false;
    if (!delete_if_exists(BUILD_STATIC_FOLDER)) return false;
    if (!clear_directory(BUILD_SHARED_FOLDER)) return false;
    if (!delete_if_exists(BUILD_SHARED_FOLDER)) return false;

    // Copy (not move - include/AntTweakBar.h stays the real, git-tracked
    // source) the public header next to the libraries above, so build/ is a
    // self-contained lib+include pair for anything linking against it,
    // without duplicating the header as a second source of truth.
    if (!nob_copy_file(INCLUDE_FOLDER "AntTweakBar.h", BUILD_INCLUDE_FOLDER "AntTweakBar.h")) return false;

    nob_log(NOB_INFO, "built %s, %s and %s", LIB_STATIC, LIB_SHARED, BUILD_INCLUDE_FOLDER "AntTweakBar.h");
    return true;
}

// One of six folders (EXAMPLES_{STATIC,SHARED}_{GLFW,SDL,SFML}_FOLDER) -
// see their own comment above for why each link-mode/backend combination
// gets a separate folder rather than sharing one.
static const char *example_output_folder(bool dynamic, Backend backend)
{
    switch (backend) {
    case BACKEND_SDL:  return dynamic ? EXAMPLES_SHARED_SDL_FOLDER  : EXAMPLES_STATIC_SDL_FOLDER;
    case BACKEND_SFML: return dynamic ? EXAMPLES_SHARED_SFML_FOLDER : EXAMPLES_STATIC_SFML_FOLDER;
    case BACKEND_RAYLIB: return dynamic ? EXAMPLES_SHARED_RAYLIB_FOLDER : EXAMPLES_STATIC_RAYLIB_FOLDER;
    case BACKEND_GLFW: default:
        return dynamic ? EXAMPLES_SHARED_GLFW_FOLDER : EXAMPLES_STATIC_GLFW_FOLDER;
    }
}

static const char *example_executable_path(const char *source, bool dynamic, Backend backend)
{
    char *base = nob_temp_strdup(nob_path_name(source));
    char *dot = strrchr(base, '.');
    if (dot) *dot = '\0';
    return nob_temp_sprintf("%s%s" EXE_EXT, example_output_folder(dynamic, backend), base);
}

// Only the examples build compiles GLFW's X11 backend (vendor/glfw/
// glfw_unity.c under build_glfw() below) - the library itself links no
// X11 headers or libraries at all (see append_shared_link_libs()) - so
// these dev headers are checked here, not for the library build.
static bool check_linux_x11_deps(void)
{
#if !defined(_WIN32) && !defined(__APPLE__)
    static const struct { const char *header; const char *pkg; } required[] = {
        { "/usr/include/X11/Xlib.h",                "libx11-dev" },
        { "/usr/include/X11/Xcursor/Xcursor.h",     "libxcursor-dev" },
        { "/usr/include/X11/extensions/Xrandr.h",   "libxrandr-dev" },
        { "/usr/include/X11/extensions/Xinerama.h", "libxinerama-dev" },
        { "/usr/include/X11/extensions/XInput2.h",  "libxi-dev" },
        { "/usr/include/X11/extensions/shape.h",    "libxext-dev" },
    };
    bool ok = true;
    for (size_t i = 0; i < NOB_ARRAY_LEN(required); ++i) {
        if (!nob_file_exists(required[i].header)) {
            nob_log(NOB_ERROR, "Missing X11 development header: %s (package: %s)",
                     required[i].header, required[i].pkg);
            ok = false;
        }
    }
    if (!ok) {
        nob_log(NOB_ERROR, "On Ubuntu/Debian install all of them with:");
        nob_log(NOB_ERROR, "    sudo apt update && sudo apt install libx11-dev libxcursor-dev libxrandr-dev libxinerama-dev libxi-dev libxext-dev");
    }
    return ok;
#else
    return true;
#endif
}

static bool check_examples_deps(bool dynamic)
{
    if (dynamic) {
        if (!nob_file_exists(LIB_SHARED)
#if defined(_WIN32)
            || !nob_file_exists(LIB_IMPORT)
#endif
            ) {
            nob_log(NOB_ERROR, "%s does not exist yet.", LIB_SHARED);
            nob_log(NOB_ERROR, "Run `./nob` first to build the library, then `./nob -examples-glfw/-examples-sdl/-examples-sfml -dynamic`.");
            return false;
        }
    } else if (!nob_file_exists(LIB_STATIC)) {
        nob_log(NOB_ERROR, "%s does not exist yet.", LIB_STATIC);
        nob_log(NOB_ERROR, "Run `./nob` first to build the library, then `./nob -examples-glfw`, `-examples-sdl`, or `-examples-sfml`.");
        return false;
    }
    return check_linux_x11_deps();
}

// Compiles GLAD once for the examples (separate from the copy baked into
// the library's own static/shared object sets - see common_sources above).
static bool build_glad_for_examples(const char *nob_exe)
{
    const char *inputs[] = { GLAD_SRC, "nob.c", nob_exe, NOB_HEADER };
    if (!build_needed(GLAD_OBJ, inputs, NOB_ARRAY_LEN(inputs))) {
        nob_log(NOB_INFO, "%s is up to date", GLAD_OBJ);
        return true;
    }

    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "cc", "-O2", "-I" GLAD_INCLUDE, "-c", GLAD_SRC, "-o", GLAD_OBJ);
    return nob_cmd_run(&cmd);
}

// Compiles the vendored GLFW3 unity build (vendor/glfw/glfw_unity.c, see
// its own header comment - this file already named this function before it
// existed) into a single object, following raylib's rglfw.c pattern (the
// same one AntTweakBar-Legacy's vendor/glfw/glfw_unity.c uses).
static bool build_glfw(const char *nob_exe)
{
    const char *inputs[] = { GLFW_SRC, "nob.c", nob_exe, NOB_HEADER };
    if (!build_needed(GLFW_OBJ, inputs, NOB_ARRAY_LEN(inputs))) {
        nob_log(NOB_INFO, "%s is up to date", GLFW_OBJ);
        return true;
    }

    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "cc", "-O2", "-I" GLFW_INCLUDE);
#if defined(_WIN32)
    nob_cmd_append(&cmd, "-D_GLFW_WIN32");
#elif defined(__APPLE__)
    // glfw_unity.c #includes Objective-C (.m) sources under _GLFW_COCOA.
    nob_cmd_append(&cmd, "-D_GLFW_COCOA", "-x", "objective-c");
#else
    nob_cmd_append(&cmd, "-D_GLFW_X11");
#endif
    nob_cmd_append(&cmd, "-c", GLFW_SRC, "-o", GLFW_OBJ);
    return nob_cmd_run(&cmd);
}

static void append_glfw_flags(Nob_Cmd *cmd)
{
    nob_cmd_append(cmd, "-I" GLFW_INCLUDE);
}

static void append_glfw_libs(Nob_Cmd *cmd)
{
    nob_cmd_append(cmd, GLFW_OBJ);
#if defined(_WIN32)
    nob_cmd_append(cmd, "-lopengl32", "-lgdi32");
#elif defined(__APPLE__)
    nob_cmd_append(cmd, "-framework", "Cocoa", "-framework", "IOKit", "-framework", "CoreVideo",
                        "-framework", "OpenGL");
#else
    // -lX11 is a genuine hard dependency (GLFW's X11 backend calls core
    // Xlib functions like XOpenDisplay directly) and -ldl is needed for
    // GLFW's own dlopen/dlsym calls (posix_module.c). -lXrandr/-lXi/
    // -lXxf86vm are NOT needed here: GLFW dlopen's each of those X11
    // extension libraries itself at runtime (see x11_init.c) and degrades
    // gracefully if one is missing, so hard-linking them only breaks the
    // build on systems that lack one (e.g. the legacy Xxf86vm extension,
    // often not packaged on modern distros) without GLFW ever needing it
    // at link time.
    nob_cmd_append(cmd, "-lGL", "-lX11", "-ldl", "-lpthread", "-lm");
#endif
}

// Compiles one vendored SDL3 source file (see sdl_sources[]) to its own
// object - unlike build_glfw() above, this can't be a single unity-build
// translation unit (SDL3's private headers aren't multi-inclusion-safe,
// see docs/plans/sdl3-backend.md), so this mirrors the library's own
// per-file build_object() instead. -DANTTWEAKBARC99_SDL_VENDORED trips the
// one deliberate edit to vendored code, in vendor/sdl/src/dynapi/
// SDL_dynapi.h, that disables SDL's dynamic-API jump table (unavoidable:
// upstream refuses to let anything but that file's own source turn it
// off, and without disabling it every public SDL function needs a real
// implementation reachable from the jump table regardless of the
// SDL_*_DISABLED config macros - see that file's comment).
static bool build_sdl_object(const char *source, Nob_File_Paths *common_deps)
{
    const char *output = object_path(SDL_OBJ_FOLDER, source);

    Nob_File_Paths inputs = {0};
    nob_da_append(&inputs, source);
    for (size_t i = 0; i < common_deps->count; ++i) {
        nob_da_append(&inputs, common_deps->items[i]);
    }

    if (!build_needed(output, inputs.items, inputs.count)) {
        nob_log(NOB_INFO, "%s is up to date", output);
        return true;
    }

    Nob_Cmd cmd = {0};
    // SDL_windowsgameinput.cpp (Windows only) is the sole .cpp file in
    // either platform's source list - its HAVE_GAMEINPUT_H-undefined stub
    // branch is plain C-shaped code but still needs the C++ driver to parse
    // the file's extern "C" blocks in its (unused) real-implementation half.
    nob_cmd_append(&cmd, is_cpp_source(source) ? "c++" : "cc");
    if (is_objc_source(source)) nob_cmd_append(&cmd, "-fobjc-arc");
    // -Wno-deprecated-declarations: some vendored libm/*.c files trip
    // deprecated-declaration warnings against this platform's own SDK
    // headers (see docs/plans/sdl3-backend.md) - harmless, and not
    // something to fix by patching vendored, unmodified upstream code.
    nob_cmd_append(&cmd, "-O2", "-Wno-deprecated-declarations", "-DANTTWEAKBARC99_SDL_VENDORED",
                        "-I" SDL_CONFIG_INCLUDE, "-I" SDL_INCLUDE, "-I" SDL_SRC_ROOT);
    nob_cmd_append(&cmd, "-c", source, "-o", output);
    return nob_cmd_run(&cmd);
}

// Builds every file in sdl_sources[] (plus the small ATB-authored
// SDL_GetGamepadTypeFromVIDPID stub - see vendor/sdl/sdl_stubs.c) and
// archives them into SDL_LIB, so examples link against one file the same
// way they link against GLFW_OBJ. Deliberately not deleted after use the
// way GLAD_OBJ/GLFW_OBJ are (see build_examples()): recompiling ~140 SDL3
// files on every `-examples-sdl` invocation would make incremental builds far
// slower than GLFW's single-object case, so the object folder and archive
// are left in place for build_needed() to skip on the next run; `./nob
// -clean` still removes them.
static bool build_sdl(const char *nob_exe)
{
#if !defined(__APPLE__) && !defined(_WIN32)
    nob_log(NOB_ERROR, "-examples-sdl is only validated on macOS/Windows so far.");
    nob_log(NOB_ERROR, "See docs/plans/sdl3-backend.md Step 4 for Linux status.");
    return false;
#endif

    if (!nob_mkdir_if_not_exists(SDL_OBJ_FOLDER)) return false;

    Nob_File_Paths common_deps = {0};
    if (!collect_tree_files(&common_deps, SDL_CONFIG_INCLUDE)) return false;
    add_common_build_deps(&common_deps, nob_exe);

    Nob_File_Paths objects = {0};
    for (size_t i = 0; i < NOB_ARRAY_LEN(sdl_sources_common); ++i) {
        const char *source = nob_temp_sprintf("%s%s", SDL_SRC_ROOT, sdl_sources_common[i]);
        if (!build_sdl_object(source, &common_deps)) return false;
        nob_da_append(&objects, object_path(SDL_OBJ_FOLDER, source));
    }
#if defined(_WIN32)
    for (size_t i = 0; i < NOB_ARRAY_LEN(sdl_sources_win32); ++i) {
        const char *source = nob_temp_sprintf("%s%s", SDL_SRC_ROOT, sdl_sources_win32[i]);
        if (!build_sdl_object(source, &common_deps)) return false;
        nob_da_append(&objects, object_path(SDL_OBJ_FOLDER, source));
    }
#else
    for (size_t i = 0; i < NOB_ARRAY_LEN(sdl_sources_macos); ++i) {
        const char *source = nob_temp_sprintf("%s%s", SDL_SRC_ROOT, sdl_sources_macos[i]);
        if (!build_sdl_object(source, &common_deps)) return false;
        nob_da_append(&objects, object_path(SDL_OBJ_FOLDER, source));
    }
#endif
    if (!build_sdl_object(SDL_STUB_SRC, &common_deps)) return false;
    nob_da_append(&objects, object_path(SDL_OBJ_FOLDER, SDL_STUB_SRC));

    Nob_File_Paths archive_inputs = {0};
    for (size_t i = 0; i < objects.count; ++i) nob_da_append(&archive_inputs, objects.items[i]);
    add_common_build_deps(&archive_inputs, nob_exe);

    if (!build_needed(SDL_LIB, archive_inputs.items, archive_inputs.count)) {
        nob_log(NOB_INFO, "%s is up to date", SDL_LIB);
        return true;
    }

    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "ar", "rcs", SDL_LIB);
    for (size_t i = 0; i < objects.count; ++i) nob_cmd_append(&cmd, objects.items[i]);
    return nob_cmd_run(&cmd);
}

static void append_sdl_flags(Nob_Cmd *cmd)
{
    nob_cmd_append(cmd, "-I" SDL_INCLUDE);
}

static void append_sdl_libs(Nob_Cmd *cmd)
{
    nob_cmd_append(cmd, SDL_LIB);
#if defined(_WIN32)
    // Validated by the Step 4 compile spike (docs/plans/sdl3-backend.md):
    // user32/gdi32 for window/device-context management, opengl32 for WGL,
    // imm32 for SDL_windowskeyboard.c's IME handling, ole32/oleaut32/uuid
    // for SDL_windowsvideo.c/SDL_windowsevents.c's use of COM (drag-and-drop
    // registration, IDropTarget), winmm for the multimedia timer used by
    // SDL_windowsevents.c's message-loop timing, setupapi for the display
    // device enumeration SDL_windowsmodes.c calls into, version for the
    // GetFileVersionInfo/VerQueryValue calls SDL_windowskeyboard.c's IME
    // version-detection code makes.
    nob_cmd_append(cmd, "-luser32", "-lgdi32", "-lopengl32", "-limm32",
                        "-lole32", "-loleaut32", "-luuid", "-lwinmm", "-lsetupapi", "-lversion");
#elif defined(__APPLE__)
    nob_cmd_append(cmd, "-framework", "Cocoa", "-framework", "IOKit", "-framework", "CoreVideo",
                        "-framework", "Carbon", "-framework", "OpenGL", "-framework", "CoreFoundation",
                        "-framework", "UniformTypeIdentifiers");
#else
    // Not yet validated - see docs/plans/sdl3-backend.md Step 4.
#endif
}

// Compiles the vendored SFML3 unity build (vendor/sfml/sfml_unity.mm) into
// a single object, same shape as build_glfw() above - a compile spike
// confirmed SFML's own sources ARE safe to concatenate into one
// translation unit (unlike SDL3), so no per-file archive is needed here
// (see docs/plans/sfml3-backend.md). No -fobjc-arc: SFML's macOS backend
// files use manual retain/release, not ARC - confirmed by the same spike
// and the opposite of vendor/sdl/'s Cocoa files.
static bool build_sfml(const char *nob_exe)
{
#if !defined(__APPLE__) && !defined(_WIN32)
    nob_log(NOB_ERROR, "-examples-sfml is only validated on macOS/Windows so far.");
    nob_log(NOB_ERROR, "See docs/plans/sfml3-backend.md for Linux status.");
    return false;
#endif

    const char *inputs[] = { SFML_SRC, "nob.c", nob_exe, NOB_HEADER };
    if (!build_needed(SFML_OBJ, inputs, NOB_ARRAY_LEN(inputs))) {
        nob_log(NOB_INFO, "%s is up to date", SFML_OBJ);
        return true;
    }

    Nob_Cmd cmd = {0};
    // SFML requires C++17 (target_compile_features(... cxx_std_17) in its
    // own CMakeLists.txt); -DSFML_STATIC matches how it's actually linked
    // here (a no-op on macOS - both its import/export macros already
    // resolve to the same visibility attribute there - but real on
    // Windows, avoiding a dllimport/dllexport mismatch once that platform
    // is validated).
    nob_cmd_append(&cmd, "c++", "-std=c++17", "-DSFML_STATIC",
                        "-I" SFML_INCLUDE, "-Ivendor/sfml/src",
                        "-Ivendor/sfml/extlibs/headers/cpp-unicodelib",
                        "-Ivendor/sfml/extlibs/headers/glad/include",
                        "-Ivendor/sfml/extlibs/headers/vulkan");
    nob_cmd_append(&cmd, "-c", SFML_SRC, "-o", SFML_OBJ);
    return nob_cmd_run(&cmd);
}

static void append_sfml_flags(Nob_Cmd *cmd)
{
    // -DSFML_STATIC: a no-op on macOS (see build_sfml()'s own comment - its
    // import/export macros already collapse to the same visibility
    // attribute there) but load-bearing on Windows: without it, SFML's
    // Export.hpp marks every sf::* symbol __declspec(dllimport), which
    // doesn't match the plain (non-DLL) symbols sfml.o actually exports,
    // producing "undefined reference to `__imp_...'" link errors - found by
    // the Step 4/5 Windows compile spike (docs/plans/sfml3-backend.md).
    nob_cmd_append(cmd, "-std=c++17", "-DSFML_STATIC", "-I" SFML_INCLUDE);
}

static void append_sfml_libs(Nob_Cmd *cmd)
{
    nob_cmd_append(cmd, SFML_OBJ);
#if defined(_WIN32)
    nob_cmd_append(cmd, "-lopengl32", "-lgdi32", "-luser32", "-lwinmm", "-lole32");
#elif defined(__APPLE__)
    nob_cmd_append(cmd, "-framework", "Foundation", "-framework", "AppKit",
                        "-framework", "IOKit", "-framework", "Carbon", "-framework", "OpenGL");
#else
    // Not yet validated - see docs/plans/sfml3-backend.md.
#endif
}

// Compile flags for raylib's own sources, matching upstream src/Makefile's
// PLATFORM_DESKTOP build: GRAPHICS_API_OPENGL_33 is what raylib's own
// desktop default is, and is also the profile AntTweakBar's TW_OPENGL_CORE
// renderer targets.
static void append_raylib_build_flags(Nob_Cmd *cmd, const char *source)
{
    nob_cmd_append(cmd,
        "-Wall", "-D_GNU_SOURCE", "-DPLATFORM_DESKTOP", "-DGRAPHICS_API_OPENGL_33",
        "-Wno-missing-braces", "-Werror=pointer-arith", "-fno-strict-aliasing",
        "-std=c99", "-O2",
        "-I" RAYLIB_SRC_FOLDER,
        "-I" RAYLIB_SRC_FOLDER "external/glfw/include");
#if defined(_WIN32)
    nob_cmd_append(cmd, "-DUNICODE");
#elif defined(__APPLE__)
    // rglfw.c is raylib's single-file build of GLFW, whose Cocoa backend is
    // Objective-C - the same reason vendor/glfw's own unity build needs this.
    if (strstr(source, "rglfw.c")) nob_cmd_append(cmd, "-x", "objective-c");
#else
    nob_cmd_append(cmd, "-fPIC", "-D_GLFW_X11", "-Werror=implicit-function-declaration");
#endif
}

static bool build_raylib(const char *nob_exe)
{
    if (!nob_mkdir_if_not_exists(RAYLIB_OBJ_FOLDER)) return false;

    Nob_File_Paths common_deps = {0};
    if (!collect_tree_files(&common_deps, RAYLIB_SRC_FOLDER)) return false;
    add_common_build_deps(&common_deps, nob_exe);

    Nob_File_Paths objects = {0};
    for (size_t i = 0; i < NOB_ARRAY_LEN(raylib_sources); ++i) {
        const char *source = raylib_sources[i];
        const char *object = object_path(RAYLIB_OBJ_FOLDER, source);
        nob_da_append(&objects, object);
        if (!build_needed(object, common_deps.items, common_deps.count)) {
            nob_log(NOB_INFO, "%s is up to date", object);
            continue;
        }
        Nob_Cmd cmd = {0};
        nob_cmd_append(&cmd, "cc");
        append_raylib_build_flags(&cmd, source);
        nob_cmd_append(&cmd, "-c", source, "-o", object);
        if (!nob_cmd_run(&cmd)) return false;
    }

    if (!build_needed(RAYLIB_LIB, objects.items, objects.count)) {
        nob_log(NOB_INFO, "%s is up to date", RAYLIB_LIB);
        return true;
    }
    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "ar", "rcs", RAYLIB_LIB);
    for (size_t i = 0; i < objects.count; ++i) nob_cmd_append(&cmd, objects.items[i]);
    return nob_cmd_run(&cmd);
}

static void append_raylib_flags(Nob_Cmd *cmd)
{
    nob_cmd_append(cmd, "-I" RAYLIB_SRC_FOLDER);
}

static void append_raylib_libs(Nob_Cmd *cmd)
{
    nob_cmd_append(cmd, RAYLIB_LIB);
#if defined(_WIN32)
    nob_cmd_append(cmd, "-lopengl32", "-lgdi32", "-lwinmm");
#elif defined(__APPLE__)
    nob_cmd_append(cmd, "-framework", "OpenGL", "-framework", "Cocoa",
                        "-framework", "IOKit", "-framework", "CoreVideo",
                        "-framework", "CoreAudio", "-framework", "AudioToolbox");
#else
    nob_cmd_append(cmd, "-lGL", "-lm", "-lpthread", "-ldl", "-lrt", "-lX11");
#endif
}

// dynamic links the example against the shared library (LIB_SHARED, plus
// LIB_IMPORT on Windows) instead of LIB_STATIC; the caller is otherwise
// identical either way. backend picks the compile/link flags and the
// GLFW_OBJ/SDL_LIB/SFML_OBJ build dependency to link against.
static bool build_example(const char *source, const char *nob_exe, bool dynamic, Backend backend)
{
    const char *output = example_executable_path(source, dynamic, backend);

    Nob_File_Paths inputs = {0};
    nob_da_append(&inputs, source);
    nob_da_append(&inputs, dynamic ? LIB_SHARED : LIB_STATIC);
#if defined(_WIN32)
    if (dynamic) nob_da_append(&inputs, LIB_IMPORT);
#endif
    // raylib's rcore.o already carries its own GLAD; linking the examples'
    // GLAD_OBJ as well would define every glad_gl* symbol twice.
    if (backend != BACKEND_RAYLIB) nob_da_append(&inputs, GLAD_OBJ);
    // Each example folder's shared glue header is a real dependency: editing it
    // must rebuild every example in that folder. Only list headers that exist -
    // build_needed() treats a missing input as a fatal error.
    switch (backend) {
    case BACKEND_SDL:  nob_da_append(&inputs, SDL_LIB);  break;
    case BACKEND_SFML: nob_da_append(&inputs, SFML_OBJ); break;
    case BACKEND_RAYLIB:
        nob_da_append(&inputs, RAYLIB_LIB);
        nob_da_append(&inputs, EXAMPLES_RAYLIB_FOLDER "atb_raylib.h");
        break;
    case BACKEND_GLFW: default:
        nob_da_append(&inputs, GLFW_OBJ);
        nob_da_append(&inputs, EXAMPLES_GLFW_FOLDER "atb_glfw.h");
        break;
    }
    add_common_build_deps(&inputs, nob_exe);

    if (!build_needed(output, inputs.items, inputs.count)) {
        nob_log(NOB_INFO, "%s is up to date", output);
        return true;
    }

    Nob_Cmd cmd = {0};
    // Pick the driver per example source, same as compiler_for_source does
    // for the library's own objects: Advanced_cpp.cpp is real C++ and needs
    // "c++", but every other kept example is plain C99 and should build
    // with "cc" - linking a C99 example through the C++ driver made every
    // one of them silently compile as C++ instead (cc1plus, not cc1), which
    // is a materially different, stricter language for those sources.
    // LIB_STATIC itself is pure C99 now (TwEventSFML.cpp, its
    // one remaining C++ object, was deleted in Step 7 - see
    // docs/plans/c99-rewrite.md), so "cc" links against it exactly as
    // build_object() does when compiling it.
    const char *compiler = compiler_for_source(source);
    nob_cmd_append(&cmd, compiler);
    nob_cmd_append(&cmd, "-Wall", "-O2", "-I" INCLUDE_FOLDER, "-I" GLAD_INCLUDE);
    // Not defining TW_STATIC when dynamic leaves AntTweakBar.h's TW_API at
    // its default (TW_IMPORT_API, __declspec(dllimport) on Windows) - the
    // correct declaration for calling into libAntTweakBarC99.dll/.so/.dylib.
    if (!dynamic) nob_cmd_append(&cmd, "-DTW_STATIC");
    switch (backend) {
    case BACKEND_SDL:    append_sdl_flags(&cmd);    break;
    case BACKEND_SFML:   append_sfml_flags(&cmd);   break;
    case BACKEND_RAYLIB: append_raylib_flags(&cmd); break;
    case BACKEND_GLFW: default: append_glfw_flags(&cmd); break;
    }

    nob_cmd_append(&cmd, source);
    if (backend != BACKEND_RAYLIB) nob_cmd_append(&cmd, GLAD_OBJ);
#if defined(_WIN32)
    nob_cmd_append(&cmd, dynamic ? LIB_IMPORT : LIB_STATIC);
#else
    nob_cmd_append(&cmd, dynamic ? LIB_SHARED : LIB_STATIC);
#endif
    nob_cmd_append(&cmd, "-o", output);
    switch (backend) {
    case BACKEND_SDL:    append_sdl_libs(&cmd);    break;
    case BACKEND_SFML:   append_sfml_libs(&cmd);   break;
    case BACKEND_RAYLIB: append_raylib_libs(&cmd); break;
    case BACKEND_GLFW: default: append_glfw_libs(&cmd); break;
    }

    return nob_cmd_run(&cmd);
}

// Printed after a successful `-examples -dynamic` build: unlike the static
// build (a single self-contained .exe/ELF/Mach-O with no runtime library
// dependency), these executables need their shared library dependencies to
// be locatable at *run* time, not just link time, and that is easy to miss
// since the build itself succeeds either way.
static void print_dynamic_runtime_notice(void)
{
#if defined(_WIN32)
    nob_log(NOB_INFO, "-dynamic executables need %s next to the .exe, or on PATH.", LIB_SHARED);
#elif defined(__APPLE__)
    nob_log(NOB_INFO, "-dynamic executables need %s to be locatable at runtime", LIB_SHARED);
    nob_log(NOB_INFO, "(next to the executable, on DYLD_LIBRARY_PATH, or installed to a standard library path).");
#else
    nob_log(NOB_INFO, "-dynamic executables need %s to be locatable at runtime", LIB_SHARED);
    nob_log(NOB_INFO, "(e.g. via LD_LIBRARY_PATH=lib, an rpath, or installed to a standard library path).");
#endif
    nob_log(NOB_INFO, "See README.md's \"Running dynamically linked examples\" section for how to run them");
    nob_log(NOB_INFO, "without copying any library files or permanently changing PATH.");
}

static bool build_examples(const char *nob_exe, bool dynamic, Backend backend)
{
    if (!check_examples_deps(dynamic)) return false;
    if (!nob_mkdir_if_not_exists(EXAMPLES_BUILD_FOLDER)) return false;
    if (!nob_mkdir_if_not_exists(example_output_folder(dynamic, backend))) return false;
    if (backend != BACKEND_RAYLIB && !build_glad_for_examples(nob_exe)) return false;

    const char **backend_examples;
    size_t backend_examples_count;
    switch (backend) {
    case BACKEND_RAYLIB:
        if (!build_raylib(nob_exe)) return false;
        backend_examples = raylib_examples;
        backend_examples_count = NOB_ARRAY_LEN(raylib_examples);
        break;
    case BACKEND_SDL:
        if (!build_sdl(nob_exe)) return false;
        backend_examples = sdl_examples;
        backend_examples_count = NOB_ARRAY_LEN(sdl_examples);
        break;
    case BACKEND_SFML:
        if (!build_sfml(nob_exe)) return false;
        backend_examples = sfml_examples;
        backend_examples_count = NOB_ARRAY_LEN(sfml_examples);
        break;
    case BACKEND_GLFW:
    default:
        if (!build_glfw(nob_exe)) return false;
        backend_examples = glfw_examples;
        backend_examples_count = NOB_ARRAY_LEN(glfw_examples);
        break;
    }

    for (size_t i = 0; i < backend_examples_count; ++i) {
        if (!build_example(backend_examples[i], nob_exe, dynamic, backend)) return false;
    }

    // GLAD_OBJ/GLFW_OBJ/SFML_OBJ are only needed while linking the examples
    // above - remove them afterward rather than leave them as stale
    // leftovers (same trade-off as build_all()'s matching cleanup: the next
    // build always recompiles GLAD/GLFW/SFML from scratch too). SDL_LIB is
    // deliberately NOT deleted here - see build_sdl()'s own comment (SDL3's
    // ~140-file archive is too slow to rebuild every time; GLFW's and
    // SFML's single unity objects are cheap enough not to bother keeping).
    // RAYLIB_LIB, like SDL_LIB, is kept: its seven objects (one of them the
    // whole of GLFW) are too slow to rebuild on every invocation.
    if (backend != BACKEND_RAYLIB && !delete_if_exists(GLAD_OBJ)) return false;
    if (backend == BACKEND_GLFW && !delete_if_exists(GLFW_OBJ)) return false;
    if (backend == BACKEND_SFML && !delete_if_exists(SFML_OBJ)) return false;

    nob_log(NOB_INFO, "built %zu %s examples into %s (%s)", backend_examples_count,
            backend_name(backend),
            example_output_folder(dynamic, backend),
            dynamic ? "dynamically linked" : "statically linked");
    if (dynamic) print_dynamic_runtime_notice();
    return true;
}

static bool build_tests(const char *nob_exe, bool record)
{
    if (!nob_mkdir_if_not_exists(BUILD_FOLDER)
        || !nob_mkdir_if_not_exists(TEST_BUILD_FOLDER)) return false;

    Nob_File_Paths deps = {0};
    if (!collect_tree_files(&deps, SRC_FOLDER)
        || !collect_tree_files(&deps, INCLUDE_FOLDER)
        || !collect_tree_files(&deps, SDS_INCLUDE)) return false;
    nob_da_append(&deps, "tests/record_graph.h");
    add_common_build_deps(&deps, nob_exe);

    Nob_File_Paths sources = {0};
    for (size_t i = 0; i < NOB_ARRAY_LEN(common_sources); ++i) {
        const char *source = common_sources[i];
        // Use the real core with test implementations of its renderer factories.
        if (is_renderer_source(source)) continue;
        nob_da_append(&sources, source);
    }
    nob_da_append(&sources, "tests/record_graph.c");
    nob_da_append(&sources, "tests/regression.c");
    Nob_File_Paths objects = {0};
    for (size_t i = 0; i < sources.count; ++i) {
        if (!build_object(sources.items[i], TEST_BUILD_FOLDER, "-DTW_STATIC", &deps)) return false;
        nob_da_append(&objects, object_path(TEST_BUILD_FOLDER, sources.items[i]));
    }
    const char *output = TEST_BUILD_FOLDER "regression" EXE_EXT;
    Nob_Cmd cmd = {0};
    if (build_needed(output, objects.items, objects.count)) {
        nob_cmd_append(&cmd, "cc", "-o", output);
        for (size_t i = 0; i < objects.count; ++i) nob_cmd_append(&cmd, objects.items[i]);
        nob_cmd_append(&cmd, "-lm");
        if (!nob_cmd_run(&cmd)) return false;
    }
    nob_cmd_append(&cmd, output);
    if (record) nob_cmd_append(&cmd, "--record");
    return nob_cmd_run(&cmd);
}

static bool clean(void)
{
    bool ok = true;

    ok = delete_if_exists(LIB_STATIC) && ok;
    ok = delete_if_exists(LIB_SHARED) && ok;
#if defined(_WIN32)
    ok = delete_if_exists(LIB_IMPORT) && ok;
#elif !defined(__APPLE__)
    ok = delete_if_exists(LIB_SHARED_SONAME) && ok;
#endif
    ok = clear_directory(LIB_FOLDER) && ok;
    ok = delete_if_exists(LIB_FOLDER) && ok;

    ok = clear_directory(BUILD_INCLUDE_FOLDER) && ok;
    ok = delete_if_exists(BUILD_INCLUDE_FOLDER) && ok;

    // clear_directory() (not just the known current examples/sources) so a
    // stale binary/object left over from a since-renamed or removed
    // example/source doesn't block removing the folder itself.
    ok = clear_directory(EXAMPLES_STATIC_GLFW_FOLDER) && ok;
    ok = delete_if_exists(EXAMPLES_STATIC_GLFW_FOLDER) && ok;
    ok = clear_directory(EXAMPLES_STATIC_SDL_FOLDER) && ok;
    ok = delete_if_exists(EXAMPLES_STATIC_SDL_FOLDER) && ok;
    ok = clear_directory(EXAMPLES_STATIC_SFML_FOLDER) && ok;
    ok = delete_if_exists(EXAMPLES_STATIC_SFML_FOLDER) && ok;
    ok = clear_directory(EXAMPLES_SHARED_GLFW_FOLDER) && ok;
    ok = delete_if_exists(EXAMPLES_SHARED_GLFW_FOLDER) && ok;
    ok = clear_directory(EXAMPLES_SHARED_SDL_FOLDER) && ok;
    ok = delete_if_exists(EXAMPLES_SHARED_SDL_FOLDER) && ok;
    ok = clear_directory(EXAMPLES_SHARED_SFML_FOLDER) && ok;
    ok = delete_if_exists(EXAMPLES_SHARED_SFML_FOLDER) && ok;
    ok = clear_directory(EXAMPLES_STATIC_RAYLIB_FOLDER) && ok;
    ok = delete_if_exists(EXAMPLES_STATIC_RAYLIB_FOLDER) && ok;
    ok = clear_directory(EXAMPLES_SHARED_RAYLIB_FOLDER) && ok;
    ok = delete_if_exists(EXAMPLES_SHARED_RAYLIB_FOLDER) && ok;
    ok = clear_directory(SDL_OBJ_FOLDER) && ok;
    ok = delete_if_exists(SDL_OBJ_FOLDER) && ok;
    ok = delete_if_exists(SDL_LIB) && ok;
    ok = clear_directory(RAYLIB_OBJ_FOLDER) && ok;
    ok = delete_if_exists(RAYLIB_OBJ_FOLDER) && ok;
    ok = delete_if_exists(RAYLIB_LIB) && ok;
    ok = delete_if_exists(SFML_OBJ) && ok;
    ok = clear_directory(EXAMPLES_BUILD_FOLDER) && ok;
    ok = delete_if_exists(EXAMPLES_BUILD_FOLDER) && ok;

    ok = clear_directory(BUILD_STATIC_FOLDER) && ok;
    ok = delete_if_exists(BUILD_STATIC_FOLDER) && ok;
    ok = clear_directory(BUILD_SHARED_FOLDER) && ok;
    ok = delete_if_exists(BUILD_SHARED_FOLDER) && ok;
    ok = clear_directory(TEST_BUILD_FOLDER) && ok;
    ok = delete_if_exists(TEST_BUILD_FOLDER) && ok;
    ok = clear_directory(BUILD_FOLDER) && ok; // e.g. a stray .DS_Store
    ok = delete_if_exists(BUILD_FOLDER) && ok;

    return ok;
}

static void usage(const char *program)
{
    printf("usage: %s [-examples-glfw | -examples-sdl | -examples-sfml | -examples-raylib]\n", program);
    printf("             [-dynamic] [-test | -test-record] [-clean] [-help]\n");
    printf("  (no flags)     build the library only - the library links against none of\n");
    printf("                 GLFW3/SDL3/SFML3/raylib, so this single build serves every\n");
    printf("                 -examples-* flag below\n");
    printf("  -examples-glfw build the GLFW3 examples against build/lib/libAntTweakBarC99.a\n");
    printf("                 (requires the library to already be built with ./nob)\n");
    printf("  -examples-sdl  same as -examples-glfw, but for the SDL3 examples\n");
    printf("                 (SDL3 backend: macOS/Windows so far, see docs/plans/sdl3-backend.md)\n");
    printf("  -examples-sfml same as -examples-glfw, but for the SFML3 examples\n");
    printf("                 (SFML3 backend: macOS/Windows so far, see docs/plans/sfml3-backend.md)\n");
    printf("  -examples-raylib same as -examples-glfw, but for the raylib examples\n");
    printf("  -dynamic       with any of the -examples-* flags above, link the examples\n");
    printf("                 against the shared library (build/lib/libAntTweakBarC99.{dll,so,dylib})\n");
    printf("                 instead of the static one (the default)\n");
    printf("  -test          build and run headless API, input and drawing regression tests\n");
    printf("  -test-record   run those tests and replace the drawing baseline for review\n");
    printf("  -clean         remove generated build files and exit\n");
    printf("  -help          print this help and exit\n");
}

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF_PLUS(argc, argv, NOB_HEADER);

    const char *nob_exe = argv[0];
    bool clean_requested = false;
    bool examples_glfw_requested = false;
    bool examples_sdl_requested = false;
    bool examples_sfml_requested = false;
    bool examples_raylib_requested = false;
    bool dynamic_requested = false;
    bool tests_requested = false;
    bool record_tests = false;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-clean") == 0) {
            clean_requested = true;
        } else if (strcmp(argv[i], "-examples-glfw") == 0) {
            examples_glfw_requested = true;
        } else if (strcmp(argv[i], "-examples-sdl") == 0) {
            examples_sdl_requested = true;
        } else if (strcmp(argv[i], "-examples-sfml") == 0) {
            examples_sfml_requested = true;
        } else if (strcmp(argv[i], "-examples-raylib") == 0) {
            examples_raylib_requested = true;
        } else if (strcmp(argv[i], "-test") == 0) {
            tests_requested = true;
        } else if (strcmp(argv[i], "-test-record") == 0) {
            tests_requested = true;
            record_tests = true;
        } else if (strcmp(argv[i], "-dynamic") == 0) {
            dynamic_requested = true;
        } else if (strcmp(argv[i], "-help") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            nob_log(NOB_ERROR, "unknown argument: %s", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    if ((examples_glfw_requested ? 1 : 0) + (examples_sdl_requested ? 1 : 0)
        + (examples_sfml_requested ? 1 : 0) + (examples_raylib_requested ? 1 : 0) > 1) {
        nob_log(NOB_ERROR, "-examples-glfw, -examples-sdl, -examples-sfml and -examples-raylib are mutually exclusive");
        return 1;
    }

    bool examples_requested = examples_glfw_requested || examples_sdl_requested
                              || examples_sfml_requested || examples_raylib_requested;
    if (tests_requested && (examples_requested || dynamic_requested || clean_requested)) {
        nob_log(NOB_ERROR, "-test/-test-record cannot be combined with example, dynamic or clean flags");
        return 1;
    }
    if (tests_requested) return build_tests(nob_exe, record_tests) ? 0 : 1;
    if (dynamic_requested && !examples_requested) {
        nob_log(NOB_WARNING, "-dynamic has no effect without -examples-glfw/-examples-sdl/-examples-sfml/-examples-raylib");
    }

    if (clean_requested) return clean() ? 0 : 1;

    if (examples_requested) {
        Backend backend = BACKEND_GLFW;
        if (examples_sdl_requested) backend = BACKEND_SDL;
        if (examples_sfml_requested) backend = BACKEND_SFML;
        if (examples_raylib_requested) backend = BACKEND_RAYLIB;
        return build_examples(nob_exe, dynamic_requested, backend) ? 0 : 1;
    }
    return build_all(nob_exe) ? 0 : 1;
}
