# UI-UX — Issues

**Project:** Interi (repository root)
**Analyzed:** 2026-10-19
**Scope:** In-game HUD and menus (`engine/hud/**`, 5 sources), input binding and rebinding (`engine/platform/include/interi/platform/InputConfig.h`, `engine/platform/src/InputConfig.cpp`), localization (`engine/loc/include/interi/loc/Localization.h`), the Dear ImGui editor (`engine/editor_ui/src/*.cpp`, 13 files, and `tools/editor/EditorApp.cpp`, 1652 lines), editor preferences (`engine/editor/include/interi/editor/EditorServices.h`), the starter sample's HUD, menus and input config (`samples/modules/starter/templates/coins/content/{ui/*.hud.json,input.json}`) and the tests that drive them (`tests/hud/*`, `tests/samples/StarterSampleTests.cpp`). This is static reading only. Nothing was run or looked at on screen.
**Summary:** 19 issues — Critical: 0 · High: 3 · Medium: 10 · Low: 4 · Info: 2

## Findings

### UX-001 — Menus have no input layer: nothing is bound to navigate, confirm or go back, and nothing outside the tests drives MenuStack
- **Severity:** High
- **Confidence:** High
- **Category:** Input parity / gamepad navigation
- **Location:** `engine/hud/include/interi/hud/HudController.h:39-43`, `engine/hud/include/interi/hud/MenuStack.h:4-10`, `engine/hud/src/MenuStack.cpp:28-34` (MenuStack::Pop), `samples/modules/starter/templates/coins/content/input.json:3-9`, `tests/samples/StarterSampleTests.cpp:315-335`
- **Evidence:** `HudController` provides `FocusNext/FocusPrevious/Activate`, but neither it nor `MenuStack` maps any input. GitNexus `cypher` finds no caller of `HudController` or `MenuStack` methods outside `tests/hud/*` and `tests/samples/StarterSampleTests.cpp` (`context` reports 0 incoming references for both classes). The only non-HUD files that include `interi/hud` are the editor's HUD designer and `EditorApp`. No `ui.*` or `menu.back` actions exist anywhere. The starter's `input.json` binds `"pause": ["Escape", "Gamepad.Start"]` but has no action for menu up/down, accept (`Gamepad.South`/Enter) or back (`Gamepad.East`/Escape). The D-pad is bound only to `move*`. Nothing handles cancel/back: a menu goes back only when a document button raises `menu.pop`. `MenuStack::Pop` on the root menu empties the stack, which the header documents as "the game resumes", even when the root is the main menu and no game is running.
- **Impact:** Each game has to invent its own navigation layer and button conventions. The template every project starts from opens a pause menu from the gamepad but gives the player no way to move focus, confirm or back out with the same device. A controller-only player hits a dead end, which blocks console certification. Play-in-editor never shows menus, so the editor can't catch this either.

### UX-002 — The editor throws away unsaved work on Quit, New, Open project and chain or HUD New/Load without asking
- **Severity:** High
- **Confidence:** High
- **Category:** Flow / destructive actions / data loss
- **Location:** `tools/editor/EditorApp.cpp:1494-1496` (window close → `result.quit`), `tools/editor/EditorApp.cpp:1626` (File > Quit), `tools/editor/EditorApp.cpp:1609-1611` (`m_document.New()`), `tools/editor/EditorApp.cpp:602-605,672-676` (`OpenProjectFolder` stops play "keeping nothing" and calls `OpenLevel` over the current document), `tools/editor/EditorApp.cpp:1126-1131` (chain "New" clears asset and undo), `tools/editor/EditorApp.cpp:1088-1097` (chain Load), `tools/editor/EditorApp.cpp:1442-1449` (HUD Load), `engine/editor_ui/src/EditorPanels.cpp:100-103`
- **Evidence:** None of these paths checks `m_document.IsDirty()`, `m_chain.undo.IsDirty()` or `m_hud.undo.IsDirty()`. The dirty flags only feed a title `*` (`EditorPanels.cpp:127`, `EditorApp.cpp:1145`) and the autosave timer (`EditorApp.cpp:526`). The HUD file bar shows no dirty marker at all. Chain "New" runs `m_chain.asset = {}; m_chain.undo.Clear();` immediately, and the undo history goes with it.
- **Impact:** A single click or closing the window silently loses scene, chain, HUD and project-data edits. Autosave covers only the scene document (`<scene>.autosave`), not chains, HUD documents or project data.

