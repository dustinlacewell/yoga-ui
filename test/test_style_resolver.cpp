#include "doctest.h"

#include <yui/core/RenderDefaults.hpp>
#include <yui/render/StyleResolver.hpp>

using namespace yui;
using namespace yui::render;
namespace rd = yui::render_defaults;

// ---------------------------------------------------------------------------
// resolveBox — cascade precedence base < hover < focus, with absent optionals
// passing the layer below through untouched.
// ---------------------------------------------------------------------------

TEST_CASE("resolveBox: unset props resolve to no-draw defaults") {
    BoxProps p;
    auto s = resolveBox(p, false, false);
    CHECK(!s.backgroundColor.has_value());
    CHECK(!s.borderColor.has_value());
    CHECK(s.borderWidth == 0);
    CHECK(s.borderRadius == 0);
}

TEST_CASE("resolveBox: base values pass through when no state is active") {
    BoxProps p;
    p.backgroundColor = 0x112233FFu;
    p.borderColor = 0x445566FFu;
    p.borderWidth = 2.0f;
    p.borderRadius = 5.0f;

    auto s = resolveBox(p, false, false);
    CHECK(*s.backgroundColor == 0x112233FFu);
    CHECK(*s.borderColor == 0x445566FFu);
    CHECK(s.borderWidth == 2.0f);
    CHECK(s.borderRadius == 5.0f);
}

TEST_CASE("resolveBox: hover overlays only the fields it sets") {
    BoxProps p;
    p.backgroundColor = 0x111111FFu;
    p.borderRadius = 3.0f;
    BoxStyle hover;
    hover.backgroundColor = 0x222222FFu;  // borderRadius left absent
    p.hoverStyle = hover;

    auto s = resolveBox(p, true, false);
    CHECK(*s.backgroundColor == 0x222222FFu);
    CHECK(s.borderRadius == 3.0f);  // absent override -> base passes through

    // Not hovered -> hoverStyle is inert.
    s = resolveBox(p, false, false);
    CHECK(*s.backgroundColor == 0x111111FFu);
}

TEST_CASE("resolveBox: focus wins over hover; absent focus fields keep hover values") {
    BoxProps p;
    p.backgroundColor = 0x0000AAFFu;
    BoxStyle hover;
    hover.backgroundColor = 0x00BB00FFu;
    hover.borderWidth = 4.0f;
    p.hoverStyle = hover;
    BoxStyle focus;
    focus.backgroundColor = 0xCC0000FFu;  // borderWidth left absent
    p.focusStyle = focus;

    auto s = resolveBox(p, true, true);
    CHECK(*s.backgroundColor == 0xCC0000FFu);  // focus over hover
    CHECK(s.borderWidth == 4.0f);              // hover survives where focus is silent

    // Focus alone: hover layer never applied.
    s = resolveBox(p, false, true);
    CHECK(*s.backgroundColor == 0xCC0000FFu);
    CHECK(s.borderWidth == 0);
}

TEST_CASE("resolveBox: ScrollProps overload runs the same cascade") {
    ScrollProps p;
    p.backgroundColor = 0x111111FFu;
    BoxStyle hover;
    hover.backgroundColor = 0x222222FFu;
    p.hoverStyle = hover;
    BoxStyle focus;
    focus.backgroundColor = 0x333333FFu;
    p.focusStyle = focus;

    CHECK(*resolveBox(p, false, false).backgroundColor == 0x111111FFu);
    CHECK(*resolveBox(p, true, false).backgroundColor == 0x222222FFu);
    CHECK(*resolveBox(p, true, true).backgroundColor == 0x333333FFu);
}

// ---------------------------------------------------------------------------
// resolveText
// ---------------------------------------------------------------------------

TEST_CASE("resolveText: unset props resolve to the shared render defaults") {
    TextProps p;
    auto s = resolveText(p, false, false);
    CHECK(s.fontSize == rd::kDefaultFontSize);
    CHECK(s.color == rd::kDefaultTextColor);
}

TEST_CASE("resolveText: base < hover < focus per field") {
    TextProps p;
    p.fontSize = 20.0f;
    p.color = 0xFF0000FFu;
    TextStyle hover;
    hover.fontSize = 22.0f;  // color left absent
    p.hoverStyle = hover;
    TextStyle focus;
    focus.color = 0x00FF00FFu;  // fontSize left absent
    p.focusStyle = focus;

    auto s = resolveText(p, true, true);
    CHECK(s.fontSize == 22.0f);     // hover, focus silent on fontSize
    CHECK(s.color == 0x00FF00FFu);  // focus over base

    s = resolveText(p, true, false);
    CHECK(s.fontSize == 22.0f);
    CHECK(s.color == 0xFF0000FFu);  // hover silent on color -> base
}

// ---------------------------------------------------------------------------
// resolveInput — owns the rd::kInput* defaults and the default hover/focus
// border rule. First tests ever for this logic.
// ---------------------------------------------------------------------------

TEST_CASE("resolveInput: unset props resolve to the shared input defaults") {
    InputProps p;
    auto s = resolveInput(p, false, false);
    CHECK(s.backgroundColor == rd::kInputBg);
    CHECK(s.borderColor == rd::kInputBorder);
    CHECK(s.borderWidth == rd::kInputBorderWidth);
    CHECK(s.borderRadius == rd::kInputBorderRadius);
    CHECK(s.fontSize == rd::kDefaultFontSize);
    CHECK(s.color == rd::kDefaultTextColor);
}

TEST_CASE("resolveInput: default hover/focus border rule") {
    InputProps p;
    CHECK(resolveInput(p, true, false).borderColor == rd::kInputHoverBorder);
    CHECK(resolveInput(p, false, true).borderColor == rd::kInputFocusBorder);
    // Both active: focus resolves last and wins.
    CHECK(resolveInput(p, true, true).borderColor == rd::kInputFocusBorder);
}

TEST_CASE("resolveInput: a user base borderColor suppresses the default state borders") {
    InputProps p;
    p.borderColor = 0x123456FFu;
    CHECK(resolveInput(p, false, false).borderColor == 0x123456FFu);
    CHECK(resolveInput(p, true, false).borderColor == 0x123456FFu);
    CHECK(resolveInput(p, false, true).borderColor == 0x123456FFu);
}

TEST_CASE("resolveInput: user state styles overlay the default border rule") {
    InputProps p;
    InputStyle hover;
    hover.borderColor = 0xAAAAAAFFu;
    p.hoverStyle = hover;
    CHECK(resolveInput(p, true, false).borderColor == 0xAAAAAAFFu);

    // Hovered AND focused with a focusStyle that is silent on borderColor: the
    // focus layer re-resolves the default focus border from BASE, so the
    // hoverStyle border does not leak through.
    InputStyle focus;
    focus.backgroundColor = 0x010101FFu;
    p.focusStyle = focus;
    auto s = resolveInput(p, true, true);
    CHECK(s.borderColor == rd::kInputFocusBorder);
    CHECK(s.backgroundColor == 0x010101FFu);
}

TEST_CASE("resolveInput: absent state-style fields pass the lower layer through") {
    InputProps p;
    p.fontSize = 18.0f;
    p.color = 0xDDDDDDFFu;
    InputStyle focus;
    focus.fontSize = 24.0f;  // color left absent
    p.focusStyle = focus;

    auto s = resolveInput(p, false, true);
    CHECK(s.fontSize == 24.0f);
    CHECK(s.color == 0xDDDDDDFFu);
    CHECK(s.backgroundColor == rd::kInputBg);  // untouched default
}
