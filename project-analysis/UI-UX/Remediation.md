# UI-UX — Remediation

How to move from the issues in Issues.md to the end state in Goal.md.
Nothing here has been applied. These are instructions, not changes.

## Recommended order
1. UX-002, UX-011: stop the editor from losing work and from hiding failures. Small, self-contained changes in `tools/editor/EditorApp.cpp`.
2. UX-001, UX-003, UX-004: give menus a complete input path on every device (navigation actions, back, initial focus, a visible focus indicator, per-device rebinding). UX-004 and UX-003 build on UX-001's UI actions.
3. UX-005, UX-006, UX-007: make the HUD format ready for TV, couch and localization (text size, safe area, locale-aware formatting). These are format additions, so do them together and bump `kHudVersion` once.
4. UX-013, UX-014, UX-017: update the starter template so it demonstrates the new patterns. It depends on 2 and 3.
5. UX-008, UX-009, UX-010, UX-012: editor accessibility and consistency.
6. UX-015, UX-016: polish.
(UX-018 and UX-019 are Info and need no action. Revisit them when a game needs UI audio or plural text.)

## UX-001 — Menus have no input layer
- **Severity / Effort:** High / M
- **Depends on:** None
- **Steps:**
  1. In `engine/hud`, add a small `MenuInput` adapter (or `MenuStack::HandleInput`) that takes `InputMapper` action states for a fixed set of UI actions: `ui.up`, `ui.down`, `ui.left`, `ui.right`, `ui.accept`, `ui.back`. It calls `FocusPrevious/FocusNext/Activate` on `Current()`, and `ui.back` performs `menu.pop`.
  2. Ship default bindings for these actions as engine data: arrows/WASD, Enter/Space, Escape; D-pad and left stick, `Gamepad.South` (accept), `Gamepad.East` (back). Load them with `ApplyInputConfig(..., InputMerge::Add)` so a project's `input.json` can rebind them with `Replace`.
  3. Let a document mark itself as the root (for example `"root": true`, or a `MenuStack::SetRoot(name)`). `ui.back` on the root raises a game action such as `menu.backAtRoot` instead of emptying the stack. Keep today's behaviour for pause menus.
  4. Add `ui.*` bindings to `samples/modules/starter/templates/coins/content/input.json` and drive `MenuStack` through the adapter in `StarterSampleTests`, using gamepad events only.
- **Illustrative change (not applied):**
  ```cpp
  // engine/hud/include/interi/hud/MenuInput.h
  struct MenuInputActions { InputMapper::ActionId up, down, accept, back; };
  void ApplyMenuInput(MenuStack& menus, const InputMapper& mapper, const MenuInputActions& ids)
  {
      HudController* top = menus.Current();
      if (top == nullptr) return;
      if (mapper.WasPressed(ids.down)) top->FocusNext();
      if (mapper.WasPressed(ids.up)) top->FocusPrevious();
      if (mapper.WasPressed(ids.accept)) top->Activate();
      if (mapper.WasPressed(ids.back)) menus.Back(); // pops, or reports "at root"
  }
  ```
- **Verification:** A new test `MenuInput.GamepadOnly_NavigatesPauseSettingsAndBack` feeds only `GamepadButtonEvent`s and reaches `game.resume`. `StarterSample.Menus_GamepadOnly` passes. A `cypher` CALLS query shows a non-test caller of `MenuStack`.
- **Risks / trade-offs:** Fixed action names become a public contract, so document them in `CLAUDE.md`/docs. The Nintendo-style A/B swap can be added later as a regional setting (Goal, out of scope for now).

## UX-002 — The editor discards unsaved work without asking
- **Severity / Effort:** High / M
- **Depends on:** None
- **Steps:**
  1. Add `bool EditorApp::HasUnsavedWork()` covering `m_document.IsDirty()`, `m_chain.undo.IsDirty()`, `m_hud.undo.IsDirty()`, the material editor's undo and the project-data edits.
  2. Route Quit (window `QuitEvent` and File > Quit), File > New, Open project, Create project, chain New/Load and HUD Load through a pending-action plus a "Save / Discard / Cancel" modal. Execute the deferred action only after the user chooses.
  3. Show a `*` dirty marker on the HUD File window, matching the chain window.
  4. Extend autosave to chain and HUD documents, or at least save `.autosave` copies of them.
