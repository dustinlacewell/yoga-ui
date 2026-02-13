// Headless performance benchmark for yui reconciler and layout
//
// Build: cmake --build build --target bench_reconciler
// Run:   ./build/bin/bench_reconciler

#include <yui/core/Host.hpp>
#include <yui/core/Reconciler.hpp>
#include <yui/core/Store.hpp>
#include <yui/core/VNode.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <numeric>
#include <vector>

using namespace yui;

// Timing utilities
using Clock = std::chrono::high_resolution_clock;
using Duration = std::chrono::duration<double, std::milli>;

struct TimingResult {
    double min_ms;
    double max_ms;
    double avg_ms;
    double median_ms;
};

TimingResult measure(int iterations, std::function<void()> fn) {
    std::vector<double> times;
    times.reserve(iterations);

    for (int i = 0; i < iterations; i++) {
        auto start = Clock::now();
        fn();
        auto end = Clock::now();
        times.push_back(Duration(end - start).count());
    }

    std::sort(times.begin(), times.end());

    double sum = std::accumulate(times.begin(), times.end(), 0.0);

    return {
        .min_ms = times.front(),
        .max_ms = times.back(),
        .avg_ms = sum / iterations,
        .median_ms = times[iterations / 2],
    };
}

// ============================================================================
// Component-based tree builders (user code pattern)
// ============================================================================

// QuadTree - recursive component that builds a 2x2 grid tree
// Depth 1 = 1 node, Depth 2 = 5 nodes (1 + 4), Depth 3 = 21 nodes, etc.
// No keys needed - structure is deterministic, position-based diffing works fine
Component QuadTree(int depth) {
    return [=](ComponentContext&) -> VNode {
        if (depth <= 1) {
            return Box().backgroundColor(0x3366CCFF);
        }

        return Column({
                          Row({QuadTree(depth - 1), QuadTree(depth - 1)}).flexGrow(1).gap(1),
                          Row({QuadTree(depth - 1), QuadTree(depth - 1)}).flexGrow(1).gap(1),
                      })
            .flexGrow(1)
            .gap(1);
    };
}

// FlatList - component with N box children
Component FlatList(int count) {
    return [=](ComponentContext&) -> VNode {
        std::vector<Child> children;
        children.reserve(count);
        for (int i = 0; i < count; i++) {
            children.push_back(Box()
                                   .setKey(static_cast<int64_t>(i))
                                   .width(20)
                                   .height(20)
                                   .backgroundColor(0x3366CCFF));
        }
        return Row(std::move(children)).flexWrap(FlexWrap::Wrap);
    };
}

// FlatListReversed - component with N box children in reverse order
Component FlatListReversed(int count) {
    return [=](ComponentContext&) -> VNode {
        std::vector<Child> children;
        children.reserve(count);
        for (int i = count - 1; i >= 0; i--) {
            children.push_back(Box()
                                   .setKey(static_cast<int64_t>(i))
                                   .width(20)
                                   .height(20)
                                   .backgroundColor(0x3366CCFF));
        }
        return Row(std::move(children)).flexWrap(FlexWrap::Wrap);
    };
}

int countNodes(int depth) {
    // Quad tree: each non-leaf has 1 Column + 2 Rows + 4 children
    if (depth <= 1)
        return 1;
    return 1 + 2 + 4 * countNodes(depth - 1);  // Column + 2 Rows + 4 subtrees
}

void printResult(const char* name, int nodes, const TimingResult& r) {
    printf("  %-30s %5d nodes  %8.3f ms avg  (min: %.3f, max: %.3f, med: %.3f)\n",
           name, nodes, r.avg_ms, r.min_ms, r.max_ms, r.median_ms);
}

// ============================================================================
// Benchmarks using Host (full component lifecycle)
// ============================================================================

