# AntTweakBarC99

AntTweakBarC99 is a C99 library, that adds a lightweight, cross-platform GUI to OpenGL + [GLFW3](https://www.glfw.org/) applications.

This version of the library is a C99 rewrite of[AntTweakBar](https://anttweakbar.sourceforge.io/doc) (**ATB**), the original C/C++ library and legacy [GLFW2](https://github.com/glfw/glfw-legacy), [SDL2](https://wiki.libsdl.org/SDL2/FrontPage), [SFML](https://www.sfml-dev.org/) by [Philippe Decaudin](https://phildec.users.sourceforge.net/).

**Key features compared to legacy ATB:**

- **Clipboard support via GLFW3** — `glfwGetClipboardString`/
  `glfwSetClipboardString` replace the original's native Win32/NSPasteboard/X11 clipboard code.
- **Custom cursors via GLFW3** — a new `TwSetCursorCallback()` API routes
  cursor changes through `glfwSetCursor()`/`glfwCreateCursor()` instead of
  AntTweakBar setting the system cursor natively.
- **OpenGL Core Profile renderer** (`TW_OPENGL_CORE`) — works with modern
  OpenGL 3.3/4.1 contexts, not just the legacy compatibility profile.
- **Single cross-platform build** — one `nob.c` script that needs only a C compiler, replaces per-platform Makefiles and Visual Studio project files.

See also this repository [AntTweakBar-Legacy](https://github.com/n-s-kiselev/AntTweakBar-Legacy) for the legacy GLFW2/FreeGLUT/OpenGL compatibility wersion of the library easy to compile and test on MacOs, Windows or Linux with [nob.h](https://github.com/tsoding/nob.h) build system which itsef depends only on your C compiler.

The fork of ATB that you can use with modern version of GLFW3 can be found here, [AntTweakBarGLFW3](https://github.com/n-s-kiselev/AntTweakBarGLFW3).

**The C99 rewrite of the core library is complete.** Every file `./nob`
builds — including `src/TwBar.c`/`TwMgr.c`, by far the largest share of
the rewrite — compiles cleanly as strict, pedantic C99
(`-std=c99 -pedantic -Wall -Wextra`), with no C++ and no Objective-C
anywhere in the library, on any platform. All 13 examples have been
manually exercised on macOS (every interactive widget, including the
color and quaternion/direction-vector visualizations) and on Linux
(built and run interactively, all examples rendering and working
correctly) with no problems found; Windows has not yet been
interactively tested by a human. See
[`docs/plans/c99-rewrite.md`](docs/plans/c99-rewrite.md) for the full
record and remaining limitations.


## How to build

Bootstrap the build tool once, from the repository root:

```sh
gcc nob.c -o nob
```

Then:

```sh
./nob                     # build the library (lib/libAntTweakBarC99.{a,so/dylib/dll})
./nob -examples           # build the example programs, statically linked (requires ./nob to have run first)
./nob -examples -dynamic  # build the example programs against the shared library instead
./nob -help               # list all flags
```

To rebuild from scratch you have to clean the folder from artifacts:

```
./nob -clean     # remove all generated build output
```

`./nob` produces:

- `lib/libAntTweakBarC99.a` — static library, on every platform. This is
  the simplest option (no extra runtime DLLs to ship) and is what
  `./nob -examples` links against on every platform.
- `lib/libAntTweakBarC99.so` (Linux) / `lib/libAntTweakBarC99.dylib`
  (macOS) / `lib/libAntTweakBarC99.dll` (Windows/MinGW) — dynamic library.
  The library calls a few GLFW3 functions (clipboard, timing) and GLAD's
  loaded OpenGL entry points directly. On Linux/macOS this simply leaves
  them as undefined symbols, resolved at load time against whichever
  single GLFW/GLAD instance the consuming application itself initializes.
  A Windows DLL cannot do that (every imported symbol must resolve to a
  concrete exporter at the DLL's own link time), so on Windows `./nob`
  additionally builds two small companion DLLs -
  `lib/AntTweakBarC99-glfw3.dll` and `lib/AntTweakBarC99-glad.dll` (+ their
  `.dll.a` import libraries) - and links `libAntTweakBarC99.dll` against
  them. **Anyone linking `libAntTweakBarC99.dll` on Windows must also link
  their own application against the same two `.dll.a` import libraries**
  (not their own separately-built GLFW/GLAD) **and ship both `.dll` files
  alongside `libAntTweakBarC99.dll`** at runtime - linking a second,
  separate copy of GLFW or GLAD instead would silently reproduce a
  "two uninitialized instances" bug (frozen timing, empty clipboard, or a
  black window depending on which one). Distinctly named, rather than
  generic `glfw3.dll`/`glad.dll`, to avoid a Windows DLL-search-order
  collision with an unrelated, ABI-incompatible DLL of the same name that
  might already be on `PATH`. If you don't need a DLL, link
  `lib/libAntTweakBarC99.a` instead - no extra DLLs involved.

`./nob -examples` compiles the examples. Every example is strict C99 except `Advanced_cpp.cpp`. By
default, examples compile statically against `lib/libAntTweakBarC99.a` and place executables in
`build/examples/static/`. Add `-dynamic` (`./nob -examples -dynamic`) to instead link them against
the shared library (`lib/libAntTweakBarC99.{so,dylib,dll}`), placing executables in
`build/examples/shared/`; on Windows this also links the examples against
`lib/AntTweakBarC99-glfw3.dll.a`/`lib/AntTweakBarC99-glad.dll.a` (the same two companion DLLs
`libAntTweakBarC99.dll` itself imports GLFW/GLAD from - see above), so running a dynamically-linked
example on Windows requires `lib/libAntTweakBarC99.dll`, `lib/AntTweakBarC99-glfw3.dll`, and
`lib/AntTweakBarC99-glad.dll` to be on `PATH` or copied next to the executable. See "Running
dynamically linked examples" below for how to do that without copying any files.

### Running dynamically linked examples

Executables in `build/examples/shared/` are not self-contained - unlike the static build, they need
to find their shared library dependencies (in `lib/`) at runtime. Rather than copying those library
files next to every executable or permanently adding `lib/` to your system `PATH`, point the loader
at `lib/` for just the current shell session or command instead:

**Windows (PowerShell)**

```powershell
cd build\examples\shared
$env:PATH = "..\..\..\lib;$env:PATH"
.\Advanced_c99.exe
```

**Linux (bash)**

```sh
cd build/examples/shared
LD_LIBRARY_PATH=../../../lib ./Advanced_c99
```

**macOS (bash)**

```sh
cd build/examples/shared
DYLD_LIBRARY_PATH=../../../lib ./Advanced_c99
```

The Windows `$env:PATH` assignment only lasts for the current PowerShell session; the
`LD_LIBRARY_PATH`/`DYLD_LIBRARY_PATH` prefix form only applies to that single command. Either way,
your system-wide `PATH`/library search path is left untouched, and no `.dll`/`.so`/`.dylib` file
needs to be copied anywhere.

You do not need to install GLFW3 in your system. GLFW3 [vendor/glfw](vendor/glfw) and [GLAD](https://glad.dav1d.de/) ([vendor/glad](vendor/glad)) are vendored and built from source automatically.

At the moment this library suports only GLFW3 event backend — the original
GLUT/SDL/SFML/X11 event-translation sources have been removed for simplicity of C++ to C99 migration. DirectX9/10/11 remain out of scope for this fork.

**Supported platforms:** Linux, macOS, and Windows (MinGW).


**License**

See [License.txt⁠](License.txt⁠).
