# AntTweakBarC99

AntTweakBarC99 is a C99 library that adds a lightweight, cross-platform GUI
to OpenGL applications, with three selectable, fully vendored windowing/event
backends: [GLFW3](https://www.glfw.org/), [SDL3](https://www.libsdl.org/), and
[SFML3](https://www.sfml-dev.org/). A fresh clone needs none of the three
installed on your system - all three are vendored and built from source
automatically (see "How to build" below).

This version of the library is a C99 rewrite of[AntTweakBar](https://anttweakbar.sourceforge.io/doc) (**ATB**), the original C/C++ library and legacy [GLFW2](https://github.com/glfw/glfw-legacy), [SDL2](https://wiki.libsdl.org/SDL2/FrontPage), [SFML](https://www.sfml-dev.org/) by [Philippe Decaudin](https://phildec.users.sourceforge.net/).

**Key features compared to legacy ATB:**

- **Clipboard support via a callback** — a new `TwSetClipboardCallback()`
  API (mirroring `TwSetCursorCallback()` below) lets the application route
  clipboard access through its own toolkit instead of AntTweakBar reaching
  into the system clipboard natively; every example wires it up to its own
  backend's clipboard API (GLFW3's `glfwGetClipboardString`/
  `glfwSetClipboardString`, SDL3's `SDL_GetClipboardText`/
  `SDL_SetClipboardText`, or SFML3's `sf::Clipboard::getString`/`setString`).
- **Custom cursors via a callback** — a new `TwSetCursorCallback()` API
  routes cursor changes through the application's own toolkit
  (`glfwSetCursor()`/`glfwCreateCursor()`, `SDL_SetCursor()`/
  `SDL_CreateColorCursor()`, or `sf::WindowBase::setMouseCursor()`/
  `sf::Cursor::createFromPixels()`) instead of AntTweakBar setting the
  system cursor natively.
- **OpenGL Core Profile renderer** (`TW_OPENGL_CORE`) — works with modern
  OpenGL 3.3/4.1 contexts, not just the legacy compatibility profile.
- **New widget layout parameters** — `full_width` (a widget spans the whole
  label+value width, with no separate label column - useful for buttons,
  separators, and wrapped multiline text) and `align_right`/`align_left`
  (right- or left-align a widget's own label within the label column;
  mutually exclusive, left-aligned by default, and each truncates an
  oversized label with an ellipsis on the side away from the kept text).
  All three are set the usual way, via `TwDefine`/`TwAddVar*`'s definition
  string (e.g. `" align_right=true "`). See `examples/glfw/Advanced_c99_glfw.c`'s
  `Background` group for a demo: `Red`, `Green`, `Blue`, and `Mode` are
  right-aligned (`Mode`'s label is deliberately long, to show the ellipsis
  truncation), while `Rot speed` and `Wireframe` keep the default left
  alignment.
- **Single cross-platform build** — one `nob.c` script that needs only a C compiler, replaces per-platform Makefiles and Visual Studio project files.

See also this repository [AntTweakBar-Legacy](https://github.com/n-s-kiselev/AntTweakBar-Legacy) for the legacy GLFW2/FreeGLUT/OpenGL compatibility wersion of the library easy to compile and test on MacOs, Windows or Linux with [nob.h](https://github.com/tsoding/nob.h) build system which itsef depends only on your C compiler.

The fork of ATB that you can use with modern version of GLFW3 can be found here, [AntTweakBarGLFW3](https://github.com/n-s-kiselev/AntTweakBarGLFW3).


## How to build

Bootstrap the build tool once, from the repository root:

```sh
gcc nob.c -o nob
```

Build the library (backend-independent - the same static/shared library is
produced no matter which backend's examples you plan to build against it):

```sh
./nob         # build the library only
./nob -glfw   # same as above - an explicit spelling for the GLFW3 workflow below
./nob -sdl    # same as above - an explicit spelling for the SDL3 workflow below
./nob -sfml   # same as above - an explicit spelling for the SFML3 workflow below
./nob -help   # list all flags
```

Then build a backend's 13 examples against that library:

```sh
./nob -examples-glfw [-dynamic]   # build the GLFW3 examples (examples/glfw/)
./nob -examples-sdl  [-dynamic]   # build the SDL3 examples (examples/sdl/)
./nob -examples-sfml [-dynamic]   # build the SFML3 examples (examples/sfml/)
```

All three demonstrate the same 13 demos, each adapted to that backend's own
windowing/event API. GLFW3 and SDL3 examples are plain C99 except
`Advanced_cpp_*.cpp`; SFML3 has no C API at all, so every SFML3 example is
C++. Add `-dynamic` to any of the three to link the examples against the
shared library instead of the static one, e.g. `./nob -examples-sdl -dynamic`.
`./nob -examples-glfw`/`-examples-sdl`/`-examples-sfml` require the library
to already be built (`./nob`, or one of `-glfw`/`-sdl`/`-sfml` above).

To rebuild from scratch you have to clean the folder from artifacts:

```
./nob -clean     # remove all generated build output
```

`./nob` produces everything under `build/` - the repository root stays source-only:

- `build/lib/libAntTweakBarC99.a` — static library, on every platform. This is
  the simplest option (no extra runtime files to ship) and is what the
  examples link against by default.
- `build/lib/libAntTweakBarC99.so` (Linux) / `build/lib/libAntTweakBarC99.dylib`
  (macOS) / `build/lib/libAntTweakBarC99.dll` (Windows/MinGW) — dynamic library,
  self-contained on every platform: the library loads its own private copy
  of GLAD's OpenGL function pointers itself (`gladLoadGL()`, called from
  `TwInit()`) rather than depending on the consuming application having
  already loaded them, and reaches the system clipboard only through an
  application-supplied `TwSetClipboardCallback()` (see above) rather than
  linking a toolkit directly - so no companion DLL, and no extra `.dll.a`
  import library, is needed on Windows either. If you don't need a shared
  library at all, link `build/lib/libAntTweakBarC99.a` instead.
- `build/include/AntTweakBar.h` — a copy of [`include/AntTweakBar.h`](include/AntTweakBar.h)
  (the real, git-tracked source, unchanged) placed next to the libraries above, so `build/`
  is a self-contained `lib`+`include` pair for anything linking against it.
- `build/examples/static-glfw/`, `static-sdl/`, `static-sfml/` (statically
  linked) and `shared-glfw/`, `shared-sdl/`, `shared-sfml/` (`-dynamic`) —
  one folder per backend and link mode, so switching between them never
  overwrites another combination's executables.

### Running dynamically linked examples

Executables in the `build/examples/shared-*/` folders are not self-contained - unlike the static
build, they need to find their shared library dependencies (in `build/lib/`) at runtime. Rather
than copying those library files next to every executable or permanently adding `build/lib/` to
your system `PATH`, point the loader at `build/lib/` for just the current shell session or command
instead (the examples below use the GLFW3 `Triangle` executable; the same pattern applies to any
example under any backend's `shared-*` folder):

**Windows (Command Prompt)**

```bat
cd build\examples\shared-glfw
set PATH=..\..\lib;%PATH%
Triangle_glfw.exe
```

**Windows (PowerShell)**

```powershell
cd build\examples\shared-glfw
$env:PATH = "..\..\lib;$env:PATH"
.\Triangle_glfw.exe
```

**Linux (bash)**

```sh
cd build/examples/shared-glfw
LD_LIBRARY_PATH=../../lib ./Triangle_glfw
```

**macOS (bash)**

```sh
cd build/examples/shared-glfw
DYLD_LIBRARY_PATH=../../lib ./Triangle_glfw
```

The `set PATH=`/`$env:PATH` assignment only lasts for the current Command Prompt/PowerShell session;
the `LD_LIBRARY_PATH`/`DYLD_LIBRARY_PATH` prefix form only applies to that single command. Either
way, your system-wide `PATH`/library search path is left untouched, and no `.dll`/`.so`/`.dylib`
file needs to be copied anywhere.

You do not need to install GLFW3, SDL3, or SFML3 on your system - all three are vendored
([vendor/glfw](vendor/glfw), [vendor/sdl](vendor/sdl), [vendor/sfml](vendor/sfml)) and built from
source automatically, alongside [GLAD](https://glad.dav1d.de/) ([vendor/glad](vendor/glad)).

GLFW3 is supported on Linux, macOS, and Windows (MinGW). SDL3 and SFML3 are currently validated on
macOS only - Linux and Windows support for those two backends is planned but not yet built or
tested. Legacy GLUT/X11-event-loop/SDL2/SFML2 event-translation sources from the original ATB have
been removed rather than ported forward; DirectX9/10/11 remain out of scope for this fork.

**Supported platforms:** Linux, macOS, and Windows (MinGW) for the GLFW3 backend; macOS only, so
far, for the SDL3 and SFML3 backends.


**License**

See [License.txt⁠](License.txt⁠).
