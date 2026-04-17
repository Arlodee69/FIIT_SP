#include <not_implemented.h>
#include "../include/allocator_boundary_tags.h"

namespace{
    size_t parent = 0;
    size_t mode = parent + sizeof(std::pmr::memory_resource*);
    size_t total_size = mode + sizeof(allocator_with_fit_mode::fit_mode);
    size_t mutex = total_size + sizeof(size_t);
    size_t first_occupied = mutex + sizeof(std::mutex);
    size_t allocator_meta_size = first_occupied + sizeof(void*);

    inline std::pmr::memory_resource* get_parent(void* trusted_memory){
        return *reinterpret_cast<std::pmr::memory_resource**>(reinterpret_cast<char*>(trusted_memory) + parent);
    }

    inline void set_parent(void* trusted_memory, std::pmr::memory_resource* new_parent){
        *reinterpret_cast<std::pmr::memory_resource**>(reinterpret_cast<char*>(trusted_memory) + parent) = new_parent;
    }

    inline allocator_with_fit_mode::fit_mode get_mode(void* trusted_memory){
        return *reinterpret_cast<allocator_with_fit_mode::fit_mode*>(reinterpret_cast<char*>(trusted_memory) + mode);
    }

    inline void set_mode(void* trusted_memory, allocator_with_fit_mode::fit_mode new_mode){
        *reinterpret_cast<allocator_with_fit_mode::fit_mode*>(reinterpret_cast<char*>(trusted_memory) + mode) = new_mode;
    }

    inline size_t get_total_size(void* trusted_memory) {
        return *reinterpret_cast<size_t*>(reinterpret_cast<char*>(trusted_memory) + total_size);
    }
    inline void set_total_size(void* trusted_memory, size_t new_size) {
        *reinterpret_cast<size_t*>(reinterpret_cast<char*>(trusted_memory) + total_size) = new_size;
    }

    inline std::mutex* get_mutex(void* trusted_memory) {
        return reinterpret_cast<std::mutex*>(reinterpret_cast<char*>(trusted_memory) + mutex);
    }

    inline void* get_first_occupied(void* trusted_memory) {
        return *reinterpret_cast<void**>(reinterpret_cast<char*>(trusted_memory) + first_occupied);
    }
    inline void set_first_occupied(void* trusted_memory, void* new_block) {
        *reinterpret_cast<void**>(reinterpret_cast<char*>(trusted_memory) + first_occupied) = new_block;
    }

    inline void* get_memory_start(void* trusted) { 
        return reinterpret_cast<char*>(trusted) + allocator_meta_size; 
    }
    inline void* get_memory_end(void* trusted) { 
        return reinterpret_cast<char*>(trusted) + get_total_size(trusted); 
    }

    size_t block_size = 0;
    size_t block_owner = block_size + sizeof(size_t);
    size_t block_prev = block_owner + sizeof(void*);
    size_t block_next = block_prev + sizeof(void*);
    size_t block_meta_size = block_next + sizeof(void*);

    inline size_t get_block_size(void* block) {
        return *reinterpret_cast<size_t*>(reinterpret_cast<char*>(block) + block_size);
    }
    inline void set_block_size(void* block, size_t size) {
        *reinterpret_cast<size_t*>(reinterpret_cast<char*>(block) + block_size) = size;
    }

    inline void* get_block_owner(void* block) {
        return *reinterpret_cast<void**>(reinterpret_cast<char*>(block) + block_owner);
    }
    inline void set_block_owner(void* block, void* owner) {
        *reinterpret_cast<void**>(reinterpret_cast<char*>(block) + block_owner) = owner;
    }

    inline void* get_block_prev(void* block) {
        return *reinterpret_cast<void**>(reinterpret_cast<char*>(block) + block_prev);
    }
    inline void set_block_prev(void* block, void* prev) {
        *reinterpret_cast<void**>(reinterpret_cast<char*>(block) + block_prev) = prev;
    }

    inline void* get_block_next(void* block) {
        return *reinterpret_cast<void**>(reinterpret_cast<char*>(block) + block_next);
    }
    inline void set_block_next(void* block, void* next) {
        *reinterpret_cast<void**>(reinterpret_cast<char*>(block) + block_next) = next;
    }

    inline void* get_block_end(void* block) {
        return reinterpret_cast<char*>(block) + block_meta_size + get_block_size(block);
    }

    inline size_t get_gap_size(void* start, void* end) {
        return reinterpret_cast<char*>(end) - reinterpret_cast<char*>(start);
    }
};

