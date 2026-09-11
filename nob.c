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
// Vendored GLFW3 built as its own DLL, Windows-only: libAntTweakBarC99.dll
// calls glfwGetTime()/glfwGetClipboardString()/glfwSetClipboardString()
// directly (see build_all()'s comment) and, unlike a Linux .so or macOS
// .dylib, a Windows DLL cannot leave those as unresolved symbols to be
// serviced by whatever GLFW the host application happens to link. Naming
// it distinctly from a generic "glfw3.dll" avoids a DLL-search-order
// collision with an unrelated, ABI-incompatible glfw3.dll that might
// already be on PATH (see docs/TASK2.md Section 2).
#define GLFW_SHARED        LIB_FOLDER "AntTweakBarC99-glfw3.dll"
#define GLFW_SHARED_IMPORT LIB_FOLDER "AntTweakBarC99-glfw3.dll.a"
#elif defined(__APPLE__)
#define LIB_SHARED LIB_FOLDER "libAntTweakBarC99.dylib"
#else
#define LIB_SHARED        LIB_FOLDER "libAntTweakBarC99.so"
#define LIB_SHARED_SONAME LIB_FOLDER "libAntTweakBarC99.so.1"
#define LIB_SHARED_SONAME_NAME "libAntTweakBarC99.so.1"
#endif

#define EXAMPLES_FOLDER       "examples/"
#define EXAMPLES_BUILD_FOLDER "build/examples/"
// Split by link mode, not just a shared EXAMPLES_BUILD_FOLDER, so switching
// between `./nob -examples` and `./nob -examples -dynamic` always rebuilds:
// build_needed() only compares mtimes against a fixed output path, so a
// static and a dynamic build sharing one executable path could otherwise
// look "up to date" against the wrong link mode's binary left over from a
// previous run.
#define EXAMPLES_STATIC_FOLDER EXAMPLES_BUILD_FOLDER "static/"
#define EXAMPLES_SHARED_FOLDER EXAMPLES_BUILD_FOLDER "shared/"

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

#if defined(_WIN32)
// Vendored GLAD built as its own DLL, Windows-only, for the same reason as
// GLFW_SHARED above: GLAD keeps every loaded GL entry point in a global
// function-pointer variable (see vendor/glad/include/glad/glad.h's
// glad_gl* declarations), populated once at runtime by gladLoadGLLoader().
// libAntTweakBarC99.dll's own TwOpenGL.c/TwOpenGLCore.c call through those
// same globals - if GLAD were instead compiled as a private, separate copy
// baked into the DLL (as build_object()'s normal per-object-set compile
// would do), the DLL's copy would never have gladLoadGLLoader() called on
// it (only the application's own copy does), leaving every GL call
// resolving through a NULL pointer. GLAD already ships the fix for this
// (vendor/glad/include/glad/glad.h's GLAD_GLAPI_EXPORT/
// GLAD_GLAPI_EXPORT_BUILD dllexport/dllimport switch, the same pattern
// GLFWAPI uses) - build one shared GLAD instance and have both
// libAntTweakBarC99.dll and the consuming application import from it,
// exactly mirroring GLFW_SHARED.
#define GLAD_SHARED        LIB_FOLDER "AntTweakBarC99-glad.dll"
#define GLAD_SHARED_IMPORT LIB_FOLDER "AntTweakBarC99-glad.dll.a"
#endif

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

// extra_defines is a NULL-terminated array of additional -D flags (may be
// NULL itself for "none") - a plain array rather than one more nullable
// string parameter because the Windows shared object set needs two
// independent extra defines (-DGLFW_DLL and -DGLAD_GLAPI_EXPORT, see
// build_all()) applied together, not one.
static bool build_object(const char *source, const char *folder, const char *tw_define,
                          const char **extra_defines, Nob_File_Paths *common_deps)
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
    // symbols stay undefined in LIB_STATIC (every platform) and LIB_SHARED
    // on Linux/macOS, resolved at final-link time against whichever single
    // GLFW instance the consuming application itself initializes (see
    // docs/plans/reapply-fork-changes-on-legacy-baseline.md Step 4b). On
    // Windows, LIB_SHARED instead imports them from GLFW_SHARED at build
    // time (extra_defines passes -DGLFW_DLL for that object set - see
    // build_all()'s own comment). The same applies to GLAD's glad_gl*
    // function-pointer globals via -DGLAD_GLAPI_EXPORT/GLAD_SHARED.
    nob_cmd_append(&cmd, "-Wall", "-Wextra", "-O3", "-fno-strict-aliasing", "-fPIC",
                        "-I" INCLUDE_FOLDER, "-I" GLAD_INCLUDE, "-I" GLFW_INCLUDE, "-I" SDS_INCLUDE, tw_define);
    if (extra_defines) {
        for (const char **d = extra_defines; *d; ++d) nob_cmd_append(&cmd, *d);
    }
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
#if defined(_WIN32)
    nob_da_append(&inputs, GLFW_SHARED_IMPORT);
    nob_da_append(&inputs, GLAD_SHARED_IMPORT);
