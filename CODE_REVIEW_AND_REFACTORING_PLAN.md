# Architectural review and incremental refactoring plan

Status: Active
Owner: Shared
Last updated: 2026-09-16

## 1. Goal and review boundary

Make the C99 core easier to understand and extend, keep its current pixels and public behavior, and improve measured hot paths. The initial review made no source or public API changes. Implementation has now started with the headless regression baseline; see §11 for progress and remaining gates.

This review uses the current tree at `902b9f3` plus read-only inspection of the working tree. The working tree already contains unrelated edits to `.gitignore`, `README.md`, `nob.c`, an SFML example, and vendored backend files, plus untracked work. Preserve them in every future stage. `AGENTS.md`'s project overview describes an earlier GLFW-dependent core; `nob.c:342-353`, `README.md`, and `2aa4a0d` establish the current toolkit-independent core with GLFW, SDL, and SFML example builds. Historical plans are useful evidence, not a replacement for current code. `docs/TASK4.md` outlines a future wiki; no checked-in wiki pages were found. Vendored GLFW, SDL, SFML, GLAD, SDS, and nob are dependency boundaries, not refactoring targets.

This plan is at the repository root because the existing `.gitignore` excludes `/docs`, including the usual `docs/plans/` location specified by `PLANS.md`. No ignore rule was changed.

## 2. Evidence from Git history

| Commit | Change surface and lesson |
| --- | --- |
| `658b961` | C99 conversion created the tagged `CTwVar` base, `CTwVarAtom`/`CTwVarGroup`, explicit containers, and `ITwGraph` function table. It preserved much C++ naming and control flow. Keep this compatibility work; improve its boundaries incrementally. |
| `49731a9`, `a923111`, `aa47c63`, `18175d6` | RotoSlider changes touched draw geometry, per-bar and per-drag state, cursor and pointer flow, public cursor callback documentation/examples, then corrected angle handling. Distinguish visual constants from drag math. |
| `30d9716` | `lines=N` needed `TwBar.h`, attribute lookup/set/get, hierarchy row creation, label/value text, scrollbar geometry/draw/hit/wheel/drag, edit activation, `TwMgr.c` help construction, and an example: 629 added lines in `TwBar.c`. One widget crosses nearly every phase. |
| `e8d0bf7`, `8e7bfc5`, `8026401` | Follow-ups fixed wrap-cache invalidation, made the edit overlay use wrapped rows, and added row navigation. Display and edit each maintain a wrap path. This is a concrete regression surface for future widgets. |
| `678c0af`, `19903b1`, `6ab4755` | Generic `full_width` added base state, attribute parsing, a row-X helper, label/value routing, backgrounds, draw, edit hit geometry, and examples. A simplification pass immediately removed duplicated boolean parsing and local work; the core still interprets the property in several phases. |
| `a5f5214` | Popup fit added an independent width calculation in `CTwBar_MouseButton` because `CTwBar_Update`'s clip width and `CTwBar_Draw`'s label origin disagree for a narrow popup. This is direct evidence that layout ownership is split. |
| `0548656`, `902b9f3` | Label alignment added base state, attribute lookup/set/get, text width/truncation logic, documentation and demos; the next commit simplified per-label scratch allocation and branches. Prefer one measured row/label result shared by draw and hit paths. |
| `068586b`, `7635547`, `6e5a4aa` | Example and build evolution means the old `./nob -examples` and GLFW-core statements in older plans are stale. Current flags are `./nob`, then `./nob -examples-glfw`, `-examples-sdl`, or `-examples-sfml`. |

The commit sizes are evidence of coupling, not a target line count. A truly new interaction will still require state, layout, input, rendering, and a test. The aim is to remove repeated decisions about the same property and rectangle.

## 3. Current architecture and extension paths

`include/AntTweakBar.h` exposes flat C calls. `TwAddVarRW/RO/CB`, `TwAddButton`, and `TwAddSeparator` enter `AddVar` (`TwMgr.c:3895`), which creates an atom, converts color/quaternion types to defined structs, applies defaults, and sends definition strings back through `TwDefine` (`TwMgr.c:4515`). `TwSetParam` and `TwGetParam` (`TwMgr.c:3450,3615`) use the same attribute dispatch: manager, bar, base variable, atom, or group. `CTwVar*_HasAttrib/SetAttrib/GetAttrib` are the current lookup, mutation, and query protocol. Built-in color and quaternion widgets are struct extensions with callback and member-proxy data in `TwMgr.c`/`TwMgr.h`, plus group-specific cases in `TwBar.c`.

`CTwBar_BrowseHierarchy` (`TwBar.c:4297`) flattens the tree to visible `CHierTag` rows. `CTwBar_Update` (`4840`) calculates bar and column bounds, rebuilds labels and values and GPU text objects, and computes bar and multiline scrollbar thumbs. `CTwBar_DrawHierHandle`, `CTwBar_Draw`, and `CTwBar_DrawMultilineWidgets` (`5177,5362,4217`) render those rows. `CTwBar_MouseMotion`, `MouseButton`, `MouseWheel`, and `KeyPressed` (`6009,6588,7132,7236`) rederive portions of the same geometry. `TwDraw` (`TwMgr.c:1981`) orders bars, manages overlap clipping, and calls draw; `TwMouseEvent` (`5402`) routes pointer events. OpenGL compatibility and Core renderers implement the existing `ITwGraph` table in `TwOpenGL.c`/`TwOpenGLCore.c`. `TwEventGLFW.c` is a legacy translation helper; SDL/SFML translation is in examples. `TwColors.c` is color math; `TwFonts.c` holds font generation and data.

### Representative change counts (current design)