allocator_boundary_tags::~allocator_boundary_tags()
{
    if (!_trusted_memory) return;

    std::mutex* mtx = get_mutex(_trusted_memory);
    size_t size = get_total_size(_trusted_memory);
    std::pmr::memory_resource* parent = get_parent(_trusted_memory);

    mtx->~mutex();

    if (parent) parent->deallocate(_trusted_memory, size);
    else ::operator delete(_trusted_memory);

    _trusted_memory = nullptr;
}   

allocator_boundary_tags::allocator_boundary_tags(
    allocator_boundary_tags &&other) noexcept
{
    _trusted_memory = other._trusted_memory;
    other._trusted_memory = nullptr;
}

allocator_boundary_tags &allocator_boundary_tags::operator=(
    allocator_boundary_tags &&other) noexcept
{
    if (this != &other){
        this->~allocator_boundary_tags();

        _trusted_memory = other._trusted_memory;
        other._trusted_memory = nullptr;
    }

    return *this;
}


/** If parent_allocator* == nullptr you should use std::pmr::get_default_resource()
 */
allocator_boundary_tags::allocator_boundary_tags(
        size_t space_size,
        std::pmr::memory_resource *parent_allocator,
        allocator_with_fit_mode::fit_mode allocate_fit_mode)
{
    if (!parent_allocator) parent_allocator = std::pmr::get_default_resource();

    if (space_size < occupied_block_metadata_size) throw std::bad_alloc();

    size_t total_size = allocator_metadata_size + space_size;

    _trusted_memory = parent_allocator->allocate(total_size);
    if (!_trusted_memory) throw std::bad_alloc();

    set_parent(_trusted_memory, parent_allocator);
    set_mode(_trusted_memory, allocate_fit_mode);
    set_total_size(_trusted_memory, total_size);

    new (get_mutex(_trusted_memory)) std::mutex();

    set_first_occupied(_trusted_memory, nullptr);
}

[[nodiscard]] void *allocator_boundary_tags::do_allocate_sm(
    size_t size)
{
    if(!_trusted_memory) throw std::bad_alloc();

    std::lock_guard<std::mutex> lock(*get_mutex(_trusted_memory));

    size_t needed_size = size + occupied_block_metadata_size;
    allocator_with_fit_mode::fit_mode mode = get_mode(_trusted_memory);
    
    void* best_gap_start = nullptr;
    void* best_left_block = nullptr;
    void* best_right_block = nullptr;
    size_t best_gap_size = 0;
    bool found = false;

    void* current_right = get_first_occupied(_trusted_memory);
    void* current_left = nullptr;

    while (true) {
        void* gap_start = current_left ? get_block_end(current_left) : get_memory_start(_trusted_memory);
        void* gap_end = current_right ? current_right : get_memory_end(_trusted_memory);

        size_t gap_size = get_gap_size(gap_start, gap_end);

        if (gap_size >= needed_size) {
            if (!found) 
            {
                found = true;
                best_gap_start = gap_start;
                best_left_block = current_left;
                best_right_block = current_right;
                best_gap_size = gap_size;
                
                if (mode == allocator_with_fit_mode::fit_mode::first_fit) break; 
            } 
            else 
            {
                if ((mode == allocator_with_fit_mode::fit_mode::the_best_fit && gap_size < best_gap_size) || (mode == allocator_with_fit_mode::fit_mode::the_worst_fit && gap_size > best_gap_size))
                {
                    best_gap_start = gap_start; 
                    best_left_block = current_left;
                    best_right_block = current_right;
                    best_gap_size = gap_size;
                }
            }
        }

        if (!current_right) break;

        current_left = current_right;
        current_right = get_block_next(current_right);
    }

    if (!found) throw std::bad_alloc();

    void* new_block = best_gap_start;
    size_t actual_payload_size = size;

    size_t leftover = best_gap_size - needed_size;
    if (leftover > 0 && leftover < occupied_block_metadata_size) actual_payload_size += leftover;
    
    set_block_size(new_block, actual_payload_size);
    set_block_owner(new_block, _trusted_memory);
    set_block_prev(new_block, best_left_block);
    set_block_next(new_block, best_right_block);

    if (best_left_block){
        set_block_next(best_left_block, new_block);
    }
    else{
        set_first_occupied(_trusted_memory, new_block);
    }

    if (best_right_block){
        set_block_prev(best_right_block, new_block);
    }

    return reinterpret_cast<char*>(new_block) + occupied_block_metadata_size;
}