#endif

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
#if defined(_WIN32)
    nob_cmd_append(&cmd, GLFW_SHARED_IMPORT);
    nob_cmd_append(&cmd, GLAD_SHARED_IMPORT);
#endif
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

// Defined further below, next to build_glfw()/build_glad_for_examples()
// (all compile the same vendored sources, just with different
// flags/outputs); forward declared here since build_all() calls them before
// that point in the file.
#if defined(_WIN32)
static bool build_glfw_shared(const char *nob_exe);
static bool build_glad_shared(const char *nob_exe);
#endif

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

    Nob_File_Paths static_objects = {0};
    Nob_File_Paths shared_objects = {0};

    for (size_t i = 0; i < sources.count; ++i) {
        if (!build_object(sources.items[i], BUILD_STATIC_FOLDER, "-DTW_STATIC", NULL, &common_deps)) return false;
        nob_da_append(&static_objects, object_path(BUILD_STATIC_FOLDER, sources.items[i]));

        // TwBar.c/TwMgr.c call glfwGetTime()/glfwGetClipboardString()/
        // glfwSetClipboardString() directly (see build_object()'s own
        // GLFW_INCLUDE comment) and leave them as undefined symbols in
        // LIB_STATIC and, on Linux/macOS, LIB_SHARED too - both tolerate an
        // unresolved symbol at build time and resolve it at load time
        // against whichever single GLFW instance the consuming application
        // itself initializes (see append_shared_link_flags()'s macOS
        // `-Wl,-undefined,dynamic_lookup` and the matching GNU ld default on
        // Linux). Windows DLLs have no equivalent: every imported symbol
        // must resolve to a concrete exporter at the DLL's own link time.
        // So on Windows this object set instead imports those three symbols
        // from GLFW_SHARED (built by build_glfw_shared() below and linked in
        // by link_shared_library()) via -DGLFW_DLL, which switches
        // <GLFW/glfw3.h>'s GLFWAPI to __declspec(dllimport). The same
        // problem exists for GLAD's glad_gl* function-pointer globals
        // (populated once by whichever gladLoadGLLoader() call happens to
        // run - normally the application's) - a private per-object-set copy
        // of glad.c baked into LIB_SHARED would never have that call
        // reach it, so on Windows glad.c is excluded from this object set
        // entirely (is_glad_source below) and imported from GLAD_SHARED
        // instead, via -DGLAD_GLAPI_EXPORT switching glad.h's GLAPI to
        // __declspec(dllimport). See docs/TASK2.md for the full reasoning
        // and the rejected alternative (embedding a second, separately-
        // uninitialized copy of GLFW's/GLAD's source directly into
        // LIB_SHARED).
#if defined(_WIN32)
        if (is_glad_source(sources.items[i])) continue;
        static const char *shared_extra_defines[] = { "-DGLFW_DLL", "-DGLAD_GLAPI_EXPORT", NULL };
        if (!build_object(sources.items[i], BUILD_SHARED_FOLDER, "-DTW_EXPORTS", shared_extra_defines, &common_deps)) return false;
#else
        if (!build_object(sources.items[i], BUILD_SHARED_FOLDER, "-DTW_EXPORTS", NULL, &common_deps)) return false;
#endif
        nob_da_append(&shared_objects, object_path(BUILD_SHARED_FOLDER, sources.items[i]));
    }

    if (!build_static_archive(&static_objects, nob_exe)) return false;

#if defined(_WIN32)
    if (!build_glfw_shared(nob_exe)) return false;
    if (!build_glad_shared(nob_exe)) return false;
