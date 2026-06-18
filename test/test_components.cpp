#include <yui/yui.hpp>

#include "doctest.h"

#include <stdexcept>
#include <string>

using namespace yui;

// Test helper: minimal Host subclass
class TestHost : public Host {
public:
    TestHost() = default;
};

TEST_CASE("Component - basic mount and render") {
    TestHost host;

    auto SimpleComp = [](ComponentContext& ctx) {
        return Box().width(100).height(50);
    };

    host.setRender(std::function<VNode()>([&]() {
        return Box({Component(SimpleComp)});
    }));

    auto result = host.update(200, 200);

    REQUIRE(host.root() != nullptr);
    REQUIRE(result.needsRepaint);

    // Root should be Box wrapper
    REQUIRE(host.root()->type() == PrimitiveType::Box);
    auto* rootBox = static_cast<BoxNode*>(host.root());

    // Component is transparent — its output is directly a child of rootBox
    REQUIRE(rootBox->children.size() == 1);
    REQUIRE(rootBox->children[0]->type() == PrimitiveType::Box);

    auto* innerBox = static_cast<BoxNode*>(rootBox->children[0].get());
    CHECK(innerBox->layout.width == 100);
    CHECK(innerBox->layout.height == 50);
}

TEST_CASE("Component - useState initial value") {
    TestHost host;
    int renderCount = 0;

    auto Counter = [&](ComponentContext& ctx) {
        renderCount++;
        auto [count, setCount] = ctx.useState<int>(5);
        return Text(std::to_string(count));
    };

    host.setRender(std::function<VNode()>([&]() {
        return Box({Component(Counter)});
    }));

    host.update(200, 200);

    CHECK(renderCount == 1);

    // Component output is directly a child of rootBox
    auto* rootBox = static_cast<BoxNode*>(host.root());
    auto* textNode = static_cast<TextNode*>(rootBox->children[0].get());
    CHECK(textNode->props.text == "5");
}

TEST_CASE("Component - useState setter triggers re-render") {
    TestHost host;
    int renderCount = 0;
    std::function<void(int)> setCount;

    auto Counter = [&](ComponentContext& ctx) {
        renderCount++;
        auto [count, setter] = ctx.useState<int>(0);
        setCount = setter;
        return Text(std::to_string(count));
    };

    host.setRender(std::function<VNode()>([&]() {
        return Box({Component(Counter)});
    }));

    host.update(200, 200);
    CHECK(renderCount == 1);

    // Trigger state update
    setCount(42);

    CHECK(host.isDirty() == false);
    CHECK(host.needsUpdate() == true);

    auto result = host.update(200, 200);
    CHECK(result.needsRepaint);
    CHECK(renderCount == 2);

    auto* rootBox = static_cast<BoxNode*>(host.root());
    auto* textNode = static_cast<TextNode*>(rootBox->children[0].get());
    CHECK(textNode->props.text == "42");
}

TEST_CASE("Component - useState preserves state across re-renders") {
    TestHost host;
    int renderCount = 0;
    std::function<void(int)> setCount;

    auto Counter = [&](ComponentContext& ctx) {
        renderCount++;
        auto [count, setter] = ctx.useState<int>(0);
        setCount = setter;
        return Text(std::to_string(count));
    };

    host.setRender(std::function<VNode()>([&]() {
        return Box({Component(Counter)});
    }));

    host.update(200, 200);
    setCount(10);
    host.update(200, 200);
    setCount(20);
    host.update(200, 200);

    CHECK(renderCount == 3);

    auto* rootBox = static_cast<BoxNode*>(host.root());
    auto* textNode = static_cast<TextNode*>(rootBox->children[0].get());
    CHECK(textNode->props.text == "20");
}