void benchmarkMount() {
    printf("\n=== Mount (Component -> Node tree) ===\n");

    for (int depth = 3; depth <= 7; depth++) {
        int nodes = countNodes(depth);
        auto result = measure(20, [=] {
            Host host;
            host.setRender([=]() -> VNode { return Box({QuadTree(depth)}); });
            host.update(800, 800, 0.016f);
        });
        char name[64];
        snprintf(name, sizeof(name), "QuadTree depth=%d", depth);
        printResult(name, nodes, result);
    }

    for (int count : {100, 500, 1000, 2000}) {
        auto result = measure(20, [=] {
            Host host;
            host.setRender([=]() -> VNode { return Box({FlatList(count)}); });
            host.update(800, 800, 0.016f);
        });
        char name[64];
        snprintf(name, sizeof(name), "FlatList n=%d", count);
        printResult(name, count + 1, result);
    }
}

// ============================================================================
// Mount breakdown - understand where time goes in the 8k node mount
// ============================================================================

// Count component invocations for QuadTree
static int componentCallCount = 0;

// VNode-only QuadTree - returns VNode directly, no nested Components
// This isolates VNode construction cost from Component overhead
VNode QuadTreeVNode(int depth) {
    if (depth <= 1) {
        return Box().backgroundColor(0x3366CCFF);
    }
    return Column({
                      Row({QuadTreeVNode(depth - 1), QuadTreeVNode(depth - 1)}).flexGrow(1).gap(1),
                      Row({QuadTreeVNode(depth - 1), QuadTreeVNode(depth - 1)}).flexGrow(1).gap(1),
                  })
        .flexGrow(1)
        .gap(1);
}

// Component QuadTree that counts invocations
Component QuadTreeCounted(int depth) {
    return [=](ComponentContext&) -> VNode {
        componentCallCount++;
        if (depth <= 1) {
            return Box().backgroundColor(0x3366CCFF);
        }
        return Column({
                          Row({QuadTreeCounted(depth - 1), QuadTreeCounted(depth - 1)}).flexGrow(1).gap(1),
                          Row({QuadTreeCounted(depth - 1), QuadTreeCounted(depth - 1)}).flexGrow(1).gap(1),
                      })
            .flexGrow(1)
            .gap(1);
    };
}

// Component that just captures an int (for measuring std::function overhead)
Component DummyComponent(int value) {
    return [=](ComponentContext&) -> VNode {
        return Box().width(static_cast<float>(value));
    };
}

