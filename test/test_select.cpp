#include "doctest.h"

#include <yui/yui.hpp>

#include <functional>
#include <string>
#include <vector>

using namespace yui;

namespace {

// First node with this key in pre-order (the test_modal idiom: reconciles
// shuffle child indexes, so tests re-find by key rather than by position).
Node* findByKey(Node* node, const std::string& key) {
    if (node->key == key)
        return node;
    for (auto& child : node->children)
        if (Node* found = findByKey(child.get(), key))
            return found;
    return nullptr;
}

// True if any node in the subtree is a ScrollNode (the popup list scrolls).
bool hasScroll(Node* node) {
    if (dynamic_cast<ScrollNode*>(node))
        return true;
    for (auto& child : node->children)
        if (hasScroll(child.get()))
            return true;
    return false;
}

// One press+release at a point through the full Host entry points.
bool clickAt(Host& host, float x, float y) {
    host.handleMouseDown(x, y, MouseButton::Left);
    return host.handleMouseUp(x, y, MouseButton::Left);
}

// A few app-supplied keycodes for the opt-in keyboard tests.
constexpr int kUp = 38;
constexpr int kDown = 40;
constexpr int kEnter = 13;
constexpr int kEsc = 27;

}  // namespace

// ---------------------------------------------------------------------------
// Open / option mount / placement: clicking the control mounts the popup, whose
// panel sits flush below the control (placePanel anchor = control.bottom) and
// matches the control's width, with one keyed node per option.
// ---------------------------------------------------------------------------

TEST_CASE("Select - click opens the popup below the control, options mount, width matches") {
    Host host;
    std::vector<std::string> opts = {"Apple", "Banana", "Cherry"};

    auto App = [&](ComponentContext& ctx) -> VNode {
        (void)ctx;
        // Control lives inside a 200-wide box near the top-left, well off the
        // bottom edge so placement anchors straight below (no upward shift).
        return Box(widgets::Select(opts).value(-1)).width(800).height(600).padding(40);
    };
    host.setRender(Component(App));
    host.update(800, 600);

    Node* control = findByKey(host.root(), "control");
    REQUIRE(control != nullptr);
    CHECK(findByKey(host.root(), "list") == nullptr);  // closed initially

    auto cr = absoluteRect(control);
    clickAt(host, cr.x + 5, cr.y + 5);
    host.update(800, 600);

    Node* list = findByKey(host.root(), "list");
    REQUIRE(list != nullptr);

    // Each option present, keyed by index.
    for (int i = 0; i < 3; ++i)
        REQUIRE(findByKey(host.root(), std::to_string(i)) != nullptr);

    // Panel flush below the control, same width.
    auto lr = absoluteRect(list);
    CHECK(lr.y == doctest::Approx(cr.y + cr.h));
    CHECK(lr.w == doctest::Approx(cr.w));
}

// ---------------------------------------------------------------------------
// Selecting an option: onChange fires with that index and the popup unmounts.
// CONTROLLED: without re-driving .value the widget does NOT self-select — the
// control keeps showing the original value's text.
// ---------------------------------------------------------------------------

TEST_CASE("Select - clicking an option reports the index, closes, and does not self-select") {
    Host host;
    std::vector<std::string> opts = {"Apple", "Banana", "Cherry"};
    int changed = -99;

    auto App = [&](ComponentContext& ctx) -> VNode {
        (void)ctx;
        // value fixed at 0 (Apple), never updated by the app -> controlled pin.
        return Box(widgets::Select(opts).value(0).onChange([&](int i) { changed = i; }))
            .width(800)
            .height(600)
            .padding(40);
    };
    host.setRender(Component(App));
    host.update(800, 600);

    Node* control = findByKey(host.root(), "control");
    auto cr = absoluteRect(control);
    clickAt(host, cr.x + 5, cr.y + 5);
    host.update(800, 600);

    Node* opt2 = findByKey(host.root(), "2");  // Cherry
    REQUIRE(opt2 != nullptr);
    auto o2 = absoluteRect(opt2);
    clickAt(host, o2.x + 5, o2.y + 5);
    host.update(800, 600);

    CHECK(changed == 2);                                // onChange(2) fired
    CHECK(findByKey(host.root(), "list") == nullptr);   // popup unmounted

    // Controlled: value is still 0 (app never changed it) — the widget did not
    // latch its own selection. The reopened list highlights nothing new; the
    // control's selected-row style still tracks index 0.
    clickAt(host, cr.x + 5, cr.y + 5);
    host.update(800, 600);
    REQUIRE(findByKey(host.root(), "list") != nullptr);
    // Row 0 is the selected row (styled), rows 1/2 are not — pins that the
    // internal selection did not move to 2.
    auto* row0 = dynamic_cast<BoxNode*>(findByKey(host.root(), "0"));
    auto* row2b = dynamic_cast<BoxNode*>(findByKey(host.root(), "2"));
    REQUIRE(row0 != nullptr);
    REQUIRE(row2b != nullptr);
    // The selected-row style paints a background on row 0 but not row 2.
    CHECK(row0->props.backgroundColor.has_value());
    CHECK_FALSE(row2b->props.backgroundColor.has_value());
}

