#pragma once

namespace yui {

// The editing operations a platform shim routes to the focused Input (via
// Host::handleEditCommand) after an unconsumed handleKeyDown. Core stays
// keycode-agnostic: the shim owns the key -> command mapping, core owns the
// command semantics.
//
// The full command set is declared up front so the enum is stable across the
// text-editing commits, but implementations land incrementally:
//   - C1 (landed):    MoveLeft, MoveRight, MoveLineStart, MoveLineEnd,
//     DeleteBackward, DeleteForward.
//   - C3 (landed):    SelectAll (and the `extend` flag on moves).
//   - C5 (landed):    Cut, Copy, Paste (through the IClipboard seam).
//   - C6 (multiline): MoveUp, MoveDown, InsertNewline.
// An unimplemented command is NOT consumed (handleEditCommand returns false),
// so shims can fall through until its commit arrives.
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
