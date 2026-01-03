// flat::bst<T, Compare, IndexT> — a flat, index-based binary search tree
// Stores nodes in a std::vector and links them by integer indices for
// better cache locality, fewer allocations, and stable references.
// Inspired by Jens Weller’s “Looking at binary trees in C++”:
//	 https://meetingcpp.com/blog/items/Looking-at-binary-trees-in-Cpp.html
// Live demo: https://compiler-explorer.com/z/rqEKxGsTT
// Requires C++26. See main.cpp for usage examples.
// Ulf Benjaminsson, 2025

#pragma once
#include <algorithm>
#include <bit>
#include <cassert>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace flat {

    // Configuration traits for packing Index + Generation into IndexT
    // Default: 32-bit IndexT -> 20 bits index (1M items), 12 bits generation
    template <typename IndexT>
    struct index_layout{
        static_assert(std::is_unsigned_v<IndexT>, "IndexT must be unsigned");

        static constexpr int total_bits = std::numeric_limits<IndexT>::digits;

        // 64-bit indices get 32 bits for generation. 32-bit indices get 12.
        // For smaller IndexT we scale down, otherwise shifts/masks become ill-formed.
        static constexpr int gen_bits =
            (total_bits >= 64) ? 32 :
            (total_bits >= 32) ? 12 :
            (total_bits >= 16) ? 3 : // 13-bit index => 8191 usable raw indices (one reserved)
            2;
                
        static_assert(gen_bits >= 0 && gen_bits < total_bits, "gen_bits must be in [0, digits)");
        static constexpr int idx_bits = total_bits - gen_bits;
        static constexpr IndexT idx_mask = (~IndexT(0)) >> gen_bits;
        static constexpr IndexT gen_mask = ~idx_mask;

        static constexpr IndexT unpack_index(IndexT handle) noexcept{ return handle & idx_mask; }
        static constexpr IndexT unpack_gen(IndexT handle) noexcept{ return handle >> idx_bits; }
        static constexpr IndexT pack(IndexT idx, IndexT gen) noexcept{ return (gen << idx_bits) | (idx & idx_mask); }
    };

    // forward declare the class template so the iterator can name it
    template<class T, class Compare = std::less<T>, class IndexT = uint32_t>
    class bst;
};

//namespace for the iterator implementation
namespace flat::detail {
    template<class T, class Compare, class IndexT>
    class inorder_iter{
        using tree_t = bst<T, Compare, IndexT>;
        static constexpr size_t traversal_stack_reserve = 16;
        // iterators use raw indices internally for performance, not handles
        const tree_t* tree_ = nullptr;
        std::vector<IndexT> stack_;
        IndexT cur_raw_ = tree_t::npos_raw;

        constexpr void push_left_(IndexT i){
            while(i != tree_t::npos_raw){
                stack_.push_back(i);
                i = tree_->internal_left(i);
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
            if(!t || end || t->empty()){ cur_raw_ = tree_t::npos_raw; return; }
            stack_.reserve(traversal_stack_reserve);
            push_left_(t->internal_root());
            if(!stack_.empty()){
                cur_raw_ = stack_.back();
                stack_.pop_back();
            } else{
                cur_raw_ = tree_t::npos_raw;
            }
        }

        constexpr reference operator*() const noexcept{ return tree_->internal_value(cur_raw_); }
        constexpr pointer   operator->() const noexcept{ return &tree_->internal_value(cur_raw_); }

        constexpr inorder_iter& operator++(){
            if(cur_raw_ == tree_t::npos_raw) return *this;

            IndexT right = tree_->internal_right(cur_raw_);
            if(right != tree_t::npos_raw){
                push_left_(right);
            }

            if(stack_.empty()){
                cur_raw_ = tree_t::npos_raw;
            } else{
                cur_raw_ = stack_.back();
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
            return a.tree_ == b.tree_ && a.cur_raw_ == b.cur_raw_ && a.stack_ == b.stack_;
        }
        friend constexpr bool operator!=(const inorder_iter& a, const inorder_iter& b) noexcept{
            return !(a == b);
        }
    };
}  // namespace flat::detail

namespace flat {

    template<class T, class Compare, class IndexT>
    class bst final{
        using Layout = index_layout<IndexT>;
    public:
        using const_inorder_iterator = flat::detail::inorder_iter<T, Compare, IndexT>;
        using value_type = T;
        using size_type = std::size_t;
        using index_type = IndexT;

        // Keep public API intact: npos is the max value (old behavior).
        static constexpr index_type npos = std::numeric_limits<index_type>::max();

