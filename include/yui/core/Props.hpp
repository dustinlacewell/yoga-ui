#pragma once

#include "Event.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <variant>

namespace yui {

// Yoga layout enums (mirrors YGEnums.h)
enum class FlexDirection { Row, Column, RowReverse, ColumnReverse };
enum class FlexWrap { NoWrap, Wrap, WrapReverse };
enum class JustifyContent { FlexStart, Center, FlexEnd, SpaceBetween, SpaceAround, SpaceEvenly };
enum class AlignItems { FlexStart, Center, FlexEnd, Stretch, Baseline };
enum class AlignContent { FlexStart, Center, FlexEnd, Stretch, SpaceBetween, SpaceAround, SpaceEvenly };
enum class AlignSelf { Auto, FlexStart, Center, FlexEnd, Stretch, Baseline };
enum class PositionType { Relative, Absolute };
enum class Display { Flex, None };

// Layout properties (shared by all primitives)
struct LayoutProps {
    std::optional<float> width;
    std::optional<float> height;
    std::optional<float> widthPercent;   // Yoga percent width (0-100)
    std::optional<float> heightPercent;  // Yoga percent height (0-100)
    std::optional<float> minWidth;
    std::optional<float> minHeight;
    std::optional<float> maxWidth;
    std::optional<float> maxHeight;

    std::optional<float> flexGrow;
    std::optional<float> flexShrink;
    std::optional<float> flexBasis;
    std::optional<FlexDirection> flexDirection;
    std::optional<FlexWrap> flexWrap;
    std::optional<JustifyContent> justifyContent;
    std::optional<AlignItems> alignItems;
    std::optional<AlignContent> alignContent;
    std::optional<AlignSelf> alignSelf;
    std::optional<PositionType> positionType;
    std::optional<Display> display;

    std::optional<float> padding;
    std::optional<float> paddingTop;
    std::optional<float> paddingRight;
    std::optional<float> paddingBottom;
    std::optional<float> paddingLeft;

    std::optional<float> margin;
    std::optional<float> marginTop;
    std::optional<float> marginRight;
    std::optional<float> marginBottom;
    std::optional<float> marginLeft;

    std::optional<float> gap;
    std::optional<float> rowGap;
    std::optional<float> columnGap;

    // Absolute positioning (requires positionType = Absolute)
    std::optional<float> positionLeft;
    std::optional<float> positionTop;
    std::optional<float> positionRight;
    std::optional<float> positionBottom;

    std::optional<float> aspectRatio;

    bool operator==(const LayoutProps&) const = default;
};

// Mouse cursor shape a node can request via EventProps::cursor. Resolved by
// Host::getCursor() (pull query): the platform polls it each frame and maps it
// to its native cursor.
enum class CursorShape { Arrow, IBeam, Pointer, Crosshair, ResizeEW, ResizeNS, ResizeAll };

// Event handlers (shared by all primitives)
struct EventProps {
    std::function<void()> onClick;
    std::function<void()> onRightClick;
    std::function<void()> onMiddleClick;
    std::function<void()> onDoubleClick;                     // second chained click (see Event::clickCount)
    std::function<void(float, float, MouseButton, uint16_t)> onMouseDown;  // (x, y, button, KeyMod bitmask)
                                                                           // on PRESS, any button
    std::function<void(float, float, MouseButton)> onMouseUp;    // (x, y, button) on RELEASE — fires on the
                                                                 // captor even off-node / off-window
    std::function<void(float, float)> onMouseMove;           // (x, y) — the captor during a press, else hover
    std::function<void(const DragEvent&)> onDrag;            // captured move past the drag threshold
    std::function<void(bool)> onHover;
    std::function<void(bool)> onFocus;
    std::function<void(float, float)> onScroll;              // (deltaX, deltaY)
    std::function<void(int, uint16_t, bool)> onKeyDown;      // (keyCode, modifiers, repeat)
    std::function<void(int, uint16_t)> onKeyUp;              // (keyCode, modifiers)
    std::optional<CursorShape> cursor;                       // pointer shape while hovered/captured
    bool focusable = false;                                  // click/Tab can move focus here
                                                             // (an Input always can)
    std::optional<bool> autoFocus;                           // focus this node when it mounts
};

// --- State-based style overrides ---

// Visual styles for Box (can override on hover/focus)
struct BoxStyle {
    std::optional<uint32_t> backgroundColor;
    std::optional<uint32_t> borderColor;
    std::optional<float> borderWidth;
    std::optional<float> borderRadius;
};

// Visual styles for Text (can override on hover/focus)
struct TextStyle {
    std::optional<uint32_t> color;
    std::optional<float> fontSize;
};

// Visual styles for Input (can override on hover/focus)
struct InputStyle {
    std::optional<uint32_t> backgroundColor;
    std::optional<uint32_t> borderColor;
    std::optional<float> borderWidth;
    std::optional<float> borderRadius;
    std::optional<uint32_t> color;
    std::optional<float> fontSize;
};

// --- Primitive props ---

// Box: layout container
struct BoxProps : LayoutProps, EventProps {
    std::optional<uint32_t> backgroundColor;
    std::optional<float> borderRadius;
    std::optional<uint32_t> borderColor;
    std::optional<float> borderWidth;

