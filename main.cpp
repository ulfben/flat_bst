#include "flat_bst.hpp"
#include <cassert>
#include <vector>
using flat::bst;

template<class T>
static std::vector<T> inorder_dump(const bst<T>& t){
	std::vector<T> out;
	t.inorder([&](const T& v){ out.push_back(v); });
	return out;
}

template<class T>
static std::vector<T> preorder_dump(const bst<T>& t){
	std::vector<T> out;
	t.preorder([&](const T& v){ out.push_back(v); });
	return out;
}

template<class T>
static std::vector<T> postorder_dump(const bst<T>& t){
	std::vector<T> out;
	t.postorder([&](const T& v){ out.push_back(v); });
	return out;
}

template<class T>
static void expect_equal_vec(const std::vector<T>& a, const std::vector<T>& b){
	assert(a == b);	
}

template<class T>
static void expect_strictly_increasing(const std::vector<T>& v){
	for(size_t i = 1; i < v.size(); ++i){
		assert(v[i - 1] < v[i]);
	}
}

// Test 1 - basic insert and size, inorder returns sorted unique values
static void test_basic_insert_and_inorder(){
	bst<int> t;	
	for(int v : {5, 2, 8, 1, 3, 7, 9}){
		auto [_, ins] = t.insert(v);
		assert(ins);
	}
	assert(t.size() == 7);
	auto in = inorder_dump(t);
	std::vector<int> expect = {1,2,3,5,7,8,9};
	expect_equal_vec(in, expect);
	expect_strictly_increasing(in);
}

// Test 2 - duplicates rejected, contains stays true, size does not grow
static void test_duplicate_insert(){
	bst<int> t;
	auto _ = t.insert(10);
	auto r1 = t.insert(10);
	assert(r1.second == false);
	assert(t.size() == 1);
	assert(t.contains(10));
	auto in = inorder_dump(t);
	expect_equal_vec(in, std::vector<int>{10});
}

// Test 3 - erase leaf node
static void test_erase_leaf(){
	bst<int> t;
	// Build a small tree where 1 will be a leaf
	for(int v : {5, 2, 8, 1, 3}){
		auto _ = t.insert(v);
	}
	assert(t.erase(1) == true);
	assert(t.size() == 4);
	assert(!t.contains(1));
	expect_equal_vec(inorder_dump(t), std::vector<int>{2, 3, 5, 8});
}

// Test 4 - erase node with one child
static void test_erase_one_child(){
	bst<int> t;
	// 5 root, 2 left, 1 left-left (so 2 has one child after removing 3)
	for(int v : {5, 2, 8, 1}){
		t.insert(v);
	}
	assert(t.erase(2) == true);  // 2 has one child (1)
	assert(t.size() == 3);
	expect_equal_vec(inorder_dump(t), std::vector<int>{1, 5, 8});
	assert(!t.contains(2));
}

// Test 5 - erase node with two children (successor splice)
static void test_erase_two_children(){
	bst<int> t;
	for(int v : {5, 2, 8, 1, 3, 7, 9}){
		t.insert(v);
	}
	// erase 2, which has children 1 and 3
	assert(t.erase(2));
	assert(!t.contains(2));
	assert(t.size() == 6);
	auto in = inorder_dump(t);
	std::vector<int> expect = {1,3,5,7,8,9};
	expect_equal_vec(in, expect);
	expect_strictly_increasing(in);
}

// Test 6 - find and find_index
static void test_find_and_find_index(){
	bst<int> t;
	const auto npos = bst<int>::npos;
	for(int v : {4, 2, 6, 1, 3, 5, 7}) t.insert(v);
	auto* p3 = t.find(3);
	assert(p3 && *p3 == 3);
	auto* p42 = t.find(42);
	assert(p42 == nullptr);
	auto i5 = t.find_index(5);
	assert(i5 != npos);
	auto i0 = t.find_index(0);
	assert(i0 == npos);
}

// Test 7 - traversal orders on a known tree
static void test_traversal_orders(){
	bst<int> t;
	// Shape:
	//        4
	//      2   6
	//     1 3 5 7
	for(int v : {4, 2, 6, 1, 3, 5, 7}){
		t.insert(v);
	}

	expect_equal_vec(inorder_dump(t), std::vector<int>{1, 2, 3, 4, 5, 6, 7});
	expect_equal_vec(preorder_dump(t), std::vector<int>{4, 2, 1, 3, 6, 5, 7});
	expect_equal_vec(postorder_dump(t), std::vector<int>{1, 3, 2, 5, 7, 6, 4});
}

// Test 8 - const in-order iterator matches inorder traversal
static void test_inorder_iterator(){
	bst<int> t;
	for(int v : {10, 5, 15, 3, 7, 12, 18}){
		t.insert(v);
	}

	std::vector<int> via_iter;
	for(auto it = t.begin_inorder(); it != t.end_inorder(); ++it){
		via_iter.push_back(*it);
	}
	expect_equal_vec(via_iter, inorder_dump(t));
	expect_strictly_increasing(via_iter);
}