        // Internal raw-index sentinel: reserve the all-ones "index field" value for npos_raw,
        // so raw indices are always in [0, npos_raw).
        static constexpr index_type npos_raw = Layout::idx_mask;

        bst() = default;

        constexpr explicit bst(Compare cmp) noexcept(std::is_nothrow_move_constructible_v<Compare>)
            : comp_(std::move(cmp)){}

        // if you already have sorted-unique data, create an empty bst and call build_from_sorted_unique instead
        // this ctor just does the right thing for arbitrary ranges.
        template<class It>
        bst(It first, It last, Compare cmp = Compare{}) : comp_(std::move(cmp)){
            if(first == last) return;
            if constexpr(std::random_access_iterator<It>){
                bool sorted = std::is_sorted(first, last, comp_);
                bool unique = sorted && std::adjacent_find(first, last, [&](const auto& a, const auto& b){
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

        // Rule of 5: Slot is a proper RAII type so we can default these safely.
        ~bst() = default;
        bst(const bst&) = default;
        bst& operator=(const bst&) = default;
        bst(bst&&) noexcept = default;
        bst& operator=(bst&&) noexcept = default;

        [[nodiscard]] constexpr size_type size() const noexcept{ return alive_count_; }
        [[nodiscard]] constexpr size_type holes() const noexcept{ return slots_.size() - alive_count_; }
        [[nodiscard]] constexpr size_type capacity() const noexcept{ return slots_.capacity(); }
        [[nodiscard]] constexpr bool empty() const noexcept{ return alive_count_ == 0; }

        inline constexpr const_inorder_iterator begin_inorder() const{ return const_inorder_iterator(this, false); }
        inline constexpr const_inorder_iterator end_inorder()   const{ return const_inorder_iterator(this, true); }
        inline constexpr auto begin() const{ return begin_inorder(); }
        inline constexpr auto end() const{ return end_inorder(); }

        //accessors for the iterators to be able to do their work
        //they bloat the public API a bit, but I don't have to deal with friend classes so...
        [[nodiscard]] constexpr index_type root_index() const noexcept{
            return (root_idx_ == npos_raw) ? npos : make_handle(root_idx_);
        }

        static constexpr bool is_valid(index_type handle) noexcept{ return handle != npos; }

        // returns true if handle is valid AND matches the current generation of the slot
        [[nodiscard]] constexpr bool is_handle_valid(index_type handle) const noexcept{
            if(handle == npos) return false;
            index_type idx = Layout::unpack_index(handle);
            index_type gen = Layout::unpack_gen(handle);
            if(idx >= slots_.size()) return false;
            return (slots_[idx].generation == gen) && slots_[idx].is_alive();
        }

        [[nodiscard]] constexpr index_type left_of(index_type handle) const noexcept{
            if(!is_handle_valid(handle)) return npos;
            index_type left_idx = slots_[Layout::unpack_index(handle)].left;
            return (left_idx == npos_raw) ? npos : make_handle(left_idx);
        }

        [[nodiscard]] constexpr index_type right_of(index_type handle) const noexcept{
            if(!is_handle_valid(handle)) return npos;
            index_type right_idx = slots_[Layout::unpack_index(handle)].right;
            return (right_idx == npos_raw) ? npos : make_handle(right_idx);
        }

        //note: noexcept! will std::terminate on invalid handle
        [[nodiscard]] constexpr const value_type& value_of(index_type handle) const noexcept{
            assert(is_handle_valid(handle) && "Invalid or stale bst handle");
            if(!is_handle_valid(handle)){
                std::terminate();
			}
            return slots_[Layout::unpack_index(handle)].value();
        }

        constexpr void reserve(size_type n){ slots_.reserve(n); }

        constexpr void clear() noexcept{
            slots_.clear();
            root_idx_ = npos_raw;
            free_head_ = npos_raw;
            alive_count_ = 0;
        }

        void swap(bst& other) noexcept{
            using std::swap;
            swap(slots_, other.slots_);
            swap(root_idx_, other.root_idx_);
            swap(free_head_, other.free_head_);
            swap(alive_count_, other.alive_count_);
            swap(comp_, other.comp_);
        }

        constexpr void rebuild_compact(){ rebalance(); }

        constexpr void rebalance(){
            if(alive_count_ < 2) return;
            std::vector<value_type> vals;
            vals.reserve(alive_count_);
            inorder([&](const value_type& v){ vals.push_back(v); });
            // Building from sorted unique completely replaces the storage,
            // effectively compacting and resetting generations for reused slots.
            // Note: This INVALIDATES all existing external handles.
            bst tmp(comp_);
            tmp.build_from_sorted_unique_into_empty(vals.begin(), vals.end());
            swap(tmp);
        }

        // insert / emplace, returns {index, inserted}
        constexpr std::pair<index_type, bool> insert(const value_type& v){ return insert_impl(v); }
        constexpr std::pair<index_type, bool> insert(value_type&& v){ return insert_impl(std::move(v)); }

        template<class... Args>
        constexpr std::pair<index_type, bool> emplace(Args&&... args){
            value_type temp(std::forward<Args>(args)...);
            return insert_impl(std::move(temp));
        }

        template<class It>
        size_type insert(It first, It last){
            if constexpr(std::forward_iterator<It>){
                auto n = static_cast<size_type>(std::distance(first, last));
                if(n) slots_.reserve(slots_.size() + n);
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
            bst tmp(comp_);
            tmp.build_from_sorted_unique_into_empty(first, last);
            swap(tmp);
        }

        // build balanced tree from arbitrary input range (sorts + uniques)
        template<class It>
        void build_from_range(It first, It last){
            std::vector<value_type> vals;
            if constexpr(std::forward_iterator<It>){
                vals.reserve(static_cast<size_type>(std::distance(first, last)));
            }
            for(; first != last; ++first) vals.push_back(*first);
            std::sort(vals.begin(), vals.end(), comp_);
            vals.erase(std::unique(vals.begin(), vals.end(),
                [&](const value_type& a, const value_type& b){
                    return !comp_(a, b) && !comp_(b, a);
                }), vals.end());
            build_from_sorted_unique(vals.begin(), vals.end());
        }

        [[nodiscard]] constexpr index_type find_index(const value_type& key) const noexcept{
            index_type raw = find_internal_raw(key).second;
            return (raw == npos_raw) ? npos : make_handle(raw);
        }

        [[nodiscard]] constexpr bool contains(const value_type& key) const noexcept{
            return find_internal_raw(key).second != npos_raw;
        }

        // returns pointer to value or nullptr
        [[nodiscard]] constexpr const value_type* find(const value_type& key) const noexcept{
            index_type raw = find_internal_raw(key).second;
            if(raw == npos_raw) return nullptr;
            return &slots_[raw].value();
        }

        [[nodiscard]] constexpr const value_type* get_ptr(index_type handle) const noexcept{
            if(!is_handle_valid(handle)) return nullptr;
            return &slots_[Layout::unpack_index(handle)].value();
        }

        // erase by key - returns true if erased
        constexpr bool erase(const value_type& key){
            auto [parent, cur] = find_internal_raw(key);
            if(cur == npos_raw) return false;
            erase_internal(parent, cur);
            return true;
        }

       // traversals: inorder, preorder, postorder. Callback recieves const T&
        template<class F>
        constexpr void inorder(F&& f) const{
            std::vector<index_type> stack;
            stack.reserve(traversal_stack_reserve);
            index_type index = root_idx_;
            while(index != npos_raw || !stack.empty()){
                while(index != npos_raw){
                    stack.push_back(index);
                    index = slots_[index].left;
                }
                index = stack.back();
                stack.pop_back();
                f(slots_[index].value());
                index = slots_[index].right;
            }
        }

        template<class F>
        constexpr void preorder(F&& f) const{
            if(root_idx_ == npos_raw) return;
            std::vector<index_type> stack;
            stack.reserve(traversal_stack_reserve);
            stack.push_back(root_idx_);
            while(!stack.empty()){
                index_type index = stack.back();
                stack.pop_back();
                const Slot& n = slots_[index];
                f(n.value());
                if(n.right != npos_raw) stack.push_back(n.right);
                if(n.left != npos_raw) stack.push_back(n.left);
            }
        }

        template<class F>
        constexpr void postorder(F&& f) const{
            if(root_idx_ == npos_raw) return;
            std::vector<index_type> stack1, stack2;
            stack1.reserve(traversal_stack_reserve);
            stack2.reserve(traversal_stack_reserve);
            stack1.push_back(root_idx_);
            while(!stack1.empty()){
                index_type index = stack1.back();
                stack1.pop_back();
                stack2.push_back(index);
                const Slot& n = slots_[index];
                if(n.left != npos_raw) stack1.push_back(n.left);
                if(n.right != npos_raw) stack1.push_back(n.right);
            }
            while(!stack2.empty()){
                f(slots_[stack2.back()].value());
                stack2.pop_back();
            }
        }

        // more access for iterators
        constexpr index_type internal_root() const{ return root_idx_; }
        constexpr index_type internal_left(index_type raw) const{ return slots_[raw].left; }
        constexpr index_type internal_right(index_type raw) const{ return slots_[raw].right; }
        constexpr const T& internal_value(index_type raw) const{ return slots_[raw].value(); }

    private:
        struct Slot{
            // Generation logic: Even = Alive, Odd = Free.
            index_type generation = 1;
            index_type left = npos_raw;
            index_type right = npos_raw; // Acts as next_free when dead

            alignas(T) std::byte storage[sizeof(T)];

            constexpr bool is_alive() const noexcept{ return (generation % 2) == 0; }

            template<typename... Args>
            constexpr void construct_value(Args&&... args){
                std::construct_at(reinterpret_cast<T*>(storage), std::forward<Args>(args)...);
            }

            constexpr void destroy_value() noexcept{
                std::destroy_at(std::launder(reinterpret_cast<T*>(storage)));
            }

            constexpr T& value() noexcept{ return *std::launder(reinterpret_cast<T*>(storage)); }
            constexpr const T& value() const noexcept{ return *std::launder(reinterpret_cast<const T*>(storage)); }

            // RAII: ensure Slot copies/moves only the live T, and destroys when needed.
            Slot() = default;

            Slot(const Slot& other)
                noexcept(std::is_nothrow_copy_constructible_v<T>)
                : generation(other.generation), left(other.left), right(other.right){
                if(other.is_alive()){
                    construct_value(other.value());
                }
            }


            Slot(Slot&& other)
                noexcept(std::is_nothrow_move_constructible_v<T>)
                : generation(other.generation), left(other.left), right(other.right){
                if(other.is_alive()){
                    construct_value(std::move(other.value()));
                    other.destroy_value();
                    other.generation++; // make 'other' free (odd), preserving its free-list link fields as copied above
                }
            }

            friend void swap(Slot& a, Slot& b)
                noexcept(
                    std::is_nothrow_move_constructible_v<T> &&
                    noexcept(std::swap(std::declval<T&>(), std::declval<T&>()))
                    ){
                using std::swap;
                const bool a_alive = a.is_alive();
                const bool b_alive = b.is_alive();

                if(a_alive && b_alive){
                    swap(a.value(), b.value());
                } else if(a_alive && !b_alive){
                    b.construct_value(std::move(a.value()));
                    a.destroy_value();
                } else if(!a_alive && b_alive){
                    a.construct_value(std::move(b.value()));
                    b.destroy_value();
                } // else: both dead
                
                swap(a.generation, b.generation);
                swap(a.left, b.left);
                swap(a.right, b.right);
            }

            // One assignment operator handles both copy and move assignment.
            Slot& operator=(Slot other) noexcept(noexcept(swap(*this, other))){
                swap(*this, other);
                return *this;
            }

            ~Slot() noexcept{
                if(is_alive()){
                    destroy_value();
                }
            }
        };

        static constexpr size_type traversal_stack_reserve = 16; // Typical traversal depth (balanced trees rarely exceed log2(N). Just a perf hint, does not affect correctness.
        std::vector<Slot> slots_;
        index_type root_idx_ = npos_raw;
        index_type free_head_ = npos_raw;
        size_type alive_count_ = 0;
        [[no_unique_address]] Compare comp_{};

        constexpr index_type make_handle(index_type raw_idx) const noexcept{
            return Layout::pack(raw_idx, slots_[raw_idx].generation);
        }

        template<class V>
        index_type allocate_node(V&& v){
            assert(free_head_ == npos_raw || !slots_[free_head_].is_alive());
            index_type idx;
            if(free_head_ != npos_raw){
                idx = free_head_;
                Slot& s = slots_[idx];
                assert(!s.is_alive());
                free_head_ = s.right;              
                try{ 
                    s.construct_value(std::forward<V>(v)); 
                } catch(...){
                    //re-push on freelist
                    s.right = free_head_;
                    free_head_ = idx;
                    throw;
                }
                s.generation++; // odd -> even, now alive
                s.left = npos_raw;
                s.right = npos_raw;
            } else{
                idx = static_cast<index_type>(slots_.size());
                // IMPORTANT: idx == npos_raw is reserved for sentinel and cannot be allocated.
                if(idx >= npos_raw) throw std::length_error("BST index overflow");
				slots_.emplace_back(); // generation starts at 1 (free)
                try{ 
                    slots_.back().construct_value(std::forward<V>(v)); 
                    slots_.back().generation++; // free -> alive
                } catch(...){ 
                    slots_.pop_back(); 
                    throw; 
                }
            }
            ++alive_count_;
            assert(free_head_ == npos_raw || !slots_[free_head_].is_alive());
            return idx;
        }

        void free_node(index_type idx){
            assert(free_head_ == npos_raw || !slots_[free_head_].is_alive());
            Slot& s = slots_[idx];
            assert(s.is_alive());
            s.destroy_value();
            s.generation++; // Even -> Odd (Free)
            s.left = npos_raw;
            s.right = free_head_;
            free_head_ = idx;
            alive_count_--;
            assert(free_head_ == npos_raw || !slots_[free_head_].is_alive());
        }

        constexpr std::pair<index_type, index_type> find_internal_raw(const value_type& key) const{
            index_type parent = npos_raw;
            index_type cur = root_idx_;
            while(cur != npos_raw){
                const Slot& s = slots_[cur];
                if(comp_(key, s.value())){
                    parent = cur;
                    cur = s.left;
                } else if(comp_(s.value(), key)){
                    parent = cur;
                    cur = s.right;
                } else{
                    return {parent, cur};
                }
            }
            return {npos_raw, npos_raw};
        }

        constexpr void relink_child(index_type parent, index_type old_child, index_type new_child){
            if(parent == npos_raw){
                root_idx_ = new_child;
                return;
            }

            Slot& p = slots_[parent];
            if(p.left == old_child){
                p.left = new_child;
            } else if(p.right == old_child){
                p.right = new_child;
            } else{
                assert(false && "parent does not point to old_child");
            }
        }

        constexpr void erase_internal(index_type parent, index_type cur){
            Slot& n = slots_[cur];
            if(n.left == npos_raw && n.right == npos_raw){
                relink_child(parent, cur, npos_raw);
                free_node(cur);
            } else if(n.left == npos_raw || n.right == npos_raw){
                index_type child = (n.left != npos_raw) ? n.left : n.right;
                relink_child(parent, cur, child);
                free_node(cur);
            } else{
                index_type succ_parent = cur;
                index_type succ = n.right;
                while(slots_[succ].left != npos_raw){
                    succ_parent = succ;
                    succ = slots_[succ].left;
                }
                T tmp = std::move(slots_[succ].value());   // may throw, tree unchanged
                n.value() = std::move(tmp);                // may throw, but at least succ still exists
                relink_child(succ_parent, succ, slots_[succ].right);
                free_node(succ);
            }
        }

        template<class V>
        constexpr std::pair<index_type, bool> insert_impl(V&& v){
            if(root_idx_ == npos_raw){
                index_type idx = allocate_node(std::forward<V>(v));
                root_idx_ = idx;
                return {make_handle(idx), true};
            }
            index_type parent = npos_raw;
            index_type cur = root_idx_;
            bool go_left = false;
            while(cur != npos_raw){
                parent = cur;
                Slot& s = slots_[cur];
                if(comp_(v, s.value())){
                    go_left = true;
                    cur = s.left;
                } else if(comp_(s.value(), v)){
                    go_left = false;
                    cur = s.right;
                } else{
                    return {make_handle(cur), false};
                }
            }
            index_type idx = allocate_node(std::forward<V>(v));
            if(go_left) slots_[parent].left = idx;
            else slots_[parent].right = idx;
            return {make_handle(idx), true};
        }

        template<class It>
        void build_from_sorted_unique_into_empty(It first, It last){
            const size_type n = static_cast<size_type>(std::distance(first, last));
            if(n == 0){ root_idx_ = npos_raw; return; }
            slots_.reserve(n);

            auto build = [&](auto&& self, It lo, It hi) -> index_type{
                if(lo == hi) return npos_raw;
                It mid = lo + (std::distance(lo, hi) / 2);
                // allocate_node will just append since we started empty
                index_type me = allocate_node(*mid);
                Slot& m = slots_[me];
                m.left = self(self, lo, mid);
                m.right = self(self, std::next(mid), hi);
                return me;
                };
            root_idx_ = build(build, first, last);
        }
    };

    template<class T, class Compare, class IndexT>
    void swap(bst<T, Compare, IndexT>& a, bst<T, Compare, IndexT>& b)
        noexcept(noexcept(a.swap(b))){
        a.swap(b);
    }
}