| Change | Places a developer now visits | Proposed steady-state path |
| --- | --- | --- |
| New displayed widget on an existing value type | API call/example, `AddVar` or type classifier, atom state/defaults, attribute triplet, row count, label/value builders, draw branches, mouse motion/button/wheel/key, edit overlay if editable, help/doc; typically 8-12 phase sites across `TwBar.c`, `TwBar.h`, sometimes `TwMgr.c`. `lines` is the example. | Keep explicit type dispatch, but collect widget policy (row count, value text, hit kind, supported properties) in small cohesive helpers. Build one row layout consumed by draw and input. A genuinely new interaction still implements its own handler. |
| Generic variable property | `CTwVar` field/init, base attribute ID/lookup/set/get, layout/text/draw/input where relevant, docs/example; `full_width` and `align_right` each crossed 6-9 functions. | One typed parse/set/get path with an invalidation category; one resolved row property used by later phases. Retain accepted spellings/aliases. |
| One widget-class property | Atom or group option state/default, class attribute triplet, validation, class-specific layout/draw/input; `lines` also required help and edit handling. | Keep property ownership with the widget class, use shared parser and row geometry; do not add one field to every atom for a rare widget. |
| New layout behavior | Hierarchy, columns, labels/values, backgrounds, draw, hit targets, popup/edit placement. | Construct a `TwRowLayout` once per visible row with bounds, text origin, value bounds, and interaction bounds. Render/input consume it. |
| New visual style parameter | Currently search many literals in update, draw, event, `TwMgr.c` help/popup, and color derivation. | Add one named field to the internal style, derive per-bar metrics once on font/style change, and update every appearance and hit-test consumer together. |

### Boundaries to preserve

The public API, `TwType` numeric values, definition-string spellings, callback calling convention, renderer table, fixed bitmap font semantics, `nob.c` build, and toolkit-independent core are stable boundaries. A widget is often a **presentation of an existing type** (`lines` on strings) rather than a new public `TwType`; do not force every widget into a public type or a heap-allocated descriptor. The current small sorted enum array and short linear custom-record scans are appropriate until profiling proves otherwise.

## 4. Findings, effects, proposals, and migration risks

| Priority / evidence | Effect | Concrete improvement / benefit | Migration risk |
| --- | --- | --- | --- |
| High: layout/draw/input repeat column and row coordinates. `CTwBar_Update:4930-4963`, `ListLabels:4342`, `Draw:5456-5501`, `MouseMotion:6047-6175`, `MouseButton:6588+`; popup mismatch documented at `6680-6722`. | Correctness, extensibility, readability. A property may change pixels but leave a stale hit target or clipping bound. | Introduce a measured `TwBarLayout` and per-visible-row `TwRowLayout` of **derived rectangles**, built only on invalidation; share between drawing and input. Keep existing formulas and integer rounding first. | High: one-pixel changes, negative bar positions, clipped multiline blocks, popup. Compare pixel captures and hit boundaries. |
| High: `TwBar.c` holds attributes, layout, drawing, events, RotoSlider, edit overlay (~8.6k lines); `TwMgr.c` holds creation, parser, built-in custom widgets, events, help (~6.5k). | Readability and change conflicts. | After naming/style stages, move cohesive functions to `TwBarLayout.c`, `TwBarInput.c`, and `TwBarText.c` only when dependencies become explicit; register in `nob.c`. First split functions within the existing file if safer. | Medium: static helpers and include cycles; moving code without semantic change needs build and visual checks. |
| High: `CHierTag` is a visible row with `m_SubLine`, but multiline repeats one atom across N tags (`BrowseHierarchy`, `Multiline*`). | New variable-height widgets duplicate this pattern; help, edit, scroll share assumptions. | Name it as a visible row; add a row span and subrow index with a row-count helper. Keep contiguous flat array for O(visible rows) rendering. | Medium-high: scroll, row navigation, partially visible blocks. |
| High: `CTwBar_Update` builds several text objects using temporary `CSdsArray`s; `ListValues` converts a multiline atom once per visible subrow (`4553-4721`). Static wrap caches compare text with `strcmp` (`4126`), and cache key lacks font identity. | Allocation/copy cost on refresh; probable stale wraps if two fonts share width and text; non-reentrant shared scratch. | Measure first. Reuse per-bar buffers, convert once per atom per update, key wraps on font identity plus text/width, or store cache with the owning atom/bar. Preserve immediate callbacks and text refresh period. | Medium: cache lifetime, client values changing without explicit invalidation. |
| High: `tw_da_reserve` (`TwBar.h:40-56`) writes `realloc` result directly and asserts; compatibility renderer does likewise (`TwOpenGL.c:75-96`); Core renderer silently drops individual vertices/colors when reserve fails (`TwOpenGLCore.c:68-94`). | Correctness under OOM: crash or mismatched vertex arrays. | In a dedicated reliability stage, use checked reserve that preserves old pointer and returns failure before parallel arrays diverge; propagate library error convention. | Medium: many call sites, error unwinding. Do not hide OOM handling in an unreadable macro. |
| Medium: `CTwVarAtom_New` and `CTwBar_Create` dereference unchecked `malloc` (`TwBar.c:212,2873`); `AddVar` allocates proxy buffers before `memset` (`TwMgr.c:3980+`). | Failure-path correctness and ownership clarity. | Check allocation, initialize only acquired resources, unwind in reverse order, use the existing last-error contract. | Medium: callback/proxy rollback paths. |
| Medium: `PopupCallback` packs enum value through a pointer using `*(unsigned int *)&_ClientData` (`TwBar.c:6563`); creation packs the inverse in `MouseButton`. | Portability/undefined-behavior risk across pointer sizes and calling conventions. | Pass a small owned popup selection record or stable entry pointer with explicit lifetime; test deletion during callback. | Medium: popup ownership and callback reentrancy. |
| Medium: `TwDraw` rebuilds overlap rect arrays every frame and can draw a bar's content once per clip rect (`TwMgr.c:1981-2092`); `CRect_SubtractMany` (`6453`) can multiply rectangles. | Measurable risk only with many overlapping bars; redundant CPU/GPU work. | Instrument bars, clip rectangles, draw calls; if material, reuse array capacity and simplify clip regions without changing order. | Medium: transparent overlap behavior. |
| Medium: `CustomMap_Find` is linear in custom proxies and rebuilt each draw; custom index range sets `m_IndexMax = min(...)` at `TwBar.c:5633` while `m_IndexMin` also uses `min`. `m_IndexMax` has no other read in current source. | Readability and a likely dormant correctness trap for future custom widgets, not a demonstrated rendering bug today. | Remove the unused field only after checking extension expectations, or update it with `max` when adding a real consumer. Keep linear lookup unless counts warrant change. | Low-medium: custom extension behavior. |
| Medium: global `g_TwMgr`, master manager/window map, file-static wrap and scratch caches, `g_KMod` in `TwEventGLFW.c`. | Reentrancy and multithread risk; ownership is implicit. | Document single-thread assumption now; move scratch and backend modifier state to explicit owner when touched by a feature. Avoid a full context-parameter rewrite of public API. | High if done wholesale; low when isolated. |
| Medium: `CTwBar_UpdateColors:3725` owns most colors, but draw embeds ARGB literals and geometric offsets; built-in color/quaternion visualization owns more colors (`TwMgr.c:1058+`). | Theme extension would require code searches and risk inconsistent roles. | Keep per-bar tint as input; group resolved colors by semantic role, then route remaining active literals through palette defaults. | Medium: alpha/rounding differences. |
| Medium: `AddVar` recursively invokes `TwDefine`; parser returns IDs from separate base/atom/group/bar/manager domains, with case-insensitive chains (`TwMgr.c:3895,4515`, `TwBar.c:975,1266,2323,3098`). | New properties require synchronized lookup/set/get edits and error semantics. | Use small static property tables for spelling/type/owner/invalidation where they reduce repetition; keep special handlers explicit. No per-frame string lookup or dynamic registry. | Medium: legacy aliases and `TwGetParam` output forms. |
| Low: `TwFonts.c` and `nob.c` still contain C++/GLFW-era comments; `AntTweakBar.h` still describes native cursor behavior though implementation now callbacks (`TwMgr.c:6312+`), and old docs retain historical assumptions. | Reader confusion. | Update active API/build comments in a dedicated documentation pass; keep historical plan records labeled historical. | Low. |