void benchmarkMountBreakdown() {
    printf("\n=== Mount Breakdown (depth=7, ~8k nodes) ===\n");
    printf("    Understanding where time goes during mount\n\n");

    const int depth = 7;
    const int iterations = 20;

    // 1. Measure Component (std::function) creation only - no mounting
    printf("  Phase 1: Component/std::function creation\n");
    {
        int componentCount = 0;
        std::function<void(int)> countComponents = [&](int d) {
            componentCount++;
            if (d > 1) {
                countComponents(d - 1);
                countComponents(d - 1);
                countComponents(d - 1);
                countComponents(d - 1);
            }
        };
        countComponents(depth);
        printf("    Component count: %d\n", componentCount);

        auto result = measure(iterations, [=] {
            std::vector<Component> components;
            components.reserve(5500);
            std::function<void(int)> createComponents = [&](int d) {
                components.push_back(DummyComponent(d));
                if (d > 1) {
                    createComponents(d - 1);
                    createComponents(d - 1);
                    createComponents(d - 1);
                    createComponents(d - 1);
                }
            };
            createComponents(depth);
        });
        printResult("std::function creation", componentCount, result);
    }

    // 2. Measure VNode tree construction only (no Components, no mounting)
    printf("\n  Phase 2: VNode tree construction (no Components)\n");
    {
        auto result = measure(iterations, [=] {
            volatile auto vnode = QuadTreeVNode(depth);
            (void)vnode;
        });
        printResult("VNode-only tree build", countNodes(depth), result);
    }

    // 3. Measure raw heap allocation overhead
    printf("\n  Phase 3: Raw allocation costs\n");
    {
        struct DummyNode {
            char data[256];
        };
        auto result = measure(iterations, [=] {
            std::vector<std::unique_ptr<DummyNode>> nodes;
            nodes.reserve(8200);
            for (int i = 0; i < 8191; i++) {
                nodes.push_back(std::make_unique<DummyNode>());
            }
        });
        printResult("8k heap allocs (256B each)", 8191, result);
    }
    {
        auto result = measure(iterations, [=] {
            std::vector<YGNodeRef> nodes;
            nodes.reserve(8200);
            for (int i = 0; i < 8191; i++) {
                nodes.push_back(YGNodeNew());
            }
            for (auto n : nodes) {
                YGNodeFree(n);
            }
        });
        printResult("8k YGNode create+free", 8191, result);
    }
    {
        auto result = measure(iterations, [=] {
            YGNodeRef root = YGNodeNew();
            std::vector<YGNodeRef> nodes;
            nodes.reserve(8200);
            for (int i = 0; i < 8191; i++) {
                YGNodeRef n = YGNodeNew();
                nodes.push_back(n);
                YGNodeInsertChild(root, n, i);
            }
            YGNodeFreeRecursive(root);
        });
        printResult("8k YGNode + flat insert", 8191, result);
    }
    {
        // Build nested YGNode tree matching QuadTree structure
        // depth=7: Column(Row(4 subtrees), Row(4 subtrees))
        std::function<YGNodeRef(int)> buildYogaTree = [&](int d) -> YGNodeRef {
            YGNodeRef node = YGNodeNew();
            if (d > 1) {
                // Column with 2 Row children, each Row has 2 subtrees
                YGNodeRef row1 = YGNodeNew();
                YGNodeRef row2 = YGNodeNew();
                YGNodeInsertChild(node, row1, 0);
                YGNodeInsertChild(node, row2, 1);
                YGNodeInsertChild(row1, buildYogaTree(d - 1), 0);
                YGNodeInsertChild(row1, buildYogaTree(d - 1), 1);
                YGNodeInsertChild(row2, buildYogaTree(d - 1), 0);
                YGNodeInsertChild(row2, buildYogaTree(d - 1), 1);
            }
            return node;
        };
        auto result = measure(iterations, [&] {
            YGNodeRef root = buildYogaTree(depth);
            YGNodeFreeRecursive(root);
        });
        printResult("YGNode nested tree (depth=7)", countNodes(depth), result);
    }

    // 4. Measure std::variant visitation cost
    printf("\n  Phase 4: Variant/recursion overhead\n");
    {
        // Measure cost of visiting Child variants recursively
        std::function<int(const VNode&)> countVNodes = [&](const VNode& v) -> int {
            int count = 1;
            for (const auto& child : v.children) {
                if (std::holds_alternative<VNode>(child)) {
                    count += countVNodes(std::get<VNode>(child));
                }
            }
            return count;
        };
        VNode tree = QuadTreeVNode(depth);
        auto result = measure(iterations * 10, [&] {
            volatile int c = countVNodes(tree);
            (void)c;
        });
        printResult("Variant traversal (200 iters)", countNodes(depth), result);
    }

    // 5. Full mount comparison
    printf("\n  Phase 5: Full mount comparison\n");
    {
        auto result = measure(1, [=] {
            Host host;
            host.setRender([=]() -> VNode { return Box({QuadTree(depth)}); });
            host.update(800, 800, 0.016f);
        });
        printResult("Full mount (Components)", countNodes(depth), result);
    }
    {
        auto result = measure(1, [=] {
            Host host;
            host.setRender([=]() -> VNode { return QuadTreeVNode(depth); });
            host.update(800, 800, 0.016f);
        });
        printResult("Full mount (VNodes only)", countNodes(depth), result);
    }

    // 6. Layout-only (on already-mounted tree)
    printf("\n  Phase 6: Layout only (tree already mounted)\n");
    {
        Host host;
        host.setRender([=]() -> VNode { return Box({QuadTree(depth)}); });
        host.update(800, 800, 0.016f);

        auto result = measure(50, [&] { host.root()->calculateLayout(800, 800); });
        printResult("Layout calculation", countNodes(depth), result);
    }

    // 6b. Pure Yoga tree - first layout timing
    printf("\n  Phase 6b: Pure Yoga first layout (isolate Yoga performance)\n");
    {
        // Build QuadTree-like Yoga tree directly (no yui nodes)
        std::function<YGNodeRef(int)> buildYogaQuadTree = [&](int d) -> YGNodeRef {
            YGNodeRef node = YGNodeNew();
            YGNodeStyleSetFlexGrow(node, 1);
            if (d <= 1) {
                return node;
            }
            // Column with 2 Row children
            YGNodeStyleSetFlexDirection(node, YGFlexDirectionColumn);
            YGNodeStyleSetGap(node, YGGutterAll, 1);

            YGNodeRef row1 = YGNodeNew();
            YGNodeRef row2 = YGNodeNew();
            YGNodeStyleSetFlexDirection(row1, YGFlexDirectionRow);
            YGNodeStyleSetFlexDirection(row2, YGFlexDirectionRow);
            YGNodeStyleSetFlexGrow(row1, 1);
            YGNodeStyleSetFlexGrow(row2, 1);
            YGNodeStyleSetGap(row1, YGGutterAll, 1);
            YGNodeStyleSetGap(row2, YGGutterAll, 1);

            YGNodeInsertChild(node, row1, 0);
            YGNodeInsertChild(node, row2, 1);

            YGNodeInsertChild(row1, buildYogaQuadTree(d - 1), 0);
            YGNodeInsertChild(row1, buildYogaQuadTree(d - 1), 1);
            YGNodeInsertChild(row2, buildYogaQuadTree(d - 1), 0);
            YGNodeInsertChild(row2, buildYogaQuadTree(d - 1), 1);

            return node;
        };

        YGNodeRef yogaRoot = buildYogaQuadTree(depth);
        printf("    Built pure Yoga tree with %d nodes\n", countNodes(depth));

        // Time FIRST layout
        auto start = Clock::now();
        YGNodeCalculateLayout(yogaRoot, 800, 800, YGDirectionLTR);
        auto end = Clock::now();
        double firstLayout = Duration(end - start).count();
        printf("    First YGNodeCalculateLayout: %.3f ms\n", firstLayout);

        // Time second layout (should be cached)
        start = Clock::now();
        YGNodeCalculateLayout(yogaRoot, 800, 800, YGDirectionLTR);
        end = Clock::now();
        double secondLayout = Duration(end - start).count();
        printf("    Second YGNodeCalculateLayout: %.3f ms\n", secondLayout);

        // Time layout with size change (invalidates cache)
        start = Clock::now();
        YGNodeCalculateLayout(yogaRoot, 801, 800, YGDirectionLTR);
        end = Clock::now();
        double sizeChangeLayout = Duration(end - start).count();
        printf("    Layout after size change: %.3f ms\n", sizeChangeLayout);

        YGNodeFreeRecursive(yogaRoot);
    }

    // 6c. FLAT Yoga tree - same node count, no nesting
    printf("\n  Phase 6c: FLAT Yoga tree (same nodes, no nesting)\n");
    {
        YGNodeRef yogaRoot = YGNodeNew();
        YGNodeStyleSetFlexDirection(yogaRoot, YGFlexDirectionRow);
        YGNodeStyleSetFlexWrap(yogaRoot, YGWrapWrap);

        for (int i = 0; i < 8191; i++) {
            YGNodeRef child = YGNodeNew();
            YGNodeStyleSetWidth(child, 20);
            YGNodeStyleSetHeight(child, 20);
            YGNodeInsertChild(yogaRoot, child, i);
        }

        auto start = Clock::now();
        YGNodeCalculateLayout(yogaRoot, 800, 800, YGDirectionLTR);
        auto end = Clock::now();
        double firstLayout = Duration(end - start).count();
        printf("    First layout (8k flat children): %.3f ms\n", firstLayout);

        YGNodeFreeRecursive(yogaRoot);
    }

    // 6d. Shallow nested tree - 3 levels deep with many children per level
    printf("\n  Phase 6d: SHALLOW Yoga tree (3 levels, ~8k nodes)\n");
    {
        YGNodeRef yogaRoot = YGNodeNew();
        YGNodeStyleSetFlexDirection(yogaRoot, YGFlexDirectionColumn);

        // 20 sections, each with 20 rows, each with 20 items = 8000 leaf nodes
        for (int s = 0; s < 20; s++) {
            YGNodeRef section = YGNodeNew();
            YGNodeStyleSetFlexDirection(section, YGFlexDirectionColumn);
            YGNodeStyleSetFlexGrow(section, 1);
            YGNodeInsertChild(yogaRoot, section, s);

            for (int r = 0; r < 20; r++) {
                YGNodeRef row = YGNodeNew();
                YGNodeStyleSetFlexDirection(row, YGFlexDirectionRow);
                YGNodeStyleSetFlexGrow(row, 1);
                YGNodeInsertChild(section, row, r);

                for (int c = 0; c < 20; c++) {
                    YGNodeRef cell = YGNodeNew();
                    YGNodeStyleSetFlexGrow(cell, 1);
                    YGNodeInsertChild(row, cell, c);
                }
            }
        }

        auto start = Clock::now();
        YGNodeCalculateLayout(yogaRoot, 800, 800, YGDirectionLTR);
        auto end = Clock::now();
        double firstLayout = Duration(end - start).count();
        printf("    First layout (20x20x20 = 8000 leaves, 3 levels): %.3f ms\n", firstLayout);

        YGNodeFreeRecursive(yogaRoot);
    }

    // 7. Deep vs Wide tree comparison
    printf("\n  Phase 7: Tree structure impact (O(n^2) detection)\n");
    {
        // Deep tree: linear chain of depth N
        auto DeepTree = [](int n) -> VNode {
            VNode node = Box().backgroundColor(0x3366CCFF);
            for (int i = 0; i < n; i++) {
                node = Box({std::move(node)});
            }
            return node;
        };

        for (int n : {100, 200, 400, 800}) {
            auto result = measure(10, [&] {
                Host host;
                host.setRender([&]() -> VNode { return DeepTree(n); });
                host.update(800, 800, 0.016f);
            });
            char name[64];
            snprintf(name, sizeof(name), "Deep chain n=%d", n);
            printResult(name, n, result);
        }
    }

    // 8. Simulate mount operations in isolation
    printf("\n  Phase 8: Simulated mount operations\n");
    {
        // Test: createNode() + updateProps() for 8k nodes (no tree structure)
        auto result = measure(iterations, [=] {
            std::vector<std::unique_ptr<Node>> nodes;
            nodes.reserve(8200);
            BoxProps props;
            props.flexGrow = 1.0f;
            props.gap = 1.0f;
            for (int i = 0; i < 8191; i++) {
                auto node = createNode(PrimitiveType::Box);
                node->updateProps(props);
                nodes.push_back(std::move(node));
            }
        });
        printResult("8k createNode+updateProps", 8191, result);
    }
    {
        // Test: Build nested Node tree matching QuadTree structure
        std::function<std::unique_ptr<Node>(int)> buildNodeTree = [&](int d) -> std::unique_ptr<Node> {
            if (d <= 1) {
                auto node = createNode(PrimitiveType::Box);
                BoxProps props;
                props.backgroundColor = 0x3366CCFF;
                node->updateProps(props);
                return node;
            }
            auto col = createNode(PrimitiveType::Box);
            BoxProps colProps;
            colProps.flexDirection = FlexDirection::Column;
            colProps.flexGrow = 1.0f;
            colProps.gap = 1.0f;
            col->updateProps(colProps);

            auto row1 = createNode(PrimitiveType::Box);
            auto row2 = createNode(PrimitiveType::Box);
            BoxProps rowProps;
            rowProps.flexDirection = FlexDirection::Row;
            rowProps.flexGrow = 1.0f;
            rowProps.gap = 1.0f;
            row1->updateProps(rowProps);
            row2->updateProps(rowProps);

            // Build children
            auto c1 = buildNodeTree(d - 1);
            auto c2 = buildNodeTree(d - 1);
            auto c3 = buildNodeTree(d - 1);
            auto c4 = buildNodeTree(d - 1);

            // Insert into yoga tree
            YGNodeInsertChild(row1->yogaNode, c1->yogaNode, 0);
            YGNodeInsertChild(row1->yogaNode, c2->yogaNode, 1);
            YGNodeInsertChild(row2->yogaNode, c3->yogaNode, 0);
            YGNodeInsertChild(row2->yogaNode, c4->yogaNode, 1);
            YGNodeInsertChild(col->yogaNode, row1->yogaNode, 0);
            YGNodeInsertChild(col->yogaNode, row2->yogaNode, 1);

            // Store ownership
            row1->children.push_back(std::move(c1));
            row1->children.push_back(std::move(c2));
            row2->children.push_back(std::move(c3));
            row2->children.push_back(std::move(c4));
            col->children.push_back(std::move(row1));
            col->children.push_back(std::move(row2));

            return col;
        };

        auto result = measure(iterations, [&] {
            auto root = buildNodeTree(depth);
        });
        printResult("Manual Node tree build", countNodes(depth), result);

        // Test: Single build + destroy cycle
        result = measure(iterations, [&] {
            auto root = buildNodeTree(depth);
            // root destroyed at end of lambda
        });
        printResult("Build + destroy cycle", countNodes(depth), result);

        // Test: Just destruction time
        {
            std::vector<std::unique_ptr<Node>> trees;
            trees.reserve(iterations);
            for (int i = 0; i < iterations; i++) {
                trees.push_back(buildNodeTree(depth));
            }
            int idx = 0;
            result = measure(iterations, [&] {
                trees[idx++].reset();
            });
            printResult("Node tree destruction", countNodes(depth), result);
        }
    }
    {
        // Test: Mount from pre-built VNode tree (mimics actual reconciler behavior)
        VNode vnodeTree = QuadTreeVNode(depth);

        std::function<std::unique_ptr<Node>(const VNode&)> mountFromVNode = [&](const VNode& vnode) -> std::unique_ptr<Node> {
            auto node = createNode(vnode.type);
            node->updateProps(vnode.props);

            size_t yogaIndex = 0;
            for (const auto& child : vnode.children) {
                if (std::holds_alternative<VNode>(child)) {
                    auto childNode = mountFromVNode(std::get<VNode>(child));
                    if (childNode && node->yogaNode && childNode->yogaNode) {
                        YGNodeInsertChild(node->yogaNode, childNode->yogaNode, yogaIndex++);
                    }
                    node->children.push_back(std::move(childNode));
                }
            }
            return node;
        };

        auto result = measure(iterations, [&] {
            auto root = mountFromVNode(vnodeTree);
        });
        printResult("Mount from VNode tree", countNodes(depth), result);
    }
    {
        // Test: Just traverse VNode tree without creating nodes
        VNode vnodeTree = QuadTreeVNode(depth);

        std::function<int(const VNode&)> traverseVNode = [&](const VNode& vnode) -> int {
            int count = 1;
            for (const auto& child : vnode.children) {
                if (std::holds_alternative<VNode>(child)) {
                    count += traverseVNode(std::get<VNode>(child));
                }
            }
            return count;
        };

        auto result = measure(iterations * 10, [&] {
            volatile int c = traverseVNode(vnodeTree);
            (void)c;
        });
        printResult("VNode traversal only", countNodes(depth), result);
    }

    printf("\n  === Conclusion ===\n");
    printf("    The bottleneck is Yoga's layout algorithm for deep trees.\n");
    printf("    Yoga is O(depth^2) or worse for nested flexbox layouts.\n");
    printf("    Solutions: flatten tree structure, virtualize, use fixed dimensions.\n");
}

