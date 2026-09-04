#define NOB_IMPLEMENTATION
#include "vendor/nob/nob.h"

#define SRC_FOLDER           "src/"
#define INCLUDE_FOLDER       "include/"
#define BUILD_FOLDER         "build/"
#define BUILD_STATIC_FOLDER  "build/static/"
#define BUILD_SHARED_FOLDER  "build/shared/"
#define LIB_FOLDER           "lib/"
#define NOB_HEADER           "vendor/nob/nob.h"

#define LIB_STATIC LIB_FOLDER "libAntTweakBarGLFW3.a"

#if defined(_WIN32)
#define LIB_SHARED LIB_FOLDER "libAntTweakBarGLFW3.dll"
#define LIB_IMPORT LIB_FOLDER "libAntTweakBarGLFW3.dll.a"
#elif defined(__APPLE__)
#define LIB_SHARED LIB_FOLDER "libAntTweakBarGLFW3.dylib"
#else
#define LIB_SHARED        LIB_FOLDER "libAntTweakBarGLFW3.so"
#define LIB_SHARED_SONAME LIB_FOLDER "libAntTweakBarGLFW3.so.1"
#define LIB_SHARED_SONAME_NAME "libAntTweakBarGLFW3.so.1"
#endif

#define EXAMPLES_FOLDER       "examples/"
#define EXAMPLES_BUILD_FOLDER "build/examples/"

// GLAD is needed by the library itself (TwOpenGLCore.cpp's Core Profile
// renderer includes <glad/glad.h> - a fork-specific change from stock
// upstream, which used GLEW/gl3.h) as well as by every example, so it's
// compiled twice: once per library object-set (see common_sources below)
// and once more for the examples (see build_glad_for_examples()).
#define GLAD_INCLUDE  "external/glad/include/"
#define GLAD_SRC      "external/glad/src/glad.c"
#define GLAD_OBJ      EXAMPLES_BUILD_FOLDER "glad.o"

// GLFW3 is vendored (unity build, see external/glfw/glfw_unity.c, already
// present in this repo and written in anticipation of this function - its
// own header comment names append_glfw_flags() by name) so examples need no
// system GLFW3 install on any platform. The library itself does not link
// GLFW at all (TwEventGLFW.c only needs the private MiniGLFW.h constants).
#define GLFW_INCLUDE  "external/glfw/include/"
#define GLFW_SRC      "external/glfw/glfw_unity.c"
#define GLFW_OBJ      EXAMPLES_BUILD_FOLDER "glfw.o"

#if defined(_WIN32)
#define EXE_EXT ".exe"
#else
#define EXE_EXT ""
#endif

// This project builds only these four examples, adapted to
// GLFW3+glad+Core Profile. The GLUT-based examples (TwSimpleGLUT.c,
// TwDualGLUT.c, TwString.cpp), the SDL/SFML examples, and the untouched
// legacy DirectX9/10/11 examples have been removed from examples/ -
// external/freeglut/ has no buildable source for Linux/macOS (headers +
// prebuilt Windows DLLs only), and DirectX/SDL/SFML are out of scope for
// this GLFW3/Core-Profile-focused project (see
// docs/plans/nob-build-system.md).
static const char *examples[] = {
    EXAMPLES_FOLDER "TwSimpleGLFW21.c",
    EXAMPLES_FOLDER "TwSimpleGLFW33.c",
    EXAMPLES_FOLDER "TwSimpleGLFW41.c",
    EXAMPLES_FOLDER "TwAdvanced1.cpp",
};