The findings distinguish confirmed structure from risks. No profiler data or cross-platform UI captures were collected for this review. Do not label a suspected cache or GPU bottleneck as a measured regression.

## 5. Naming convention and priority changes

Public `Tw*` functions/types and definition-string keys remain byte-for-byte compatible; `CTwBar` is the public header's opaque struct tag (`typedef struct CTwBar TwBar`) and should remain. Preserve old enum values and callback typedefs. For internal additions, use `Tw`-prefixed type names, `TwSubsystem_Action` functions, lower-case descriptive parameters/locals, `*_px` for pixel values, `*_rows` for row counts, `*_seconds` for time, and `*_deg`/`*_rad` for angles. An existing `m_` member may retain the prefix during focused work; do not alternate new `m_` and bare members within the same struct. When a struct is migrated, name owned data and derived data separately. Keep short `x`, `y`, `i` inside a tiny coordinate/iteration scope; expand them when values cross phases.

| Current | Suggested internal name or interpretation | Reason and scope |
| --- | --- | --- |
| `CHierTag`, `m_Level`, `m_SubLine`, `m_Closing` | `TwVisibleRow`, `group_depth`, `text_subrow_index`, `closes_group` | It is a flattened on-screen row, not a markup tag; its subrow matters to wrapping/scroll. Internal only. |
| `CTwVal`, `m_Val` | `TwAtomOptions`, `options` (eventual) | The union holds per-type settings and widget state, not the client value; broad rename only when that section is touched. |
| `m_VarX0/X1/X2`, `m_VarY0/Y1/Y2` | `content_left_px`, `value_left_px`, `content_right_px`, `content_top_px`, `content_bottom_px`, `bar_bottom_px` in derived layout | Identifies coordinate space and edge meaning; check inclusive/exclusive conventions before replacing. |
| `m_Sep`, `m_LineSep`, `m_ValuesWidth` | `bar_separator_px`, `row_gap_px`, `value_column_width_px` | `Sep` and `Values` alone do not indicate what is separated/measured. Existing bar attributes and sentinel require compatibility audit. |
| `m_ScrollYW/H/Y0/Y1` | `scrollbar_width_px`, `scroll_track_height_px`, `scroll_thumb_top_px`, `scroll_thumb_bottom_px` in layout | Distinguishes track, thumb, and coordinate units. |
| `m_NoSlider`, `m_DrawHandles`, `m_UpToDate` | `uses_direct_edit`, `has_input_focus`, `layout_is_current` only if verified at all uses | Existing booleans combine behavior; prefer splitting state only if semantics truly differ. |
| `m_ColValTextNE`, `m_ColValTextRO`, `m_ColHighBtn` | `value_text_noneditable`, `value_text_read_only`, `button_hover_fill` in palette | Names color *roles* instead of abbreviations; preserve actual color values. |
| `CTwBar_ListLabels`, `CTwBar_ListValues`, `ClampText`, `SplitString` | `CTwBar_BuildLabelLines`, `CTwBar_BuildValueLines`, `TwText_TruncateToWidth`, `TwText_WrapToWidth` when those functions get clear owners | Their current verbs understate that they allocate/build text or measure pixels. Keep existing symbols until call sites move coherently. |
| `enum EVarAttribs`, `EVarAtomAttribs`, `EVarGroupAttribs`, `EBarAttribs`, `EMgrAttribs` | Keep separate domains but give IDs owner-qualified names such as `TW_VAR_ATTR_FULL_WIDTH` when migrating lookup | `V_*`/`VA_*`/`VG_*`/`BAR_*`/`MGR_*` identify owners with varying clarity; do not alter public `TW_TYPE_*` enums. |
| `PopupCallback`, `CQuaternionExt_DrawCB`, `TwCursorCB` | `TwEnumPopup_SelectCallback`, `TwQuaternion_DrawCallback` internally; keep public `TwCursorCB` | Callback role and owner are otherwise unclear. Public typedef spelling is compatibility-sensitive. |
| `g_HelpTextLines`, `TW_GLOBAL_BAR`, `VALUES_WIDTH_FIT`, `m_DrawRotoBtn` | `help_text_default_rows`, `TW_GLOBAL_TARGET`, `TW_VALUE_WIDTH_AUTO`, `show_roto_control` if a touched change warrants it | Distinguish a file constant from global state, a sentinel from a bar pointer, an automatic-width mode from a width, and a cached visibility flag from a draw command. Preserve sentinel values. |
| `SetBoolAttrib(..., _Invert)`, `V_ALIGN_LEFT/RIGHT` | explicit parse + a `TwLabelAlignment` value when adding more alignments | Today inverse storage is reasonable for two mutually exclusive options; do not replace solely for symmetry. |
| `FullWidthWrap`, `ValueWrap`, `Summary` static scratch | `full_width_wrap_cache`, `value_wrap_cache`, `summary_scratch` with explicit owner | Reveals lifetime and makes cache keys testable. |
| `e`, `r`, `sProxy`, `p`, `n`, `Len`, `Etc` in long functions | `enum_def`, `custom_record`, `struct_proxy`, `parse_advance`, `entry_count`, `text_len`, `ellipsis_state` where applicable | Rename only in touched cohesive functions; purpose otherwise requires nearby source reading. |
| `CColorExt`, `CQuaternionExt` | Keep initially; document as built-in struct widgets | Renaming them alone would touch many callback/type checks without improving the underlying dispatch. |

