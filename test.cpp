#include <gtest/gtest.h>

#include "flat_bst.hpp" 
#include <vector>

using flat::bst;

template <class T>
static std::vector<T> inorder_dump(const bst<T>& t){
    std::vector<T> out;
    t.for_each_inorder([&](const T& v){ out.push_back(v); });
    return out;
}

template <class T>
static std::vector<T> preorder_dump(const bst<T>& t){
    std::vector<T> out;
    t.for_each_preorder([&](const T& v){ out.push_back(v); });
    return out;
}

template <class T>
static std::vector<T> postorder_dump(const bst<T>& t){
    std::vector<T> out;
    t.for_each_postorder([&](const T& v){ out.push_back(v); });
    return out;
}

template <class T>
static void expect_equal_vec(const std::vector<T>& a, const std::vector<T>& b){
    EXPECT_EQ(a, b);
}

template <class T>
static void expect_strictly_increasing(const std::vector<T>& v){
    for(size_t i = 1; i < v.size(); ++i){
        EXPECT_LT(v[i - 1], v[i]) << "at index " << i;
    }
}

// Test 1 - basic insert and size, inorder returns sorted unique values
TEST(FlatBst, BasicInsertAndInorder){
    bst<int> t;
    for(int v : {5, 2, 8, 1, 3, 7, 9}){
        auto [_, ins] = t.insert(v);
        ASSERT_TRUE(ins);
    }
    EXPECT_EQ(t.size(), 7u);
    auto in = inorder_dump(t);
    std::vector<int> expect = {1, 2, 3, 5, 7, 8, 9};
    expect_equal_vec(in, expect);
    expect_strictly_increasing(in);
}

// Test 2 - duplicates rejected, contains stays true, size does not grow
TEST(FlatBst, DuplicateInsert){
    bst<int> t;
    (void) t.insert(10);
    auto r1 = t.insert(10);
    EXPECT_FALSE(r1.second);
    EXPECT_EQ(t.size(), 1u);
    EXPECT_TRUE(t.contains(10));
    expect_equal_vec(inorder_dump(t), std::vector<int>{10});
}

// Test 3 - erase leaf node
TEST(FlatBst, EraseLeaf){
    bst<int> t;
    for(int v : {5, 2, 8, 1, 3}){
        (void) t.insert(v);
    }
    EXPECT_TRUE(t.erase(1));
    EXPECT_EQ(t.size(), 4u);
    EXPECT_FALSE(t.contains(1));
    expect_equal_vec(inorder_dump(t), std::vector<int>({2, 3, 5, 8}));
}

// Test 4 - erase node with one child
TEST(FlatBst, EraseOneChild){
    bst<int> t;
    for(int v : {5, 2, 8, 1}){
        (void) t.insert(v);
    }
    EXPECT_TRUE(t.erase(2)); // 2 has one child (1)
    EXPECT_EQ(t.size(), 3u);
    expect_equal_vec(inorder_dump(t), std::vector<int>({1, 5, 8}));
    EXPECT_FALSE(t.contains(2));
}

// Test 5 - erase node with two children (successor splice)
TEST(FlatBst, EraseTwoChildren){
    bst<int> t;
    for(int v : {5, 2, 8, 1, 3, 7, 9}){
        (void) t.insert(v);
    }
    EXPECT_TRUE(t.erase(2));
    EXPECT_FALSE(t.contains(2));
    EXPECT_EQ(t.size(), 6u);

    auto in = inorder_dump(t);
    std::vector<int> expect = {1, 3, 5, 7, 8, 9};
    expect_equal_vec(in, expect);
    expect_strictly_increasing(in);
}

// Test 6 - find and find_index
TEST(FlatBst, FindAndFindIndex){
    bst<int> t;
    const auto npos = bst<int>::npos;

    for(int v : {4, 2, 6, 1, 3, 5, 7}) (void)t.insert(v);

    auto* p3 = t.find_ptr(3);
    ASSERT_NE(p3, nullptr);
    EXPECT_EQ(*p3, 3);

    auto* p42 = t.find_ptr(42);
    EXPECT_EQ(p42, nullptr);

    auto i5 = t.find_handle(5);
    EXPECT_NE(i5, npos);

    auto i0 = t.find_handle(0);
    EXPECT_EQ(i0, npos);
}