void benchmarkReconcileNoChange() {
    printf("\n=== Reconcile (no changes) ===\n");

    for (int depth = 3; depth <= 7; depth++) {
        int nodes = countNodes(depth);

        Host host;
        host.setRender([=]() -> VNode { return Box({QuadTree(depth)}); });
        host.update(800, 800, 0.016f);

        auto result = measure(50, [&] {
            host.markDirty();
            host.update(800, 800, 0.016f);
        });
        char name[64];
        snprintf(name, sizeof(name), "QuadTree depth=%d", depth);
        printResult(name, nodes, result);
    }
}

void benchmarkReconcileReorder() {
    printf("\n=== Reconcile (full reorder) ===\n");

    for (int count : {100, 500, 1000}) {
        bool reversed = false;

        Host host;
        host.setRender([&, count]() -> VNode {
            return Box({reversed ? FlatListReversed(count) : FlatList(count)});
        });
        host.update(800, 800, 0.016f);

        auto result = measure(50, [&] {
            reversed = !reversed;
            host.markDirty();
            host.update(800, 800, 0.016f);
        });
        char name[64];
        snprintf(name, sizeof(name), "FlatList n=%d reverse", count);
        printResult(name, count + 1, result);
    }
}

void benchmarkLayout() {
    printf("\n=== Layout (Yoga calculation) ===\n");

    for (int depth = 3; depth <= 7; depth++) {
        int nodes = countNodes(depth);

        Host host;
        host.setRender([=]() -> VNode { return Box({QuadTree(depth)}); });
        host.update(800, 800, 0.016f);

        auto result = measure(50, [&] { host.root()->calculateLayout(800, 800); });
        char name[64];
        snprintf(name, sizeof(name), "QuadTree depth=%d", depth);
        printResult(name, nodes, result);
    }

    for (int count : {100, 500, 1000, 2000}) {
        Host host;
        host.setRender([=]() -> VNode { return Box({FlatList(count)}); });
        host.update(800, 800, 0.016f);

        auto result = measure(50, [&] { host.root()->calculateLayout(800, 800); });
        char name[64];
        snprintf(name, sizeof(name), "FlatList n=%d", count);
        printResult(name, count + 1, result);
    }
}