    // State-based style overrides (focus takes precedence over hover)
    std::optional<BoxStyle> hoverStyle;
    std::optional<BoxStyle> focusStyle;
};

// Text: text display
struct TextProps : LayoutProps, EventProps {
    std::string text;
    std::optional<float> fontSize;
    std::optional<uint32_t> color;
    // Registered font face to render/measure with (empty/unset ⇒ the default
    // font). Selected by NAME so it is backend-agnostic and stable across a GL
    // context rebuild; the renderer resolves the name to a backend handle.
    std::optional<std::string> font;
    // Soft-wrap at the available layout width (unset ⇒ true). false keeps the
    // text one run, measured/drawn at its full single-line advance ('\n' still
    // breaks).
    std::optional<bool> wrap;

    // State-based style overrides
    std::optional<TextStyle> hoverStyle;
    std::optional<TextStyle> focusStyle;
};

// Input: text input field (controlled component)
struct InputProps : LayoutProps, EventProps {
    std::string value;  // Controlled value - set via onChange callback
    std::optional<std::string> placeholder;
    std::optional<bool> password;
    // Multiline (textarea) mode: the value soft-wraps at the content width,
    // Enter inserts '\n' (single-line Enter fires onSubmit instead), and the
    // input grows to fit its wrapped lines via a Yoga measure func. password
    // WINS over multiline (a masked textarea is unsupported — stars have no
    // line structure): a node with both renders as a single-line password.
    std::optional<bool> multiline;
    std::optional<float> fontSize;
    std::optional<uint32_t> color;
    std::optional<std::string> font;  // registered font face (see TextProps::font)
    std::optional<uint32_t> backgroundColor;
    std::optional<uint32_t> borderColor;
    std::optional<float> borderWidth;
    std::optional<float> borderRadius;
    std::function<void(const std::string&)> onChange;
    std::function<void()> onSubmit;

    // State-based style overrides
    std::optional<InputStyle> hoverStyle;
    std::optional<InputStyle> focusStyle;
};

// Scroll: scrollable container
struct ScrollProps : LayoutProps, EventProps {
    std::optional<uint32_t> backgroundColor;
    std::optional<float> borderRadius;
    std::optional<uint32_t> borderColor;
    std::optional<float> borderWidth;

    // State-based style overrides
    std::optional<BoxStyle> hoverStyle;
    std::optional<BoxStyle> focusStyle;
};

// Canvas: custom drawing primitive
// The draw callback is renderer-agnostic - it receives an opaque context
// that the renderer knows how to interpret (e.g., NVGcontext* for NvgRenderer)
using CanvasDrawFn = std::function<void(void* ctx, float width, float height)>;

struct CanvasProps : LayoutProps, EventProps {
    CanvasDrawFn draw;
};

// Portal: detached-content container (the Scroll detachment generalized). Its
// content reconciles in place (state/hooks/refs in the logical parent) but is
// laid out against the viewport and painted/hit-tested at root z-order. The
// node itself is pure plumbing — zero-size, chrome-less — so beyond the shared
// prop slices it carries only trap plumbing.
struct PortalProps : LayoutProps, EventProps {
    // Trap Tab traversal inside the portal's content while it is mounted.
    // Applied by the reconciler's MOUNT PEEK (before the content mounts), so
    // the previously-focused node is saved before any content autoFocus runs;
    // unmounting the portal (or clearing the trap) restores that focus.
    std::optional<bool> trapFocus;
};

// Variant holding any primitive's props
using PropsVariant = std::variant<BoxProps, TextProps, InputProps, ScrollProps, CanvasProps, PortalProps>;

}  // namespace yui