// Test 7 - traversal orders on a known tree
TEST(FlatBst, TraversalOrders){
    bst<int> t;
    for(int v : {4, 2, 6, 1, 3, 5, 7}){
        (void) t.insert(v);
    }

    expect_equal_vec(inorder_dump(t), std::vector<int>({1, 2, 3, 4, 5, 6, 7}));
    expect_equal_vec(preorder_dump(t), std::vector<int>({4, 2, 1, 3, 6, 5, 7}));
    expect_equal_vec(postorder_dump(t), std::vector<int>({1, 3, 2, 5, 7, 6, 4}));
}

// Test 8 - const in-order iterator matches inorder traversal
TEST(FlatBst, InorderIteratorMatchesTraversal){
    bst<int> t;
    for(int v : {10, 5, 15, 3, 7, 12, 18}){
        (void) t.insert(v);
    }

    std::vector<int> via_iter;
    for(auto it = t.begin(); it != t.end(); ++it){
        via_iter.push_back(*it);
    }

    expect_equal_vec(via_iter, inorder_dump(t));
    expect_strictly_increasing(via_iter);
}

// Test 9 - clear, empty, reserve have sane effects
TEST(FlatBst, ClearEmptyReserve){
    bst<int> t;
    t.reserve(100);
    EXPECT_TRUE(t.empty());

    for(int v : {3, 1, 4}){
        (void) t.insert(v);
    }
    EXPECT_FALSE(t.empty());

    t.clear();
    EXPECT_EQ(t.size(), 0u);
    EXPECT_TRUE(t.empty());

    (void) t.insert(2);
    expect_equal_vec(inorder_dump(t), std::vector<int>{2});
}

// Test 10 - range constructor handles unsorted input with duplicates (sort+unique path)
TEST(FlatBst, CtorRangeUnsortedDupes){
    std::vector<int> v = {5, 2, 8, 1, 3, 7, 9, 3, 5};
    bst<int> t(v.begin(), v.end());
    EXPECT_EQ(t.size(), 7u);
    expect_equal_vec(inorder_dump(t), std::vector<int>({1, 2, 3, 5, 7, 8, 9}));
}

// Test 11 - range constructor fast path on already sorted unique input
TEST(FlatBst, CtorRangeSortedUniqueFastPath){
    std::vector<int> v = {1, 2, 3, 4, 5, 6, 7};
    bst<int> t(v.begin(), v.end());
    EXPECT_EQ(t.size(), 7u);
    expect_equal_vec(inorder_dump(t), std::vector<int>({1, 2, 3, 4, 5, 6, 7}));
    expect_equal_vec(preorder_dump(t), std::vector<int>({4, 2, 1, 3, 6, 5, 7}));
}

// Test 12 - initializer list constructor with duplicates
TEST(FlatBst, IlistCtorDupes){
    bst<int> t{3, 1, 4, 3, 1};
    EXPECT_EQ(t.size(), 3u);
    expect_equal_vec(inorder_dump(t), std::vector<int>({1, 3, 4}));
}

// Test 13 - bulk_insert basic and duplicate handling
TEST(FlatBst, BulkInsert){
    bst<int> t;
    std::vector<int> a = {5, 2, 8, 1, 3, 7, 9};

    auto n1 = t.insert(a.begin(), a.end());
    EXPECT_EQ(n1, a.size());
    EXPECT_EQ(t.size(), a.size());
    expect_equal_vec(inorder_dump(t), std::vector<int>({1, 2, 3, 5, 7, 8, 9}));

    std::vector<int> b = {1, 2, 2, 10};
    auto n2 = t.insert(b.begin(), b.end());
    EXPECT_EQ(n2, 1u);
    EXPECT_EQ(t.size(), a.size() + 1u);
    EXPECT_TRUE(t.contains(10));
    expect_equal_vec(inorder_dump(t), std::vector<int>({1, 2, 3, 5, 7, 8, 9, 10}));
}

// Test 14 - build_from_sorted_unique API constructs balanced tree
TEST(FlatBst, BuildFromSortedUniqueApi){
    std::vector<int> v = {1, 2, 3, 4, 5, 6, 7};
    bst<int> t;

    t.build_from_sorted_unique(v.begin(), v.end());

    EXPECT_EQ(t.size(), 7u);
    expect_equal_vec(inorder_dump(t), std::vector<int>({1, 2, 3, 4, 5, 6, 7}));
    expect_equal_vec(preorder_dump(t), std::vector<int>({4, 2, 1, 3, 6, 5, 7}));
}

