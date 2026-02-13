#include <yui/yui.hpp>

#include "doctest.h"

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