### UX-003 — RebindPrompt always replaces binding slot 0, so rebinding on one device removes the other device's binding, and a gamepad user can't cancel
- **Severity:** High
- **Confidence:** High
- **Category:** Input parity / remapping
- **Location:** `engine/platform/include/interi/platform/InputConfig.h:59-95`, `engine/platform/src/InputConfig.cpp:409-460` (RebindAction), `engine/platform/src/InputConfig.cpp:478-499` (RebindPrompt::Update), `samples/modules/starter/templates/coins/content/input.json:4-5`
- **Evidence:** `BeginFromAction(menuAction, index = 0)` targets binding index 0 whatever the device. In the starter, `moveRight` is `["D", "Right", "Gamepad.DpadRight"]`. A player who rebinds with a gamepad button replaces `D`, leaving keyboard players with only the arrow key. A keyboard rebind never touches the gamepad slot. `Update` treats only `"Escape"` as cancel (`if (*pressed == "Escape")`), so a gamepad press always rebinds and a controller player can't back out. `RebindAction` has no conflict check (a key already bound to `pause` or to another move can be bound twice), and there is no rebind path for axes (only `DigitalInput`). The prompt also depends on event order: if the same frame's events are fed after arming, the confirm key that activated the button can be captured as the new binding (inferred from the "before or after InputMapper::Update" comment).
- **Impact:** Remapping, which is an accessibility requirement, can quietly break the other input device. A controller-only player who starts a rebind is forced to bind something. Duplicate bindings let one key trigger two actions.

### UX-004 — HUD focus indicator is weak, looks the same as hover, and menus open with nothing focused
- **Severity:** Medium
- **Confidence:** High
- **Category:** Accessibility (focus visible) / gamepad navigation
- **Location:** `engine/hud/src/HudLayout.cpp:226-239`, `engine/hud/src/HudController.cpp:18-31` (Update), `engine/hud/src/HudController.cpp:105-115` (Activate), `engine/hud/src/MenuStack.cpp:17-26` (Push)
- **Evidence:** The only cue for focus or hover is adding 0.2 to the button background: default linear (0.2, 0.2, 0.25) becomes (0.4, 0.4, 0.45), which is about 1.8:1 against the unfocused state, below WCAG 2.2 non-text contrast (3:1). No outline is drawn. A background at or above 0.8 shows almost no change. Hover and focus get identical styling, and `PointerMove` also moves focus. `Push` creates a controller with an empty `focused`, so `Activate()` does nothing until the player presses a direction (`Activate_WithoutFocus_DoesNothing` test). When the focused button is hidden by `visibleIf`, `Update` clears focus rather than moving it to a neighbour.
- **Impact:** Couch and low-vision players can't see where focus is, and the first confirm press on every new menu is swallowed.

### UX-005 — HUD text has no size in the data model and doesn't scale with the layout
- **Severity:** Medium
- **Confidence:** High
- **Category:** Accessibility (text size) / couch UX
- **Location:** `engine/hud/include/interi/hud/HudDocument.h:59-76`, `engine/hud/include/interi/hud/HudLayout.h:145-154`, `engine/hud/src/HudLayout.cpp:164`, `engine/editor_ui/src/HudDesigner.cpp:103-111`
- **Evidence:** `HudWidget` and `DrawItem` carry no font size or text scale. `BuildFrame` scales rectangles by `min(width/refW, height/refH)`, but `DrawHudFrame` draws text with `ImGui::CalcTextSize` and `AddText` at the editor's fixed font size, then clips to the rectangle (`PushClipRect`). At 4K the boxes are three times larger and the text is not. In a small preview, text overflows and is cut off without any warning. The format has no minimum text size and no player text-scale setting.
- **Impact:** Text that can't be read from the couch is a common certification and accessibility failure. The designer's preview doesn't match what a game renderer would do, and localized strings that run long are truncated silently.