Avoid renaming `TwAddVar*`, `TwDefine`, `TwType`, `TW_TYPE_*`, `TwGraphAPI`, `TwBar`, public `TwEnumVal`/`TwStructMember` fields, `ITwGraph`, or vendored names. Avoid a repository-wide `C`/`m_` removal. Some C++ residue is worth correcting in comments (`TwFonts.c`'s outdated bridge description, `TwGraph.h`'s still-C++ caller note), but comment volume should decrease as ownership becomes explicit.

## 6. Visual style architecture

Start with an **internal**, per-bar resolved `TwStyle` containing `TwStyleGeometry geometry` and `TwStyleColors colors`, with the existing `CTexFont *` as the typography input. `CTwMgr` owns a default theme description later; `CTwBar` resolves its style when created and when font, tint, dark-text mode, or a future theme changes. A resolved geometry field is always in **screen pixels** (`_px`); dimensionless counts use `_rows` or `_glyphs`. Derive font-relative defaults once using the exact current integer expressions and rounding. Keep the current `m_Color`, `m_DarkText`, font, `m_ValuesWidth`, and public bar size/position as inputs or runtime overrides. `TwStyleColors` contains semantic resolved colors, initially calculated by the existing `CTwBar_UpdateColors` formulas from the bar tint and text mode. This adds no per-row allocations and no per-frame virtual dispatch. A separate typography settings struct is premature: current fonts are a fixed small set and `fontscaling` is already an initialization choice. A future public theme API can change manager defaults and trigger bar style/layout invalidation, without exposing internal layout structs now.

The next tables are a migration inventory. `H` means the active bar font's `m_CharHeight`; `S` means its space-glyph width; `sep` and `line_sep` are currently `m_Sep`/`m_LineSep`, both default 1 px. Expressions are **required default formulas**, not approximate replacements. For each row, search all named consumers (including draw and hit testing) when implementing. Repeated `+1/-1` raster edge operations should be tagged either as a named visual width or as a documented inclusive-coordinate invariant; do not blindly make every arithmetic 1 a theme knob.

### Geometry and typography mapping

