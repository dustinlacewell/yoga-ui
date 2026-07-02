#include <yui/yui.hpp>

#include "doctest.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

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

    auto ConditionalComponent = [&](ComponentContext& ctx) -> VNode {
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

    auto SwitchingComponent = [&](ComponentContext& ctx) -> VNode {
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

// ============================================================================
// UpdateStatus: the three all-false early-returns must be distinguishable from a
// steady-state no-op, and each must emit one diagnostic through the error sink.
// ============================================================================

TEST_CASE("UpdateStatus - no render fn reports NoRenderFn and a diagnostic") {
    TestHost host;  // setRender never called -> render_ unset

    int errorCount = 0;
    std::string where;
    host.setErrorHandler([&](std::string_view w, const std::exception*) {
        errorCount++;
        where = std::string(w);
    });

    auto r = host.update(200, 200);
    CHECK(r.status == UpdateStatus::NoRenderFn);
    CHECK(r.needsRepaint == false);
    CHECK(errorCount == 1);
    CHECK(where == "Host::update: no render function set");

    // State-transition gating: a second update in the same state does not re-spam.
    auto r2 = host.update(200, 200);
    CHECK(r2.status == UpdateStatus::NoRenderFn);
    CHECK(errorCount == 1);
}

TEST_CASE("UpdateStatus - zero viewport reports ZeroViewport and a diagnostic") {
    TestHost host;
    host.setRender(std::function<VNode()>([&]() { return Box(); }));

    int errorCount = 0;
    std::string where;
    host.setErrorHandler([&](std::string_view w, const std::exception*) {
        errorCount++;
        where = std::string(w);
    });

    auto r = host.update(0, 200);  // width <= 0
    CHECK(r.status == UpdateStatus::ZeroViewport);
    CHECK(r.needsRepaint == false);
    CHECK(errorCount == 1);
    CHECK(where == "Host::update: viewport has non-positive dimensions");

    // A subsequent valid update returns Ok and clears the latch.
    auto ok = host.update(200, 200);
    CHECK(ok.status == UpdateStatus::Ok);

    // Re-entering the bad state re-fires the diagnostic (latch was cleared).
    auto r2 = host.update(-1, 200);
    CHECK(r2.status == UpdateStatus::ZeroViewport);
    CHECK(errorCount == 2);
}

TEST_CASE("UpdateStatus - empty render reports EmptyRender and a diagnostic") {
    TestHost host;
    host.setRender(std::function<VNode()>([&]() { return VNode::empty(); }));

    int errorCount = 0;
    std::string where;
    host.setErrorHandler([&](std::string_view w, const std::exception*) {
        errorCount++;
        where = std::string(w);
    });

    auto r = host.update(200, 200);
    CHECK(r.status == UpdateStatus::EmptyRender);
    CHECK(r.needsRepaint == false);
    CHECK(errorCount == 1);
    CHECK(where == "Host::update: root rendered empty");
}

TEST_CASE("UpdateStatus - normal update reports Ok with no diagnostic") {
    TestHost host;
    host.setRender(std::function<VNode()>([&]() { return Box().width(100).height(50); }));

    int errorCount = 0;
    host.setErrorHandler([&](std::string_view, const std::exception*) { errorCount++; });

    auto r = host.update(200, 200);
    CHECK(r.status == UpdateStatus::Ok);
    CHECK(r.needsRepaint == true);
    CHECK(errorCount == 0);
}

TEST_CASE("UpdateStatus - reentrant update() from a callback is ignored, not crashed") {
    // update() walks the fiber/render trees holding raw pointers; a nested update()
    // (here from inside the click handler that fires during the outer update's event
    // dispatch path is simulated by calling update() from the render fn) would mutate
    // the very trees the outer walk reads. The reentrancy latch must turn the inner
    // call into a diagnosed no-op while the outer update completes normally.
    TestHost host;

    int errorCount = 0;
    std::string where;
    host.setErrorHandler([&](std::string_view w, const std::exception*) {
        errorCount++;
        where = std::string(w);
    });

    // The render fn re-enters update() once, capturing the inner result. The inner
    // call happens while the outer update() is mid-flight (render_ is being invoked),
    // so it is the genuine reentrant case the latch guards against.
    UpdateResult innerResult;
    bool reentered = false;
    host.setRender(std::function<VNode()>([&]() -> VNode {
        if (!reentered) {
            reentered = true;
            innerResult = host.update(200, 200);  // reentrant — must be ignored
        }
        return Box().width(100).height(50);
    }));

    // Outer update completes normally despite the nested call.
    auto outer = host.update(200, 200);
    CHECK(reentered);

    // Inner call was rejected: Reentrant status, all-false result, one diagnostic.
    CHECK(innerResult.status == UpdateStatus::Reentrant);
    CHECK(innerResult.needsRepaint == false);
    CHECK(innerResult.layoutChanged == false);
    CHECK(innerResult.animating == false);
    CHECK(errorCount == 1);
    CHECK(where == "Host::update: reentrant call ignored");

    // Outer update ran to completion and produced a real tree — no crash/corruption.
    CHECK(outer.status == UpdateStatus::Ok);
    CHECK(outer.needsRepaint == true);
    REQUIRE(host.root() != nullptr);
    CHECK(host.root()->type() == PrimitiveType::Box);

    // Latch is fully released afterward: a fresh, top-level update() is accepted.
    auto after = host.update(200, 200);
    CHECK(after.status == UpdateStatus::Ok);
}

TEST_CASE("Threading - Store::set marks atomic dirty flag, re-render on host thread") {
    // Single-threaded sanity for the atomic dirty flags. The host-subscriber path
    // (top-level use()) flips Host::dirty_; the fiber-subscriber path flips
    // Host::componentsDirty_. After making both atomic, both must still mark the
    // host dirty so the next update() on the host thread re-renders. This guards
    // the atomic change against breaking the normal (single-thread) path.
    TestHost host;
    Store<int> counter(0);

    int topRenderCount = 0;
    host.setRender(std::function<VNode()>([&]() {
        topRenderCount++;
        int n = counter.use();  // top-level use() -> subscribes the whole host
        return Box({Text(std::to_string(n))});
    }));

    host.update(200, 200);
    CHECK(topRenderCount == 1);
    CHECK(host.needsUpdate() == false);

    // set() flips the atomic dirty_ via the host-subscriber path.
    counter.set(7);
    CHECK(host.isDirty() == true);       // atomic dirty_ observed set
    CHECK(host.needsUpdate() == true);

    auto result = host.update(200, 200);
    CHECK(result.needsRepaint);
    CHECK(topRenderCount == 2);
    CHECK(host.needsUpdate() == false);  // cleared by update()

    auto* rootBox = static_cast<BoxNode*>(host.root());
    CHECK(static_cast<TextNode*>(rootBox->children[0].get())->props.text == "7");
}

TEST_CASE("Threading - cross-thread Store::set smoke test (no crash, change applied)") {
    // SMOKE TEST ONLY. Exercises the sanctioned cross-thread contract: a worker
    // thread calls Store::set() while the host thread later calls update(). The
    // atomic dirty flags make the flag write/read race-free; update() applies the
    // change on the host thread.
    //
    // LIMITATION: this CANNOT prove race-freedom. A real data-race detector (TSan)
    // is required for that, and TSan is unavailable on MinGW (the toolchain here).
    // The threads are serialised (worker joined before update()), so this verifies
    // the contract's *semantics* (off-thread set is visible at the next update,
    // no crash), not the absence of a race under true concurrency.
    TestHost host;
    Store<int> counter(0);

    int renderCount = 0;
    host.setRender(std::function<VNode()>([&]() {
        renderCount++;
        int n = counter.use();
        return Box({Text(std::to_string(n))});
    }));

    host.update(200, 200);
    REQUIRE(renderCount == 1);
    REQUIRE(host.needsUpdate() == false);

    // Off-thread write, then join before touching the host (single sanctioned op).
    std::thread worker([&] { counter.set(123); });
    worker.join();

    // The dirty flag was marked from the worker thread; the host thread applies it.
    CHECK(host.needsUpdate() == true);
    CHECK_NOTHROW(host.update(200, 200));
    CHECK(renderCount == 2);

    auto* rootBox = static_cast<BoxNode*>(host.root());
    CHECK(static_cast<TextNode*>(rootBox->children[0].get())->props.text == "123");
}

// Run `body` on a worker and fail (rather than hang the whole suite) if it does
// not finish within `timeout`. A deadlocked Store::set() would otherwise wedge
// the test process forever; this turns "hung" into "failed" with a clean report.
// On timeout the worker is detached and left wedged — acceptable for a CI failure
// signal (the process exits non-zero); a passing run always joins cleanly.
static bool runsWithin(std::chrono::milliseconds timeout, const std::function<void()>& body) {
    auto done = std::make_shared<std::atomic<bool>>(false);
    std::thread worker([done, body] {
        body();
        done->store(true);
    });
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!done->load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (done->load()) {
        worker.join();
        return true;
    }
    worker.detach();  // wedged — leak the thread; the suite still reports failure
    return false;
}

TEST_CASE("Store - reentrant set() from a re-rendered subscriber does not deadlock") {
    // FIX #9 regression. Store::set() must NOT hold its mutex across the re-render
    // it triggers: notify() only flags subscribers dirty (deferred), and the value
    // mutation is the sole locked region. A component subscribed to a store that,
    // when re-rendered by that store, calls set()/peek()/use() on the SAME store
    // would self-deadlock if set() serialised user code under a non-recursive lock.
    //
    // The set inside render is GUARDED (fires once) so this exercises reentrancy
    // without the per-frame livelock of an unconditional set — see the livelock
    // test below for the unconditional case.
    bool finished = runsWithin(std::chrono::seconds(5), [] {
        TestHost host;
        Store<int> counter(0);
        bool bumped = false;
        int renderCount = 0;

        auto Comp = [&](ComponentContext& ctx) {
            renderCount++;
            int n = counter.use();             // subscribe + read under no Store lock
            if (n == 0 && !bumped) {
                bumped = true;
                counter.peek();                // reentrant read — locks mutex_ again
                counter.set(n + 1);            // reentrant write — re-enters set()
            }
            return Text(std::to_string(n));
        };

        host.setRender(std::function<VNode()>([&]() { return Box({Component(Comp)}); }));

        host.update(200, 200);                 // initial render: bumps counter to 1
        host.update(200, 200);                 // applies the deferred re-render
        host.update(200, 200);                 // steady state (guard stops further sets)

        auto* box = static_cast<BoxNode*>(host.root());
        CHECK(static_cast<TextNode*>(box->children[0].get())->props.text == "1");
        CHECK(renderCount >= 2);
    });
    REQUIRE(finished);  // false == the operation hung (deadlock regression)
}

TEST_CASE("Store - unconditional set() during render is diagnosed once (livelock guard)") {
    // FIX #10 regression. A component whose body calls set() unconditionally re-
    // dirties its own fiber every frame -> per-frame re-render livelock. set()
    // detects the active render context (currentRenderFiber / currentRenderHost the
    // reconciler installs) and routes ONE deduped diagnostic through the host's
    // error sink. We diagnose-and-proceed: the set still applies; the warning is the
    // actionable signal. The latch is per-Store, so the diagnostic fires exactly
    // once across many frames rather than once per frame.
    TestHost host;
    Store<int> counter(0);

    int diagnostics = 0;
    std::string lastWhere;
    host.setErrorHandler([&](std::string_view where, const std::exception*) {
        diagnostics++;
        lastWhere = std::string(where);
    });

    auto Comp = [&](ComponentContext& ctx) {
        int n = counter.use();
        counter.set(n + 1);  // UNCONDITIONAL set in render: the livelock pattern
        return Text(std::to_string(n));
    };

    host.setRender(std::function<VNode()>([&]() { return Box({Component(Comp)}); }));

    // Several frames, each of which renders the component and hits the in-render set.
    for (int i = 0; i < 5; ++i) {
        host.update(200, 200);
    }

    CHECK(diagnostics == 1);  // deduped to a single report, not one per frame
    CHECK(lastWhere.find("during render") != std::string::npos);
}

TEST_CASE("Store - set() outside render emits no spurious diagnostic") {
    // The in-render detection must not false-positive on a normal set() made
    // outside any render (the common case: an event handler or worker thread).
    TestHost host;
    Store<int> counter(0);

    int diagnostics = 0;
    host.setErrorHandler([&](std::string_view, const std::exception*) { diagnostics++; });

    host.setRender(std::function<VNode()>([&]() {
        int n = counter.use();
        return Text(std::to_string(n));
    }));

    host.update(200, 200);
    counter.set(42);            // outside render — must NOT diagnose
    host.update(200, 200);

    CHECK(diagnostics == 0);
}

TEST_CASE("Store - concurrent set() vs fiber unmount does not use a freed subscriber") {
    // Phase-1 fiber-subscriber UAF regression. A component subscribes to a Store
    // (recorded in fiberSubscribers_). When that component unmounts, ~Fiber must
    // unsubscribe UNDER Store::mutex_ before freeing itself, and notify() must
    // verify each subscriber's weak liveness token UNDER the same mutex before
    // markDirty(). Together those close the window where a worker thread's set()
    // could mark a fiber that the host thread just destroyed.
    //
    // We drive it adversarially: a worker hammers set() in a tight loop while the
    // host thread repeatedly mounts and unmounts the subscribing component by
    // toggling it in/out of the tree. Pre-fix this races markDirty() against a
    // freed Fiber (heap-corruption UAF); post-fix it runs clean. runsWithin also
    // guards against a lock-hierarchy deadlock between mutex_ and liveHostsMutex.
    bool finished = runsWithin(std::chrono::seconds(10), [] {
        TestHost host;
        Store<int> counter(0);
        std::atomic<bool> stop{false};

        // Worker: pound set() from another thread for the whole test.
        std::thread worker([&] {
            while (!stop.load(std::memory_order_relaxed)) {
                counter.set([](int& n) { ++n; });
            }
        });

        auto Sub = [&](ComponentContext& ctx) {
            int n = counter.use();  // subscribe THIS fiber to the store
            return Text(std::to_string(n));
        };

        // Toggle the subscribing component in and out of the tree: each removal
        // unmounts (and frees) its fiber while the worker is still notifying.
        bool present = true;
        host.setRender(std::function<VNode()>([&]() -> VNode {
            if (present)
                return Box({Component(Sub)});
            return Box(std::vector<Child>{});
        }));

        for (int i = 0; i < 2000; ++i) {
            present = !present;
            host.markDirty();       // force a full re-render (structural toggle)
            host.update(200, 200);  // mount or unmount Sub's fiber
        }

        stop.store(true, std::memory_order_relaxed);
        worker.join();
    });
    REQUIRE(finished);  // false == deadlock; a UAF would crash the process instead
}

TEST_CASE("Store - set(mutator) re-entered from its own mutator is diagnosed, not deadlocked") {
    // Phase-1 reentrancy guard (E2: diagnose, don't deadlock). A mutator that
    // itself calls set(mutator) on the SAME store would re-lock the non-recursive
    // mutex_ and deadlock. The guard detects the reentry by thread id, routes ONE
    // diagnostic through the render-context sink, and drops the reentrant write.
    // runsWithin turns a deadlock regression into a clean failure instead of a hang.
    bool finished = runsWithin(std::chrono::seconds(5), [] {
        TestHost host;
        Store<int> counter(0);

        int diagnostics = 0;
        std::string lastWhere;
        host.setErrorHandler([&](std::string_view where, const std::exception*) {
            diagnostics++;
            lastWhere = std::string(where);
        });

        auto Comp = [&](ComponentContext& ctx) {
            int n = counter.use();
            if (n == 0) {
                // Reentrant set(mutator): the outer mutator's re-render runs this
                // body, which calls set(mutator) on the same store from within the
                // in-flight mutation. Must be dropped-and-diagnosed, not deadlock.
                counter.set([](int& v) {
                    // No further store access here — the reentry is the outer/inner
                    // set() pair below.
                    v += 1;
                });
                counter.set([&](int& /*v*/) {
                    counter.set([](int& inner) { inner += 100; });  // REENTRANT
                });
            }
            return Text(std::to_string(n));
        };

        host.setRender(std::function<VNode()>([&]() { return Box({Component(Comp)}); }));

        host.update(200, 200);
        host.update(200, 200);
        host.update(200, 200);

        // The reentrant inner set(+100) was dropped; only the outer +1 (and the
        // first set) applied. The exact value is not the contract — surviving
        // without deadlock and emitting the diagnostic is.
        CHECK(diagnostics >= 1);
        CHECK(lastWhere.find("mutator") != std::string::npos);
    });
    REQUIRE(finished);  // false == the reentrant set() deadlocked (regression)
}

// --- FIX #7 / #8: per-slot hook type tag (rules-of-hooks + any_cast guard) ---

TEST_CASE("Hooks - changing a hook's TYPE at a stable index is diagnosed (FIX #7)") {
    // A conditional that swaps WHICH hook runs at a fixed call-order index, keeping
    // the same total count. Render 1 puts a useState<int> at index 0; render 2 puts
    // a useState<std::string> at index 0. Pre-fix the slot's std::any_cast<string&>
    // would throw a context-free std::bad_any_cast. Now the per-slot type tag catches
    // it first: a "rules-of-hooks violation" diagnostic naming the index, then a
    // clear throw (fail-stop but diagnosable) routed through the sink as a re-render
    // error. We assert the specific diagnostic fired — not an exact total count.
    TestHost host;
    Store<bool> useStringVariant(false);

    std::vector<std::string> diagnostics;
    host.setErrorHandler([&](std::string_view where, const std::exception*) {
        diagnostics.push_back(std::string(where));
    });

    auto Comp = [&](ComponentContext& ctx) -> VNode {
        bool asString = useStringVariant.use();  // subscribe so set() re-renders THIS fiber
        if (asString) {
            auto [s, setS] = ctx.useState<std::string>("hi");  // index 0: string
            return Text(s);
        }
        auto [n, setN] = ctx.useState<int>(0);                 // index 0: int
        return Text(std::to_string(n));
    };

    host.setRender(std::function<VNode()>([&]() { return Box({Component(Comp)}); }));

    host.update(200, 200);                 // render 1: index 0 == useState<int>
    CHECK(diagnostics.empty());            // no false positive on the clean first render

    useStringVariant.set(true);            // flip the branch; re-renders the SAME fiber
    CHECK_NOTHROW(host.update(200, 200));  // the throw is isolated by the reconciler

    // The specific rules-of-hooks / type-change diagnostic must have fired.
    bool sawRulesViolation = false;
    for (auto& d : diagnostics) {
        if (d.find("rules-of-hooks violation") != std::string::npos &&
            d.find("hook index 0") != std::string::npos) {
            sawRulesViolation = true;
        }
    }
    CHECK(sawRulesViolation);
}

TEST_CASE("Hooks - reordering different-KIND hooks at an index is diagnosed (FIX #8)") {
    // Same total count, but two hooks of different KIND swap positions across renders.
    // Render 1: [useState<int>, useRef<int>]. Render 2: [useRef<int>, useState<int>].
    // Count-only checks (the old #ifndef NDEBUG guard) miss this entirely; the per-
    // slot tag catches it because Kind::State != Kind::Ref at index 0.
    TestHost host;
    Store<bool> swapped(false);

    std::vector<std::string> diagnostics;
    host.setErrorHandler([&](std::string_view where, const std::exception*) {
        diagnostics.push_back(std::string(where));
    });

    auto Comp = [&](ComponentContext& ctx) -> VNode {
        bool flip = swapped.use();
        if (flip) {
            int& r = ctx.useRef<int>(1);                 // index 0: ref
            auto [n, setN] = ctx.useState<int>(2);       // index 1: state
            return Text(std::to_string(r + n));
        }
        auto [n, setN] = ctx.useState<int>(2);           // index 0: state
        int& r = ctx.useRef<int>(1);                     // index 1: ref
        return Text(std::to_string(r + n));
    };

    host.setRender(std::function<VNode()>([&]() { return Box({Component(Comp)}); }));

    host.update(200, 200);
    CHECK(diagnostics.empty());

    swapped.set(true);
    CHECK_NOTHROW(host.update(200, 200));

    bool sawRulesViolation = false;
    for (auto& d : diagnostics) {
        if (d.find("rules-of-hooks violation") != std::string::npos) sawRulesViolation = true;
    }
    CHECK(sawRulesViolation);
}

TEST_CASE("Hooks - changing the hook COUNT between renders is diagnosed (FIX #8)") {
    // A conditional trailing hook: render 1 calls one hook, render 2 calls two. No
    // per-slot mismatch occurs at the indices both renders reach (index 0 matches),
    // so only the always-on count backstop in ~ComponentContext catches it.
    TestHost host;
    Store<bool> extra(false);

    std::vector<std::string> diagnostics;
    host.setErrorHandler([&](std::string_view where, const std::exception*) {
        diagnostics.push_back(std::string(where));
    });

    auto Comp = [&](ComponentContext& ctx) -> VNode {
        bool more = extra.use();
        auto [a, setA] = ctx.useState<int>(0);   // index 0 on both renders
        if (more) {
            auto [b, setB] = ctx.useState<int>(0);  // index 1 only on render 2
            return Text(std::to_string(a + b));
        }
        return Text(std::to_string(a));
    };

    host.setRender(std::function<VNode()>([&]() { return Box({Component(Comp)}); }));

    host.update(200, 200);
    CHECK(diagnostics.empty());

    extra.set(true);
    CHECK_NOTHROW(host.update(200, 200));

    bool sawCountViolation = false;
    for (auto& d : diagnostics) {
        if (d.find("rules-of-hooks violation") != std::string::npos &&
            d.find("hook count changed") != std::string::npos) {
            sawCountViolation = true;
        }
    }
    CHECK(sawCountViolation);
}

TEST_CASE("Hooks - stable hooks across many renders produce ZERO diagnostics (no false positives)") {
    // The critical no-false-positive guard. A component with a fixed hook sequence
    // (state, ref, field, effect) re-rendered repeatedly must never trip the tag or
    // count checks. This mirrors the shape every real component uses.
    struct Form { int n = 0; std::string label = "x"; };
    TestHost host;
    Store<int> bump(0);
    Store<Form> form;

    int diagnostics = 0;
    host.setErrorHandler([&](std::string_view, const std::exception*) { diagnostics++; });

    auto Comp = [&](ComponentContext& ctx) -> VNode {
        int b = bump.use();                                  // subscribe -> re-render on set()
        auto [n, setN] = ctx.useState<int>(b);               // index 0
        int& r = ctx.useRef<int>(7);                         // index 1
        auto [label, setLabel] = ctx.useField(form, &Form::label);  // index 2
        ctx.useEffect([&]() { return nullptr; });            // index 3
        (void)setN; (void)setLabel;
        return Text(std::to_string(n + r) + label);
    };

    host.setRender(std::function<VNode()>([&]() { return Box({Component(Comp)}); }));

    for (int i = 0; i < 5; ++i) {
        bump.set(i + 1);          // re-render the same fiber each frame
        host.update(200, 200);
    }

    CHECK(diagnostics == 0);
}

// ============================================================================
// Hook-state index correctness (counter split). The positional/tag counter
// (hookIndex_) advances for EVERY hook, but hookState slots are owned only by
// useState/useRef. If a hookState slot were indexed by the positional counter, a
// non-stateful hook (useEffect/useField) preceding a stateful one would push the
// stateful hook's slot index past its true position — an out-of-bounds any_cast
// (UB). The three cases below order hooks the dangerous way; each would read OOB
// under a single shared counter, so they prove the two index spaces are separate.
// ============================================================================

TEST_CASE("Hooks - useEffect BEFORE useState keeps state at the right slot") {
    // hookIndex_ reaches 1 at the useState, but its hookState slot must be 0.
    TestHost host;
    int effectRuns = 0;
    std::function<void(int)> setCount;

    auto Comp = [&](ComponentContext& ctx) -> VNode {
        ctx.useEffect([&]() { effectRuns++; return nullptr; });   // tag index 0, no state slot
        auto [count, setter] = ctx.useState<int>(7);              // tag index 1, state slot 0
        setCount = setter;
        return Text(std::to_string(count));
    };

    host.setRender(std::function<VNode()>([&]() { return Box({Component(Comp)}); }));

    host.update(200, 200);
    auto* rootBox = static_cast<BoxNode*>(host.root());
    auto* textNode = static_cast<TextNode*>(rootBox->children[0].get());
    CHECK(textNode->props.text == "7");

    setCount(42);
    host.update(200, 200);
    CHECK(textNode->props.text == "42");
    CHECK(effectRuns >= 1);
}

TEST_CASE("Hooks - useField BEFORE useState/useRef keeps state at the right slot") {
    // useField advances only the positional counter; the following useState/useRef
    // must still land on hookState slots 0 and 1.
    struct Form { std::string label = "L"; };
    TestHost host;
    Store<Form> form;
    std::function<void(int)> setCount;

    auto Comp = [&](ComponentContext& ctx) -> VNode {
        auto [label, setLabel] = ctx.useField(form, &Form::label);  // tag index 0, no state slot
        auto [count, setter] = ctx.useState<int>(3);                // tag index 1, state slot 0
        int& r = ctx.useRef<int>(100);                              // tag index 2, state slot 1
        (void)setLabel;
        setCount = setter;
        return Text(label + ":" + std::to_string(count) + ":" + std::to_string(r));
    };

    host.setRender(std::function<VNode()>([&]() { return Box({Component(Comp)}); }));

    host.update(200, 200);
    auto* rootBox = static_cast<BoxNode*>(host.root());
    auto* textNode = static_cast<TextNode*>(rootBox->children[0].get());
    CHECK(textNode->props.text == "L:3:100");

    setCount(9);
    host.update(200, 200);
    CHECK(textNode->props.text == "L:9:100");  // state updated, ref persisted
}

TEST_CASE("Hooks - mixed effect/field/state/ref persists state with ZERO diagnostics") {
    // The interleaved-worst-case shape: a non-stateful hook precedes EACH stateful
    // one. Both stateful values must persist across re-renders AND no rules-of-hooks
    // diagnostic may fire (the counter split must not desync the tag path).
    struct Form { std::string label = "F"; };
    TestHost host;
    Store<Form> form;
    int effectRuns = 0;
    std::function<void(int)> setN;
    std::string* refSlot = nullptr;

    int diagnostics = 0;
    host.setErrorHandler([&](std::string_view, const std::exception*) { diagnostics++; });

    auto Comp = [&](ComponentContext& ctx) -> VNode {
        ctx.useEffect([&]() { effectRuns++; return nullptr; });      // tag 0, no state slot
        auto [label, setLabel] = ctx.useField(form, &Form::label);   // tag 1, no state slot
        auto [n, setter] = ctx.useState<int>(5);                     // tag 2, state slot 0
        std::string& s = ctx.useRef<std::string>("ref");             // tag 3, state slot 1
        (void)setLabel;
        setN = setter;
        refSlot = &s;
        return Text(label + ":" + std::to_string(n) + ":" + s);
    };

    host.setRender(std::function<VNode()>([&]() { return Box({Component(Comp)}); }));

    host.update(200, 200);
    auto* rootBox = static_cast<BoxNode*>(host.root());
    auto* textNode = static_cast<TextNode*>(rootBox->children[0].get());
    CHECK(textNode->props.text == "F:5:ref");

    // Mutate the ref slot in place; it must survive the re-render the setter triggers.
    *refSlot = "kept";
    setN(11);
    host.update(200, 200);
    CHECK(textNode->props.text == "F:11:kept");  // state updated AND ref persisted
    CHECK(diagnostics == 0);                      // no false rules-of-hooks positives
}

// ─── Defect A: remount components on identity change ─────────────────────────

TEST_CASE("Component - different lambda at same unkeyed slot remounts (fresh state)") {
    // Two DISTINCT component lambdas occupy the same unkeyed position across two
    // renders. The second must get a FRESH fiber (fresh hook state), NOT inherit
    // the first's useState value. Pre-fix, typeMatches returned true for any
    // component-vs-component, so the second lambda ran on the first's fiber and
    // read its state.
    TestHost host;

    auto CompA = [](ComponentContext& ctx) -> VNode {
        auto [v, set] = ctx.useState<int>(111);
        (void)set;
        return Text("A:" + std::to_string(v));
    };
    auto CompB = [](ComponentContext& ctx) -> VNode {
        auto [v, set] = ctx.useState<int>(222);
        (void)set;
        return Text("B:" + std::to_string(v));
    };

    bool useA = true;
    host.setRender(std::function<VNode()>([&]() -> VNode {
        if (useA) return Box({Component(CompA)});
        return Box({Component(CompB)});
    }));

    host.update(200, 200);
    auto* rootBox = static_cast<BoxNode*>(host.root());
    CHECK(static_cast<TextNode*>(rootBox->children[0].get())->props.text == "A:111");

    // Swap the component identity at the same slot.
    useA = false;
    host.markDirty();
    host.update(200, 200);

    rootBox = static_cast<BoxNode*>(host.root());
    REQUIRE(rootBox->children.size() == 1);
    // Fresh mount => B's initial state, not A's inherited 111.
    CHECK(static_cast<TextNode*>(rootBox->children[0].get())->props.text == "B:222");
}

TEST_CASE("Component - function-pointer components remount by address (&A vs &B)") {
    // Function pointers all share one target_type; only the captured target
    // address disambiguates them. Swapping &funcA -> &funcB at one slot must
    // remount (fresh state), proving the address is part of the identity.
    TestHost host;

    struct Fns {
        static VNode A(ComponentContext& ctx) {
            auto [v, set] = ctx.useState<int>(111);
            (void)set;
            return Text("A:" + std::to_string(v));
        }
        static VNode B(ComponentContext& ctx) {
            auto [v, set] = ctx.useState<int>(222);
            (void)set;
            return Text("B:" + std::to_string(v));
        }
    };

    VNode (*fn)(ComponentContext&) = &Fns::A;
    host.setRender(std::function<VNode()>([&]() -> VNode {
        return Box({Component(ComponentFn(fn))});
    }));

    host.update(200, 200);
    auto* rootBox = static_cast<BoxNode*>(host.root());
    CHECK(static_cast<TextNode*>(rootBox->children[0].get())->props.text == "A:111");

    fn = &Fns::B;
    host.markDirty();
    host.update(200, 200);

    rootBox = static_cast<BoxNode*>(host.root());
    REQUIRE(rootBox->children.size() == 1);
    CHECK(static_cast<TextNode*>(rootBox->children[0].get())->props.text == "B:222");
}

TEST_CASE("Component - same lambda across renders preserves state (no false remount)") {
    // Regression guard for defect A's fix: the SAME component lambda at a stable
    // slot must be treated as the same component (identity match), so its hook
    // state is PRESERVED across renders — no spurious remount.
    TestHost host;
    std::function<void(int)> setter;

    auto Comp = [&](ComponentContext& ctx) -> VNode {
        auto [v, set] = ctx.useState<int>(0);
        setter = set;
        return Text(std::to_string(v));
    };

    host.setRender(std::function<VNode()>([&]() -> VNode {
        return Box({Component(Comp)});
    }));

    host.update(200, 200);
    setter(77);
    host.update(200, 200);

    auto* rootBox = static_cast<BoxNode*>(host.root());
    // State survived (would be 0 again if the fiber were spuriously remounted).
    CHECK(static_cast<TextNode*>(rootBox->children[0].get())->props.text == "77");
}
