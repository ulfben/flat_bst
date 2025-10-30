// flat::bst<T, Compare, IndexT> — a flat, index-based binary search tree
// Stores nodes in a std::vector and links them by integer indices for
// better cache locality, fewer allocations, and stable references.
// Inspired by Jens Weller’s “Looking at binary trees in C++”:
//	 https://meetingcpp.com/blog/items/Looking-at-binary-trees-in-Cpp.html
// Live demo: https://compiler-explorer.com/z/1ffT3Y8xd
// Requires C++26. See main.cpp for usage examples.
// Ulf Benjaminsson, 2025
#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

// forward declare the class template so the iterator can name it
namespace flat {
	template<class T, class Compare = std::less<T>, class IndexT = uint32_t>
	class bst;
};

//namespace for the iterator implementation
namespace flat::detail {
	template<class T, class Compare, class IndexT>
	class inorder_iter{
		using tree_t = bst<T, Compare, IndexT>;
		using index_type = IndexT;
		static constexpr index_type npos = tree_t::npos;

		const tree_t* tree_ = nullptr;
		std::vector<index_type> stack_;
		index_type cur_ = npos;

		constexpr void push_left_(index_type i){
			while(tree_t::is_valid(i)){
				stack_.push_back(i);
				i = tree_->left_of(i);
			}
		}

	public:
		using value_type = const T;
		using reference = const T&;
		using pointer = const T*;
		using difference_type = std::ptrdiff_t;
		using iterator_category = std::forward_iterator_tag;

		constexpr inorder_iter() = default;
		explicit constexpr inorder_iter(const tree_t* t, bool end) : tree_(t){
			if(!t || end || !tree_t::is_valid(t->root_index())){ cur_ = npos; return; }
			stack_.reserve(16);
			push_left_(t->root_index());
			cur_ = stack_.back();
			stack_.pop_back();
		}

		constexpr reference operator*()  const noexcept{ return tree_->value_of(cur_); }
		constexpr pointer   operator->() const noexcept{ return &tree_->value_of(cur_); }

		constexpr inorder_iter& operator++(){
			if(!tree_ || !tree_t::is_valid(cur_)){
				return *this;
			}
			index_type next = tree_->right_of(cur_);
			if(tree_t::is_valid(next)){
				push_left_(next);
			}
			if(stack_.empty()){
				cur_ = npos;
			} else{
				cur_ = stack_.back();
				stack_.pop_back();
			}
			return *this;
		}
		constexpr inorder_iter operator++(int){
			auto tmp = *this;
			++(*this);
			return tmp;
		}

		friend constexpr bool operator==(const inorder_iter& a, const inorder_iter& b) noexcept{
			return a.tree_ == b.tree_ && a.cur_ == b.cur_;
		}
		friend constexpr bool operator!=(const inorder_iter& a, const inorder_iter& b) noexcept{
			return !(a == b);
		}
	};
} // namespace flat::detail

namespace flat{
	
	template<class T, class Compare, class IndexT>
	class bst final{
	public:
		using const_inorder_iterator = flat::detail::inorder_iter<T, Compare, IndexT>;
		using value_type = T;
		using size_type = std::size_t;
		using index_type = IndexT;

		static_assert(std::is_unsigned_v<index_type>, "IndexT must be an unsigned integer type");
		static constexpr index_type npos = std::numeric_limits<index_type>::max();

		bst() = default;

		constexpr explicit bst(Compare cmp) noexcept : comp_(std::move(cmp)){}

		// if you already have sorted-unique data, create an empty bst and call build_from_sorted_unique instead
		// this ctor just does the right thing for arbitrary ranges.
		template<class It>
		bst(It first, It last, Compare cmp = Compare{})
			: comp_(std::move(cmp)){
			if(first == last){
				return;
			}
			if constexpr(std::random_access_iterator<It>){ // possible fast path if already sorted and unique				
				bool sorted = std::is_sorted(first, last, comp_);
				bool unique = sorted &&
					std::adjacent_find(first, last, [&](const value_type& a, const value_type& b){
					return !comp_(a, b) && !comp_(b, a);
						}) == last;
				if(sorted && unique){
					build_from_sorted_unique(first, last);
					return;
				}
			}
			build_from_range(first, last);
		}