// ---------------------------------------------------------------------------
// Outside click: a click on the transparent backdrop (far from the list)
// closes the popup, fires no onChange, and does NOT reach a wrapping parent's
// onClick — the portal + backdrop consume it (a Select is a light modal-ish).
// ---------------------------------------------------------------------------

TEST_CASE("Select - clicking outside closes without onChange and without reaching the parent") {
    Host host;
    std::vector<std::string> opts = {"Apple", "Banana", "Cherry"};
    int changed = -99;
    int parentClicks = 0;

    auto App = [&](ComponentContext& ctx) -> VNode {
        (void)ctx;
        return Box(widgets::Select(opts).value(-1).onChange([&](int i) { changed = i; }))
            .width(800)
            .height(600)
            .padding(40)
            .onClick([&] { parentClicks++; });  // app-level ancestor handler
    };
    host.setRender(Component(App));
    host.update(800, 600);

    Node* control = findByKey(host.root(), "control");
    auto cr = absoluteRect(control);
    clickAt(host, cr.x + 5, cr.y + 5);
    host.update(800, 600);
    REQUIRE(findByKey(host.root(), "list") != nullptr);

    // Click bottom-right corner — far from the popup, on the backdrop.
    clickAt(host, 790, 590);
    host.update(800, 600);

    CHECK(findByKey(host.root(), "list") == nullptr);  // closed
    CHECK(changed == -99);                             // no selection
    CHECK(parentClicks == 0);                          // backdrop consumed it
}

// ---------------------------------------------------------------------------
// Toggle: a second click on the control coords while open lands on the backdrop
// first (portal hit-order is above the tree) and closes the popup.
// ---------------------------------------------------------------------------

TEST_CASE("Select - a second click on the control while open closes the popup") {
    Host host;
    std::vector<std::string> opts = {"Apple", "Banana", "Cherry"};

    auto App = [&](ComponentContext& ctx) -> VNode {
        (void)ctx;
        return Box(widgets::Select(opts).value(-1)).width(800).height(600).padding(40);
    };
    host.setRender(Component(App));
    host.update(800, 600);

    Node* control = findByKey(host.root(), "control");
    auto cr = absoluteRect(control);
    clickAt(host, cr.x + 5, cr.y + 5);
    host.update(800, 600);
    REQUIRE(findByKey(host.root(), "list") != nullptr);

    // Same control coords again: the backdrop (above the tree) catches it.
    clickAt(host, cr.x + 5, cr.y + 5);
    host.update(800, 600);
    CHECK(findByKey(host.root(), "list") == nullptr);
}

// ---------------------------------------------------------------------------
// Bottom-edge placement: a control near the window bottom makes placePanel
// shift the popup UP (move-don't-shrink), so its top is ABOVE the control's
// bottom (mirrors the tooltip bottom-edge placement).
// ---------------------------------------------------------------------------

TEST_CASE("Select - near the bottom edge the popup is shifted up") {
    Host host;
    std::vector<std::string> opts = {"Apple", "Banana", "Cherry", "Date", "Elderberry"};

    auto App = [&](ComponentContext& ctx) -> VNode {
        (void)ctx;
        // Push the control to the very bottom: a tall spacer then the Select.
        std::vector<Child> kids;
        kids.push_back(Box().width(100).height(560).setKey("filler"));
        kids.push_back(widgets::Select(opts).value(-1));
        return Box(std::move(kids)).width(800).height(600).flexDirection(FlexDirection::Column);
    };
    host.setRender(Component(App));
    host.update(800, 600);

    Node* control = findByKey(host.root(), "control");
    auto cr = absoluteRect(control);
    clickAt(host, cr.x + 5, cr.y + 5);
    host.update(800, 600);

    Node* list = findByKey(host.root(), "list");
    REQUIRE(list != nullptr);
    auto lr = absoluteRect(list);
    // Shifted up: the popup top is above the control's bottom edge.
    CHECK(lr.y < cr.y + cr.h);
}