### UX-006 — HUD layout has no safe-area (title-safe) support
- **Severity:** Medium
- **Confidence:** High
- **Category:** Console / TV UX
- **Location:** `engine/hud/src/HudLayout.cpp:54-64` (Place), `samples/modules/starter/templates/coins/content/ui/hud.hud.json:6-9`
- **Evidence:** Anchors resolve to the raw screen edges (`ax = 0` / `width - w`), and neither `BuildFrame` nor `HudDocument` has a safe-area inset. The starter HUD places the coins and score at a 16 px offset on a 1280-wide reference (1.25%), well inside a typical 5% title-safe margin.
- **Impact:** HUD elements can be cut off on TVs with overscan, and the engine can't honour a platform-reported safe area.

### UX-007 — HUD formatting and the starter's UI are not ready for localization
- **Severity:** Medium
- **Confidence:** High
- **Category:** Localization readiness
- **Location:** `engine/hud/src/HudLayout.cpp:68-100` (DisplayText), `engine/hud/src/HudLayout.cpp:143-149`, `engine/editor_ui/src/HudDesigner.cpp:206-213,226`, `samples/modules/starter/templates/coins/content/ui/*.hud.json`
- **Evidence:** `DisplayText` returns English literals `"true"`/`"false"` and `"{} items"`, and an unknown Value prints `"?"`. Numbers use `std::format("{}")` / `"{:.{}f}"`, which always uses `.` as the decimal separator and no grouping. Every starter string is a hard-coded English literal ("Play", "Paused", "Rebind: move right", "You collected every coin!", "Coins {…}/{…}"), with no `{@key}` and no `.strings.json`, even though `engine/loc` exists. The HUD designer previews with `BuildFrame(document, width, height, values)` and no `Localization`, so `{@key}` shows as raw keys. It warns about unknown Values but not about missing string keys.
- **Impact:** Projects that start from the template copy non-localizable UI. Booleans, list counts and decimals can't be translated or formatted per locale, and translators can't preview their text in context.

### UX-008 — The editor uses ImGui's default bitmap font, has no UI scale setting and leaves keyboard navigation off
- **Severity:** Medium
- **Confidence:** Medium
- **Category:** Accessibility (desktop tool) / HiDPI
- **Location:** `tools/editor/EditorApp.cpp:149-154`, `engine/editor/include/interi/editor/EditorServices.h:24-37`, `engine/platform_sdl/src/SdlPlatform.cpp:341`
- **Evidence:** ImGui setup is `ConfigFlags |= DockingEnable; StyleColorsDark();`. No `AddFont*`, no `FontGlobalScale`/`ScaleAllSizes`, no `DisplayFramebufferScale`, and no `ImGuiConfigFlags_NavEnableKeyboard` (grep finds none in `engine/editor_ui` or `tools/editor`). The window is created with `SDL_WINDOW_HIGH_PIXEL_DENSITY`, but the editor doesn't read the content scale. `Preferences` has no font size, UI scale or theme.
- **Impact:** On HiDPI or 4K monitors the text is a fixed 13 px. Asset, Value or node names outside ImGui's default glyph range (Latin-1) show as `?`. Keyboard-only users can't move between panels and widgets (WCAG 2.1.1). Whether rendering is actually blurry on a scaled display needs a human visual check.