// Test 15 - build_from_range API sorts, uniques, and balances
TEST(FlatBst, BuildFromRangeApi){
    std::vector<int> v = {5, 2, 8, 1, 3, 7, 9, 3, 5};
    bst<int> t;

    t.build_from_range(v.begin(), v.end());

    EXPECT_EQ(t.size(), 7u);
    expect_equal_vec(inorder_dump(t), std::vector<int>({1, 2, 3, 5, 7, 8, 9}));
}

// Test 16 - rebalance on a degenerate tree changes shape but preserves inorder and size
TEST(FlatBst, RebalanceChangesShapePreservesOrder){
    bst<int> t;
    for(int i = 1; i <= 7; ++i){
        (void) t.insert(i);
    }

    expect_equal_vec(preorder_dump(t), std::vector<int>({1, 2, 3, 4, 5, 6, 7}));
    expect_equal_vec(inorder_dump(t), std::vector<int>({1, 2, 3, 4, 5, 6, 7}));

    auto before_size = t.size();

    t.rebuild_balanced();

    EXPECT_EQ(t.size(), before_size);
    expect_equal_vec(inorder_dump(t), std::vector<int>({1, 2, 3, 4, 5, 6, 7}));
    expect_equal_vec(preorder_dump(t), std::vector<int>({4, 2, 1, 3, 6, 5, 7}));

    EXPECT_TRUE(t.contains(5));
    EXPECT_FALSE(t.contains(42));

    EXPECT_TRUE(t.erase(4));
    expect_equal_vec(inorder_dump(t), std::vector<int>({1, 2, 3, 5, 6, 7}));
}

#include <stdexcept>
#include <functional>

// Generic inorder dump for any bst<T, Compare, IndexT>
template <class Tree>
static auto inorder_dump_any(const Tree& t){
    using T = typename Tree::value_type;
    std::vector<T> out;
    t.for_each_inorder([&](const T& v){ out.push_back(v); });
    return out;
}

// ------------------------------------------------------------
// Test 17 - stale handle after erase + immediate slot reuse
TEST(FlatBst, HandleBecomesStaleAfterEraseAndReuse){
    bst<int> t;

    auto [h10, ins10] = t.insert(10);
    ASSERT_TRUE(ins10);
    auto [h5, ins5] = t.insert(5);
    ASSERT_TRUE(ins5);

    ASSERT_NE(t.try_get(h10), nullptr);
    EXPECT_EQ(t.at(h10), 10);

    EXPECT_TRUE(t.erase(10));

    // Old handle must go stale
    EXPECT_EQ(t.try_get(h10), nullptr);
    EXPECT_THROW((void) t.at(h10), std::out_of_range);

    // Next insertion should reuse the freed slot (free list head),
    // producing a different handle value (same raw idx, bumped generation).
    auto [h20, ins20] = t.insert(20);
    ASSERT_TRUE(ins20);
    EXPECT_NE(h20, h10);
    EXPECT_EQ(t.at(h20), 20);

    expect_equal_vec(inorder_dump(t), std::vector<int>({5, 20}));
}

// Test 18 - erase(two children) invalidates erased handle, keeps others valid
TEST(FlatBst, EraseTwoChildrenInvalidatesErasedHandleOnly){
    bst<int> t;
    for(int v : {4, 2, 6, 1, 3, 5, 7}) (void)t.insert(v);

    auto h4 = t.find_handle(4);
    auto h2 = t.find_handle(2);
    auto h6 = t.find_handle(6);
    ASSERT_NE(h4, bst<int>::npos);
    ASSERT_NE(h2, bst<int>::npos);
    ASSERT_NE(h6, bst<int>::npos);

    EXPECT_TRUE(t.erase(4));
    EXPECT_FALSE(t.contains(4));

    // erased node's handle must be stale
    EXPECT_EQ(t.try_get(h4), nullptr);
    EXPECT_THROW((void) t.at(h4), std::out_of_range);

    // unrelated handles should still be valid
    ASSERT_NE(t.try_get(h2), nullptr);
    ASSERT_NE(t.try_get(h6), nullptr);
    EXPECT_EQ(t.at(h2), 2);
    EXPECT_EQ(t.at(h6), 6);

    expect_equal_vec(inorder_dump(t), std::vector<int>({1, 2, 3, 5, 6, 7}));
}