TEST_CASE("Component - useRef does not trigger re-render") {
    TestHost host;
    int renderCount = 0;

    auto RefComponent = [&](ComponentContext& ctx) {
        renderCount++;
        int& counter = ctx.useRef<int>(0);
        counter++; // Mutate ref on every render
        return Text(std::to_string(counter));
    };

    host.setRender(std::function<VNode()>([&]() {
        return Box({Component(RefComponent)});
    }));

    host.update(200, 200);
    CHECK(renderCount == 1);

    // Ref mutation doesn't trigger re-render
    host.update(200, 200);
    CHECK(renderCount == 1); // Still 1, no re-render
}

TEST_CASE("Component - useRef persists across re-renders triggered by useState") {
    TestHost host;
    std::function<void(int)> setState;

    auto Comp = [&](ComponentContext& ctx) {
        auto [state, setter] = ctx.useState<int>(0);
        setState = setter;

        int& refValue = ctx.useRef<int>(100);
        refValue += 10; // Increment ref on each render

        return Text(std::to_string(refValue));
    };

    host.setRender(std::function<VNode()>([&]() {
        return Box({yui::Component(Comp)});
    }));

    host.update(200, 200);
    auto* rootBox = static_cast<BoxNode*>(host.root());
    auto* textNode = static_cast<TextNode*>(rootBox->children[0].get());
    CHECK(textNode->props.text == "110"); // 100 + 10

    setState(1);
    host.update(200, 200);
    CHECK(textNode->props.text == "120"); // 110 + 10

    setState(2);
    host.update(200, 200);
    CHECK(textNode->props.text == "130"); // 120 + 10
}

TEST_CASE("Component - useEffect runs after mount") {
    TestHost host;
    bool effectRan = false;

    auto EffectComponent = [&](ComponentContext& ctx) {
        ctx.useEffect([&]() {
            effectRan = true;
            return nullptr; // No cleanup
        });
        return Box();
    };

    host.setRender(std::function<VNode()>([&]() {
        return Box({Component(EffectComponent)});
    }));

    CHECK(effectRan == false);
    host.update(200, 200);
    CHECK(effectRan == true);
}

TEST_CASE("Component - useEffect cleanup runs on unmount") {
    TestHost host;
    bool effectRan = false;
    bool cleanupRan = false;
    Store<bool> showComponent(true);

    auto EffectComponent = [&](ComponentContext& ctx) {
        ctx.useEffect([&]() {
            effectRan = true;
            return [&]() {
                cleanupRan = true;
            };
        });
        return Box();
    };

    host.setRender(std::function<VNode()>([&]() {
        if (showComponent.use()) {
            return Box({Component(EffectComponent)});
        }
        return Box();
    }));

    host.update(200, 200);
    CHECK(effectRan == true);
    CHECK(cleanupRan == false);

    // Unmount component by changing what we render
    showComponent.set(false);
    host.update(200, 200);
    CHECK(cleanupRan == true);
}

TEST_CASE("Component - multiple effects run in order") {
    TestHost host;
    std::vector<int> effectOrder;
    std::vector<int> cleanupOrder;
    Store<bool> showComponent(true);

    auto MultiEffectComponent = [&](ComponentContext& ctx) {
        ctx.useEffect([&]() {
            effectOrder.push_back(1);
            return [&]() { cleanupOrder.push_back(1); };
        });

        ctx.useEffect([&]() {
            effectOrder.push_back(2);
            return [&]() { cleanupOrder.push_back(2); };
        });

        ctx.useEffect([&]() {
            effectOrder.push_back(3);
            return nullptr; // No cleanup
        });

        return Box();
    };

    host.setRender(std::function<VNode()>([&]() {
        if (showComponent.use()) {
            return Box({Component(MultiEffectComponent)});
        }
        return Box();
    }));

    host.update(200, 200);
    CHECK(effectOrder == std::vector<int>{1, 2, 3});

    showComponent.set(false);
    host.update(200, 200);
    CHECK(cleanupOrder == std::vector<int>{1, 2});
}