### UX-009 — Scene shortcuts are global and fire while the chain editor has focus
- **Severity:** Medium
- **Confidence:** High
- **Category:** Consistency / input handling
- **Location:** `engine/editor_ui/src/EditorPanels.cpp:134-167` (HandleShortcuts), `engine/editor_ui/src/ChainEditor.cpp:507-548`, `tools/editor/EditorApp.cpp:1513`
- **Evidence:** `HandleShortcuts` checks only `WantTextInput`, not which window has focus. With the chain canvas focused, pressing Delete removes the selected chain nodes (`ChainEditor.cpp:509`) and also the scene's selected objects (`EditorPanels.cpp:154-157`). Ctrl+G groups both. Ctrl+Z/Ctrl+Y undo and redo the scene's stack, never `m_chain.undo`. The chain editor has no undo shortcut, and the HUD designer has only buttons (`EditorApp.cpp:1468-1476`).
- **Impact:** Edits land somewhere the user isn't looking (objects vanish from the scene while they work in a chain), and undo doesn't do what users expect.

### UX-010 — The chain canvas and debugger use colour alone for status
- **Severity:** Medium
- **Confidence:** High
- **Category:** Accessibility (colour-blindness)
- **Location:** `engine/editor_ui/src/ChainEditor.cpp:23-32`, `engine/editor_ui/src/ChainEditor.cpp:594-606`, `engine/editor_ui/src/ChainEditor.cpp:666-705`
- **Evidence:** Debugger link heat is `kHotColor` (yellow) when a link passed and `kErrorColor` (red) when it failed. A node border is red for an error and orange for a warning. A link with diagnostics simply turns red. Trigger and action node headers differ only by green versus blue. None of these has a shape, icon or label. The Problems tab mitigates it for diagnostics, but not for the debugger's passed/failed heat.
- **Impact:** Deuteranopes and protanopes can't tell passed from failed links or errors from warnings on the canvas.

### UX-011 — Editor errors go only to the console log and leave state half-applied
- **Severity:** Medium
- **Confidence:** High
- **Category:** Error states / feedback
- **Location:** `tools/editor/EditorApp.cpp:602-676` (OpenProjectFolder), `tools/editor/EditorApp.cpp:1615-1625` (Save), `tools/editor/EditorApp.cpp:1601-1605` (open level)
- **Evidence:** `EditorApp.cpp` has 45 `INTERI_LOG_ERROR`/`INTERI_LOG_WARN` calls. A failed scene save, a project that won't load, a failed startup scene and failed imports are reported only through the logger and the Console panel. No modal, toast or status-bar message appears. `OpenProjectFolder` stops play and swaps registries, file mounts and the asset database before it tries `OpenLevel`. If the scene fails, the project switches but the previous level stays open, and only a log line explains it.
- **Impact:** Users miss failed saves (a data-loss risk) and can't tell why a project looks half-loaded.

### UX-012 — Opening a project imports every asset synchronously on the UI thread, with no progress shown
- **Severity:** Medium
- **Confidence:** Medium
- **Category:** Loading states
- **Location:** `tools/editor/EditorApp.cpp:569-599` (OpenAssetDatabase: `Scan()` then `CookAll()`), called from `tools/editor/EditorApp.cpp:167` (startup) and `tools/editor/EditorApp.cpp:668` (project open)
- **Evidence:** `CookAll()` runs inside the frame that handles the Project menu request (and during start-up). It imports glTF, FBX, OBJ and textures and runs BC encoding. There is no job, no progress bar and no busy indicator, and a summary only reaches the log afterwards.
- **Impact:** The window freezes, possibly long enough for the OS to report it as not responding, on a first open or after large asset changes. Measuring how long needs a run with a real project.

