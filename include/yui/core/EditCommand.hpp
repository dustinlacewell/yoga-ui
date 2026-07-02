#pragma once

namespace yui {

// The editing operations a platform shim routes to the focused Input (via
// Host::handleEditCommand) after an unconsumed handleKeyDown. Core stays
// keycode-agnostic: the shim owns the key -> command mapping, core owns the
// command semantics.
//
// The full command set is implemented. A command inapplicable to the focused
// input's mode (MoveUp/MoveDown on a single-line input) is NOT consumed
// (handleEditCommand returns false), so shims can route the key elsewhere.
// InsertNewline is the Enter mapping for BOTH modes — core decides: a
// multiline input inserts '\n', a single-line input fires onSubmit.
enum class EditCommand {
    MoveLeft,
    MoveRight,
    MoveLineStart,
    MoveLineEnd,
    MoveUp,
    MoveDown,
    SelectAll,
    DeleteBackward,
    DeleteForward,
    Cut,
    Copy,
    Paste,
    InsertNewline,
};

}  // namespace yui