// Test 19 - try_get/at behavior for invalid handles (npos + cleared tree)
TEST(FlatBst, AtThrowsAndTryGetNullOnInvalidHandles){
    bst<int> t;
    auto [h, ins] = t.insert(1);
    ASSERT_TRUE(ins);

    EXPECT_THROW((void) t.at(bst<int>::npos), std::out_of_range);
    EXPECT_EQ(t.try_get(bst<int>::npos), nullptr);

    t.clear();
    EXPECT_EQ(t.try_get(h), nullptr);
    EXPECT_THROW((void) t.at(h), std::out_of_range);
}

// Test 20 - lower/upper/equal_range correctness
TEST(FlatBst, BoundsQueriesBasic){
    bst<int> t;
    for(int v : {1, 3, 5, 7, 9}) (void)t.insert(v);

    auto lb = [&](int k){ return t.lower_bound_handle(k); };
    auto ub = [&](int k){ return t.upper_bound_handle(k); };

    // lower_bound: first elem where !comp(elem, key)  (elem >= key for std::less)
    EXPECT_EQ(t.at(lb(0)), 1);
    EXPECT_EQ(t.at(lb(1)), 1);
    EXPECT_EQ(t.at(lb(2)), 3);
    EXPECT_EQ(t.at(lb(5)), 5);
    EXPECT_EQ(t.at(lb(6)), 7);
    EXPECT_EQ(lb(10), bst<int>::npos);

    // upper_bound: first elem where comp(key, elem) (elem > key for std::less)
    EXPECT_EQ(t.at(ub(0)), 1);
    EXPECT_EQ(t.at(ub(1)), 3);
    EXPECT_EQ(t.at(ub(5)), 7);
    EXPECT_EQ(ub(9), bst<int>::npos);

    // equal_range = {lower_bound, upper_bound}
    {
        auto [a, b] = t.equal_range_handle(5);
        ASSERT_NE(a, bst<int>::npos);
        EXPECT_EQ(t.at(a), 5);
        ASSERT_NE(b, bst<int>::npos);
        EXPECT_EQ(t.at(b), 7);
    }
    {
        auto [a, b] = t.equal_range_handle(6);
        ASSERT_NE(a, bst<int>::npos);
        EXPECT_EQ(t.at(a), 7);
        ASSERT_NE(b, bst<int>::npos);
        EXPECT_EQ(t.at(b), 7);
    }
    {
        auto [a, b] = t.equal_range_handle(10);
        EXPECT_EQ(a, bst<int>::npos);
        EXPECT_EQ(b, bst<int>::npos);
    }
}

// Test 21 - bounds on empty tree
TEST(FlatBst, BoundsOnEmptyTree){
    bst<int> t;
    EXPECT_EQ(t.lower_bound_handle(0), bst<int>::npos);
    EXPECT_EQ(t.upper_bound_handle(0), bst<int>::npos);
    auto [a, b] = t.equal_range_handle(0);
    EXPECT_EQ(a, bst<int>::npos);
    EXPECT_EQ(b, bst<int>::npos);
}

// Test 22 - swap and copy independence
TEST(FlatBst, SwapAndCopyIndependence){
    bst<int> a;
    bst<int> b;
    for(int v : {1, 2, 3}) (void)a.insert(v);
    for(int v : {10, 20}) (void)b.insert(v);

    a.swap(b);

    expect_equal_vec(inorder_dump(a), std::vector<int>({10, 20}));
    expect_equal_vec(inorder_dump(b), std::vector<int>({1, 2, 3}));

    bst<int> c = b;          // copy
    (void) c.insert(4);
    EXPECT_TRUE(c.contains(4));
    EXPECT_FALSE(b.contains(4)); // original unchanged
    expect_equal_vec(inorder_dump(b), std::vector<int>({1, 2, 3}));
    expect_equal_vec(inorder_dump(c), std::vector<int>({1, 2, 3, 4}));
}

// Test 23 - custom comparator (descending order)
TEST(FlatBst, CustomComparatorDescendingOrder){
    bst<int, std::greater<int>> t;
    for(int v : {1, 2, 3, 4, 5}) (void)t.insert(v);

    // Inorder should be sorted according to Compare, i.e. descending.
    auto in = inorder_dump_any(t);
    expect_equal_vec(in, std::vector<int>({5, 4, 3, 2, 1}));
}

// ------------------------------------------------------------
// Exception-safety + move-only tests

struct ThrowOnMove final{
    int x = 0;
    static inline int moves_left = -1; // -1 = never throw