		bst(std::initializer_list<value_type> values, Compare cmp = Compare{}) : bst(values.begin(), values.end(), cmp){}

		[[nodiscard]] constexpr size_type size() const noexcept{ return alive_; }
		[[nodiscard]] constexpr size_type holes() const noexcept{ return free_.size(); }
		[[nodiscard]] constexpr size_type capacity() const noexcept{ return nodes_.capacity(); }
		[[nodiscard]] constexpr bool empty() const noexcept{ return alive_ == 0; }

		//accessors for the iterators to be able to do their work
		//they bloat the public API a bit, but I don't have to deal with friend classes so...
		[[nodiscard]] inline constexpr index_type root_index() const noexcept{ return root_; }
		static inline constexpr bool is_valid(index_type i) noexcept{ return i != npos; }
		[[nodiscard]] inline constexpr index_type left_of(index_type i)  const noexcept{ return get(i).left; }
		[[nodiscard]] inline constexpr index_type right_of(index_type i) const noexcept{ return get(i).right; }
		[[nodiscard]] inline constexpr const value_type& value_of(index_type i) const noexcept{ return get(i).value; }


		constexpr void reserve(size_type n){ nodes_.reserve(n); }
		constexpr void rebuild_compact(){ rebalance(); }

		constexpr void clear(){
			nodes_.clear();
			free_.clear();
			root_ = npos;
			alive_ = 0;
		}

		// insert / emplace, returns {index, inserted}
		constexpr std::pair<index_type, bool> insert(const value_type& v){
			return insert_impl(v);
		}
		constexpr std::pair<index_type, bool> insert(value_type&& v){
			return insert_impl(std::move(v));
		}
		template<class... Args>
		constexpr std::pair<index_type, bool> emplace(Args&&... args){
			return emplace_impl(std::forward<Args>(args)...);
		}

		//bulk insert from range, returns number of inserted elements
		//note: this merges into existing tree, does not rebalance
		template<class It>
		size_type insert(It first, It last){
			if constexpr(std::forward_iterator<It>){
				auto n = static_cast<size_type>(std::distance(first, last));
				if(n) nodes_.reserve(nodes_.size() + n); // best effort
			}
			size_type inserted = 0;
			for(; first != last; ++first){
				inserted += insert(*first).second ? 1 : 0;
			}
			return inserted;
		}

		// build balanced tree from pre-sorted-unique input. replacing any existing tree contents
		template<class It>
		void build_from_sorted_unique(It first, It last){
			assert(std::is_sorted(first, last, comp_) && "Input range must be sorted according to Compare");
			clear();
			const size_type n = static_cast<size_type>(std::distance(first, last));
			if(n == 0) return;
			nodes_.reserve(n);
			// recursive builder returns index of subtree root
			auto build = [&](auto&& self, It lo, It hi) -> index_type{
				if(lo == hi) return npos;
				It mid = lo + (std::distance(lo, hi) / 2);
				index_type me = make_node(*mid); // create node for *mid
				node& m = get(me);
				m.left = self(self, lo, mid); // left subtree				
				m.right = self(self, std::next(mid), hi); // right subtree
				return me;
				};
			root_ = build(build, first, last);
		}

		// build balanced tree from arbitrary input range (sorts + uniques)
		template<class It>
		void build_from_range(It first, It last){
			std::vector<value_type> vals;
			if constexpr(std::forward_iterator<It>){
				vals.reserve(static_cast<size_type>(std::distance(first, last)));
			}
			for(; first != last; ++first){
				vals.push_back(*first);
			}
			std::sort(vals.begin(), vals.end(), comp_);
			vals.erase(std::unique(vals.begin(), vals.end(),
				[&](const value_type& a, const value_type& b){
					return !comp_(a, b) && !comp_(b, a);
				}), vals.end());
			build_from_sorted_unique(vals.begin(), vals.end());
		}

		void rebalance(){
			if(alive_ < 2){ return; }
			std::vector<value_type> vals;
			vals.reserve(alive_);
			inorder([&](const value_type& v){ vals.push_back(v); }); //inorder sorts by cmp_		
			build_from_sorted_unique(vals.begin(), vals.end());
		}

