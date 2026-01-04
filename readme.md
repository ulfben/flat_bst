# flat::bst - a flat, index-based binary search tree for C++

`flat::bst<T, Compare, IndexT>` is a header-only binary search tree that stores nodes in a flat container and links them with compact integer handles rather than pointers. It aims to be a pragmatic, cache-friendly alternative to pointer-based node trees for unique keys and read-heavy workloads.

This design was inspired by Jens Weller’s article "[Looking at binary trees in C++](https://meetingcpp.com/blog/items/Looking-at-binary-trees-in-Cpp.html)", where he explores possible designs and points out the potential of using flat storage with indices rather than heap-allocated pointers.

It was further refined after input from @filippocrocchini: if you expose raw slot indices as identifiers and allow erasure, a slot can later be reused and an old index can silently start referring to a different node - a rare, data-dependent bug that is very hard to pin down. The fix is to use generational handles (index + small generation counter), so stale handles fail validation instead of turning into "accidentally valid" references. He also suggested reusing a child-link field in freed nodes to chain a free list, keeping the structure minimal: a single `std::vector` plus a root handle and a free-list head, with no extra allocations or side containers.

[Try it live on Compiler Explorer](https://compiler-explorer.com/z/Pjd5ea7zj) or see demo.cpp for tests and usage.

## Why flat storage

Pointer-based node trees typically allocate each node on the heap. That's simple and flexible, but it scatters nodes in memory and increases allocator overhead. A flat, index-based layout offers several benefits:

* Fewer allocations - a growing `std::vector` amortizes allocations and reduces allocator churn.
* Better cache locality - nodes live close together, improving traversal performance.
* Stable handles - compact handles survive vector growth and can be stored outside the tree; generations prevent stale-handle aliasing after erase.
* Hole reuse - erased nodes become free slots that can be reused without reshuffling the structure.

## Important notes

* Not self-balancing: `insert` can produce a skewed tree (for example, inserting already-sorted data). Use `rebalance()` / `rebuild_compact()` to rebuild into a balanced shape.
* Handle invalidation: operations that rebuild storage (`build_from_range`, `build_from_sorted_unique`, `rebalance()`, `rebuild_compact()`, and the range/initializer-list constructors) invalidate all previously issued handles.
* Pointer lifetime: `find()` returns a pointer into internal storage. That pointer is invalidated by rebuild operations, by erasing that node, and potentially by any operation that causes the underlying vector to reallocate (unless you `reserve()` enough capacity up front).

## Feature overview

* Unique-key BST with `insert`, `emplace`, `erase`, `contains`, `find`, `find_index`.
* Bulk build from arbitrary ranges (`build_from_range`, sorts + uniques) or from pre-sorted-unique ranges (`build_from_sorted_unique`).
* `rebalance()` / `rebuild_compact()` rebuild from inorder values to produce a balanced tree and remove holes (invalidates all handles).
* Iterative traversals: `inorder`, `preorder`, `postorder` via callbacks.
* In-order iteration is provided via `begin()`, `end()` (and `begin_inorder()`, `end_inorder()`).
* Header-only, requires C++20 or later.

## Quick start

```cpp
#include "flat_bst.hpp"
#include <vector>
#include <print>

int main() {
    flat::bst<int> t;

    t.insert(8);
    t.insert(3);
    t.insert(10);
    t.insert(1);
    t.insert(6);
    t.insert(14);

    if (t.contains(6)) {
        // ...
    }

    // range build (arbitrary order) - sorts, uniques, then builds balanced
    std::vector<int> vals { 7, 2, 11, 4, 13, 5, 9, 1 };
    t.build_from_range(vals.begin(), vals.end());

    // balanced build from sorted-unique input
    std::vector<int> sorted { 1,2,3,4,5,6,7,8,9 };
    flat::bst<int> u;
    u.build_from_sorted_unique(sorted.begin(), sorted.end());

    // inorder iteration
    for (const auto& v : t) {
        std::print("{} ", v);
    }

    return 0;
}
```

See demo.cpp for additional usage examples and tests.
