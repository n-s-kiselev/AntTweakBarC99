# Regression baseline

From the repository root, after bootstrapping `nob`:

```sh
./nob -test
```

This builds and runs the real C99 manager, widgets, fonts and event code with a
recording implementation of `ITwGraph`. It needs no window, graphics driver,
toolkit, network access or prebuilt library. The production renderers are built
and exercised separately. Test outputs and cached objects live in `build/tests/`;
`./nob -clean` removes them along with the other build outputs.

The tests cover parameter defaults, definition strings, typed set/get round
trips, rejected values, grouping, `full_width`, label alignment, multiline row
ownership and scrolling, checkbox/button input, enum popup selection, RotoSlider
activation/release, bar dragging, boundary hits, multiline keyboard navigation
and cancellation, and text-object cleanup after termination.

Six configurations use `fontsize=1/2/3` at `fontscaling=1/2`. Each captures an
unfocused bar, focused bar, scrolled multiline block, enum popup, active
RotoSlider, negative bar X with dark text, clipped content and expanded help.
The long numeric label exercises right-aligned ellipsis clipping.

`widget-layout.txt` stores ordered integer geometry, packed colors, text,
font height, text spacing/background widths, viewport and scissor calls. It
contains no addresses or timestamps. Text builds are recorded when drawn, so
cache rebuild frequency does not affect expectations. The actual trace is
written to `build/tests/actual-layout.txt`; failures report the first differing
line. Native Windows text line endings are accepted when reading fixtures.

To establish or intentionally change the expected appearance:

```sh
./nob -test-record
./nob -test
git diff -- tests/widget-layout.txt
```

Recording first runs the behavior checks; a failed check does not replace the
fixture. Never use recording to dismiss an unexplained regression. For naming,
style extraction and layout refactoring, the expected fixture should stay
unchanged. The original fixture was captured on macOS arm64 with Apple clang,
from the production core at `902b9f3`, before any production refactoring.

These are drawing-command fixtures, not pixel screenshots. They do not test
OpenGL rasterization/state restoration, font atlas contents, custom 3D widgets,
toolkit event translation, timed autorepeat or callback reentrancy. Real-window
screenshots and manual input checks are still required before accepting style
or renderer changes. Help refresh is forced and timed autorepeat is disabled
during the held RotoSlider capture to make test timing irrelevant. Cross-platform
fixture equivalence has not yet been validated; investigate any difference
before introducing platform-specific expectations.