TEST_CASE("Component - Store subscription triggers selective re-render") {
    TestHost host;
    Store<int> counter(0);

    int comp1RenderCount = 0;
    int comp2RenderCount = 0;
    int topRenderCount = 0;

    auto Component1 = [&](ComponentContext& ctx) {
        comp1RenderCount++;
        int count = counter.use(); // Subscribe
        return Text(std::to_string(count));
    };

    auto Component2 = [&](ComponentContext& ctx) {
        comp2RenderCount++;
        // Does NOT subscribe to counter
        return Text("static");
    };

    host.setRender(std::function<VNode()>([&]() {
        topRenderCount++;
        return Box({
            Component(Component1),
            Component(Component2)
        });
    }));

    host.update(200, 200);
    CHECK(topRenderCount == 1);
    CHECK(comp1RenderCount == 1);
    CHECK(comp2RenderCount == 1);

    // Change store - should only re-render Component1
    counter.set(42);

    CHECK(host.isDirty() == false);
    CHECK(host.needsUpdate() == true);

    auto result = host.update(200, 200);
    CHECK(result.needsRepaint);

    CHECK(topRenderCount == 1);
    CHECK(comp1RenderCount == 2);
    CHECK(comp2RenderCount == 1);

    // Component output is directly in render tree
    auto* boxNode = static_cast<BoxNode*>(host.root());
    auto* text1Node = static_cast<TextNode*>(boxNode->children[0].get());
    CHECK(text1Node->props.text == "42");
}

TEST_CASE("Component - Store subscription cleanup prevents use-after-free") {
    Store<int> counter(0);
    int renderCount = 0;

    {
        TestHost host;

        auto Comp = [&](ComponentContext& ctx) {
            renderCount++;
            int count = counter.use(); // Subscribe
            return Text(std::to_string(count));
        };

        host.setRender(std::function<VNode()>([&]() {
            return Box({yui::Component(Comp)});
        }));

        host.update(200, 200);
        CHECK(renderCount == 1);
    }

    // Store should have cleaned up subscription
    renderCount = 0;
    counter.set(99);
    CHECK(renderCount == 0);
}

TEST_CASE("Component - nested components work correctly") {
    TestHost host;
    Store<int> counter(0);

    int innerRenderCount = 0;
    int outerRenderCount = 0;

    auto InnerComponent = [&](ComponentContext& ctx) {
        innerRenderCount++;
        int count = counter.use(); // Inner subscribes
        return Text(std::to_string(count));
    };

    auto OuterComponent = [&](ComponentContext& ctx) {
        outerRenderCount++;
        return Box({Component(InnerComponent)});
    };

    host.setRender(std::function<VNode()>([&]() {
        return Box({Component(OuterComponent)});
    }));

    host.update(200, 200);
    CHECK(outerRenderCount == 1);
    CHECK(innerRenderCount == 1);

    // Change store - should only re-render inner component
    counter.set(10);
    host.update(200, 200);

    CHECK(outerRenderCount == 1);
    CHECK(innerRenderCount == 2);
}

TEST_CASE("Component - returning empty VNode") {
    TestHost host;
    Store<bool> showContent(true);

    auto ConditionalComponent = [&](ComponentContext& ctx) {
        if (showContent.use()) {
            return Box().width(100).height(100);
        }
        VNode empty; empty.isEmpty = true; return empty;
    };

    host.setRender(std::function<VNode()>([&]() {
        return Box({Component(ConditionalComponent)});
    }));

    host.update(200, 200);
    auto* rootBox = static_cast<BoxNode*>(host.root());
    // Component returns Box, so rootBox has 1 child
    REQUIRE(rootBox->children.size() == 1);
    CHECK(rootBox->children[0]->type() == PrimitiveType::Box);

    // Return empty — component contributes no render nodes
    showContent.set(false);
    host.update(200, 200);

    REQUIRE(host.root() != nullptr);
    rootBox = static_cast<BoxNode*>(host.root());
    CHECK(rootBox->children.size() == 0);
}

