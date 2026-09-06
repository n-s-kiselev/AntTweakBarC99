#define NOB_IMPLEMENTATION
#include "vendor/nob/nob.h"

#define SRC_FOLDER           "src/"
#define INCLUDE_FOLDER       "include/"
#define BUILD_FOLDER         "build/"
#define BUILD_STATIC_FOLDER  "build/static/"
#define BUILD_SHARED_FOLDER  "build/shared/"
#define LIB_FOLDER           "lib/"
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

#define EXAMPLES_FOLDER       "examples/"
#define EXAMPLES_BUILD_FOLDER "build/examples/"

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
static const char *examples[] = {
    EXAMPLES_FOLDER "SimpleGL21.c",
    EXAMPLES_FOLDER "SimpleGL33.c",
    EXAMPLES_FOLDER "SimpleGL41.c",
    EXAMPLES_FOLDER "Shapes.c",
    EXAMPLES_FOLDER "MultiCubes.c",
    EXAMPLES_FOLDER "Particles.c",
    EXAMPLES_FOLDER "Strip.c",
    EXAMPLES_FOLDER "Triangle.c",
    EXAMPLES_FOLDER "Sponge.c",
    EXAMPLES_FOLDER "String.c",
    EXAMPLES_FOLDER "MultiWindow.c",
    EXAMPLES_FOLDER "Advanced_c99.c",
    EXAMPLES_FOLDER "Advanced_cpp.cpp",
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
    // repo's own established convention).
    // -I GLFW_INCLUDE: TwBar.c's EditInPlaceGetClipboard/SetClipboard call
    // glfwGetClipboardString/glfwSetClipboardString directly (see its own
    // header comment) - only the header is needed here, not GLFW_OBJ. The
    // symbols stay undefined in LIB_STATIC/LIB_SHARED and resolve at
    // final-link time against whichever single GLFW instance the consuming
    // application itself initializes (see docs/plans/
    // reapply-fork-changes-on-legacy-baseline.md Step 4b).
    nob_cmd_append(&cmd, "-Wall", "-Wextra", "-O3", "-fno-strict-aliasing", "-fPIC",
                        "-I" INCLUDE_FOLDER, "-I" GLAD_INCLUDE, "-I" GLFW_INCLUDE, "-I" SDS_INCLUDE, tw_define);
    // The whole library is real C99 now (Clusters 1-4 + Step 7 complete -
    // every common_sources entry compiles with "cc", not "c++"; nothing
    // left in this build needs a C++ standard at all) - request it
    // explicitly rather than relying on the compiler's own default C
    // dialect. -std=c99 is meaningless (and would be rejected as
    // conflicting) for a genuine C++ compile, which can't happen here in
    // practice (is_cpp_source has nothing left to say "c++" to in
    // common_sources) but guard on `compiler` anyway rather than assume.
    if (strcmp(compiler, "cc") == 0) {
        nob_cmd_append(&cmd, "-std=c99", "-pedantic");
    }
    append_platform_defines(&cmd);
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
    if (!collect_tree_files(&common_deps, SDS_INCLUDE)) return false;
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
    nob_cmd_append(cmd, "-lGL", "-lX11", "-ldl", "-lpthread");
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
    // source extension): some kept examples (e.g. Advanced_cpp.cpp) are
    // themselves real C++ sources, so this step needs a driver that can
    // compile those too. lib/libAntTweakBarC99.a itself is pure C99 now
    // (TwEventSFML.cpp, its one remaining C++ object, was deleted in
    // Step 7 - see docs/plans/c99-rewrite.md) - only the example sources,
    // not the library, motivate "c++" here.
    nob_cmd_append(&cmd, "c++");
    nob_cmd_append(&cmd, "-Wall", "-O2", "-DTW_STATIC", "-I" INCLUDE_FOLDER, "-I" GLAD_INCLUDE);
    append_glfw_flags(&cmd);

    nob_cmd_append(&cmd, source, GLAD_OBJ, LIB_STATIC);
    nob_cmd_append(&cmd, "-o", output);
    append_glfw_libs(&cmd);

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
    printf("  -examples  build the example programs against lib/libAntTweakBarC99.a\n");
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
