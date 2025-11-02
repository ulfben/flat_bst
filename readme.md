# flat::bst - a flat, index-based binary search tree for C++

`flat::bst<T, Compare, IndexT>` is a header-only binary search tree that stores nodes in a flat container and links them with integer indices instead of pointers. It aims to be a pragmatic, cache-friendly alternative to pointer-based node trees for unique keys and read-heavy workloads.

This design was inspired by Jens Weller’s article "[Looking at binary trees in C++](https://meetingcpp.com/blog/items/Looking-at-binary-trees-in-Cpp.html)" where he briefly explores possible designs and points out the potential of using flat storage with indices rather than heap-allocated pointers.

[Try it live on Compiler Explorer](https://compiler-explorer.com/z/rqEKxGsTT) or see main.cpp for tests and useage.

## Why flat storage

Pointer-based node trees typically allocate each node on the heap. That's simple and flexible, but it scatters nodes in memory and increases allocator overhead. A flat, index-based layout offers several benefits:

- Fewer allocations - a growing `std::vector` amortizes allocations and reduces allocator churn.
- Better cache locality - nodes live close together, improving traversal performance.
- Stable indices - indices are small integers that survive vector growth and can be stored outside the tree.
- Hole reuse - erased nodes become tombstones that can be reused without reshuffling the structure.

## Feature overview

- Unique-key BST with `insert`, `emplace`, `erase`, `contains`, `find`, `find_index`.
- Can build from arbitrary ranges or from pre-sorted-unique ranges.
- `rebuild_compact()` to rebalance and remove holes by rebuilding from inorder values.
- Iterative traversals: `inorder`, `preorder`, `postorder` via callbacks.
- in-order iteration is provided via `begin()`, `end()`. Writing iterators for other traversals is straightforward.  
- Header-only, reqiures C++26 or later.

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

    // range build (arbitrary order)
    std::vector<int> vals { 7, 2, 11, 4, 13, 5, 9, 1 };
    t.build_from_range(vals.begin(), vals.end());

    // balanced build from sorted-unique
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

See main.cpp for additional usage examples and tests.