- **Illustrative change (not applied):**
  ```cpp
  if (requests.quit || quitEvent)
  {
      if (HasUnsavedWork()) { m_pending = PendingAction::Quit; ImGui::OpenPopup("Unsaved changes"); }
      else result.quit = true;
  }
  ```
- **Verification:** Headless `EditorApp` tests (or a `ConfirmUnsaved` model in `engine/editor`, tested like `ModuleManagerState::Queue`) show that a dirty document plus Quit/New/Open keeps the document until the user confirms. Manual check: close the window with edits and see the prompt.
- **Risks / trade-offs:** Headless `--frames N` smoke runs must not block, so skip the prompt when there is no interactive window or add a `--discard-unsaved` flag.

## UX-003 — RebindPrompt clobbers the other device's binding and only Escape cancels
- **Severity / Effort:** High / M
- **Depends on:** UX-001 (for the `ui.back` cancel)
- **Steps:**
  1. Make rebinding device-aware. Either `RebindPrompt::Begin(action, device)` replaces the action's first binding of the pressed input's device (and appends if there is none), or `InputMapper` stores bindings in per-device slots.
  2. Cancel on Escape or on `Gamepad.East`/`Gamepad.Start` (or the `ui.back` binding), and add a timeout. Ignore events from the frame in which the prompt was armed, so the confirm press is never captured.
  3. Add conflict detection to `RebindAction`: return the actions already using the input so the menu can offer to swap or refuse.
  4. Add axis rebinding (`RebindAxis`) for sticks and triggers.
- **Illustrative change (not applied):**
  ```cpp
  // Replace the first binding from the same device as `input`, else append.
  const auto sameDevice = std::ranges::find_if(inputs, [&](const DigitalInput& d) { return d.index() == input.index(); });
  if (sameDevice != inputs.end()) *sameDevice = input; else inputs.push_back(input);
  ```
- **Verification:** New tests: `RebindPrompt.GamepadPress_KeepsKeyboardBinding`, `RebindPrompt.GamepadBack_Cancels`, `RebindPrompt.ArmingFrame_IsIgnored`, `RebindAction.Conflict_IsReported`. In the starter test, rebinding `moveRight` with a gamepad button leaves `D` working.
- **Risks / trade-offs:** The behaviour of the `index` parameter changes. Keep the index overload for tools and document the device-aware default.

## UX-004 — Weak focus indicator, hover equals focus, no initial focus
- **Severity / Effort:** Medium / S
- **Depends on:** None
- **Steps:**
  1. In `BuildFrame`, emit a focus outline: a new `DrawKind::Outline`, or four thin Rects 2–3 reference px wide, in a colour with at least 3:1 contrast against both the button and the page. Allow `focusColor` per document.
  2. Draw focus differently from hover (outline versus background tint) so mouse and gamepad users each get a clear cue.
  3. Focus the first visible button (or a document `"defaultFocus"`) when a controller is pushed or its document set, and after `Update` removes the focused button, move focus to the nearest visible button instead of clearing it.
- **Verification:** `HudLayout` tests assert that an outline item exists for the focused button. `MenuStack.Push_FocusesDefaultButton` passes. Compute the colour contrast in a unit test using the WCAG relative-luminance formula.
- **Risks / trade-offs:** Default focus changes test expectations (`Activate_WithoutFocus_DoesNothing`). Keep pointer-only HUDs (no buttons) unaffected.

## UX-005 — HUD text has no size and doesn't scale
- **Severity / Effort:** Medium / M
- **Depends on:** None
- **Steps:**
  1. Add `textSize` (reference px, default for example 24) to `HudWidget`, and a `textSize` field (already scaled) to `DrawItem`. Add `kMinTextSize` validation in `ParseHud`, or warn when text at the reference size would be below about 18 px at 1080p.
  2. Have `BuildFrame` take a player `textScale` (accessibility setting, for example 1.0–2.0) and multiply it in.
  3. Make `DrawHudFrame` honour `item.textSize` (`AddText(font, size, ...)`). Add an overflow warning in `HudDesigner` when `CalcTextSize` exceeds the rectangle, so truncation shows up during design.
- **Verification:** Round-trip test with `textSize`. A `BuildFrame` test at 3840x2160 shows text size three times the reference. A designer test flags overflow.
- **Risks / trade-offs:** This is a format change, so bump `kHudVersion` and accept version 1 with the default.