void allocator_boundary_tags::do_deallocate_sm(
    void *at)
{
    if (!at || !_trusted_memory) return; 
    std::lock_guard<std::mutex> lock(*get_mutex(_trusted_memory));

    void* block = reinterpret_cast<char*>(at) - occupied_block_metadata_size;

    if (get_block_owner(block) != _trusted_memory) {
        throw std::logic_error("Попытка освободить чужую память");
    }

    void* prev = get_block_prev(block);
    void* next = get_block_next(block);

    if (prev){
        set_block_next(prev, next);
    }
    else{
        set_first_occupied(_trusted_memory, next);
    }

    if (next){
        set_block_prev(next, prev);
    }
}

inline void allocator_boundary_tags::set_fit_mode(
    allocator_with_fit_mode::fit_mode mode)
{
    if (!_trusted_memory) return;

    std::lock_guard<std::mutex> lock(*get_mutex(_trusted_memory));
    set_mode(_trusted_memory, mode);
}


std::vector<allocator_test_utils::block_info> allocator_boundary_tags::get_blocks_info() const
{
    if (!_trusted_memory) return {};

    std::lock_guard<std::mutex> lock(*get_mutex(_trusted_memory));
    return get_blocks_info_inner();
}

allocator_boundary_tags::boundary_iterator allocator_boundary_tags::begin() const noexcept
{
    return boundary_iterator(_trusted_memory);
}

allocator_boundary_tags::boundary_iterator allocator_boundary_tags::end() const noexcept
{
    return boundary_iterator();
}

std::vector<allocator_test_utils::block_info> allocator_boundary_tags::get_blocks_info_inner() const
{
    std::vector<allocator_test_utils::block_info> result;

    for (auto it = begin(); it != end(); ++it){

        allocator_test_utils::block_info info;
        info.block_size = it.size();
        info.is_block_occupied = it.occupied();
        result.push_back(info);
    }

    return result;
}

allocator_boundary_tags::allocator_boundary_tags(const allocator_boundary_tags &other)
{
    if (!other._trusted_memory){
        _trusted_memory = nullptr;
        return;
    }

    std::lock_guard<std::mutex> lock(*get_mutex(_trusted_memory));

    size_t total_size = get_total_size(other._trusted_memory);
    auto parent = get_parent(other._trusted_memory);

    _trusted_memory = parent ? parent -> allocate(total_size) : ::operator new(total_size);
    std::memcpy(_trusted_memory, other._trusted_memory, total_size);

    new(get_mutex(_trusted_memory)) std::mutex();

    std::ptrdiff_t delta = reinterpret_cast<char*>(_trusted_memory) - reinterpret_cast<char*>(other._trusted_memory);

    void* first_occ = get_first_occupied(_trusted_memory);
    if (first_occ) {
        first_occ = reinterpret_cast<char*>(first_occ) + delta;
        set_first_occupied(_trusted_memory, first_occ);

        void* current = first_occ;
        while (current) { 
            set_block_owner(current, _trusted_memory);

            void* prev = get_block_prev(current);
            if (prev) set_block_prev(current, reinterpret_cast<char*>(prev) + delta);

            void* next = get_block_next(current);
            if (next) set_block_next(current, reinterpret_cast<char*>(next) + delta); 

            current = get_block_next(current);
        }
    }
}

allocator_boundary_tags &allocator_boundary_tags::operator=(const allocator_boundary_tags &other)
{
    if (this != &other){
        allocator_boundary_tags temp(other);
        *this = std::move(temp);
    }
    return *this;
}

bool allocator_boundary_tags::do_is_equal(const std::pmr::memory_resource &other) const noexcept
{
    return this == &other;
}

bool allocator_boundary_tags::boundary_iterator::operator==(
        const allocator_boundary_tags::boundary_iterator &other) const noexcept
{
    return _trusted_memory == other._trusted_memory && _occupied_ptr == other._occupied_ptr && _occupied == other._occupied;
}

bool allocator_boundary_tags::boundary_iterator::operator!=(
        const allocator_boundary_tags::boundary_iterator & other) const noexcept
{
    return !(*this == other);
}

allocator_boundary_tags::boundary_iterator &allocator_boundary_tags::boundary_iterator::operator++() & noexcept
{
    if (!_trusted_memory) return *this;
    if (!_occupied){
        if (_occupied_ptr){
            _occupied = true;
        }
        else{
            _trusted_memory = nullptr;
            _occupied_ptr = nullptr;
            _occupied = false;
        }
    }

    else{
        void* next_block = get_block_next(_occupied_ptr);
        void* current_end = get_block_end(_occupied_ptr);
        void* next_start = next_block ? next_block : get_memory_end(_trusted_memory);

        if (current_end < next_start){
            _occupied_ptr = next_block;
            _occupied = false;
        }
        else{
            if(next_block){
                _occupied_ptr = next_block;
                _occupied = true;
            }
            else{
                _trusted_memory = nullptr;
                _occupied_ptr = nullptr;
                _occupied = false;
            }
        }
    }
    return *this;
}

