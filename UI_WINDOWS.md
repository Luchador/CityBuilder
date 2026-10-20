# Floating UI windows

Press **F2** to open the control demonstration. It contains text labels, an
image, two editable text boxes, a radio group, and a footer button. Its values
are previews: editing them does not change the city.

Drag a title bar with the left mouse button. Clicking a window brings it to the
front. The close icon, footer Close button, and Escape close the demo. Ordinary
windows are kept entirely inside the game window; if a window is larger than
the available screen, its title bar and close icon remain reachable. Closing
hides an instance so that it can be reopened without losing its values.

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
F11 and Alt+Enter still toggle fullscreen. Tab continues to minimize the UI
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
  lifecycle, rendering integration, and the F2 demonstration.
- `text.hpp/.cpp`: shared font/glyph cache, per-window text groups, clipping,
  visibility, and horizontal text offset.

Each window's background, images, and text are drawn together, before the next
window. This keeps text from a covered window behind the covering window.
Font atlases and image textures remain shared. Window geometry uploads only
when it changes. Windows and their labels are destroyed before the shared
text renderer and texture cache.

`tests/ui_manager_tests.cpp` covers input routing, clipping boundaries, z order,
drag constraints, missed releases, focus loss, text editing, radio selection,
minimization, reopening, and resource cleanup with portable device/font doubles.
The real Windows/Direct3D 12 game still needs a visual run on Windows.