| Current location / literal | Meaning | Proposed resolved `TwStyleGeometry` field / default | Consumers |
| --- | --- | --- | --- |
| `TwBar.c:2888-2891` `24*n-8`, `200`, `320`; `TwMgr.c:1786-1790` `32,32,400,200` | Default bar cascade/size; help bar placement/size | `new_bar_cascade_step_px=24`, `new_bar_origin_offset_px=-8`, `default_bar_width_px=200`, `default_bar_height_px=320`; `help_origin_x/y_px=32`, `help_width_px=400`, `help_height_px=200` | Creation only; preserve user-set bar geometry as state. |
| `TwBar.c:2922-2925` `m_Sep=1`, `m_LineSep=1`; popup `6719` uses sep as row gap | Bar separator and row gap | `bar_separator_px=1`, `row_gap_px=1`, `popup_row_gap_px=1` | Update, draw, row hit, popup. Keep popup alias to bar separator until behavior is deliberately changed. |
| `TwBar.c:4878-4890` `8*H`, `5*H`; `4907-4914` `2*H`, `4*H`; `4946` `32` | Resizable minimum bar, value-column min/max, minimum label column | `min_bar_width_px=8*H`, `min_bar_height_px=5*H`, `min_value_width_px=2*H`, `max_value_width_reserved_px=4*H`, `min_label_width_px=32` | Update and drag constraints. These are limits, not current widths. |
| `TwBar.c:4933-4960` `H+sep`, `H+sep+2`, `H+2+sep+6`, `H+2+sep`, popup left `2`, top `4` | Content insets and title/footer clearance | `content_left_inset_px=H+sep`, `content_right_inset_px=H+sep+2`, `content_top_inset_px=H+sep+8`, `content_bottom_inset_px=H+sep+2`; `popup_content_left_inset_px=2`, `popup_content_top_inset_px=4` | Update, draw, hit, popup fit. Preserve expression grouping and integer results. |
| `TwBar.c:3856,4353,4730,5389,5602,6716` `max(H-6,4)`; draw label origin `LevelSpace+6` | Group depth step and label draw origin | `group_indent_step_px=max(H-6,4)`, `label_origin_after_indent_px=6` | Row helper, labels, highlights, separators, popup and hit. One shared origin is a prerequisite to popup cleanup. |
| `TwBar.c:4722-4797` `3*S`, `2*S`; `4344,4557` two ellipsis dots; `4800` clamp | Label/value extra width and truncation | `label_value_gap_px=3*S`, `value_text_trailing_space_px=2*S`, `ellipsis_glyph_count=2` | Auto-fit, left/right alignment, clip. Keep text measurement in glyph widths. |
| `TwBar.c:5046,5153,5161` `5*H`, `16*H`, `3*H`; title draw around `5413-5435` | Title, minimized title, status text clipping and title height | `title_text_reserved_width_px=5*H`, `minimized_title_max_width_px=16*H`, `status_text_reserved_width_px=3*H`, `title_height_px=H+2` | Update, draw, title hit/drag. |
| `TwBar.c:4992-5031`, `5177-5300`, `3866-3885` `2`, `H-2`, `H-4`, `4` | Outer scrollbar, inner multiline scrollbar and minimum thumb | `outer_scrollbar_inset_px=2`, `outer_scrollbar_width_px=H-3` as inclusive `x1-x0+1`; `multiline_scrollbar_width_px=H-4`, `min_scroll_thumb_px=4`, `multiline_scrollbar_edge_inset_px=2` | Thumb math, draw, hover, drag, wrap gutter. Verify actual outer width after parity adjustment before freezing metric. |
| `TwBar.h:580-590` `H-4+2`; `TwBar.c:3946,4217` min track `4`, inset `2`; `TwMgr.c:5925` 3 rows, `6283` 6 rows | Multiline gutter/track and help defaults | `multiline_text_gutter_px=(H-4)+2`, `multiline_min_track_px=4`, `multiline_scrollbar_inner_inset_px=2`, `help_text_default_rows=3`, `roto_help_rows=6` | Wrap, thumb, draw, edit, help generation. `lines=N` itself remains widget state, not style. |
| `TwBar.c:3850` `((2*H)/3+2)&~1`; `5066-5100` thresholds `4,5,2,4` button widths; `5550-5599` `+2,+1,+3,-3` | Action button and rectangle/glyph geometry | `action_button_width_px=((2*H)/3+2)&~1`, `button_vertical_inset_px=3`, `button_horizontal_inset_px=1`, `button_shadow_offset_px=2`; keep availability thresholds named as **counts** `click=4`, `increment=5`, `list=2`, `bool=4` | Update, button draw, hit. Derive button rect once; use it for painting and interaction. |
| `TwBar.c:5708-5755` `bw/4`, offsets `1,2,4`; `4553-4610` `" -"`/`0x7f` | Boolean glyph and check visualization | `boolean_chevron_extent_px=bw/4`, `boolean_glyph_nudge_px` values preserving the two drawn chevrons; `checkbox_mark` remains font glyph data (`0x7f`) | Value text and boolean button draw. Measure visual before merging glyph and button concepts. |
| `TwBar.c:5535-5540,5523` `checker=8`, `H-2` | Color swatch rows/checker pattern | `color_swatch_checker_cells=8`, `color_swatch_vertical_inset_px=1` (current height `H-2`) | Color group draw and swatch bounds. Color itself is runtime client value. |
| `TwBar.c:5602-5604` `H/2`, custom bounds `5612-5615` `-2,+1` | Separator baseline and custom viewport inset | `separator_center_y_px=H/2`, `custom_right_inset_px=2`, `custom_top_inset_px=1` | Separator/custom draw and custom hit map. |
| `TwBar.c:6680-6735` cap `32*max_glyph`, x `-2`, width `content+H+sep+indent+8`, height `entries*(H+sep)+H/2+2`, edge `2`, minimum `3` rows | Enum popup fit and viewport policy | `popup_max_label_glyphs=32`, `popup_anchor_left_shift_px=2`, `popup_extra_right_space_px=8`, `popup_bottom_extra_px=H/2+2`, `popup_viewport_gap_px=2`, `popup_min_visible_rows=3` | Popup creation plus shared label origin/clip. Derive width from resolved row metrics, preserving this exact formula first. |
| `TwBar.c:3000,7668-7745` radius `24`, rings `31/32/33`, bounds dots `7/4`, tail `(36,8),(17,7),(0,6),(-16,5),(-30,4),(-43,3)` | RotoSlider visual scale and decoration | `roto_activation_radius_px=24`, `roto_ring_inner/middle/outer_radius_px=31/32/33`, `roto_max/min_dot_radius_px=7/4`, named tail-dot offsets (arc pixels) and radii in a six-entry style array | Roto drawing and drag boundary. `m_RotoNbSubdiv=256`, angle accumulation, step and current values are behavior/state, not theme geometry. |
| `TwBar.c:5456,5471,5478-5490` highlight inset `1`, marker width `4`, shadow `3`, outline 1 px; `TwMgr.c:2052` overlap margin `4` | Row highlight, bar border/shadow and overlap bleed | `row_highlight_top_inset_px=1`, `read_only_marker_width_px=4`, `bar_shadow_extent_px=3`, `bar_border_width_px=1`, `overlap_clip_bleed_px=4` | Draw and clip. Treat coordinate endpoint `-1` separately as raster convention. |
| `TwBar.c:6027,6038,6100+` contained margin `32`, click movement `6`, column grip `±5`; `TwMgr.c:2182` icon margin `8` | Input tolerance versus appearance | `offscreen_drag_recovery_margin_px=32`, `button_click_slop_px=6`, `column_resize_hit_half_width_px=5`, `default_icon_margin_x/y_px=8` | Hit/drag/icon placement. Click slop is interaction policy; it may sit in `TwStyleInteraction`, not visual geometry. Existing `iconmargin` remains runtime user setting. |
| `TwMgr.c:5932-5961,6036-6040,6201+` help indent via S, top margin `-sep`/`2`, clamp `H-3` | Help text padding and header offsets | `help_indent_space_glyphs` preserving `(level+1)` or `(level+2)` call-site formulas, `help_text_top_shift_px=-sep`, `help_header_top_inset_px=2`, `help_top_inset_limit_px=H-3` | Help generation, label draw. Replace encoded `m_LeftMargin` interpretation only after captures match. |
| `TwMgr.c:1322-1344`, `1347+` custom quaternion view with normalized sphere/arrow geometry; `TwFonts.c` font atlas | Widget-specific math and typography | Keep normalized 3D mesh constants in custom widget implementation; only pixel viewport inset and semantic colors enter style. `CTexFont` glyph widths remain font metrics. | Custom visualization; font generation is not theme spacing. |

