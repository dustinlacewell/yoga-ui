# Text Editing

`Input` supports full caret/selection editing, multiline (textarea) mode,
password masking, and a clipboard seam — all driven through a keycode-agnostic
`EditCommand` model, so core never has to know whether you're running under
SDL, GLFW, or anything else.

## Multiline

```cpp
Input()
    .value(body)
    .multiline()
    .onChange([&](const std::string& v) { setBody(v); })
    .backgroundColor(0x1E1E1EFF);
```

`.multiline()` turns `Input` into a textarea: the value soft-wraps at the
content width, Enter inserts `\n` (a single-line `Input`'s Enter fires
`onSubmit` instead), and the box grows to fit its wrapped lines via a Yoga
measure function — the same measure/paint agreement the rest of layout relies
on (see [Architecture — rendering](architecture.md#rendering)).

`.password(true)` **wins over** `.multiline(true)` — a node with both renders
as a single-line password field, since masked textareas (stars with line
structure) aren't a coherent thing to support. Don't rely on the two
composing.

## Fonts

```cpp
Text("Heading").font("mono");
Input().value(code).font("mono").multiline();
```

Both `Text` and `Input` take an optional `.font(name)`, resolved against the
renderer's registered font faces by **name** — not a raw handle — so it stays
backend-agnostic and survives a GL context rebuild. An empty/unset font uses
the default. This threads all the way through every measure/draw call in the
render seam (`ITextMeasurer::measureRun`/`fontMetrics`,
`IRenderBackend::drawTextRun`) so a run always draws in the face it was
measured in.

## Caret, selection, and `EditCommand`

Editing operations are represented as `yui::EditCommand`, not raw keycodes:

```cpp
enum class EditCommand {
    MoveLeft, MoveRight, MoveLineStart, MoveLineEnd, MoveUp, MoveDown,
    SelectAll,
    DeleteBackward, DeleteForward,
    Cut, Copy, Paste,
    InsertNewline,
};
```

A platform shim maps raw keycodes to `EditCommand`s; core owns what each
command *means*. `InsertNewline` is the Enter mapping for both modes — core
decides the behavior: a multiline input inserts `'\n'`, a single-line input
fires `onSubmit`. A command that doesn't apply to the focused input's current
mode (e.g. `MoveUp`/`MoveDown` sent to a single-line input) is reported as
**not consumed**, so a shim can route the key elsewhere instead of it being
silently swallowed.

Route input through the 5-argument `Host::handleKeyDown` overload, which owns
the full priority order:

```cpp
bool handleKeyDown(int keyCode, uint16_t mods, bool repeat,
                    std::optional<EditCommand> edit, bool focusNav = false);
```

Priority: **(1)** the focused `Input`'s edit command (if `edit` is set and
applicable), **(2)** focus navigation (`focusNav` — your shim's Tab
detection), **(3)** your app's own `onKeyDown` handlers. An edit command the
focused input didn't consume falls through to focus nav, which falls through
to app dispatch. See [Focus](primitives.md#focus) for the `focusNav` half of
this contract.

```cpp
// platform shim (sketch): map SDL keycodes to EditCommand, then route through Host
std::optional<EditCommand> mapKey(SDL_Keycode k, uint16_t mods) {
    switch (k) {
        case SDLK_LEFT:      return EditCommand::MoveLeft;
        case SDLK_RIGHT:     return EditCommand::MoveRight;
        case SDLK_BACKSPACE: return EditCommand::DeleteBackward;
        case SDLK_DELETE:    return EditCommand::DeleteForward;
        case SDLK_RETURN:    return EditCommand::InsertNewline;
        case SDLK_a: if (mods & KeyMod_Ctrl) return EditCommand::SelectAll; break;
        case SDLK_x: if (mods & KeyMod_Ctrl) return EditCommand::Cut; break;
        case SDLK_c: if (mods & KeyMod_Ctrl) return EditCommand::Copy; break;
        case SDLK_v: if (mods & KeyMod_Ctrl) return EditCommand::Paste; break;
        default: return std::nullopt;
    }
    return std::nullopt;
}

host.handleKeyDown(keyCode, mods, repeat, mapKey(keyCode, mods),
                    /*focusNav=*/ keyCode == SDLK_TAB);
```

For direct control without going through the routing overload, call
`Host::handleEditCommand(EditCommand cmd, bool extend = false)` yourself —
`extend` is the Shift-held selection modifier: a `Move*` command with
`extend=true` moves only the caret, leaving the selection anchor in place to
span the new selection.

Password masking is per **code point**, not per byte — a masked multi-byte
UTF-8 character still renders as exactly one `*`.

## Clipboard

`Cut`/`Copy`/`Paste` route through a platform clipboard seam:

```cpp
struct IClipboard {
    virtual std::string getText() = 0;
    virtual void setText(const std::string& text) = 0;
};

host.setClipboard(myClipboard);  // install once; nullptr = uninstalled
```

With no clipboard installed, `Cut`/`Copy`/`Paste` report as unconsumed rather
than silently discarding data. Lifetime is self-managed in both directions —
either the `Host` or the `IClipboard` may be destroyed first, and each side
detaches cleanly (the same pattern `ITextMeasurer` uses for the text-measurer
link). A bundled `yui::sdl::SdlClipboard` is available for SDL apps
(`<yui/sdl/SdlClipboard.hpp>`); apps embedded inside a host application need
to supply their own `IClipboard`, since there's no OS clipboard access to
piggyback on in that setting.