### UX-013 — The starter pause menu saves, loads and quits with no confirmation or feedback
- **Severity:** Medium
- **Confidence:** High
- **Category:** Flow / destructive actions / save indicator
- **Location:** `samples/modules/starter/templates/coins/content/ui/pause.hud.json:7-10`, `samples/modules/starter/templates/coins/content/ui/main_menu.hud.json:8`
- **Evidence:** `game.save:0` overwrites slot 0, `game.load:0` replaces current progress and `app.quit` exits. All three fire from a single press, and the HUD format has no confirmation dialog or save-indicator pattern. There is one fixed slot and no slot list or timestamp. `app.quit` is offered unconditionally, but console platforms don't let titles quit themselves.
- **Impact:** The reference project teaches patterns that lose player progress and that console certification rejects.

### UX-014 — The starter settings menu covers only two actions and doesn't show current bindings
- **Severity:** Low
- **Confidence:** High
- **Category:** Remapping / feedback
- **Location:** `samples/modules/starter/templates/coins/content/ui/settings.hud.json:6-7`, `samples/modules/starter/templates/coins/content/input.json:1-9`
- **Evidence:** Only `moveRight` and `moveLeft` have rebind buttons, so `moveUp`, `moveDown` and `pause` can't be remapped. The labels are static ("Rebind: move right"), so they show neither the current key nor a "press a key…" state while the prompt is armed. `deadZone` is set but no analog axis (`Gamepad.LeftX`) is bound, so the left stick does nothing. The menu has no options for dead zone, sensitivity or hold-versus-toggle.
- **Impact:** Remapping is incomplete in the reference project, and players get no feedback during a rebind.

### UX-015 — Chain canvas text doesn't scale with zoom, and panning needs a middle mouse button
- **Severity:** Low
- **Confidence:** High
- **Category:** Editor usability / motor accessibility
- **Location:** `engine/editor_ui/src/ChainEditor.cpp:324-337`, `engine/editor_ui/src/ChainEditor.cpp:674-698`
- **Evidence:** Node boxes scale with `state.zoom` (0.25–2.5), but `AddText` uses the default font size, so names are clipped when zoomed out and look tiny when zoomed in (the `Fit` truncation hides most of each label). Panning is only `IsMouseDragging(ImGuiMouseButton_Middle)`. There is no Space+drag, Alt+drag or keyboard pan, so trackpad users depend on the minimap.
- **Impact:** The canvas is hard to use on laptops and for users who can't use a middle button.

### UX-016 — Chain and HUD files are opened by typing a path, and the HUD designer shows no unsaved state
- **Severity:** Low
- **Confidence:** High
- **Category:** Discoverability / feedback
- **Location:** `tools/editor/EditorApp.cpp:1118-1146` (Chain File window), `tools/editor/EditorApp.cpp:1439-1478` (HUD File window)
- **Evidence:** Both windows use `ImGui::InputText("path", …)` plus Load/Save buttons. Neither has a browse or picker button, even though the asset browser already lists files. The chain window shows `*` when dirty. The HUD window has no dirty indicator.
- **Impact:** Users have to remember exact virtual paths, typos produce load errors, and unsaved HUD edits are invisible.

### UX-017 — HUD bar range can't bind Values, so the starter progress bar hard-codes the coin count
- **Severity:** Low
- **Confidence:** High
- **Category:** HUD data model
- **Location:** `engine/hud/include/interi/hud/HudDocument.h:71-72`, `samples/modules/starter/templates/coins/content/ui/hud.hud.json:10-11`
- **Evidence:** `min`/`max` are literal floats. The starter bar uses `"max": 5` next to the Value `com.interi.starter:total` (default 5, `samples/modules/starter/module.json:18`), which the text widget already reads.
- **Impact:** A level with a different coin count shows a wrong progress bar. This is a small consistency gap, but authors will copy it.

### UX-018 — HudController exposes no focus or activate events for audio and haptic feedback
- **Severity:** Info
- **Confidence:** Medium
- **Category:** Feedback
- **Location:** `engine/hud/include/interi/hud/HudController.h:45-49`
- **Evidence:** Only `TakeActions()` (activations) is observable. A game can poll `Focused()` to detect changes, but there's no event for focus moved, press started or invalid move.
- **Impact:** Informational only. Games have to diff the state themselves to play UI sounds or rumble.