TEST_CASE("Component - changing child type") {
    TestHost host;
    Store<bool> showBox(true);

    auto SwitchingComponent = [&](ComponentContext& ctx) {
        if (showBox.use()) {
            return Box().width(100).height(100);
        }
        return Text("Hello");
    };

    host.setRender(std::function<VNode()>([&]() {
        return Box({Component(SwitchingComponent)});
    }));

    host.update(200, 200);
    auto* rootBox = static_cast<BoxNode*>(host.root());
    CHECK(rootBox->children[0]->type() == PrimitiveType::Box);

    // Switch to Text
    showBox.set(false);
    host.update(200, 200);

    rootBox = static_cast<BoxNode*>(host.root());
    CHECK(rootBox->children[0]->type() == PrimitiveType::Text);
}

TEST_CASE("Component - multiple useState calls") {
    TestHost host;
    std::function<void(int)> setCount;
    std::function<void(std::string)> setName;

    auto MultiStateComponent = [&](ComponentContext& ctx) {
        auto [count, countSetter] = ctx.useState<int>(0);
        auto [name, nameSetter] = ctx.useState<std::string>("Alice");

        setCount = countSetter;
        setName = nameSetter;

        return Text(name + ": " + std::to_string(count));
    };

    host.setRender(std::function<VNode()>([&]() {
        return Box({Component(MultiStateComponent)});
    }));

    host.update(200, 200);
    auto* rootBox = static_cast<BoxNode*>(host.root());
    auto* textNode = static_cast<TextNode*>(rootBox->children[0].get());
    CHECK(textNode->props.text == "Alice: 0");

    setCount(5);
    host.update(200, 200);
    CHECK(textNode->props.text == "Alice: 5");

    setName("Bob");
    host.update(200, 200);
    CHECK(textNode->props.text == "Bob: 5");
}

TEST_CASE("Component - UpdateResult reflects component changes") {
    TestHost host;
    Store<int> counter(0);

    auto Comp = [&](ComponentContext& ctx) {
        int count = counter.use();
        return Text(std::to_string(count));
    };

    host.setRender(std::function<VNode()>([&]() {
        return Box({yui::Component(Comp)});
    }));

    auto result = host.update(200, 200);
    CHECK(result.needsRepaint);
    CHECK(result.layoutChanged);

    // Trigger component-only re-render
    counter.set(1);
    result = host.update(200, 200);
    CHECK(result.needsRepaint);
    CHECK(result.layoutChanged);

    // No changes
    result = host.update(200, 200);
    CHECK(result.needsRepaint == false);
    CHECK(result.layoutChanged == false);
}

TEST_CASE("Component - useState setter is a safe no-op after unmount") {
    // Regression for B3: the useState setter captures the fiber. A consumer may
    // store the setter so it outlives the component (here: a plain std::function
    // that survives the unmount), then invoke it after the component unmounts via
    // conditional render. Without a liveness guard the setter dereferenced the
    // freed fiber (use-after-free). It must now be a safe no-op.
    TestHost host;
    Store<bool> showComponent(true);
    int renderCount = 0;

    std::function<void(int)> storedSetter;  // outlives the component
    std::shared_ptr<bool> fiberAlive;       // the unmounted fiber's liveness token

    auto Counter = [&](ComponentContext& ctx) {
        renderCount++;
        auto [count, setter] = ctx.useState<int>(0);
        storedSetter = setter;          // escape: stored outside the fiber
        fiberAlive = ctx.fiber()->alive;  // observe the fiber's liveness from the test
        return Text(std::to_string(count));
    };

    host.setRender(std::function<VNode()>([&]() {
        if (showComponent.use()) {
            return Box({Component(Counter)});
        }
        return Box();
    }));

    host.update(200, 200);
    CHECK(renderCount == 1);
    CHECK(*fiberAlive == true);

    // Setter works while mounted.
    storedSetter(7);
    host.update(200, 200);
    CHECK(renderCount == 2);

    // Unmount the component via conditional render — its fiber is freed.
    showComponent.set(false);
    host.update(200, 200);

    // The fiber is genuinely dead: ~Fiber cleared its liveness token. This is the
    // precondition the stale setter must detect. (A sanitizer would flag the
    // pre-fix freed-memory access; this assertion locks in the death point even
    // without one.)
    CHECK(*fiberAlive == false);

    int renderCountAfterUnmount = renderCount;

    // Invoke the stale setter. Pre-fix this dereferenced the freed fiber. It must
    // now no-op: no crash, no re-render of the gone component.
    storedSetter(99);
    host.update(200, 200);
    CHECK(renderCount == renderCountAfterUnmount);
}