#endif
    if (!link_shared_library(&shared_objects, nob_exe)) return false;

    // LIB_STATIC/LIB_SHARED above already contain everything these
    // intermediate .o files provided, so remove them (and the now-empty
    // per-object-set folders) rather than leave stale build output behind.
    // Trade-off: build_object()'s build_needed() sees a missing .o as
    // needing a rebuild, so every subsequent `./nob` always recompiles
    // every source from scratch - there is no longer an incremental/no-op
    // `./nob` re-run once this cleanup runs.
    if (!delete_objects(&static_objects)) return false;
    if (!delete_objects(&shared_objects)) return false;
    if (!delete_if_exists(BUILD_STATIC_FOLDER)) return false;
    if (!delete_if_exists(BUILD_SHARED_FOLDER)) return false;

    nob_log(NOB_INFO, "built %s and %s", LIB_STATIC, LIB_SHARED);
    return true;
}

static const char *example_executable_path(const char *source, bool dynamic)
{
    char *base = nob_temp_strdup(nob_path_name(source));
    char *dot = strrchr(base, '.');
    if (dot) *dot = '\0';
    const char *folder = dynamic ? EXAMPLES_SHARED_FOLDER : EXAMPLES_STATIC_FOLDER;
    return nob_temp_sprintf("%s%s" EXE_EXT, folder, base);
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
        if (!nob_file_exists(LIB_SHARED)) {
            nob_log(NOB_ERROR, "%s does not exist yet.", LIB_SHARED);
            nob_log(NOB_ERROR, "Run `./nob` first to build the library, then `./nob -examples -dynamic`.");
            return false;
        }
#if defined(_WIN32)
        // On Windows, the example must import GLFW/GLAD from the very same
        // DLLs libAntTweakBarC99.dll itself imports them from (see
        // GLAD_SHARED's own comment above and build_all()'s) - a private
        // per-example copy of GLFW/GLAD, as the static build uses, would
        // give the process two independent, uninitialized-against-each-
        // other GLFW/GLAD instances.
        static const struct { const char *path; } required[] = {
            { LIB_IMPORT }, { GLFW_SHARED_IMPORT }, { GLAD_SHARED_IMPORT },
        };
        for (size_t i = 0; i < NOB_ARRAY_LEN(required); ++i) {
            if (!nob_file_exists(required[i].path)) {
                nob_log(NOB_ERROR, "%s does not exist yet.", required[i].path);
                nob_log(NOB_ERROR, "Run `./nob` first to build the library, then `./nob -examples -dynamic`.");
                return false;
            }
        }
#endif
    } else if (!nob_file_exists(LIB_STATIC)) {
        nob_log(NOB_ERROR, "%s does not exist yet.", LIB_STATIC);
        nob_log(NOB_ERROR, "Run `./nob` first to build the library, then `./nob -examples`.");
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

#if defined(_WIN32)
// Compiles vendor/glad/src/glad.c into its own DLL, for
// libAntTweakBarC99.dll (and any application that links it) to import
// GLAD's glad_gl* function-pointer globals from - see GLAD_SHARED's own
// comment for why a private per-DLL copy of GLAD does not work. Unlike
// build_glfw_shared(), there is no separate plain-object compile of GLAD to
// contrast this with for the library build - build_glad_for_examples()'s
// GLAD_OBJ is a distinct, examples-only compile, not part of the library's
// own object sets. GLAD's win32 loader dlopen's opengl32.dll itself
// (LoadLibraryW("opengl32.dll") in glad.c) rather than importing it, so
// no -lopengl32 is needed at this link step, unlike build_glfw_shared()'s.
static bool build_glad_shared(const char *nob_exe)
{
    const char *inputs[] = { GLAD_SRC, "nob.c", nob_exe, NOB_HEADER };
    if (!build_needed(GLAD_SHARED, inputs, NOB_ARRAY_LEN(inputs))) {
        nob_log(NOB_INFO, "%s is up to date", GLAD_SHARED);
        return true;
    }

    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "cc", "-O2", "-I" GLAD_INCLUDE,
                        "-DGLAD_GLAPI_EXPORT", "-DGLAD_GLAPI_EXPORT_BUILD");
    nob_cmd_append(&cmd, "-shared", GLAD_SRC, "-o", GLAD_SHARED,
                        "-Wl,--out-implib," GLAD_SHARED_IMPORT);
    return nob_cmd_run(&cmd);
}
#endif

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