### UX-019 — Localization has no plural or gender selection
- **Severity:** Info
- **Confidence:** High
- **Category:** Localization readiness
- **Location:** `engine/loc/include/interi/loc/Localization.h:66-75`
- **Evidence:** `Translate`/`Format` use named `{name}` placeholders, which allow reordering. That part is good. There is no plural-category or select mechanism, so strings like "1 coins" can't be correct in every language.
- **Impact:** Informational. It matters once shipped text includes counts.

## GitNexus exploration log
**Access:** mcp · **Index:** refreshed (@4631f35 = HEAD)

| # | Tool | Argument | Result (one line) |
|---|---|---|---|
| 1 | list_repos | — | Interi: 37,920 files, 365,792 nodes, 614 processes, indexed @ 4631f35 = HEAD |
| 2 | cypher | Community by symbolCount LIMIT 30 | Top 30 are all third-party (SDL Joystick/Hidapi, Tracy, Manifold, ImGui docking); no first-party UI cluster in the top 30 |
| 3 | cypher | `MATCH (p:Process) RETURN p LIMIT 30` | 30 processes, all SDL3 examples or backends (SDL_AppIterate, X11/KMSDRM_CreateWindow); none first-party |
| 4 | query | "HUD menu focus gamepad navigation MenuStack HudController" | MenuStack, HudController (FocusNext), MenuStack.cpp Push/Update, HudEditing AddHudWidget, HudDesigner DrawWidgetFields; 0 processes |
| 5 | query | "input binding rebind prompt InputMapper" | RebindPrompt, CaptureInputConfig, RebindAction, InputMapper::ActionBindings (+ SDL gamepad noise) |
| 6 | query | "editor settings preferences panel" | EditorApp (m_preferences, UpdateServices), EditorExtensions DrawMenu/DrawPanels/SetPanelVisible |
| 7 | context | HudController (Class) | 18 methods, 7 properties; 0 incoming refs; used as the MenuStack::Entry::controller type; no processes |
| 8 | context | RebindPrompt (Class) | 6 methods (Begin, BeginFromAction, Update, Cancel…); 0 incoming refs |
| 9 | context | MenuStack (Class) | 11 methods; 0 incoming refs; no processes |
| 10 | cypher | CALLS into engine/hud/* or RebindAction/FirstPressedInput from outside engine/hud | Callers only in tests/hud/*, tests/platform/InputConfigTests.cpp, and RebindPrompt::Update itself (+2 false SDL Haiku matches); no runtime consumer |
| 11 | cypher | File list under engine/hud, engine/editor_ui, tools/editor, engine/loc | 50 files; defined the reading scope |

## Analysis limitations
- I could not see the running UI. Contrast ratios are computed from the colour constants in code (HUD colours are linear RGBA). Text legibility, HiDPI blur (UX-008) and freeze length (UX-012) need a human visual check or a run.
- No builds, tests or executables were run (read-only rules), so `level_editor` behaviour is inferred from the code.
- The BM25 index is broken (queries took about 55 s with weak keyword ranking), and the clusters and processes are dominated by third-party code. Discovery relied on `context`, targeted `cypher` and reading files.
- `modules/` (Destruction editor extension, physics) is not indexed or tracked and was not reviewed, so their panels may have their own UX issues.
- Console certification themes are described generically (no NDA requirement IDs).

## Cross-discipline notes
- Unit-Tests: `tests/hud/HudControllerTests.cpp:83` is named `FocusPrevious_FromNothing_SelectsTheFirst` but asserts that it selects the last visible button ("b").
- Architecture: `EditorApp.cpp` (1652 lines) owns the chain, HUD and project file flows inline, which makes shared dirty and confirmation handling (UX-002) harder to add.
- Performance: `OpenAssetDatabase` runs `CookAll()` synchronously on the UI thread (`tools/editor/EditorApp.cpp:584`).