### Colors and color ownership

`CTwBar_UpdateColors` (`TwBar.c:3725-3810`) already centralizes most per-bar colors: label/value states, group/help/shortcut/edit, title, buttons, hierarchy, and RotoSlider. Move the **resolved results**, not `m_Color` itself, into `TwStyleColors`. Preserve the existing HLS offsets (`l-0.05`, `-0.10`, `-0.15`, `-0.25`, `-0.30`, `-0.35`, `+0.10`), alpha formulas and dark/light alternatives exactly. The bar's `color` and `alpha` attributes remain per-bar inputs; widget client values and hover state remain runtime inputs. Color conversion functions in `TwColors.c` should stay simple math helpers.

| Current color source | Proposed role(s) | Consumers |
| --- | --- | --- |
| `m_ColBg/Bg1/Bg2`, `m_ColTitleBg/HighBg/UnactiveBg/Text/Shadow`, `m_ColHierBg`, `m_ColLine/LineShadow`, `m_ColUnderline` | bar/background/title/border roles in `TwStyleColors` | `UpdateColors`, `Draw`, `DrawHierHandle` |
| `m_ColLabelText`, `m_ColStructText`, `m_ColValBg/Text/TextRO/TextNE/Min/Max`, `m_ColGrpBg/Text`, `m_ColShortcutText/Bg`, `m_ColInfoText`, `m_ColHelpBg/Text`, `m_ColStaticText` | text, group and value roles | text builders, help and previews |
| `m_ColBtn/HighBtn/Fold/HighFold`, `m_ColRoto/Val/Min/Max`, `m_ColEditBg/Text/SelBg/SelText`, `m_ColSeparator` | widget roles | buttons, scrollbars, RotoSlider, editor, separator |
| `TwBar.c:5421-5422` `0x50ffffff/0x501f1f1f`, `5439` `0x30ffffff`, `5477` `0x5fffffff`, `5580-5590` `0xaf000000/0x7f000000`, `4217+` `0x11000000/0x4f000000`, `5389+` popup minimum alpha `0xa0` vs normal `0x70` | title sheen, border, button shadow, scroll tint, minimum focus alpha | Route into palette defaults without changing bit patterns; inspect all active non-client ARGB literals with `rg '0x[0-9A-Fa-f]{8}' src/TwBar.c src/TwMgr.c`. |
| `TwMgr.c:1058-1060` sphere gradient, `TwBar.c:5523-5535` swatch checker white/overlay, Roto contrast white | custom widget palette and decorative roles | Define only roles that a theme would plausibly change; keep vertex colors in custom-widget style, not global bar colors. |

Keep font atlas UVs, actual swatch color, current min/max value colors after state selection, mouse coordinates, popup measured content width, scroll offset/thumb position, row rectangles, and per-bar size/position **out of the style defaults**. They are font data, client/runtime state, or derived layout. A theme change invalidates affected bars and recalculates these once.

### Style migration acceptance gate

For each table row, record a search manifest of active source expressions before replacement; compare all three font sizes and font scaling 1x/2x, normal/help/popup bars, light/dark text, focused/unfocused and clipped states. Use pixel captures at fixed window size and deterministic sample values. Compare exact pixels first; investigate any difference rather than accepting a tolerance by default. Test hit regions at left/right/top/bottom edge pixels. After migration, search for remaining active layout and ARGB literals in `TwBar.c`, `TwMgr.c`, and custom widget paths; each remainder must be classified as runtime state, mathematical invariant, raster endpoint, or named style field. Do not hand-edit font assets or generated build output.

## 7. Performance review and measurement plan

`CTwBar_Update` is periodic (`m_UpdatePeriod=2` seconds by default at `TwBar.c:2929`) or invalidated; `CTwBar_Draw` and `TwDraw` run each frame. Avoid calling a refresh cost a per-frame cost. Existing text objects keep vertex capacity after rebuild; the Core renderer still uploads text/background arrays on each draw (`TwOpenGLCore.c:938-1030`). Rendering goes through one stable `ITwGraph` call table; that indirect call is a useful two-renderer boundary, not an optimization target.