void benchmarkFullFrame() {
    printf("\n=== Full Frame (build VNode + reconcile + layout) ===\n");

    for (int depth = 3; depth <= 7; depth++) {
        int nodes = countNodes(depth);

        Host host;
        host.setRender([=]() -> VNode { return Box({QuadTree(depth)}); });
        host.update(800, 800, 0.016f);

        auto result = measure(50, [&] {
            host.markDirty();
            host.update(800, 800, 0.016f);
        });
        char name[64];
        snprintf(name, sizeof(name), "QuadTree depth=%d", depth);
        printResult(name, nodes, result);
    }
}

// ============================================================================
// Component subscription tests
// ============================================================================

void testComponentSubscriptions() {
    printf("\n=== Component Subscription Test ===\n");

    // Create stores
    Store<int> fastStore(0);   // Updates every frame
    Store<int> slowStore(0);   // Updates rarely

    int fastRenderCount = 0;
    int slowRenderCount = 0;

    // FastComponent subscribes to fastStore
    auto FastComponent = [&]() -> Component {
        return [&](ComponentContext&) -> VNode {
            fastRenderCount++;
            int val = fastStore.use();
            return Box().width(static_cast<float>(val));
        };
    };

    // SlowComponent subscribes to slowStore
    auto SlowComponent = [&]() -> Component {
        return [&](ComponentContext&) -> VNode {
            slowRenderCount++;
            int val = slowStore.use();
            return Box().width(static_cast<float>(val));
        };
    };

    // Root render function
    auto renderFn = [&]() -> VNode {
        return Box({FastComponent(), SlowComponent()});
    };

    // Create host
    Host host;
    host.setRender(renderFn);

    // Initial render
    host.update(800, 600, 0.016f);
    printf("  After initial mount: fast=%d, slow=%d\n", fastRenderCount, slowRenderCount);

    // Reset counts
    fastRenderCount = 0;
    slowRenderCount = 0;

    // Update fastStore 10 times
    for (int i = 0; i < 10; i++) {
        fastStore.set(i + 1);
        host.update(800, 600, 0.016f);
    }

    printf("  After 10 fastStore updates: fast=%d, slow=%d\n", fastRenderCount, slowRenderCount);

    if (fastRenderCount == 10 && slowRenderCount == 0) {
        printf("  PASS: Only FastComponent re-rendered\n");
    } else {
        printf("  FAIL: Expected fast=10, slow=0\n");
    }

    // Reset counts
    fastRenderCount = 0;
    slowRenderCount = 0;

    // Update slowStore once
    slowStore.set(100);
    host.update(800, 600, 0.016f);

    printf("  After 1 slowStore update: fast=%d, slow=%d\n", fastRenderCount, slowRenderCount);

    if (fastRenderCount == 0 && slowRenderCount == 1) {
        printf("  PASS: Only SlowComponent re-rendered\n");
    } else {
        printf("  FAIL: Expected fast=0, slow=1\n");
    }
}