    explicit ThrowOnMove(int v = 0) noexcept : x(v){}

    ThrowOnMove(const ThrowOnMove&) noexcept = default;
    ThrowOnMove& operator=(const ThrowOnMove&) noexcept = default;

    ThrowOnMove(ThrowOnMove&& other){
        if(moves_left == 0) throw std::runtime_error("ThrowOnMove move");
        if(moves_left > 0) --moves_left;
        x = other.x;
    }
};

struct ThrowOnMoveComp final{
    bool operator()(const ThrowOnMove& a, const ThrowOnMove& b) const noexcept{
        return a.x < b.x;
    }
};

// Test 24 - exception safety: failed insert into freed slot restores free list
TEST(FlatBst, ExceptionSafetyInsertIntoFreedSlotRestoresFreeList){
    bst<ThrowOnMove, ThrowOnMoveComp> t;

    (void) t.insert(ThrowOnMove{1});
    (void) t.insert(ThrowOnMove{2});
    (void) t.insert(ThrowOnMove{3});

    EXPECT_TRUE(t.erase(ThrowOnMove{2}));
    EXPECT_EQ(t.size(), 2u);

    // Next insert will try to reuse the freed slot (revive path).
    ThrowOnMove::moves_left = 0;
    EXPECT_THROW((void) t.insert(ThrowOnMove{42}), std::runtime_error);

    // Tree should be unchanged, and free list should still be consistent
    EXPECT_EQ(t.size(), 2u);
    EXPECT_FALSE(t.contains(ThrowOnMove{42}));

    // Now allow moves, and ensure we can successfully insert again.
    ThrowOnMove::moves_left = -1;
    auto [h2, ins2] = t.insert(ThrowOnMove{2});
    ASSERT_TRUE(ins2);
    ASSERT_NE(t.try_get(h2), nullptr);
    EXPECT_EQ(t.try_get(h2)->x, 2);

    EXPECT_EQ(t.size(), 3u);

    // Verify ordering via an inorder dump of x-values
    std::vector<int> xs;
    t.for_each_inorder([&](const ThrowOnMove& v){ xs.push_back(v.x); });
    expect_equal_vec(xs, std::vector<int>({1, 2, 3}));
}

// Move-only type to ensure insert(value_type&&) works and comparisons don't require copies
struct MoveOnly final{
    int x = 0;
    explicit MoveOnly(int v = 0) noexcept : x(v){}

    MoveOnly(const MoveOnly&) = delete;
    MoveOnly& operator=(const MoveOnly&) = delete;

    MoveOnly(MoveOnly&&) noexcept = default;
    MoveOnly& operator=(MoveOnly&&) noexcept = default;
};

struct MoveOnlyComp final{
    bool operator()(const MoveOnly& a, const MoveOnly& b) const noexcept{
        return a.x < b.x;
    }
};

// Test 25 - move-only values: insert/contains/erase still work
TEST(FlatBst, MoveOnlyValuesWorkForInsertContainsErase){
    bst<MoveOnly, MoveOnlyComp> t;

    auto [h5, ins5] = t.insert(MoveOnly{5});
    ASSERT_TRUE(ins5);
    auto [h2, ins2] = t.insert(MoveOnly{2});
    ASSERT_TRUE(ins2);
    auto [h8, ins8] = t.insert(MoveOnly{8});
    ASSERT_TRUE(ins8);

    ASSERT_NE(t.try_get(h5), nullptr);
    EXPECT_EQ(t.try_get(h5)->x, 5);

    EXPECT_TRUE(t.contains(MoveOnly{2}));
    EXPECT_FALSE(t.contains(MoveOnly{7}));

    EXPECT_TRUE(t.erase(MoveOnly{5}));
    EXPECT_FALSE(t.contains(MoveOnly{5}));
    EXPECT_EQ(t.try_get(h5), nullptr);

    std::vector<int> xs;
    t.for_each_inorder([&](const MoveOnly& v){ xs.push_back(v.x); });
    expect_equal_vec(xs, std::vector<int>({2, 8}));
}

// Test 26 - erase missing key is a no-op
TEST(FlatBst, EraseMissingKeyNoOp){
    bst<int> t;
    for(int v : {1, 2, 3}) (void)t.insert(v);

    auto before = inorder_dump(t);
    EXPECT_FALSE(t.erase(42));
    EXPECT_EQ(t.size(), 3u);
    expect_equal_vec(inorder_dump(t), before);
}