| Path | Observed implementation | Priority and experiment |
| --- | --- | --- |
| Layout and traversal | `BrowseHierarchy` traverses the whole open tree even when only a few rows are visible; creates tags for visible rows. `ListLabels`, `ListValues`, width-fit each scan visible tags; custom records scan on draw. | **Likely measurable for large bars:** benchmark 100/1,000/10,000 collapsed/open variables with a fixed viewport; count traversal nodes and update microseconds. Consider cached subtree row counts or skip logic only if this is dominant and invalidation is explicit. |
| Text measurement and conversion | Every refresh formats visible values, sums glyph widths, builds `sds` lines; multiline subrows call `ValueToString`, `strcmp`, and wrapping cache. Popup opening scans options and up to 224 glyph widths once. | **Likely measurable for long multiline text:** convert once per atom per refresh and reuse buffers. Popup scan is one-time and probably fine. Benchmark long changing CDSTRING, many numeric rows, and idle refresh separately. |
| Draw | Draw traverses visible rows; Core renderer issues GPU uploads and many primitive draw calls. Overlap clipping can draw content repeatedly. | **Potentially measurable:** time CPU draw and count GL draw calls/uploads/clip rectangles with 1/10/100 overlapping bars. Profile before batching. Preserve renderer semantics and order. |
| String parsing/parameter lookup | `ParseToken`, `TwDefine`, `TwSetParam/GetParam` do `sds` allocations and linear string comparisons; `AddVar` invokes `TwDefine`. | **Inexpensive structural gain, modest runtime effect:** share typed bool/number parsing and attribute metadata for maintenance. These are setup/configuration paths, so avoid a hash table or runtime registry for speed. |
| Event dispatch and hit testing | `TwMouseEvent` scans bars/order; `MouseMotion` resets focus flags across bars and checks row/scroll/button coordinates; multiline scrollbar scans visible blocks. | **Inexpensive structural gain:** use stored row and widget rectangles to eliminate rederivation and drift. Profile pointer motion on large bars before indexing rows beyond direct Y division. |
| Allocation and temporary buffers | Update creates/frees label/value arrays and strings; `TwDraw` creates/free clip arrays each frame; static `sds` scratch persists; renderer vector arrays retain capacity. | **Credible gains:** reuse temporary capacities per bar/manager; keep allocation failure semantics correct. Count alloc/realloc bytes and calls before selecting work. |
| Repeated state/conversions | `CTwBar_UpdateColors` recalculates HLS on every update; `ListValues` gets string and double/min-max for atoms; Roto move repeats min/max/step queries. | **Measure if active:** cache style colors on tint/mode change and hoist stable Roto values during a drag, while accounting for client callbacks that can change bounds. Do not cache live values without a clear invalidation contract. |
| Low-value micro-optimizations | Sorted enum lookup is linear, `DrawArc` uses trig while actively dragging, `ITwGraph` dispatch is indirect, `CTwBar_CustomMap_Find` scans a handful of proxies. | Leave as-is unless a benchmark shows a real cost. A dispatch registry, hash map, or generalized scene graph would add complexity and likely more indirection. |

Instrumentation should be compile-time optional through `nob.c` flags or existing `PERF` hooks (`TwMgr.h:78+`, `TwBar.c:5362`), with zero default overhead. Record update/draw/event time distributions, allocations, visible/total row counts, GL calls, and clip rectangles. Keep a repeatable benchmark scene in an example or test harness and use the same compiler flags, window size, font, and bar contents before and after each optimization. Profile macOS first, then validate functional behavior on Linux and MinGW when available; do not infer their GPU timing from macOS.

## 8. Staged implementation roadmap

Every stage is a separate focused patch; after each, run `./nob`, `./nob -examples-glfw` (plus SDL/SFML where available), the new headless checks, focused manual smoke tests, and diff review. Future implementation must start from the then-current tree and reconcile this draft with any new commits or local edits.

### Stage 0: Baseline, safety and regression fixtures

Status: In progress. Headless API/input checks and drawing-command fixtures are implemented and passing; real OpenGL screenshots, remaining platform checks and performance measurements are pending. Add deterministic API checks for defaults and `TwDefine`/`TwSetParam`/`TwGetParam` round trips (`full_width`, alignment, `lines`, enum, button, group, bar). Add layout/hit probes using an instrumented `ITwGraph` or stable internal layout assertions and screenshot fixtures for GLFW normal/help/popup/Roto/multiline at small/normal/large fonts and 1x/2x scaling. Cover nested groups, full-width with negative bar X, clipped blocks, long labels and popup options. Record baseline build warnings and timings. Gate: baseline green on available platforms, captures saved with provenance.

### Stage 1: Targeted naming and ownership cleanup

Status: Pending. Rename only internal symbols whose role is misunderstood in touched functions; first clarify row/column/scroll coordinate names and cache ownership. Preserve the opaque `CTwBar` tag and all exported names/parameter strings. Update active comments and the relevant documentation. Gate: no behavior/pixel changes; compile and tests identical.

### Stage 2: Internal style and color context

Status: Pending. Add internal `TwStyleGeometry` and `TwStyleColors`, default initialization with the exact formulas in §6, and resolution on bar/font/tint changes. Migrate one category at a time: bar/content/title, row/text, buttons/scrollbars, popup/multiline/Roto, then color literals. Change draw and hit consumers in the same small step for each metric. Keep existing bar overrides. Gate: exact pixel and edge hit comparisons; audit every §6 row and remaining active literal.

### Stage 3: Derived layout and row model

Status: Pending. Build a per-bar `TwBarLayout` and compact `TwRowLayout` for visible rows only, containing consistent half-open screen-space bounds and text origins. Derive popup width and placement from these bounds; share scroll/thumb geometry. Preserve existing integer rounding and clipped multiline behavior. Move cohesive layout helpers to a file only when dependencies are simple; register it in `nob.c`. Gate: layout probes and full visual matrix match baseline.

### Stage 4: Widget and property extension points

Status: Pending. Keep static explicit C dispatch. Factor row count, display text, supported attributes, interaction eligibility, and invalidation into small widget-class helpers; separate base, atom, group, and built-in custom ownership. Introduce typed property metadata only for common parsing/lookup/get semantics, with special behavior in named functions. Use a small new widget/property as a proof exercise and count phase sites; do not ship speculative public API. Gate: legacy attribute strings, error values and callbacks unchanged; extension touches only its owner plus intentional render/input functions.