TEST_CASE("Component - a remounted sibling's setter still works after another unmounts") {
    // Liveness is per-fiber: unmounting one component must not poison a still-
    // mounted component's setter.
    TestHost host;
    Store<bool> showFirst(true);
    int liveRenderCount = 0;

    std::function<void(int)> staleSetter;  // from the component that unmounts
    std::function<void(int)> liveSetter;   // from the component that stays

    auto Vanishing = [&](ComponentContext& ctx) {
        auto [count, setter] = ctx.useState<int>(0);
        staleSetter = setter;
        return Text(std::to_string(count));
    };

    auto Persistent = [&](ComponentContext& ctx) {
        liveRenderCount++;
        auto [count, setter] = ctx.useState<int>(0);
        liveSetter = setter;
        return Text(std::to_string(count));
    };

    host.setRender(std::function<VNode()>([&]() {
        std::vector<Child> kids;
        if (showFirst.use()) {
            kids.push_back(Component(Vanishing));
        }
        kids.push_back(Component(Persistent));
        return Box(kids);
    }));

    host.update(200, 200);
    CHECK(liveRenderCount == 1);

    // Unmount the first component.
    showFirst.set(false);
    host.update(200, 200);

    // Stale setter no-ops.
    staleSetter(123);
    host.update(200, 200);

    // The still-mounted component's setter remains live.
    int before = liveRenderCount;
    liveSetter(5);
    host.update(200, 200);
    CHECK(liveRenderCount == before + 1);

    auto* rootBox = static_cast<BoxNode*>(host.root());
    REQUIRE(rootBox->children.size() == 1);
    auto* textNode = static_cast<TextNode*>(rootBox->children[0].get());
    CHECK(textNode->props.text == "5");
}

TEST_CASE("Component - useField binds to Store field") {
    struct FormState {
        std::string username;
        int age;
    };

    TestHost host;
    Store<FormState> formStore(FormState{"", 0});
    std::function<void(const std::string&)> setUsername;
    std::function<void(const int&)> setAge;

    auto FormComponent = [&](ComponentContext& ctx) {
        auto [username, setUser] = ctx.useField(formStore, &FormState::username);
        auto [age, setAgeVal] = ctx.useField(formStore, &FormState::age);

        setUsername = setUser;
        setAge = setAgeVal;

        return Text(username + " is " + std::to_string(age));
    };

    host.setRender(std::function<VNode()>([&]() {
        return Box({Component(FormComponent)});
    }));

    host.update(200, 200);
    auto* rootBox = static_cast<BoxNode*>(host.root());
    auto* textNode = static_cast<TextNode*>(rootBox->children[0].get());
    CHECK(textNode->props.text == " is 0");

    setUsername("Alice");
    host.update(200, 200);
    CHECK(textNode->props.text == "Alice is 0");

    setAge(25);
    host.update(200, 200);
    CHECK(textNode->props.text == "Alice is 25");

    CHECK(formStore.peek().username == "Alice");
    CHECK(formStore.peek().age == 25);
}

// ============================================================================
// Exception-safety contract (B2 commit 2): a throwing user callback is isolated,
// routed to the host error sink, and never corrupts the tree or escapes update().
// ============================================================================