#if defined(_WIN32)
// Compiles vendor/glfw/glfw_unity.c into its own DLL, for
// libAntTweakBarC99.dll to import GLFW3 functions from (see build_all()'s
// comment and docs/TASK2.md). This is a second, separate compile of the
// same source as build_glfw() with different defines (-D_GLFW_BUILD_DLL
// instead of a plain object) - the two outputs are not interchangeable.
static bool build_glfw_shared(const char *nob_exe)
{
    const char *inputs[] = { GLFW_SRC, "nob.c", nob_exe, NOB_HEADER };
    if (!build_needed(GLFW_SHARED, inputs, NOB_ARRAY_LEN(inputs))) {
        nob_log(NOB_INFO, "%s is up to date", GLFW_SHARED);
        return true;
    }

    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "cc", "-O2", "-I" GLFW_INCLUDE,
                        "-D_GLFW_WIN32", "-D_GLFW_BUILD_DLL");
    nob_cmd_append(&cmd, "-shared", GLFW_SRC, "-o", GLFW_SHARED,
                        "-Wl,--out-implib," GLFW_SHARED_IMPORT);
    nob_cmd_append(&cmd, "-lopengl32", "-lgdi32");
    return nob_cmd_run(&cmd);
}
#endif

static void append_glfw_flags(Nob_Cmd *cmd)
{
    nob_cmd_append(cmd, "-I" GLFW_INCLUDE);
}