allocator_boundary_tags::boundary_iterator &allocator_boundary_tags::boundary_iterator::operator--() & noexcept
{
    if (!_trusted_memory) return *this;

    if (!_occupied) {
        if (!_occupied_ptr) {
            void* last = get_first_occupied(_trusted_memory);
            if (last) {
                while (get_block_next(last)) last = get_block_next(last); 
            }
            _occupied_ptr = last;
            _occupied = (last != nullptr);
        } else {
            void* prev = get_block_prev(_occupied_ptr);
            _occupied_ptr = prev;
            _occupied = (prev != nullptr);
        }
    } else { 
        void* prev = get_block_prev(_occupied_ptr);
        void* current_start = _occupied_ptr;
        void* prev_end = prev ? get_block_end(prev) : get_memory_start(_trusted_memory);

        if (prev_end < current_start) {
            _occupied = false; 
        } else {
            _occupied_ptr = prev; 
            _occupied = true;
        }
    }
    
    return *this;
}

allocator_boundary_tags::boundary_iterator allocator_boundary_tags::boundary_iterator::operator++(int n)
{
    allocator_boundary_tags::boundary_iterator copy = *this;
    ++(*this);
    return copy;
}

allocator_boundary_tags::boundary_iterator allocator_boundary_tags::boundary_iterator::operator--(int n)
{
    allocator_boundary_tags::boundary_iterator copy = *this;
    --(*this);
    return copy;    
}

size_t allocator_boundary_tags::boundary_iterator::size() const noexcept
{
    if (!_trusted_memory || (_occupied_ptr == get_memory_end(_trusted_memory))) return 0;

    if (_occupied) { //ВНУТРИ БЛОКА
        return allocator_boundary_tags::occupied_block_metadata_size + get_block_size(_occupied_ptr);
    }

    void* gap_start = nullptr;
    void* gap_end = nullptr;

    if (!_occupied_ptr) { //ДЫРКА В КОНЦЕ ПАМЯТИ
        void* last = get_first_occupied(_trusted_memory);
        if (last) {
            while (get_block_next(last)) last = get_block_next(last);
            gap_start = get_block_end(last);
        } else {
            gap_start = get_memory_start(_trusted_memory);
        }

        gap_end = get_memory_end(_trusted_memory);
    } else { //ДЫРКА МЕЖДУ БЛОКАМИ
        void* prev = get_block_prev(_occupied_ptr);
        gap_start = prev ? get_block_end(prev) : get_memory_start(_trusted_memory);
        gap_end = _occupied_ptr;
    }

    return get_gap_size(gap_start, gap_end);
}

bool allocator_boundary_tags::boundary_iterator::occupied() const noexcept
{
    return _occupied;
}

void* allocator_boundary_tags::boundary_iterator::operator*() const noexcept
{
    get_ptr();
}

allocator_boundary_tags::boundary_iterator::boundary_iterator()
{
    _occupied_ptr = nullptr;
    _occupied = false;
    _trusted_memory = nullptr;
}

allocator_boundary_tags::boundary_iterator::boundary_iterator(void *trusted)
{
    _occupied_ptr = nullptr;
    _occupied = false;
    _trusted_memory = trusted;

    if (!_trusted_memory) return;

    void* first = get_first_occupied(_trusted_memory);
    if (!first) return;

    if (get_memory_start(_trusted_memory) < first){
        _occupied_ptr = first;
        _occupied = false;
    } else{
        _occupied_ptr = first;
        _occupied = true;
    }
}

void *allocator_boundary_tags::boundary_iterator::get_ptr() const noexcept
{
    if (!_trusted_memory && (_occupied_ptr == get_memory_end(_trusted_memory))) return nullptr;

    if (_occupied) return _occupied_ptr; 
    if (!_occupied_ptr) { //ОБЛАСТЬ ПОСЛЕ ПОСЛЕДНЕГО БЛОКА
        void* last = get_first_occupied(_trusted_memory);
        if (last) { 
            while (get_block_next(last)) last = get_block_next(last);
            return get_block_end(last);
        }
        return get_memory_start(_trusted_memory); //ДЫРОК НЕТ
    }
    
    void* prev = get_block_prev(_occupied_ptr); //МЕЖДУ БЛОКАМИ
    return prev ? get_block_end(prev) : get_memory_start(_trusted_memory);
}