TEST_CASE("Exception - throwing component fn is isolated and tree stays intact") {
    TestHost host;
    Store<int> counter(0);

    int errorCount = 0;
    std::string errorWhere;
    host.setErrorHandler([&](std::string_view where, const std::exception*) {
        errorCount++;
        errorWhere = std::string(where);
    });

    int renderCount = 0;
    auto Throwing = [&](ComponentContext& ctx) -> VNode {
        renderCount++;
        // Subscribe FIRST (hooks before any throw), then throw on the 2nd render.
        int value = counter.use();
        if (renderCount >= 2) {
            throw std::runtime_error("boom");
        }
        return Text("ok:" + std::to_string(value));
    };

    host.setRender(std::function<VNode()>([&]() {
        return Box({Component(Throwing)});
    }));

    // First render succeeds: good subtree present.
    host.update(200, 200);
    REQUIRE(renderCount == 1);
    auto* rootBox = static_cast<BoxNode*>(host.root());
    REQUIRE(rootBox->children.size() == 1);
    REQUIRE(rootBox->children[0]->type() == PrimitiveType::Text);
    CHECK(static_cast<TextNode*>(rootBox->children[0].get())->props.text == "ok:0");

    // Trigger a selective re-render that throws. update() must NOT throw, and the
    // previous good subtree must remain unchanged.
    counter.set(5);
    CHECK(host.needsUpdate() == true);
    CHECK_NOTHROW(host.update(200, 200));
    CHECK(renderCount == 2);

    CHECK(errorCount == 1);
    CHECK(errorWhere == "rerenderComponent");

    // Previous good child subtree intact (root children unchanged, still the old Text).
    rootBox = static_cast<BoxNode*>(host.root());
    REQUIRE(rootBox->children.size() == 1);
    REQUIRE(rootBox->children[0]->type() == PrimitiveType::Text);
    CHECK(static_cast<TextNode*>(rootBox->children[0].get())->props.text == "ok:0");

    // The fiber's own dirty flag was NOT falsely cleared by the failed pass
    // (rerenderComponent leaves it set on the catch path). We observe this
    // indirectly: the fiber is still subscribed, so mutating the store re-arms the
    // host and re-renders the fiber — proving the failed render's use() left the
    // store membership intact (the subscription survived the throw).
    counter.set(7);
    CHECK(host.needsUpdate() == true);
    CHECK_NOTHROW(host.update(200, 200));
    CHECK(renderCount == 3);          // re-rendered again => still subscribed
    CHECK(errorCount == 2);           // and still isolated
    // Tree still the original good Text — never corrupted by either throw.
    rootBox = static_cast<BoxNode*>(host.root());
    REQUIRE(rootBox->children.size() == 1);
    CHECK(static_cast<TextNode*>(rootBox->children[0].get())->props.text == "ok:0");
}

TEST_CASE("Exception - throw BEFORE use() keeps subscription (re-render still fires)") {
    // The B2 subscription-rollback bug. A component subscribes to a Store on render
    // 1, then on render 2 throws BEFORE calling use(). Store::set() triggers render
    // 2: notify() has already removed the fiber from the store's subscriber set, and
    // the throwing render never re-subscribes. The fix must re-arm the pre-render
    // subscription on the failure path, so a LATER set() still re-renders the fiber.
    //
    // Pre-fix this FAILED: the component was permanently dead to the store (the catch
    // only resized the cleanups vector, never re-establishing membership), so the
    // second set() never re-rendered it (renderCount stuck at 2).
    TestHost host;
    Store<int> counter(0);

    int errorCount = 0;
    host.setErrorHandler([&](std::string_view, const std::exception*) { errorCount++; });

    int renderCount = 0;
    bool armed = true;  // controls whether render 2 throws before use()
    auto Throwing = [&](ComponentContext& ctx) -> VNode {
        renderCount++;
        if (renderCount >= 2 && armed) {
            throw std::runtime_error("boom before use");  // THROW BEFORE use()
        }
        int value = counter.use();  // subscribe (render 1, and re-renders once disarmed)
        return Text("ok:" + std::to_string(value));
    };

    host.setRender(std::function<VNode()>([&]() {
        return Box({Component(Throwing)});
    }));

    // Render 1 subscribes.
    host.update(200, 200);
    REQUIRE(renderCount == 1);

    // Render 2 throws before use(). update() must not throw; error isolated.
    counter.set(5);
    CHECK(host.needsUpdate() == true);
    CHECK_NOTHROW(host.update(200, 200));
    CHECK(renderCount == 2);
    CHECK(errorCount == 1);

    // Disarm and set() AGAIN. The component must STILL be subscribed -> re-renders.
    armed = false;
    counter.set(9);
    CHECK(host.needsUpdate() == true);  // proves the fiber is still a subscriber
    CHECK_NOTHROW(host.update(200, 200));
    CHECK(renderCount == 3);            // re-rendered => subscription survived the throw

    auto* rootBox = static_cast<BoxNode*>(host.root());
    REQUIRE(rootBox->children.size() == 1);
    CHECK(static_cast<TextNode*>(rootBox->children[0].get())->props.text == "ok:9");
}