// ---------------------------------------------------------------------------
// Long list: more options than fit in maxListHeight -> the panel height is
// capped and the options live under a Scroll node.
// ---------------------------------------------------------------------------

TEST_CASE("Select - a long list caps the panel height and scrolls the options") {
    Host host;
    std::vector<std::string> opts;
    for (int i = 0; i < 40; ++i)
        opts.push_back("Option " + std::to_string(i));
    const float cap = 120.0f;

    auto App = [&](ComponentContext& ctx) -> VNode {
        (void)ctx;
        return Box(widgets::Select(opts).value(-1).maxListHeight(cap)).width(800).height(600).padding(40);
    };
    host.setRender(Component(App));
    host.update(800, 600);

    Node* control = findByKey(host.root(), "control");
    auto cr = absoluteRect(control);
    clickAt(host, cr.x + 5, cr.y + 5);
    host.update(800, 600);

    Node* list = findByKey(host.root(), "list");
    REQUIRE(list != nullptr);
    auto lr = absoluteRect(list);
    // 40 rows overflow the cap, so the panel is capped (allow the border).
    CHECK(lr.h <= doctest::Approx(cap + 4.0f));
    // And the options are inside a Scroll.
    CHECK(hasScroll(list));
}

// ---------------------------------------------------------------------------
// Keyboard opt-in: the popup is opened by CLICK (a closed Select has no key
// handler — keyboard opens nothing). With the list OPEN, Down moves the
// highlight, Enter commits the highlighted index; a reopen + Esc dismisses.
// ---------------------------------------------------------------------------

TEST_CASE("Select - keyboard nav on the OPEN list highlights, commits, and dismisses") {
    Host host;
    std::vector<std::string> opts = {"Apple", "Banana", "Cherry"};
    int changed = -99;

    auto App = [&](ComponentContext& ctx) -> VNode {
        (void)ctx;
        return Box(widgets::Select(opts)
                       .value(0)
                       .onChange([&](int i) { changed = i; })
                       .upKeyCode(kUp)
                       .downKeyCode(kDown)
                       .selectKeyCode(kEnter)
                       .dismissKeyCode(kEsc))
            .width(800)
            .height(600)
            .padding(40);
    };
    host.setRender(Component(App));
    host.update(800, 600);

    // Click opens the popup (highlight seeded from value = 0). The click also
    // leaves the control focused, so its open-only onKeyDown receives keys.
    Node* control = findByKey(host.root(), "control");
    auto cr = absoluteRect(control);
    clickAt(host, cr.x + 5, cr.y + 5);
    host.update(800, 600);
    REQUIRE(findByKey(host.root(), "list") != nullptr);
    host.focus(findByKey(host.root(), "control"));

    // Down moves the highlight to 1, Enter commits 1.
    host.handleKeyDown(kDown, KeyMod_None);
    host.update(800, 600);
    host.handleKeyDown(kEnter, KeyMod_None);
    host.update(800, 600);
    CHECK(changed == 1);
    CHECK(findByKey(host.root(), "list") == nullptr);  // commit closes

    // Reopen (by click) and dismiss with Esc: no new onChange.
    changed = -99;
    control = findByKey(host.root(), "control");
    cr = absoluteRect(control);
    clickAt(host, cr.x + 5, cr.y + 5);
    host.update(800, 600);
    REQUIRE(findByKey(host.root(), "list") != nullptr);
    host.focus(findByKey(host.root(), "control"));
    host.handleKeyDown(kEsc, KeyMod_None);
    host.update(800, 600);
    CHECK(findByKey(host.root(), "list") == nullptr);
    CHECK(changed == -99);
}

// ---------------------------------------------------------------------------
// Keyboard opt-OUT: with NO keycode configured, the control attaches no
// onKeyDown, so a key bubbles up to an app-level ancestor onKeyDown (the
// campaign no-black-holing invariant).
// ---------------------------------------------------------------------------

TEST_CASE("Select - with no keycodes a key bubbles to an app-level ancestor") {
    Host host;
    std::vector<std::string> opts = {"Apple", "Banana", "Cherry"};
    int appKeys = 0;

    auto App = [&](ComponentContext& ctx) -> VNode {
        (void)ctx;
        return Box(widgets::Select(opts).value(0))  // no keycode setters
            .width(800)
            .height(600)
            .padding(40)
            .onKeyDown([&](int, uint16_t, bool) { appKeys++; });
    };
    host.setRender(Component(App));
    host.update(800, 600);

    Node* control = findByKey(host.root(), "control");
    host.focus(control);
    host.handleKeyDown(kDown, KeyMod_None);
    CHECK(appKeys == 1);  // not black-holed at the control
}

