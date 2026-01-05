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

    auto* p3 = t.find(3);
    ASSERT_NE(p3, nullptr);
    EXPECT_EQ(*p3, 3);

    auto* p42 = t.find(42);
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

    t.rebalance();

    EXPECT_EQ(t.size(), before_size);
    expect_equal_vec(inorder_dump(t), std::vector<int>({1, 2, 3, 4, 5, 6, 7}));
    expect_equal_vec(preorder_dump(t), std::vector<int>({4, 2, 1, 3, 6, 5, 7}));

    EXPECT_TRUE(t.contains(5));
    EXPECT_FALSE(t.contains(42));

    EXPECT_TRUE(t.erase(4));
    expect_equal_vec(inorder_dump(t), std::vector<int>({1, 2, 3, 5, 6, 7}));
}