		[[nodiscard]] constexpr index_type find_index(const value_type& key) const noexcept{
			return find_with_parent(key).second;
		}

		[[nodiscard]] constexpr bool contains(const value_type& key) const noexcept{
			return find_index(key) != npos;
		}

		// returns pointer to value or nullptr 
		[[nodiscard]] inline constexpr const value_type* find(const value_type& key) const noexcept{
			index_type idx = find_index(key);
			return get_ptr(idx);
		}

		[[nodiscard]] inline constexpr const value_type* get_ptr(index_type i) const noexcept{
			if(is_valid(i) && i < nodes_.size() && nodes_[i].has_value()){
				return &value_of(i);
			}
			return nullptr;
		}


		// erase by key - returns true if erased
		constexpr bool erase(const value_type& key) noexcept{
			auto [parent, cur] = find_with_parent(key);
			if(cur == npos){
				return false;
			}
			erase_at(parent, cur);
			return true;
		}

		// traversals: inorder, preorder, postorder. Callback recieves const T&
		template<class F>
		constexpr void inorder(F&& f) const{
			std::vector<index_type> stack;
			stack.reserve(64);
			index_type index = root_;
			while(is_valid(index) || !stack.empty()){
				while(is_valid(index)){
					stack.push_back(index);
					index = left_of(index);
				}
				index = stack.back();
				stack.pop_back();
				f(value_of(index));
				index = right_of(index);
			}
		}

		template<class F>
		constexpr void preorder(F&& f) const{
			if(!is_valid(root_)) return;
			std::vector<index_type> stack;
			stack.reserve(64);
			stack.push_back(root_);
			while(!stack.empty()){
				index_type index = stack.back();
				stack.pop_back();
				f(value_of(index));
				const node& n = get(index);
				if(is_valid(n.right)){
					stack.push_back(n.right);
				}
				if(is_valid(n.left)){
					stack.push_back(n.left);
				}
			}
		}

		template<class F>
		constexpr void postorder(F&& f) const{
			if(!is_valid(root_)) return;
			std::vector<index_type> stack1, stack2;
			stack1.reserve(64);
			stack2.reserve(64);
			stack1.push_back(root_);
			while(!stack1.empty()){
				index_type index = stack1.back();
				stack1.pop_back();
				stack2.push_back(index);
				const node& n = get(index);
				if(is_valid(n.left)){
					stack1.push_back(n.left);
				}
				if(is_valid(n.right)){
					stack1.push_back(n.right);
				}
			}
			while(!stack2.empty()){
				index_type index = stack2.back();
				stack2.pop_back();
				f(value_of(index));
			}
		}

		inline constexpr const_inorder_iterator begin_inorder() const{ return const_inorder_iterator(this, false); }
		inline constexpr const_inorder_iterator end_inorder()   const{ return const_inorder_iterator(this, true); }
		inline constexpr auto begin() const{ return begin_inorder(); }
		inline constexpr auto end() const{ return end_inorder(); }

	private:
		struct node final{
			value_type value;
			index_type left = npos;
			index_type right = npos;
			constexpr explicit node(const value_type& v) noexcept : value(v){}
			constexpr explicit node(value_type&& v) noexcept : value(std::move(v)){}
			template<class... Args>
			constexpr explicit node(std::in_place_t, Args&&... args) noexcept : value(std::forward<Args>(args)...){}
		};
		index_type root_ = npos;
		size_type alive_ = 0;
		std::vector<std::optional<node>> nodes_;
		std::vector<index_type> free_;
		[[no_unique_address]] Compare comp_{};

		template<class V>
		constexpr index_type make_node(V&& v){
			if(!free_.empty()){ //can we re-use a free slot?
				index_type idx = free_.back();
				try{ // try construct before popping
					nodes_[idx].emplace(std::forward<V>(v));
					free_.pop_back();
					++alive_;
					return idx;
				} catch(...){
					// leave free_ unchanged and rethrow
					throw;
				}
			}
			// no free slots available, let's append (grow)
			index_type idx = static_cast<index_type>(nodes_.size());
			if(nodes_.size() == size_type(std::numeric_limits<index_type>::max())){
				throw std::length_error("flat::bst index overflow!");
			}
			nodes_.emplace_back(node(std::forward<V>(v))); // may throw, but no state updated yet
			++alive_;
			return idx;
		}

