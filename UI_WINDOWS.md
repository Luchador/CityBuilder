# Floating UI windows

The **Create New City** window appears at startup. F2 no longer opens it.
Until Accept succeeds, only this window and the background are drawn: the
application has no City or terrain heightmap and does not run the simulation.

Drag a title bar with the left mouse button. Clicking a window brings it to the
front. Ordinary windows can be closed with their close icon or Escape. Ordinary
windows are kept entirely inside the game window; if a window is larger than
the available screen, its title bar and close icon remain reachable. Closing
hides an instance so that it can be reopened without losing its values.

## Required startup form

`CreateNewCityWindow()` in `src/ui_windows_data.cpp` builds the form. Its Accept
callback reads and validates the controls, then calls `UiActions::createCity`.
Application supplies that callback and prepares the terrain, grid, water,
skirt, minimap, and City. Only after successful preparation does it publish
the new city and let the form close. A failed attempt releases partial terrain
buffers, displays an error in the form, and leaves Accept available for retry.

The city name, mayor name, and starting year are applied. Population and funds
start at zero; the date starts January 1 of the selected year, with simulation
paused. Waiting in the form and generating terrain do not advance the date.
Difficulty and map-size selections are stored on City for future use. Difficulty
does not yet change simulation rules, and terrain still uses the existing
fixed 256 x 256 dimensions regardless of the selected map-size label.

The startup form is required: clicks outside it, Tab, Escape, UI minimization,
and loss/regain of focus cannot dismiss it. Tab navigates its controls. Native
application close/Alt+F4 and F11/Alt+Enter fullscreen remain available.

For any window that should not have user dismissal controls:

```cpp
panel.SetClosable(false);
```

This hides the X, removes its hit target, and makes Escape leave the window
open. `panel.Close()` still works from an Accept callback. SetClosable alone
does not make an arbitrary window modal; UiManager enforces the required
startup flow separately. Pass `true` to restore ordinary close behavior.

## Creating a window

Create windows through `UiManager`, which owns them, routes input, and draws
them above the docks. For example, inside a `UiManager` method:

```cpp
auto& panel = AddWindow("Mayor details", {200, 140, 480, 260});

panel.AddLabel(1, {0, 0, 400, 24}, "Mayor name");
panel.AddTextBox(2, {0, 32, 400, 34}, "Jane Smith");
panel.AddFooterButton(3, "Close", [](UiWindow& window)
{
    window.Close();
});

// Retain this ID as a UiManager member to reopen the same window later:
// mayorWindowId = panel.GetId();
// OpenWindow(mayorWindowId);
```

Window bounds use the game's logical screen coordinates. Child bounds use
coordinates relative to `GetContentBounds()` (below the title bar, inside the
padding). The footer is anchored to the bottom right automatically. Controls
outside the content area are clipped for drawing and pointer input. Each
window has its own control IDs, so different windows can reuse `1`, `2`, etc.

Use `AddImage(id, bounds, texture)` for images loaded through `TextureManager`;
the texture must outlive the window. `AddButton` adds a text button in the
content area. `AddFooterButton` adds one footer button. The button label can be
"Accept", "Close", or other text; its callback decides what happens.

An Accept callback reads `window.GetText(textBoxId)` and
`window.IsChecked(radioId)`, validates the values, and sends a game action
through `UiActions`. Call `Close()` after a successful action; leave the window
open to display a validation message. `SetText(labelId, message)` updates a
label, and `SetText(textBoxId, value)` updates an input field. Do not release the
UI manager from inside a control callback.

## Radio groups and editing

```cpp
panel.AddRadioButton(10, 1, {0, 80, 130, 28}, "Light", true);
panel.AddRadioButton(11, 1, {140, 80, 130, 28}, "Medium");
panel.AddRadioButton(12, 1, {280, 80, 130, 28}, "Dense");
```

The second argument is the group ID. Selecting a radio button unchecks the
others in that group, within that window. Different group IDs are independent.
Clicking either the radio artwork or its label selects the option.

Tab and Shift+Tab move between controls in creation order. Space or Enter
activates a focused button/radio; Enter in a text field activates the footer.
While a floating window has focus, world shortcuts and polled WASD/arrow-key
panning are blocked. Click the world or a dock to give the game focus again.
F11 and Alt+Enter still toggle fullscreen. Once city creation is complete, Tab minimizes the UI
when a floating window does not have focus. Minimizing hides the windows along
with the docks and cancels text input; restoring retains open-window state.

Text boxes support single-line UTF-8 input, a caret, horizontal scrolling,
mouse selection, Shift+Left/Right, Home/End, Backspace/Delete, and
Ctrl+A/C/X/V. They use SDL text-input events instead of turning keycodes into
characters. The default limit is 128 Unicode code points. Navigation/deletion
currently moves by code point, so combining marks are separate editing steps.
IME composition is displayed while editing and is committed only by a text
input event. Multiline editing, undo history, window resizing, and scrolling
containers are later extensions.

## Code ownership and rendering

- `ui_window.hpp/.cpp`: window state, child controls, movement, input, focus.
- `ui_window_draw.cpp`: window geometry, control backgrounds, image drawing.
- `ui_text_input.hpp/.cpp`: text editing state with no GPU or SDL dependency.
- `ui_manager_windows.cpp`: ownership, stacking, SDL capture/text-input
  lifecycle, required-window focus, and rendering integration.
- `ui_windows_data.cpp`: creation form layout, validation, and Accept action.
- `city.hpp/.cpp`: creation options, validation, and initial city values.
- `text.hpp/.cpp`: shared font/glyph cache, per-window text groups, clipping,
  visibility, and horizontal text offset.

Each window's background, images, and text are drawn together, before the next
window. This keeps text from a covered window behind the covering window.
Font atlases and image textures remain shared. Window geometry uploads only
when it changes. Windows and their labels are destroyed before the shared
text renderer and texture cache.

The Windows/Direct3D 12 game needs a visual run on Windows. Portable checks can
exercise input routing and application startup with device/font doubles.