// Sources common to every platform, matching src/Makefile's SRC_COMMON.
// TwPrecomp.cpp is deliberately excluded: it is just "#include
// \"TwPrecomp.h\"" (an MSVC precompiled-header trigger stub with no other
// content), already excluded by this repo's own src/Makefile.
static const char *common_sources[] = {
    GLAD_SRC,
    SRC_FOLDER "TwColors.cpp",
    SRC_FOLDER "TwFonts.cpp",
    SRC_FOLDER "TwOpenGL.cpp",
    SRC_FOLDER "TwOpenGLCore.cpp",
    SRC_FOLDER "TwBar.cpp",
    SRC_FOLDER "TwMgr.cpp",
    SRC_FOLDER "LoadOGL.cpp",
    SRC_FOLDER "LoadOGLCore.cpp",
    SRC_FOLDER "TwEventGLFW.c",
    SRC_FOLDER "TwEventGLUT.c",
    SRC_FOLDER "TwEventSDL.c",
    SRC_FOLDER "TwEventSDL12.c",
    SRC_FOLDER "TwEventSDL13.c",
    SRC_FOLDER "TwEventSFML.cpp",
};

// TwEventX11.c on Linux only. src/Makefile's own "non-Windows" check would
// also compile it on macOS, but TwEventX11.c unconditionally #includes
// <X11/Xlib.h> (no internal platform guard) - a real gap in the existing
// Makefile (X11 isn't part of macOS without XQuartz), not a deliberate
// choice worth preserving. Nothing is added on Windows, matching
// src/Makefile exactly (it does not add TwEventWin.c there).
#if defined(_WIN32) || defined(__APPLE__)
static const char *platform_sources[] = { NULL };
#define PLATFORM_SOURCES_COUNT 0
#else
static const char *platform_sources[] = { SRC_FOLDER "TwEventX11.c" };
#endif

#if !defined(PLATFORM_SOURCES_COUNT)
#define PLATFORM_SOURCES_COUNT NOB_ARRAY_LEN(platform_sources)
#endif

static void collect_sources(Nob_File_Paths *sources)
{
    for (size_t i = 0; i < NOB_ARRAY_LEN(common_sources); ++i) {
        nob_da_append(sources, common_sources[i]);
    }
    for (size_t i = 0; i < PLATFORM_SOURCES_COUNT; ++i) {
        nob_da_append(sources, platform_sources[i]);
    }
}