### Stage 5: Rendering and input cleanup where shared geometry helps

Status: Pending. Have draw and hit paths consume Stage 3 rectangles; isolate Roto and edit overlay state transitions without creating a broad widget vtable. Consolidate duplicated scrollbar painting/geometry only if it keeps current outer versus multiline differences explicit. Gate: pointer/keyboard/wheel, popup deletion during callback, and custom viewport tests.

### Stage 6: Reliability and measured performance work

Status: Pending. Fix checked allocations/rollback and popup enum payload lifetime. Then use §7 profiles to select buffer reuse, multiline conversion, layout traversal, overlap clipping, or Core upload work. Benchmark before/after and revert any change that complicates code without a reproducible gain. Gate: failure injection for reserves, sanitizer runs where supported, no meaningful regression in median and tail times, all behavior checks green.

### Stage 7: Future public theme API, only after internal style stabilizes

Status: Pending. Decide whether manager-wide themes with optional bar overrides meet actual users' needs. Add public API in a separate compatibility-reviewed task; specify ownership, font-scaling units, invalidation and serialization expectations. Do not expose internal row geometry or a large public struct prematurely. Gate: an alternate theme works across normal/help/popup bars and all three example backends without changing default captures.

## 9. Risks, validation limits, and deliberate non-changes

The highest migration risk is visual drift from integer rounding, inclusive GL rectangle endpoints, font scaling, and bar/help/popup special cases. A second risk is stale client data: callbacks can change or destroy manager state while an update or event is in progress (`TwBar.c:1590+,6588+`), so caching must obey those lifetimes. A third is platform variation in font/raster/GL behavior; exact captures should be compared within the same platform, not across unrelated GPUs. No automated test suite or benchmark currently ships, and this review did not execute GUI examples or collect measurements.

Leave `TwColors.c`'s conversion algorithms, `TwFonts.c`'s source bitmap tables, the two renderer implementations behind `ITwGraph`, the sorted enum array, the small custom proxy array, and the flat public API in place unless targeted evidence demonstrates a problem. Keep `nob.c` as the only build entry point. Do not add a widget registration framework, per-widget heap allocations, a theme parser, or a scene graph solely to make the design look extensible.

## 10. Review coverage and handoff

Read commit summaries/diffs for the C99 migration, RotoSlider, multiline/widget editor, `full_width`, popup fitting, alignment and backend/build evolution named in §2; inspected the public header, all core source/header modules and their build registration, representative GLFW/SDL/SFML examples and example inventory, `README.md`, `PLANS.md`, `docs/TASK*.md`, C99 and multiline plans, backend plans, cursor notes, and historical migration review. `docs/TASK4.md` is a proposed wiki plan, not an existing wiki. Excluded line-by-line review of vendored dependency internals and 39 near-parallel example programs; neither is an appropriate place for the proposed core refactoring. The current working-tree changes listed in §1 were left untouched.

Baseline validation on macOS: `./nob` succeeded, building the static and shared C99 libraries without warnings; `./nob -examples-glfw` succeeded, building 13 static examples without warnings. GUI examples were compiled but not launched. SDL/SFML examples and Linux/MinGW builds were not run. A whitespace check of the new Markdown file produced no diagnostics. Only this review file was added by the review; existing uncommitted files remain as found.

Before implementation, reread `AGENTS.md`, `PLANS.md`, this plan, `git status`, and relevant current source/history. Update this plan with each stage's actual files, decisions, test commands/results, measured performance, and remaining risks. Mark it Completed only when the future refactoring and validation are actually finished.

## 11. Implementation progress

### 2026-09-16: Stage 0, headless baseline

- Added `tests/regression.c`, `tests/record_graph.c/.h`, the generated
  `tests/widget-layout.txt` fixture, and `tests/README.md`. The recording renderer
  implements the existing factories in a separate test executable; production
  `src/` and `include/` remain unchanged.
- Added `./nob -test` (build and compare) and `./nob -test-record` (explicit
  fixture generation). Reused core source registration, compiler flags and
  dependency checks. Outputs live under `build/tests/`, included in `-clean`.
  Added test instructions to `README.md`, preserving existing local edits.
- API tests cover defaults, typed set/get and definition-string round trips,
  invalid values without state corruption, removal and termination. Input checks
  cover checkbox/button actions, enum selection and popup destruction, row/bar
  edges, nested multiline scrolling, RotoSlider activation/release, negative-X
  title dragging, and multiline keyboard navigation/cancellation.
- Recorded 48 drawing scenes: eight states across three fonts and two scales,
  including expanded help, popup, clipped content and dark text. Fixtures retain
  integer geometry, colors, text and clipping commands, without pointer values
  or timestamps. Font selection precedes explicit bar sizing because changing
  the font also rescales existing bar geometry. Hovering a numeric row and
  drawing it precedes pressing its RotoSlider button, as required by current
  action-button invalidation.
- Validation on macOS arm64, Apple clang 21.0.0: `./nob -test-record` succeeded;
  repeated `./nob -test` runs passed; temporarily changing the scene's red color
  component from 70 to 71 made comparison fail at line 6 with exit status 1;
  restoring the scene made it pass. `./nob` and `./nob -examples-glfw` succeeded
  without compiler warnings. Reviewed the fixture, diffs and whitespace.
- Remaining Stage 0 work: real OpenGL pixel captures, custom-widget coverage,
  callback reentrancy, platform/backend smoke checks, and measured performance
  baselines. Drawing-command equality does not establish GPU pixel equality.
  No stage beyond the headless baseline has been implemented; no commit or push
  was performed. A local execution record also lives at
  `docs/plans/refactoring-regression-baseline.md` (under the existing ignored
  `docs/` directory).
