// Headless performance benchmark for yui reconciler and layout
//
// Build: cmake --build build --target bench_reconciler
// Run:   ./build/bin/bench_reconciler

#include <yui/core/Host.hpp>
#include <yui/core/Reconciler.hpp>
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

// Build a quad tree VNode of given depth
// Depth 1 = 1 node, Depth 2 = 5 nodes (1 + 4), Depth 3 = 21 nodes, etc.
// Total nodes = (4^depth - 1) / 3
VNode buildQuadTree(int depth, int id = 0) {
    if (depth <= 1) {
        return Box().setKey(static_cast<int64_t>(id)).backgroundColor(0x3366CCFF);
    }

    int childBase = id * 4 + 1;
    return Column({
                      Row({
                              buildQuadTree(depth - 1, childBase),
                              buildQuadTree(depth - 1, childBase + 1),
                          })
                          .flexGrow(1)
                          .gap(1),
                      Row({
                              buildQuadTree(depth - 1, childBase + 2),
                              buildQuadTree(depth - 1, childBase + 3),
                          })
                          .flexGrow(1)
                          .gap(1),
                  })
        .setKey(static_cast<int64_t>(id))
        .flexGrow(1)
        .gap(1);
}

// Build a flat list of N boxes
VNode buildFlatList(int count) {
    std::vector<VNode> children;
    children.reserve(count);
    for (int i = 0; i < count; i++) {
        children.push_back(Box()
                               .setKey(static_cast<int64_t>(i))
                               .width(20)
                               .height(20)
                               .backgroundColor(0x3366CCFF));
    }
    return Row(std::move(children)).flexWrap(FlexWrap::Wrap);
}

// Build a flat list with reversed order (for reconcile stress test)
VNode buildFlatListReversed(int count) {
    std::vector<VNode> children;
    children.reserve(count);
    for (int i = count - 1; i >= 0; i--) {
        children.push_back(Box()
                               .setKey(static_cast<int64_t>(i))
                               .width(20)
                               .height(20)
                               .backgroundColor(0x3366CCFF));
    }
    return Row(std::move(children)).flexWrap(FlexWrap::Wrap);
}

int countNodes(int depth) {
    // Quad tree: each non-leaf has 1 Column + 2 Rows + 4 children
    // Actually let's just count recursively for accuracy
    if (depth <= 1)
        return 1;
    return 1 + 2 + 4 * countNodes(depth - 1);  // Column + 2 Rows + 4 subtrees
}

void printResult(const char* name, int nodes, const TimingResult& r) {
    printf("  %-30s %5d nodes  %8.3f ms avg  (min: %.3f, max: %.3f, med: %.3f)\n",
           name, nodes, r.avg_ms, r.min_ms, r.max_ms, r.median_ms);
}

void benchmarkMount() {
    printf("\n=== Mount (VNode -> Node tree) ===\n");
    Reconciler reconciler;

    for (int depth = 3; depth <= 7; depth++) {
        int nodes = countNodes(depth);
        auto result = measure(20, [&] {
            auto vnode = buildQuadTree(depth);
            auto root = reconciler.mount(vnode);
        });
        char name[64];
        snprintf(name, sizeof(name), "QuadTree depth=%d", depth);
        printResult(name, nodes, result);
    }

    for (int count : {100, 500, 1000, 2000}) {
        auto result = measure(20, [&] {
            auto vnode = buildFlatList(count);
            auto root = reconciler.mount(vnode);
        });
        char name[64];
        snprintf(name, sizeof(name), "FlatList n=%d", count);
        printResult(name, count + 1, result);
    }
}

void benchmarkReconcileNoChange() {
    printf("\n=== Reconcile (no changes) ===\n");
    Reconciler reconciler;

    for (int depth = 3; depth <= 7; depth++) {
        int nodes = countNodes(depth);
        auto vnode1 = buildQuadTree(depth);
        auto root = reconciler.mount(vnode1);

        auto result = measure(50, [&] {
            auto vnode2 = buildQuadTree(depth);
            reconciler.reconcile(root.get(), vnode2);
        });
        char name[64];
        snprintf(name, sizeof(name), "QuadTree depth=%d", depth);
        printResult(name, nodes, result);
    }
}

void benchmarkReconcileReorder() {
    printf("\n=== Reconcile (full reorder) ===\n");
    Reconciler reconciler;

    for (int count : {100, 500, 1000}) {
        auto vnode1 = buildFlatList(count);
        auto root = reconciler.mount(vnode1);

        bool reversed = false;
        auto result = measure(50, [&] {
            auto vnode2 = reversed ? buildFlatList(count) : buildFlatListReversed(count);
            reconciler.reconcile(root.get(), vnode2);
            reversed = !reversed;
        });
        char name[64];
        snprintf(name, sizeof(name), "FlatList n=%d reverse", count);
        printResult(name, count + 1, result);
    }
}

void benchmarkLayout() {
    printf("\n=== Layout (Yoga calculation) ===\n");
    Reconciler reconciler;

    for (int depth = 3; depth <= 7; depth++) {
        int nodes = countNodes(depth);
        auto vnode = buildQuadTree(depth);
        auto root = reconciler.mount(vnode);

        auto result = measure(50, [&] { root->calculateLayout(800, 800); });
        char name[64];
        snprintf(name, sizeof(name), "QuadTree depth=%d", depth);
        printResult(name, nodes, result);
    }

    for (int count : {100, 500, 1000, 2000}) {
        auto vnode = buildFlatList(count);
        auto root = reconciler.mount(vnode);

        auto result = measure(50, [&] { root->calculateLayout(800, 800); });
        char name[64];
        snprintf(name, sizeof(name), "FlatList n=%d", count);
        printResult(name, count + 1, result);
    }
}

void benchmarkFullFrame() {
    printf("\n=== Full Frame (build VNode + reconcile + layout) ===\n");
    Reconciler reconciler;

    for (int depth = 3; depth <= 7; depth++) {
        int nodes = countNodes(depth);
        auto vnode = buildQuadTree(depth);
        auto root = reconciler.mount(vnode);
        root->calculateLayout(800, 800);

        auto result = measure(50, [&] {
            auto vnode2 = buildQuadTree(depth);
            reconciler.reconcile(root.get(), vnode2);
            root->calculateLayout(800, 800);
        });
        char name[64];
        snprintf(name, sizeof(name), "QuadTree depth=%d", depth);
        printResult(name, nodes, result);
    }
}

int main() {
    printf("YUI Headless Performance Benchmark\n");
    printf("===================================\n");

    benchmarkMount();
    benchmarkReconcileNoChange();
    benchmarkReconcileReorder();
    benchmarkLayout();
    benchmarkFullFrame();

    printf("\nDone.\n");
    return 0;
}