TEST_CASE("Exception - throw AFTER use() leaves exactly one live subscription") {
    // Companion to the throw-before-use case: subscribing then throwing must leave a
    // single, live subscription — one re-render per later set(), no double-subscribe.
    TestHost host;
    Store<int> counter(0);

    int errorCount = 0;
    host.setErrorHandler([&](std::string_view, const std::exception*) { errorCount++; });

    int renderCount = 0;
    bool armed = true;
    auto Throwing = [&](ComponentContext& ctx) -> VNode {
        renderCount++;
        int value = counter.use();  // subscribe FIRST
        if (renderCount >= 2 && armed) {
            throw std::runtime_error("boom after use");
        }
        return Text("ok:" + std::to_string(value));
    };

    host.setRender(std::function<VNode()>([&]() {
        return Box({Component(Throwing)});
    }));

    host.update(200, 200);
    REQUIRE(renderCount == 1);

    counter.set(5);
    CHECK_NOTHROW(host.update(200, 200));
    CHECK(renderCount == 2);
    CHECK(errorCount == 1);

    // Disarm; each subsequent set() must re-render exactly once (no doubled sub).
    armed = false;
    counter.set(6);
    host.update(200, 200);
    CHECK(renderCount == 3);

    counter.set(7);
    host.update(200, 200);
    CHECK(renderCount == 4);
}

TEST_CASE("Component - successful re-render keeps exactly one subscription") {
    // Several successful re-renders to the same store, then a single set(): must
    // re-render exactly once — no lost subscription, no double-subscribe (which
    // would either drop notifications or fire markDirty twice).
    TestHost host;
    Store<int> counter(0);

    int renderCount = 0;
    auto Comp = [&](ComponentContext& ctx) {
        renderCount++;
        int v = counter.use();
        return Text(std::to_string(v));
    };

    host.setRender(std::function<VNode()>([&]() {
        return Box({Component(Comp)});
    }));

    host.update(200, 200);
    CHECK(renderCount == 1);

    // Drive several successful re-renders via set().
    counter.set(1);
    host.update(200, 200);
    counter.set(2);
    host.update(200, 200);
    counter.set(3);
    host.update(200, 200);
    CHECK(renderCount == 4);

    // One more set() -> exactly one further re-render (subscription is single + live).
    counter.set(4);
    CHECK(host.needsUpdate() == true);
    host.update(200, 200);
    CHECK(renderCount == 5);

    auto* rootBox = static_cast<BoxNode*>(host.root());
    CHECK(static_cast<TextNode*>(rootBox->children[0].get())->props.text == "4");
}

