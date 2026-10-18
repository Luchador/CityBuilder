# Minimize the UI

Click the existing minimize button or press Tab to toggle the interface. Only
the minimize button remains visible while minimized. Its current bottom-right
position, reduced size, and textures are retained. Tab key repeats are ignored.

The world viewport expands to fill the window. Hidden panels, camera buttons,
and the minimap do not intercept mouse input, so wheel zoom and right-button
panning work over the uncovered area. The remaining button still intercepts
input normally. Keyboard camera controls continue to work.

Pan indicator graphics are hidden with the interface; right-button panning keeps
the normal mouse cursor visible in this mode. Toggling either way cancels an
active pan or button press so a changed layout cannot leave a stale mouse anchor
or trigger a click on mouse release.

## Code structure

- `UiSettings::minimized` owns the toggle state; `UiLayout::minimized` records the
  state used for the current layout. Dock rectangles stay available for anchoring
  the restore button, while `world` becomes the whole window when minimized.
- `Application::ToggleUi()` is shared by the button click and Tab shortcut.
- `UiButtons::ShowOnly(id)` filters both drawing and hit testing to one button.
  `ShowOnly(std::nullopt)` restores the normal set without changing individual
  visibility, enabled, or selected properties. Newly added buttons obey the same
  filter automatically.
- `EnsureUi()` notices visibility changes as well as window size changes.
- The render path skips panel geometry, minimap texture/outline, text, and pan
  overlays while minimized. It skips text preparation and outline uploads too.

The toggle lasts for the current game session; the interface starts visible.

## Verification

The UI tests cover full-window world bounds and hidden-panel hit testing across
multiple window/minimap sizes, drawing only the remaining button, hover/press/
release behavior, filtering later-added buttons, restoration of individual
button states, and cancellation of captured presses.

Optional test commands from the Visual Studio developer command prompt:

```bat
cmake -S . -B build -DCITY_BUILD_UI_TESTS=ON
cmake --build build --target CityUiVisibilityTests
ctest --test-dir build -R CityUiVisibilityTests --output-on-failure
```

The tests use texture/draw doubles rather than a GPU. They pass with
AddressSanitizer and UndefinedBehaviorSanitizer, and all application C++ files
pass strict syntax checks with the available headers. Windows/Direct3D 12 runtime
validation still needs to be done in the game.

For a quick in-game check, toggle with each control, hold Tab briefly, resize
while minimized, pan/zoom over the old panel locations, then restore the UI.
