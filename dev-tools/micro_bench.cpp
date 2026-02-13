#include <chrono>
#include <iostream>
#include <vector>
#include <yui/core/VNode.hpp>
#include <yui/core/Reconciler.hpp>

using namespace yui;
using Clock = std::chrono::high_resolution_clock;

// Recursive quad tree builder (same as bench_reconciler)
VNode buildQuadTree(int depth, int id = 0) {
    if (depth <= 1) {
        return Box().setKey(static_cast<int64_t>(id)).backgroundColor(0x3366CCFF);
    }
    int childBase = id * 4 + 1;
    return Column({
        Row({
            buildQuadTree(depth - 1, childBase),
            buildQuadTree(depth - 1, childBase + 1),
        }).flexGrow(1).gap(1),
        Row({
            buildQuadTree(depth - 1, childBase + 2),
            buildQuadTree(depth - 1, childBase + 3),
        }).flexGrow(1).gap(1),
    }).setKey(static_cast<int64_t>(id)).flexGrow(1).gap(1);
}

int countNodes(int depth) {
    if (depth <= 1) return 1;
    return 1 + 2 + 4 * countNodes(depth - 1);
}

template<typename F>
double measure_ms(int iterations, F fn) {
    auto start = Clock::now();
    for (int i = 0; i < iterations; i++) {
        fn();
    }
    auto end = Clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count() / iterations;
}

int main() {
    constexpr int DEPTH = 5;  // 511 nodes
    constexpr int ITERS = 20;

    int nodes = countNodes(DEPTH);
    std::cout << "Testing with depth=" << DEPTH << " (" << nodes << " nodes)\n\n";

    // 1. Just build VNode tree (no reconciliation)
    double build_only = measure_ms(ITERS, [] {
        auto vnode = buildQuadTree(DEPTH);
        (void)vnode;
    });

    // 2. Mount (VNode build + Node creation)
    Reconciler rec1;
    double mount_time = measure_ms(ITERS, [&rec1] {
        auto vnode = buildQuadTree(DEPTH);
        auto root = rec1.mount(vnode);
    });

    // 3. Reconcile (VNode build + diff/update existing)
    Reconciler rec2;
    auto vnode_init = buildQuadTree(DEPTH);
    auto root2 = rec2.mount(vnode_init);

    double reconcile_time = measure_ms(ITERS, [&rec2, &root2] {
        auto vnode = buildQuadTree(DEPTH);
        rec2.reconcile(root2.get(), vnode);
    });

    // 4. Layout only (cached - tree unchanged)
    Reconciler rec3;
    auto vnode3 = buildQuadTree(DEPTH);
    auto root3 = rec3.mount(vnode3);

    double layout_cached = measure_ms(50, [&root3] {
        root3->calculateLayout(800, 800);
    });

    // 5. Layout after reconcile (might be dirty)
    Reconciler rec4;
    auto vnode4 = buildQuadTree(DEPTH);
    auto root4 = rec4.mount(vnode4);
    root4->calculateLayout(800, 800);

    double layout_after_reconcile = measure_ms(ITERS, [&rec4, &root4] {
        auto vnode = buildQuadTree(DEPTH);
        rec4.reconcile(root4.get(), vnode);
        root4->calculateLayout(800, 800);
    });

    std::cout << "=== Breakdown (ms) ===\n";
    std::cout << "VNode build only:           " << build_only << " ms\n";
    std::cout << "Mount (build + create):     " << mount_time << " ms\n";
    std::cout << "Reconcile (build + diff):   " << reconcile_time << " ms\n";
    std::cout << "Layout (cached):            " << layout_cached << " ms\n";
    std::cout << "Full (reconcile + layout):  " << layout_after_reconcile << " ms\n";

    std::cout << "\n=== Derived ===\n";
    std::cout << "Node creation overhead:     " << (mount_time - build_only) << " ms\n";
    std::cout << "Reconcile overhead:         " << (reconcile_time - build_only) << " ms\n";
    std::cout << "Layout dirty cost:          " << (layout_after_reconcile - reconcile_time) << " ms\n";

    std::cout << "\n=== Per-node (us) ===\n";
    std::cout << "VNode build:                " << (build_only * 1000 / nodes) << " us\n";
    std::cout << "Mount:                      " << (mount_time * 1000 / nodes) << " us\n";
    std::cout << "Reconcile:                  " << (reconcile_time * 1000 / nodes) << " us\n";

    return 0;
}