// Benchmark component dirty reconciliation vs full reconciliation
void benchmarkComponentDirty() {
    printf("\n=== Component Dirty Reconciliation ===\n");

    for (int depth = 4; depth <= 6; depth++) {
        // Store must be scoped with Host to avoid dangling pointers
        Store<int> statsStore(0);
        int treeRenderCount = 0;
        int statsRenderCount = 0;

        // TreeComponent - expensive, builds large tree (using Component QuadTree)
        auto TreeComponent = [&, depth]() -> Component {
            return [&, depth](ComponentContext&) -> VNode {
                treeRenderCount++;
                // Use the Component version - wrap its result in a Box
                // Note: QuadTree returns Component, we return its invocation result
                return Box({QuadTree(depth)});
            };
        };

        // StatsComponent - cheap, subscribes to statsStore
        auto StatsComponent = [&]() -> Component {
            return [&](ComponentContext&) -> VNode {
                statsRenderCount++;
                int val = statsStore.use();
                return Text(std::to_string(val));
            };
        };

        int nodes = countNodes(depth);

        auto renderFn = [&]() -> VNode {
            return Box({TreeComponent(), StatsComponent()});
        };

        Host host;
        host.setRender(renderFn);
        host.update(800, 600, 0.016f);

        // Reset after mount
        treeRenderCount = 0;
        statsRenderCount = 0;

        // Simulate 50 frames of stats updates
        auto result = measure(50, [&] {
            statsStore.set(statsStore.peek() + 1);
            host.update(800, 600, 0.016f);
        });

        char name[64];
        snprintf(name, sizeof(name), "Stats update (tree=%d nodes)", nodes);
        printResult(name, nodes, result);
        printf("    Tree renders: %d, Stats renders: %d\n", treeRenderCount, statsRenderCount);
    }
}

int main() {
    printf("YUI Headless Performance Benchmark\n");
    printf("===================================\n");
    fflush(stdout);

    printf("Running testComponentSubscriptions...\n");
    fflush(stdout);
    testComponentSubscriptions();

    printf("\nRunning benchmarkComponentDirty...\n");
    fflush(stdout);
    benchmarkComponentDirty();

    printf("\nRunning benchmarkMount...\n");
    fflush(stdout);
    benchmarkMount();

    printf("\nRunning benchmarkMountBreakdown...\n");
    fflush(stdout);
    benchmarkMountBreakdown();

    printf("\nRunning benchmarkReconcileNoChange...\n");
    fflush(stdout);
    benchmarkReconcileNoChange();

    printf("\nRunning benchmarkFullFrame...\n");
    fflush(stdout);
    benchmarkFullFrame();

    printf("\nDone.\n");
    return 0;
}