TEST_CASE("Component - conditional subscription tears down old store, no stale notify") {
    // A component subscribes to store A on some renders and store B on others. After
    // a successful render that switches A -> B, only B is live: a later A.set() must
    // NOT re-render it (stale subscription torn down), and a B.set() must.
    TestHost host;
    Store<int> a(0);
    Store<int> b(0);

    int renderCount = 0;
    bool useA = true;  // which store the component subscribes to this render
    auto Comp = [&](ComponentContext& ctx) {
        renderCount++;
        int v = useA ? a.use() : b.use();
        return Text(std::to_string(v));
    };

    host.setRender(std::function<VNode()>([&]() {
        return Box({Component(Comp)});
    }));

    // Render 1: subscribed to A.
    host.update(200, 200);
    CHECK(renderCount == 1);

    // A.set() re-renders (subscribed to A). During this render, switch to B.
    useA = false;
    a.set(1);
    host.update(200, 200);
    CHECK(renderCount == 2);  // re-rendered, now subscribed to B (A torn down)

    // A is no longer a live subscription: A.set() must NOT re-render.
    int before = renderCount;
    a.set(2);
    CHECK(host.needsUpdate() == false);  // no subscriber armed by the A write
    host.update(200, 200);
    CHECK(renderCount == before);        // stale A subscription was torn down

    // B IS live: B.set() re-renders exactly once.
    b.set(5);
    CHECK(host.needsUpdate() == true);
    host.update(200, 200);
    CHECK(renderCount == before + 1);

    auto* rootBox = static_cast<BoxNode*>(host.root());
    CHECK(static_cast<TextNode*>(rootBox->children[0].get())->props.text == "5");
}

TEST_CASE("Exception - throwing effect body is isolated, other effects still run") {
    TestHost host;

    int errorCount = 0;
    std::string errorWhere;
    host.setErrorHandler([&](std::string_view where, const std::exception*) {
        errorCount++;
        errorWhere = std::string(where);
    });

    bool effect1Ran = false;
    bool effect3Ran = false;

    auto Comp = [&](ComponentContext& ctx) {
        ctx.useEffect([&]() {
            effect1Ran = true;
            return nullptr;
        });
        ctx.useEffect([&]() -> std::function<void()> {
            throw std::runtime_error("effect boom");
        });
        ctx.useEffect([&]() {
            effect3Ran = true;
            return nullptr;
        });
        return Box();
    };

    host.setRender(std::function<VNode()>([&]() {
        return Box({Component(Comp)});
    }));

    CHECK_NOTHROW(host.update(200, 200));

    // The throwing middle effect did not abort the loop.
    CHECK(effect1Ran == true);
    CHECK(effect3Ran == true);
    CHECK(errorCount == 1);
    CHECK(errorWhere == "effect");
}

TEST_CASE("Exception - throwing cleanup is isolated; ~Host teardown does not crash") {
    Store<bool> show(true);

    int errorCount = 0;
    bool goodCleanupRan = false;

    {
        TestHost host;
        host.setErrorHandler([&](std::string_view, const std::exception*) { errorCount++; });

        auto Comp = [&](ComponentContext& ctx) {
            // A throwing cleanup and a well-behaved cleanup on the same fiber.
            ctx.useEffect([&]() -> std::function<void()> {
                return [&]() { throw std::runtime_error("cleanup boom"); };
            });
            ctx.useEffect([&]() -> std::function<void()> {
                return [&]() { goodCleanupRan = true; };
            });
            return Box();
        };

        host.setRender(std::function<VNode()>([&]() {
            if (show.use()) {
                return Box({Component(Comp)});
            }
            return Box();
        }));

        host.update(200, 200);

        // Unmount via conditional render: cleanups run during reconcile. The
        // throwing cleanup must not abort the sibling cleanup nor escape.
        show.set(false);
        CHECK_NOTHROW(host.update(200, 200));
        CHECK(goodCleanupRan == true);
        CHECK(errorCount >= 1);

        // Remount, then let ~Host run teardown cleanups with a throwing cleanup
        // present — must not terminate during destruction.
        show.set(true);
        host.update(200, 200);
    }  // ~Host here runs willUnmount(); a throwing cleanup must be swallowed.

    // Reaching here at all proves ~Host did not crash on the throwing cleanup.
    CHECK(errorCount >= 1);
}