// dynamic selects, on Windows only, importing GLFW from GLFW_SHARED_IMPORT
// (the same DLL libAntTweakBarC99.dll itself imports GLFW from) instead of
// linking in the example's own private GLFW_OBJ - see check_examples_deps()'s
// comment. Elsewhere, LIB_SHARED already leaves its few direct GLFW calls
// unresolved for the consuming application's own GLFW instance to satisfy
// (see build_object()'s GLFW_INCLUDE comment), so every other platform links
// GLFW_OBJ the same way regardless of dynamic.
static void append_glfw_libs(Nob_Cmd *cmd, bool dynamic)
{
#if defined(_WIN32)
    if (dynamic) {
        nob_cmd_append(cmd, GLFW_SHARED_IMPORT);
        return;
    }
#endif
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

// dynamic links the example against the shared library (LIB_SHARED, plus
// LIB_IMPORT/GLFW_SHARED_IMPORT/GLAD_SHARED_IMPORT on Windows - see
// check_examples_deps()) instead of LIB_STATIC; the caller is otherwise
// identical either way.
static bool build_example(const char *source, const char *nob_exe, bool dynamic)
{
    const char *output = example_executable_path(source, dynamic);

    Nob_File_Paths inputs = {0};
    nob_da_append(&inputs, source);
    nob_da_append(&inputs, dynamic ? LIB_SHARED : LIB_STATIC);
#if defined(_WIN32)
    if (dynamic) {
        nob_da_append(&inputs, LIB_IMPORT);
        nob_da_append(&inputs, GLFW_SHARED_IMPORT);
        nob_da_append(&inputs, GLAD_SHARED_IMPORT);
    } else {
        nob_da_append(&inputs, GLAD_OBJ);
        nob_da_append(&inputs, GLFW_OBJ);
    }
#else
    nob_da_append(&inputs, GLAD_OBJ);
    nob_da_append(&inputs, GLFW_OBJ);
#endif
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
    // lib/libAntTweakBarC99.a itself is pure C99 now (TwEventSFML.cpp, its
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
#if defined(_WIN32)
    // Matches the -DGLFW_DLL/-DGLAD_GLAPI_EXPORT pair build_all() compiles
    // LIB_SHARED itself with (see its own comment) - the example must agree
    // with the DLL it is importing GLFW/GLAD from on whether those symbols
    // are dllimport-declared.
    if (dynamic) nob_cmd_append(&cmd, "-DGLFW_DLL", "-DGLAD_GLAPI_EXPORT");
#endif
    append_glfw_flags(&cmd);

    nob_cmd_append(&cmd, source);
#if defined(_WIN32)
    if (dynamic) {
        nob_cmd_append(&cmd, GLAD_SHARED_IMPORT, LIB_IMPORT);
    } else {
        nob_cmd_append(&cmd, GLAD_OBJ, LIB_STATIC);
    }
#else
    nob_cmd_append(&cmd, GLAD_OBJ, dynamic ? LIB_SHARED : LIB_STATIC);
#endif
    nob_cmd_append(&cmd, "-o", output);
    append_glfw_libs(&cmd, dynamic);

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
    // Three separate DLLs, not one: libAntTweakBarC99.dll itself imports
    // GLFW/GLAD from GLFW_SHARED/GLAD_SHARED rather than embedding private
    // copies (see GLAD_SHARED's own comment above), so the example needs all
    // three at once, not just libAntTweakBarC99.dll.
    nob_log(NOB_INFO, "-dynamic executables need these 3 DLLs next to the .exe, or on PATH:");
    nob_log(NOB_INFO, "  %s", LIB_SHARED);
    nob_log(NOB_INFO, "  %s", GLFW_SHARED);
    nob_log(NOB_INFO, "  %s", GLAD_SHARED);
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

static bool build_examples(const char *nob_exe, bool dynamic)
{
    if (!check_examples_deps(dynamic)) return false;
    if (!nob_mkdir_if_not_exists(EXAMPLES_BUILD_FOLDER)) return false;
    if (!nob_mkdir_if_not_exists(dynamic ? EXAMPLES_SHARED_FOLDER : EXAMPLES_STATIC_FOLDER)) return false;
    if (!build_glad_for_examples(nob_exe)) return false;
    if (!build_glfw(nob_exe)) return false;

    for (size_t i = 0; i < NOB_ARRAY_LEN(examples); ++i) {
        if (!build_example(examples[i], nob_exe, dynamic)) return false;
    }

    // GLAD_OBJ/GLFW_OBJ are only needed while linking the examples above -
    // remove them afterward rather than leave them as stale leftovers (same
    // trade-off as build_all()'s matching cleanup: the next `./nob
    // -examples` always recompiles GLAD/GLFW from scratch too).
    if (!delete_if_exists(GLAD_OBJ)) return false;
    if (!delete_if_exists(GLFW_OBJ)) return false;

    nob_log(NOB_INFO, "built %zu examples into %s (%s)", NOB_ARRAY_LEN(examples),
            dynamic ? EXAMPLES_SHARED_FOLDER : EXAMPLES_STATIC_FOLDER,
            dynamic ? "dynamically linked" : "statically linked");
    if (dynamic) print_dynamic_runtime_notice();
    return true;
}

static bool clean(void)
{
    bool ok = true;

    ok = delete_if_exists(LIB_STATIC) && ok;
    ok = delete_if_exists(LIB_SHARED) && ok;
#if defined(_WIN32)
    ok = delete_if_exists(LIB_IMPORT) && ok;
    ok = delete_if_exists(GLFW_SHARED) && ok;
    ok = delete_if_exists(GLFW_SHARED_IMPORT) && ok;
    ok = delete_if_exists(GLAD_SHARED) && ok;
    ok = delete_if_exists(GLAD_SHARED_IMPORT) && ok;
#elif !defined(__APPLE__)
    ok = delete_if_exists(LIB_SHARED_SONAME) && ok;
#endif

    // clear_directory() (not just the known current examples/sources) so a
    // stale binary/object left over from a since-renamed or removed
    // example/source doesn't block removing the folder itself.
    ok = clear_directory(EXAMPLES_STATIC_FOLDER) && ok;
    ok = delete_if_exists(EXAMPLES_STATIC_FOLDER) && ok;
    ok = clear_directory(EXAMPLES_SHARED_FOLDER) && ok;
    ok = delete_if_exists(EXAMPLES_SHARED_FOLDER) && ok;
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
    printf("usage: %s [-clean] [-examples [-dynamic]] [-help]\n", program);
    printf("  -clean     remove generated build files and exit\n");
    printf("  -examples  build the example programs against lib/libAntTweakBarC99.a\n");
    printf("             (requires the library to already be built with ./nob)\n");
    printf("  -dynamic   with -examples, link them against the shared library\n");
    printf("             (lib/libAntTweakBarC99.{dll,so,dylib}) instead of the static one\n");
    printf("  -help      print this help and exit\n");
}

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF_PLUS(argc, argv, NOB_HEADER);

    const char *nob_exe = argv[0];
    bool clean_requested = false;
    bool examples_requested = false;
    bool dynamic_requested = false;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-clean") == 0) {
            clean_requested = true;
        } else if (strcmp(argv[i], "-examples") == 0) {
            examples_requested = true;
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

    if (dynamic_requested && !examples_requested) {
        nob_log(NOB_WARNING, "-dynamic has no effect without -examples");
    }

    if (clean_requested) return clean() ? 0 : 1;
    if (examples_requested) return build_examples(nob_exe, dynamic_requested) ? 0 : 1;
    return build_all(nob_exe) ? 0 : 1;
}