// Test 9 - clear, empty, reserve have sane effects
static void test_clear_empty_reserve(){
	bst<int> t;
	t.reserve(100);
	assert(t.empty());
	for(int v : {3, 1, 4}){
		t.insert(v);
	}
	assert(!t.empty());
	t.clear();
	assert(t.size() == 0);
	assert(t.empty());
	// operations after clear still work
	t.insert(2);
	expect_equal_vec(inorder_dump(t), std::vector<int>{2});
}

// Test 10 - range constructor handles unsorted input with duplicates (sort+unique path)
static void test_ctor_range_unsorted_dupes(){
	std::vector<int> v = {5,2,8,1,3,7,9,3,5};
	bst<int> t(v.begin(), v.end()); // should sort+unique and then build balanced
	assert(t.size() == 7);
	expect_equal_vec(inorder_dump(t), std::vector<int>{1, 2, 3, 5, 7, 8, 9});
}

// Test 11 - range constructor fast path on already sorted unique input
static void test_ctor_range_sorted_unique_fastpath(){
	std::vector<int> v = {1,2,3,4,5,6,7};
	bst<int> t(v.begin(), v.end());
	assert(t.size() == 7);
	expect_equal_vec(inorder_dump(t), std::vector<int>{1, 2, 3, 4, 5, 6, 7});
	// The midpoint-balanced build should yield this preorder:
	expect_equal_vec(preorder_dump(t), std::vector<int>{4, 2, 1, 3, 6, 5, 7});
}

// Test 12 - initializer list constructor with duplicates
static void test_ilist_ctor_dupes(){
	bst<int> t{3,1,4,3,1};
	assert(t.size() == 3);
	expect_equal_vec(inorder_dump(t), std::vector<int>{1, 3, 4});
}

// Test 13 - bulk_insert basic and duplicate handling
static void test_bulk_insert(){
	bst<int> t;
	std::vector<int> a = {5,2,8,1,3,7,9};
	auto n1 = t.insert(a.begin(), a.end());
	assert(n1 == a.size());
	assert(t.size() == a.size());
	expect_equal_vec(inorder_dump(t), std::vector<int>{1, 2, 3, 5, 7, 8, 9});

	// Insert some duplicates and one new element
	std::vector<int> b = {1,2,2,10};
	auto n2 = t.insert(b.begin(), b.end());
	assert(n2 == 1);                 // only 10 should be inserted
	assert(t.size() == a.size() + 1);
	assert(t.contains(10));
	expect_equal_vec(inorder_dump(t), std::vector<int>{1, 2, 3, 5, 7, 8, 9, 10});
}

// Test 14 - build_from_sorted_unique API constructs balanced tree
static void test_build_from_sorted_unique_api(){
	std::vector<int> v = {1,2,3,4,5,6,7};
	bst<int> t;
	t.build_from_sorted_unique(v.begin(), v.end());
	assert(t.size() == 7);
	expect_equal_vec(inorder_dump(t), std::vector<int>{1, 2, 3, 4, 5, 6, 7});
	expect_equal_vec(preorder_dump(t), std::vector<int>{4, 2, 1, 3, 6, 5, 7});
}

// Test 15 - build_from_range API sorts, uniques, and balances
static void test_build_from_range_api(){
	std::vector<int> v = {5,2,8,1,3,7,9,3,5};
	bst<int> t;
	t.build_from_range(v.begin(), v.end());
	assert(t.size() == 7);
	expect_equal_vec(inorder_dump(t), std::vector<int>{1, 2, 3, 5, 7, 8, 9});
}

// Test 16 - rebalance on a degenerate tree changes shape but preserves inorder and size
static void test_rebalance_changes_shape_preserves_order(){
	bst<int> t;
	// Ascending inserts form a degenerate chain
	for(int i = 1; i <= 7; ++i){
		t.insert(i);
	}
	// Preorder for a right-leaning chain of 1..7 is 1..7
	expect_equal_vec(preorder_dump(t), std::vector<int>{1, 2, 3, 4, 5, 6, 7});
	expect_equal_vec(inorder_dump(t), std::vector<int>{1, 2, 3, 4, 5, 6, 7});
	auto before_size = t.size();

	t.rebalance();

	// Inorder preserved and size unchanged
	assert(t.size() == before_size);
	expect_equal_vec(inorder_dump(t), std::vector<int>{1, 2, 3, 4, 5, 6, 7});
	// Balanced preorder should now be midpoint layout
	expect_equal_vec(preorder_dump(t), std::vector<int>{4, 2, 1, 3, 6, 5, 7});

	// Basic ops still work after rebalance
	assert(t.contains(5));
	assert(!t.contains(42));
	t.erase(4);
	expect_equal_vec(inorder_dump(t), std::vector<int>{1, 2, 3, 5, 6, 7});
}

int main(){
	test_basic_insert_and_inorder();
	test_duplicate_insert();
	test_erase_leaf();
	test_erase_one_child();
	test_erase_two_children();
	test_find_and_find_index();
	test_traversal_orders();
	test_inorder_iterator();
	test_clear_empty_reserve();
	test_ctor_range_unsorted_dupes();
	test_ctor_range_sorted_unique_fastpath();
	test_ilist_ctor_dupes();
	test_bulk_insert();
	test_build_from_sorted_unique_api();
	test_build_from_range_api();
	test_rebalance_changes_shape_preserves_order();
	return 0;
}