// ---------------------------------------------------------------------------
// Defect 1 regression: a CLOSED Select WITH keycodes configured attaches NO
// onKeyDown, so an unmapped (or any) key reaches an app-level ancestor — no
// black-hole. Focusing the control without a click keeps the list closed.
// ---------------------------------------------------------------------------

TEST_CASE("Select - closed with keycodes set does not black-hole keys") {
    Host host;
    std::vector<std::string> opts = {"Apple", "Banana", "Cherry"};
    int appKeys = 0;

    auto App = [&](ComponentContext& ctx) -> VNode {
        (void)ctx;
        return Box(widgets::Select(opts)
                       .value(0)
                       .upKeyCode(kUp)
                       .downKeyCode(kDown)
                       .selectKeyCode(kEnter)
                       .dismissKeyCode(kEsc))
            .width(800)
            .height(600)
            .padding(40)
            .onKeyDown([&](int, uint16_t, bool) { appKeys++; });
    };
    host.setRender(Component(App));
    host.update(800, 600);

    // Focus the control WITHOUT clicking, so the list stays closed.
    Node* control = findByKey(host.root(), "control");
    host.focus(control);
    CHECK(findByKey(host.root(), "list") == nullptr);  // still closed

    // A key with keycodes configured but list closed must reach the app.
    host.handleKeyDown(kDown, KeyMod_None);
    CHECK(appKeys == 1);  // no black-hole while closed
}

// ---------------------------------------------------------------------------
// Defect 1 (open side): an OPEN Select DOES consume keys — an unmapped key
// while open does NOT reach the app ancestor (nav capture is intentional).
// ---------------------------------------------------------------------------

TEST_CASE("Select - open consumes keys so an unmapped key does not reach the app") {
    Host host;
    std::vector<std::string> opts = {"Apple", "Banana", "Cherry"};
    int appKeys = 0;

    auto App = [&](ComponentContext& ctx) -> VNode {
        (void)ctx;
        return Box(widgets::Select(opts)
                       .value(0)
                       .upKeyCode(kUp)
                       .downKeyCode(kDown)
                       .selectKeyCode(kEnter)
                       .dismissKeyCode(kEsc))
            .width(800)
            .height(600)
            .padding(40)
            .onKeyDown([&](int, uint16_t, bool) { appKeys++; });
    };
    host.setRender(Component(App));
    host.update(800, 600);

    // Open by click, keep the control focused.
    Node* control = findByKey(host.root(), "control");
    auto cr = absoluteRect(control);
    clickAt(host, cr.x + 5, cr.y + 5);
    host.update(800, 600);
    REQUIRE(findByKey(host.root(), "list") != nullptr);
    host.focus(findByKey(host.root(), "control"));

    // An unmapped key (not up/down/enter/esc) is consumed by the open handler.
    constexpr int kUnmapped = 65;  // 'A'
    host.handleKeyDown(kUnmapped, KeyMod_None);
    CHECK(appKeys == 0);  // open Select captures every key
}

// ---------------------------------------------------------------------------
// Defect 2 regression: clicking the ALREADY-selected option fires no onChange
// (the Radio/Slider dedup contract) but STILL closes the popup.
// ---------------------------------------------------------------------------

TEST_CASE("Select - clicking the already-selected option closes without onChange") {
    Host host;
    std::vector<std::string> opts = {"Apple", "Banana", "Cherry"};
    int changed = -99;

    auto App = [&](ComponentContext& ctx) -> VNode {
        (void)ctx;
        // value = 1 (Banana) is the current selection.
        return Box(widgets::Select(opts).value(1).onChange([&](int i) { changed = i; }))
            .width(800)
            .height(600)
            .padding(40);
    };
    host.setRender(Component(App));
    host.update(800, 600);

    Node* control = findByKey(host.root(), "control");
    auto cr = absoluteRect(control);
    clickAt(host, cr.x + 5, cr.y + 5);
    host.update(800, 600);

    Node* opt1 = findByKey(host.root(), "1");  // the already-selected Banana
    REQUIRE(opt1 != nullptr);
    auto o1 = absoluteRect(opt1);
    clickAt(host, o1.x + 5, o1.y + 5);
    host.update(800, 600);

    CHECK(changed == -99);                             // no onChange on re-select
    CHECK(findByKey(host.root(), "list") == nullptr);  // but the popup closed
}
