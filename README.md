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
color and quaternion/direction-vector visualizations) with no problems
found; Windows and Linux have not yet been interactively tested by a
human. See [`docs/plans/c99-rewrite.md`](docs/plans/c99-rewrite.md) for
the full record and remaining limitations.


## How to build

Bootstrap the build tool once, from the repository root:

```sh
gcc nob.c -o nob
```

Then:

```sh
./nob            # build the library (lib/libAntTweakBarC99.{a,so/dylib/dll})
./nob -clean     # remove all generated build output
./nob -examples  # build the example programs (requires ./nob to have run first)
./nob -help      # list all flags
```

`./nob` produces:

- `lib/libAntTweakBarC99.a` — static library
- `lib/libAntTweakBarC99.so` (Linux) / `lib/libAntTweakBarC99.dylib`
  (macOS) / `lib/libAntTweakBarC99.dll` + `.dll.a` (Windows/MinGW) —
  dynamic library

`./nob -examples` compiles the examples. Every example is strict C99 except `Advanced_cpp.cpp`. All examples compile statically against `lib/libAntTweakBarC99.a` into
`build/examples/`.

Note, no system GLFW3 install is needed. GLFW3 [vendor/glfw](vendor/glfw) and [GLAD](https://glad.dav1d.de/) ([vendor/glad](vendor/glad)) are vendored and built from source.
GLFW3 is this library's only supported event backend — the original
GLUT/SDL/SFML/X11 event-translation sources have been removed outright
(not ported), since nothing else in this fork ever used them. DirectX9/10/11
remain out of scope for this fork.

**Supported platforms:** Linux, macOS, and Windows (MinGW).


**License**

See [License.txt⁠](License.txt⁠).