		constexpr void tombstone(index_type idx) noexcept{
			assert(is_valid(idx) && idx < nodes_.size() && nodes_[idx].has_value());
			nodes_[idx].reset();
			free_.push_back(idx);
			--alive_;
		}

		constexpr node& get(index_type idx) noexcept{
			assert(is_valid(idx) && idx < nodes_.size() && nodes_[idx].has_value());
			return *nodes_[idx];
		}
		constexpr const node& get(index_type idx) const noexcept{
			assert(is_valid(idx) && idx < nodes_.size() && nodes_[idx].has_value());
			return *nodes_[idx];
		}

		// find node and its parent by key
		constexpr std::pair<index_type, index_type> find_with_parent(const value_type& key) const noexcept{
			index_type parent = npos;
			index_type cur = root_;
			while(is_valid(cur)){
				const node& n = get(cur);
				if(comp_(key, n.value)){
					parent = cur;
					cur = n.left;
				} else if(comp_(n.value, key)){
					parent = cur;
					cur = n.right;
				} else{
					return {parent, cur}; // equal
				}
			}
			return {npos, npos};
		}

		// link parent's child pointer to new child
		constexpr void relink_child(index_type parent, index_type old_child, index_type new_child){
			if(parent == npos){
				root_ = new_child;
				return;
			}
			node& p = get(parent);
			if(p.left == old_child){
				p.left = new_child;
			} else if(p.right == old_child){
				p.right = new_child;
			} else{
				assert(false && "parent does not point to old_child");
			}
		}

		// erase at specific index, given its parent
		constexpr void erase_at(index_type parent, index_type cur) noexcept{
			node& n = get(cur);
			// Case 1: 0 children
			if(!is_valid(n.left) && !is_valid(n.right)){
				relink_child(parent, cur, npos);
				tombstone(cur);
				return;
			}
			// Case 2: 1 child
			if(!is_valid(n.left) || !is_valid(n.right)){
				index_type child = is_valid(n.left) ? n.left : n.right;
				relink_child(parent, cur, child);
				tombstone(cur);
				return;
			}
			// Case 3: 2 children: replace value with inorder successor, then delete successor
			// Find leftmost in right subtree
			index_type succ_parent = cur;
			index_type succ = n.right;
			while(is_valid(left_of(succ))){
				succ_parent = succ;
				succ = left_of(succ);
			}
			n.value = std::move(value_of(succ));
			// remove successor node (which has at most one child on the right)
			index_type succ_right = right_of(succ);
			relink_child(succ_parent, succ, succ_right);
			tombstone(succ);
		}

		// unique-key insert helpers
		template<class V>
		constexpr std::pair<index_type, bool> insert_impl(V&& v){
			if(!is_valid(root_)){
				root_ = make_node(std::forward<V>(v));
				return {root_, true};
			}
			index_type parent = npos;
			index_type index = root_;
			while(is_valid(index)){
				parent = index;
				node& n = get(index);
				if(comp_(v, n.value)){
					index = n.left;
				} else if(comp_(n.value, v)){
					index = n.right;
				} else{ // equal - reject duplicate by default					
					return {index, false};
				}
			}
			index = make_node(std::forward<V>(v));
			node& p = get(parent);
			if(comp_(get(index).value, p.value)){
				p.left = index;
			} else{
				p.right = index;
			}
			return {index, true};
		}

		template<class... Args>
		constexpr std::pair<index_type, bool> emplace_impl(Args&&... args){
			if(!is_valid(root_)){
				root_ = make_node(node(std::in_place, std::forward<Args>(args)...));
				return {root_, true};
			}
			// two-pass: descend with a temporary key constructed for comparisons,
			// then construct in place at leaf. To avoid extra T, we construct once,
			// then move into slot. That is fine for most Ts.
			value_type temp(std::forward<Args>(args)...);
			return insert_impl(std::move(temp));
		}
	};
} //namespace flat