## UX-006 — No safe-area support
- **Severity / Effort:** Medium / S
- **Depends on:** None
- **Steps:**
  1. Add a `safeArea` (insets in pixels, or a fraction) parameter to `BuildFrame`/`HudController::Update`, supplied by the platform layer. Anchors resolve inside it, while full-screen backgrounds can opt out with `"ignoreSafeArea": true`.
  2. Draw the title-safe frame (default 5%) as an overlay in the HUD designer preview.
  3. Move the starter HUD offsets inside the safe area.
- **Verification:** A `BuildFrame` test with 5% insets places a `topLeft` widget at (0.05w, 0.05h). Designer overlay test.
- **Risks / trade-offs:** None significant. Defaulting to zero insets keeps today's output for existing goldens.

## UX-007 — HUD formatting and starter UI are not localization-ready
- **Severity / Effort:** Medium / M
- **Depends on:** UX-005 (text expansion headroom)
- **Steps:**
  1. Route `DisplayText`'s literals through `Localization` keys with English fallbacks (`hud.bool.true`, `hud.list.count`). Format numbers with a locale descriptor from `Localization` (decimal and grouping separators per language).
  2. Pass the active `Localization` (and a language picker) into `DrawHudDesignerPanel`'s preview, and warn about `LocalizationKeys(document)` that `Find` doesn't resolve.
  3. Convert the starter's HUD and menus to `{@key}` and add `content/strings/en.strings.json` (plus a pseudo-locale with about 40% longer text to test expansion).
- **Verification:** `StarterSample` test asserts that no button or text in the starter documents contains literal letters outside `{@…}` (a lint over the JSON). `FormatHudText` test for a comma-decimal locale. Designer test for missing-key warnings.
- **Risks / trade-offs:** Bit-exact layout tests that print floats need updates.

## UX-008 — Editor font, scaling and keyboard navigation
- **Severity / Effort:** Medium / M
- **Depends on:** None
- **Steps:**
  1. Bundle a permissively licensed TTF with wide coverage (for example Noto Sans, OFL; check `license_policy.json`) and load it with glyph ranges for Latin Extended, Cyrillic and CJK (or ImGui 1.92 dynamic glyph loading).
  2. Add `uiScale` (and optionally `fontSize`) to `Preferences`. Initialise it from `SDL_GetWindowDisplayScale` and apply it through `style.ScaleAllSizes` and the font size. Set `io.DisplayFramebufferScale` from the pixel size versus the window size.
  3. Enable `ImGuiConfigFlags_NavEnableKeyboard`, and `NavEnableGamepad` once gamepad events are fed.
- **Verification:** `ParsePreferences` round-trips `uiScale`. Screenshot smoke at scale 2 (`--screenshot`), checked by a human. An ImGui headless test shows Tab/arrow navigation moving focus between widgets.
- **Risks / trade-offs:** A bundled font adds a dependency and notice entry. Changing the dock layout may require bumping `interi_editor_layout_v*.ini`.

## UX-009 — Global scene shortcuts fire in other panels
- **Severity / Effort:** Medium / S
- **Depends on:** None
- **Steps:**
  1. Make `HandleShortcuts` act only when the Outliner, Inspector or Viewport window (or no specific window) is focused, for example by tracking the focused window name or using ImGui 1.92's `Shortcut()` with `ImGuiInputFlags_RouteFocused`.
  2. Give the chain editor and HUD designer their own Ctrl+Z/Ctrl+Y bound to their own undo stacks.
- **Verification:** ImGui headless test: with the chain canvas focused, Delete removes the chain node and the scene selection survives. Ctrl+Z restores the chain node.
- **Risks / trade-offs:** Users used to global undo may need the focused stack named in the Edit menu ("Undo (Chain): Add node").

## UX-010 — Colour-only status in the chain canvas
- **Severity / Effort:** Medium / S
- **Depends on:** None
- **Steps:**
  1. Add a second channel to each status: an error node gets a "!" icon and a thick solid border, a warning node a "?" or triangle and a dashed border. Failed links in the debugger are drawn dashed or with an "x" marker.
  2. Label node kinds in the header ("Trigger" / "Action" glyph) rather than relying only on green versus blue.
  3. Consider a colour-blind-safe palette (for example Okabe-Ito) as the default.
- **Verification:** Review a screenshot through a CVD simulator (deuteranopia, protanopia). Statuses must stay distinguishable.
- **Risks / trade-offs:** Visual noise. Keep the icons small.

