# UI-UX — Goal State

## Vision
Every menu built with `engine/hud` can be fully operated by gamepad, by keyboard and by mouse, with the same flow: a default focus, a clearly visible focus ring, consistent accept and back, and no dead ends. HUD documents scale their text, respect the platform safe area and contain only localization keys, so a project started from the starter template is ready for TV, couch play and translation. The editor never loses work silently: every destructive action asks first, failures are shown to the user rather than only logged, long imports show progress, and the UI scales, reads in any script and stays usable from the keyboard and for colour-blind users.

## Acceptance criteria
- [ ] AC-1 The engine defines `ui.up/down/accept/back` actions with keyboard and gamepad defaults, and a non-test caller drives `MenuStack` from them. A test using only `GamepadButtonEvent`s goes Pause > Settings > Back > Resume, and `ui.back` on a root menu never leaves an empty screen. (resolves UX-001)
- [ ] AC-2 With any document dirty (scene, chain, HUD, material, project data), Quit, window close, New, Open project, Create project, chain New/Load and HUD Load show a Save / Discard / Cancel prompt, and Cancel leaves everything unchanged. This is covered by a headless test. (resolves UX-002)
- [ ] AC-3 Rebinding an action with a gamepad button keeps its keyboard bindings and vice versa. The gamepad back button and Escape both cancel a prompt. The activating press is never captured. Conflicts are reported. Axes can be rebound. Each has a unit test. (resolves UX-003)
- [ ] AC-4 A focused HUD button is drawn with an outline whose contrast is at least 3:1 against both the button and its surroundings, distinct from hover. A newly pushed menu has a focused button, and hiding the focused button moves focus to a neighbour. (resolves UX-004)
- [ ] AC-5 HUD widgets carry a `textSize` that scales with the layout and with a player text-scale setting. At 1920x1080 no shipped starter text is below 18 px. The HUD designer warns when text overflows its rectangle. (resolves UX-005)
- [ ] AC-6 `BuildFrame` accepts safe-area insets, anchors resolve inside them, and the designer draws a 5% title-safe overlay. Starter HUD widgets lie inside it. (resolves UX-006)
- [ ] AC-7 `DisplayText` and number formatting contain no hard-coded English and format decimals per language. Every text and button in the starter's `.hud.json` files is a `{@key}` backed by `en.strings.json`. The designer preview uses the active `Localization` and flags missing keys. (resolves UX-007)
- [ ] AC-8 The editor loads a bundled Unicode font, applies a `uiScale` preference (defaulting to the display scale) and enables keyboard navigation. A screenshot at scale 2 is legible on human review, and a headless test navigates widgets with Tab and the arrow keys. (resolves UX-008)
- [ ] AC-9 Delete, Ctrl+Z/Y and Ctrl+G act only on the focused editor (scene, chain or HUD), and each has its own undo shortcut. This is covered by an ImGui headless test. (resolves UX-009)
- [ ] AC-10 Every status in the chain canvas and debugger (error/warning, passed/failed, trigger/action) has a non-colour cue (icon, dash pattern or label), confirmed with a CVD-simulated screenshot. (resolves UX-010)
- [ ] AC-11 Save, open and import failures show a user-visible notification or modal. Opening a project whose startup scene fails leaves the previous project fully active. (resolves UX-011)
- [ ] AC-12 Asset scan and cook run off the UI thread with a progress indicator, and the frame time stays under 100 ms while importing. (resolves UX-012)
- [ ] AC-13 The starter's Load and Quit go through a confirmation menu, saving shows an indicator and a confirmation, and Quit is hidden where the platform forbids it. (resolves UX-013)
- [ ] AC-14 The starter settings menu can rebind every action, shows the current binding and a "press a key / back to cancel" state, and the left stick moves the player. (resolves UX-014)
- [ ] AC-15 Chain node text scales with zoom, and the canvas can be panned without a middle mouse button. (resolves UX-015)
- [ ] AC-16 Chain and HUD documents can be opened from a picker or the asset browser, and the HUD File window shows a dirty marker. (resolves UX-016)
- [ ] AC-17 A bar's `min`/`max` can bind a Value, and the starter progress bar binds `total`. (resolves UX-017)
- [ ] AC-18 (Info, optional) Decide whether `HudController` should emit focus-changed and activated events for audio and haptics, and whether `Localization` needs plural selection before the first shipped game. Record the decision in docs. (tracks UX-018, UX-019)

## Guardrails to keep it that way
- A `StarterSample` test that plays every menu with gamepad events only, and another with keyboard only.
- A JSON lint test over `samples/**/*.hud.json` that fails on literal user-facing text outside `{@key}` and on widgets outside the safe area.
- A `HudLayout` unit test that computes the focus-outline contrast with the WCAG luminance formula.
- A headless editor test matrix: every destructive request combined with a dirty state must produce a prompt.
- A review checklist item for new editor panels: shortcuts scoped to focus, no colour-only status, errors shown to the user, long work off the UI thread.
- Keep `HudController`/`MenuStack` free of game logic (actions stay strings). The UI input adapter is the only place that reads `InputMapper`.

## Out of scope
- Platform-specific certification requirement IDs (NDA). Only the public themes are covered.
- Screen-reader or narration integration for the editor and HUD. Worth revisiting when a console backend exposes a narration API.
- Localizing the editor's own UI (a developer tool, English only for now).
- Regional confirm/cancel button swapping and per-platform button glyphs. These need a glyph and icon system the HUD doesn't have yet. Plan it after AC-1.
- The UI of the gitignored `modules/` projects (Destruction editor extension), which have their own repositories.