static bool delete_if_exists(const char *path)
{
    if (nob_file_exists(path)) return nob_delete_file(path);
    return true;
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

static bool check_linux_desktop_deps(void)
{
#if !defined(_WIN32) && !defined(__APPLE__)
    if (!nob_file_exists("/usr/include/X11/Xlib.h")) {
        nob_log(NOB_ERROR, "Missing X11 development headers: X11/Xlib.h");
        nob_log(NOB_ERROR, "On Ubuntu/Debian install them with:");
        nob_log(NOB_ERROR, "    sudo apt update && sudo apt install libx11-dev libxxf86vm-dev libxext-dev");
        return false;
    }
#endif
    return true;
}

static bool make_dirs(void)
{
    return nob_mkdir_if_not_exists(BUILD_FOLDER)
        && nob_mkdir_if_not_exists(BUILD_STATIC_FOLDER)
        && nob_mkdir_if_not_exists(BUILD_SHARED_FOLDER)
        && nob_mkdir_if_not_exists(LIB_FOLDER);
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

static const char *compiler_for_source(const char *source)
{
#if defined(__APPLE__)
    // src/Makefile compiles every source (including the plain .c helpers)
    // as Objective-C++ on macOS (-ObjC++), since TwPrecomp.h pulls in
    // Foundation/AppKit there.
    (void)source;
    return "c++";
#else
    return is_cpp_source(source) ? "c++" : "cc";
#endif
}

static void append_platform_defines(Nob_Cmd *cmd)
{
#if defined(_WIN32)
    nob_cmd_append(cmd, "-D_WIN32");
#elif defined(__APPLE__)
    // Objective-C++ mode itself is requested via "-x objective-c++" below,
    // where it's added (not here) since it applies to a subset of callers.
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
    nob_cmd_append(&cmd, compiler_for_source(source));
    // Matches src/Makefile's CPPCFG: unconditional -fPIC (not just for the
    // shared object set - harmless for the static archive, and matches this
    // repo's own established convention).
    // -I GLFW_INCLUDE: TwBar.cpp's EditInPlaceGetClipboard/SetClipboard call
    // glfwGetClipboardString/glfwSetClipboardString directly (see its own
    // header comment) - only the header is needed here, not GLFW_OBJ. The
    // symbols stay undefined in LIB_STATIC/LIB_SHARED and resolve at
    // final-link time against whichever single GLFW instance the consuming
    // application itself initializes (see docs/plans/
    // reapply-fork-changes-on-legacy-baseline.md Step 4b).
    nob_cmd_append(&cmd, "-Wall", "-O3", "-fno-strict-aliasing", "-fPIC",
                        "-I" INCLUDE_FOLDER, "-I" GLAD_INCLUDE, "-I" GLFW_INCLUDE, tw_define);
    append_platform_defines(&cmd);
#if defined(__APPLE__)
    nob_cmd_append(&cmd, "-x", "objective-c++");
#endif
    nob_cmd_append(&cmd, "-c", source, "-o", output);
    return nob_cmd_run(&cmd);
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
    nob_cmd_append(cmd, "-lGL", "-lX11", "-lXxf86vm", "-lXext", "-lpthread", "-lm");
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
    nob_cmd_append(&cmd, "c++");
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
    if (!check_linux_desktop_deps()) return false;
    if (!make_dirs()) return false;

    Nob_File_Paths sources = {0};
    collect_sources(&sources);

    Nob_File_Paths common_deps = {0};
    if (!collect_tree_files(&common_deps, SRC_FOLDER)) return false;
    if (!collect_tree_files(&common_deps, INCLUDE_FOLDER)) return false;
    if (!collect_tree_files(&common_deps, GLAD_INCLUDE)) return false;
    add_common_build_deps(&common_deps, nob_exe);

    Nob_File_Paths static_objects = {0};
    Nob_File_Paths shared_objects = {0};

    for (size_t i = 0; i < sources.count; ++i) {
        if (!build_object(sources.items[i], BUILD_STATIC_FOLDER, "-DTW_STATIC", &common_deps)) return false;
        nob_da_append(&static_objects, object_path(BUILD_STATIC_FOLDER, sources.items[i]));

        if (!build_object(sources.items[i], BUILD_SHARED_FOLDER, "-DTW_EXPORTS", &common_deps)) return false;
        nob_da_append(&shared_objects, object_path(BUILD_SHARED_FOLDER, sources.items[i]));
    }

    if (!build_static_archive(&static_objects, nob_exe)) return false;
    if (!link_shared_library(&shared_objects, nob_exe)) return false;

    nob_log(NOB_INFO, "built %s and %s", LIB_STATIC, LIB_SHARED);
    return true;
}

static const char *example_executable_path(const char *source)
{
    char *base = nob_temp_strdup(nob_path_name(source));
    char *dot = strrchr(base, '.');
    if (dot) *dot = '\0';
    return nob_temp_sprintf("%s%s" EXE_EXT, EXAMPLES_BUILD_FOLDER, base);
}

static bool check_examples_deps(void)
{
    if (!nob_file_exists(LIB_STATIC)) {
        nob_log(NOB_ERROR, "%s does not exist yet.", LIB_STATIC);
        nob_log(NOB_ERROR, "Run `./nob` first to build the library, then `./nob -examples`.");
        return false;
    }
    return true;
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

// Compiles the vendored GLFW3 unity build (external/glfw/glfw_unity.c, see
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
    nob_cmd_append(cmd, "-lGL", "-lX11", "-lXrandr", "-lXi", "-lXxf86vm", "-ldl", "-lpthread");
#endif
}

static bool build_example(const char *source, const char *nob_exe)
{
    const char *output = example_executable_path(source);

    Nob_File_Paths inputs = {0};
    nob_da_append(&inputs, source);
    nob_da_append(&inputs, LIB_STATIC);
    nob_da_append(&inputs, GLAD_OBJ);
    nob_da_append(&inputs, GLFW_OBJ);
    add_common_build_deps(&inputs, nob_exe);

    if (!build_needed(output, inputs.items, inputs.count)) {
        nob_log(NOB_INFO, "%s is up to date", output);
        return true;
    }

    Nob_Cmd cmd = {0};
    // Always use the C++ driver here (regardless of the example's own
    // source extension): this step both compiles and links against
    // lib/libAntTweakBarGLFW3.a, which always contains C++ object code
    // (TwBar.cpp, TwMgr.cpp, etc.), so the link needs libstdc++ pulled in
    // automatically. Using "cc" for a .c-sourced example leaves operator
    // new/delete, RTTI, and exception symbols undefined on Linux (found and
    // fixed the same way in AntTweakBar-Legacy's nob.c).
    nob_cmd_append(&cmd, "c++");
    nob_cmd_append(&cmd, "-Wall", "-O2", "-DTW_STATIC", "-I" INCLUDE_FOLDER, "-I" GLAD_INCLUDE);
    append_glfw_flags(&cmd);

    nob_cmd_append(&cmd, source, GLAD_OBJ, LIB_STATIC);
    nob_cmd_append(&cmd, "-o", output);
    append_glfw_libs(&cmd);

#if defined(__APPLE__)
    // libAntTweakBarGLFW3.a uses NSCursor/NSImage internally (compiled as
    // Objective-C++); a plain C/C++ example linking it needs the ObjC
    // runtime explicitly since its own translation unit isn't ObjC.
    nob_cmd_append(&cmd, "-framework", "AppKit", "-framework", "Foundation", "-lobjc");
#endif

    return nob_cmd_run(&cmd);
}

static bool build_examples(const char *nob_exe)
{
    if (!check_examples_deps()) return false;
    if (!nob_mkdir_if_not_exists(EXAMPLES_BUILD_FOLDER)) return false;
    if (!build_glad_for_examples(nob_exe)) return false;
    if (!build_glfw(nob_exe)) return false;

    for (size_t i = 0; i < NOB_ARRAY_LEN(examples); ++i) {
        if (!build_example(examples[i], nob_exe)) return false;
    }

    nob_log(NOB_INFO, "built %zu examples into %s", NOB_ARRAY_LEN(examples), EXAMPLES_BUILD_FOLDER);
    return true;
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

    // clear_directory() (not just the known current examples/sources) so a
    // stale binary/object left over from a since-renamed or removed
    // example/source doesn't block removing the folder itself.
    ok = clear_directory(EXAMPLES_BUILD_FOLDER) && ok;
    ok = delete_if_exists(EXAMPLES_BUILD_FOLDER) && ok;

    ok = clear_directory(BUILD_STATIC_FOLDER) && ok;
    ok = delete_if_exists(BUILD_STATIC_FOLDER) && ok;
    ok = clear_directory(BUILD_SHARED_FOLDER) && ok;
    ok = delete_if_exists(BUILD_SHARED_FOLDER) && ok;
    ok = clear_directory(BUILD_FOLDER) && ok; // e.g. a stray .DS_Store
    ok = delete_if_exists(BUILD_FOLDER) && ok;

    return ok;
}

static void usage(const char *program)
{
    printf("usage: %s [-clean] [-examples] [-help]\n", program);
    printf("  -clean     remove generated build files and exit\n");
    printf("  -examples  build the example programs against lib/libAntTweakBarGLFW3.a\n");
    printf("             (requires the library to already be built with ./nob)\n");
    printf("  -help      print this help and exit\n");
}

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF_PLUS(argc, argv, NOB_HEADER);

    const char *nob_exe = argv[0];
    bool clean_requested = false;
    bool examples_requested = false;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-clean") == 0) {
            clean_requested = true;
        } else if (strcmp(argv[i], "-examples") == 0) {
            examples_requested = true;
        } else if (strcmp(argv[i], "-help") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            nob_log(NOB_ERROR, "unknown argument: %s", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    if (clean_requested) return clean() ? 0 : 1;
    if (examples_requested) return build_examples(nob_exe) ? 0 : 1;
    return build_all(nob_exe) ? 0 : 1;
}