## UX-011 — Errors only in the console, half-applied project switch
- **Severity / Effort:** Medium / M
- **Depends on:** UX-002 (shared modal infrastructure)
- **Steps:**
  1. Add a notification area (toast or status bar) fed by a `UserMessage` sink for save, open and import failures. A failed save gets a modal with Retry / Save As.
  2. Restructure `OpenProjectFolder` as "prepare everything, then commit": load the project, manifests, graph, data and startup scene into locals, and swap state only when all succeed (`OpenLevel` before the mounts change, or roll back).
- **Verification:** Test: opening a project with a broken startup scene leaves the previous project and level fully active and reports one user-visible error.
- **Risks / trade-offs:** Opening takes more memory while both projects are live.

## UX-012 — Synchronous asset cook on project open
- **Severity / Effort:** Medium / M
- **Depends on:** None
- **Steps:**
  1. Run `Scan`/`CookAll` as Low-priority jobs (as `AsyncFileReader` does). Keep the UI responsive and show a progress bar ("Importing 12/80: hero.glb") with cancellation.
  2. Disable asset-dependent actions (drag to viewport) until cooking finishes, and show that state in the Assets panel.
- **Verification:** Open a project with many large sources. The frame time stays below 100 ms and the progress bar advances (manual or profile preset).
- **Risks / trade-offs:** `AssetDatabase` thread safety. Keep one cook at a time.

## UX-013 — Starter pause menu: no confirmation or save feedback
- **Severity / Effort:** Medium / S
- **Depends on:** UX-001
- **Steps:**
  1. Add a reusable `confirm.hud.json` (message plus Yes/No) and have Load and Quit go through it (`menu.push:confirm_load`).
  2. Show a save indicator, for example a `visibleIf: saving` text or icon on the HUD, and a "Saved" confirmation. Show the slot timestamp next to Load.
  3. Hide `app.quit` on console builds (`visibleIf: platform.canQuit`).
- **Verification:** The starter test walks Pause > Load > Confirm > Yes with gamepad events. A Quit without confirmation is no longer reachable.
- **Risks / trade-offs:** None.

## UX-014 — Starter settings: partial remapping, no binding display
- **Severity / Effort:** Low / S
- **Depends on:** UX-003
- **Steps:**
  1. Add rebind buttons for every action (`moveUp`, `moveDown`, `pause`) and labels that show the current binding through a Value (for example `{input.binding.moveRight}`), switching to "Press a key… (Esc / B to cancel)" while the prompt is armed.
  2. Bind `Gamepad.LeftX/LeftY` as axes for movement, and add dead-zone and hold-or-toggle options where relevant.
- **Verification:** Starter test: all actions appear in the settings menu, and the label changes after a rebind.
- **Risks / trade-offs:** None.

## UX-015 — Chain canvas text scale and pan input
- **Severity / Effort:** Low / S
- **Depends on:** UX-008 (scalable font)
- **Steps:**
  1. Draw node text with `AddText(font, baseSize * zoom, ...)`, hiding it below a readable minimum.
  2. Add Space+left-drag and Alt+left-drag panning, plus arrow-key panning when the canvas is focused.
- **Verification:** ImGui headless test: Space+drag changes `state.pan`. Visual check at zoom 0.5 and 2.0.
- **Risks / trade-offs:** None.

## UX-016 — Path-typed file opening, no HUD dirty marker
- **Severity / Effort:** Low / S
- **Depends on:** UX-002
- **Steps:**
  1. Add a "Browse…" button that opens a filtered list from `m_assetList` (`.chain.json`, `.hud.json`), and support double-click in the asset browser to open chains and HUD documents.
  2. Show `*` on the HUD File window when `m_hud.undo.IsDirty()`.
- **Verification:** Manual check, plus a test that a double-click request opens the chain.
- **Risks / trade-offs:** None.

## UX-017 — HUD bar range can't bind Values
- **Severity / Effort:** Low / S
- **Depends on:** UX-005 (same format bump)
- **Steps:**
  1. Allow `"max": "com.interi.starter:total"` (a Value id string) as an alternative to a number, include it in `BoundValues`, and validate `max > min` at layout time.
  2. Update the starter bar to bind `total`.
- **Verification:** `BuildFrame` test with a bound max. Starter test with a scene of a different coin count.
- **Risks / trade-offs:** `ParseHud` currently fails a bar without `max > min`. That check moves to layout time for bound values.
