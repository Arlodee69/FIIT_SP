#include <not_implemented.h>
#include "../include/allocator_sorted_list.h"
#include <mutex>
#include <new>
#include <algorithm>
#include <stdexcept>
#include <cstdint>

namespace {

struct allocator_header
{
    std::pmr::memory_resource *parent_allocator;
    allocator_with_fit_mode::fit_mode mode;
    size_t total_size;
    std::mutex mutex;
    void *first_block;
};

struct block_header
{
    void *next_block;
    size_t block_size;
};
}

allocator_sorted_list::~allocator_sorted_list()
{
    if (_trusted_memory == nullptr) return;

    allocator_header* data = reinterpret_cast<allocator_header*>(_trusted_memory);
    std::pmr::memory_resource* parent_allocator = data->parent_allocator;
    size_t total_size = data->total_size;
    data->~allocator_header();

    if (parent_allocator != nullptr){
        parent_allocator -> deallocate(_trusted_memory, total_size);
    }
    else {
        ::operator delete(_trusted_memory);
    }

    _trusted_memory = nullptr;
}

allocator_sorted_list::allocator_sorted_list(
    allocator_sorted_list &&other) noexcept
{
    _trusted_memory = other._trusted_memory;
    other._trusted_memory = nullptr;
}

allocator_sorted_list &allocator_sorted_list::operator=(
    allocator_sorted_list &&other) noexcept
{
    if (this != &other)
    {
        this->~allocator_sorted_list();
        _trusted_memory = other._trusted_memory;
        other._trusted_memory = nullptr;
    }
    return *this;
}

allocator_sorted_list::allocator_sorted_list(
        size_t space_size,
        std::pmr::memory_resource *parent_allocator,
        allocator_with_fit_mode::fit_mode allocate_fit_mode)
{

    if (space_size < sizeof(block_header)) {
        space_size = sizeof(block_header);
    }

    size_t total_needed = sizeof(allocator_header) + space_size;

    if (parent_allocator != nullptr) {
        _trusted_memory = parent_allocator->allocate(total_needed);
    } else {
        _trusted_memory = ::operator new(total_needed); 
    }

    allocator_header* data = new(_trusted_memory) allocator_header();

    data -> parent_allocator = parent_allocator;
    data -> total_size = total_needed;
    data -> mode = allocate_fit_mode;

    void* first_block_address = reinterpret_cast<char*>(_trusted_memory) + sizeof(allocator_header);
    data->first_block = first_block_address;

    block_header* first_block = new (first_block_address) block_header();
    first_block->block_size = space_size;
    first_block->next_block = nullptr;
}

[[nodiscard]] void *allocator_sorted_list::do_allocate_sm(
    size_t size)
{
    if (_trusted_memory == nullptr) throw std::bad_alloc();

    allocator_header* data = reinterpret_cast<allocator_header*>(_trusted_memory);

    std::lock_guard<std::mutex> lock(data->mutex);

    size_t alignment = alignof(std::max_align_t);
    size_t payload_size = (size + alignment - 1) & ~(alignment - 1);

    size_t total_block_size = payload_size + sizeof(block_header);

    block_header* prev = nullptr;
    block_header* curr = reinterpret_cast<block_header*>(data->first_block);

    block_header* best_prev = nullptr;
    block_header* best_curr = nullptr;

    while (curr != nullptr) {
        if (curr->block_size >= total_block_size) {
            if (data->mode == allocator_with_fit_mode::fit_mode::first_fit) {
                best_curr = curr;
                best_prev = prev;
                break;
            } else if (data->mode == allocator_with_fit_mode::fit_mode::the_best_fit) { //самый маленький из подходящих
                if (best_curr == nullptr || curr->block_size < best_curr->block_size) {
                    best_curr = curr;
                    best_prev = prev;
                }
            } else if (data->mode == allocator_with_fit_mode::fit_mode::the_worst_fit) { //самый большой из подходящих
                if (best_curr == nullptr || curr->block_size > best_curr->block_size) {
                    best_curr = curr;
                    best_prev = prev;
                }
            }
        }
        prev = curr;
        curr = reinterpret_cast<block_header*>(curr->next_block);
    }

    if (best_curr == nullptr) {
        throw std::bad_alloc();
    }

    if (best_curr->block_size >= total_block_size + sizeof(block_header) + alignment) {
        void* new_free_addr = reinterpret_cast<char*>(best_curr) + total_block_size;

        block_header* new_free = new(new_free_addr) block_header();
        new_free->block_size = best_curr->block_size - total_block_size;
        
        new_free->next_block = best_curr->next_block;

        if (best_prev == nullptr) {
            data->first_block = new_free;
        } else {
            best_prev->next_block = new_free;
        }

        best_curr->block_size = total_block_size;
    } else {
        if (best_prev == nullptr) {
            data->first_block = best_curr->next_block;
        } else {
            best_prev->next_block = best_curr->next_block;
        }
    }

    best_curr->next_block = _trusted_memory;

    return reinterpret_cast<char*>(best_curr) + sizeof(block_header);
}

allocator_sorted_list::allocator_sorted_list(const allocator_sorted_list &other)
{
    if (other._trusted_memory == nullptr)
    {
        _trusted_memory = nullptr;
        return;
    }

    allocator_header* other_data = reinterpret_cast<allocator_header*>(other._trusted_memory);
    size_t space_size = other_data->total_size - sizeof(allocator_header);

    allocator_sorted_list temp(space_size, other_data->parent_allocator, other_data->mode);
    _trusted_memory = temp._trusted_memory;
    temp._trusted_memory = nullptr;
}

allocator_sorted_list &allocator_sorted_list::operator=(const allocator_sorted_list &other)
{
    if (this != &other)
    {
        allocator_sorted_list temp(other);
        *this = std::move(temp);
    }
    return *this;
}

bool allocator_sorted_list::do_is_equal(const std::pmr::memory_resource &other) const noexcept
{
    auto p = dynamic_cast<const allocator_sorted_list *>(&other);
    return p && p->_trusted_memory == _trusted_memory;
}

void allocator_sorted_list::do_deallocate_sm(
    void *at)
{
    if (!at) return;

    allocator_header* data = reinterpret_cast<allocator_header*>(_trusted_memory);
    std::lock_guard<std::mutex> lock(data->mutex);

    block_header* block_to_free = reinterpret_cast<block_header*>(reinterpret_cast<char*>(at) - sizeof(block_header));

    if (block_to_free->next_block != _trusted_memory) {
        throw std::logic_error("Попытка освободить свободный блок или чужую память");
    }

    block_header* prev = nullptr;
    block_header* curr = reinterpret_cast<block_header*>(data->first_block);

    while (curr != nullptr && curr < block_to_free) {
        prev = curr;
        curr = reinterpret_cast<block_header*>(curr->next_block);
    }

    block_to_free->next_block = curr;
    if (prev == nullptr) {
        data->first_block = block_to_free;
    } else {
        prev->next_block = block_to_free; 
    }

    if (curr != nullptr) {
        char* end_of_block = reinterpret_cast<char*>(block_to_free) + block_to_free->block_size;
        
        if (end_of_block == reinterpret_cast<char*>(curr)) {
            block_to_free->block_size += curr->block_size;
            block_to_free->next_block = curr->next_block;
        }
    }

    if (prev != nullptr) {
        char* end_of_block = reinterpret_cast<char*>(prev) + prev->block_size;

        if (end_of_block == reinterpret_cast<char*>(block_to_free)) {
            prev->block_size += block_to_free->block_size;
            prev->next_block = block_to_free->next_block;
        }
    }
}

inline void allocator_sorted_list::set_fit_mode(
    allocator_with_fit_mode::fit_mode mode)
{
    if (_trusted_memory == nullptr) return;
    allocator_header* data = reinterpret_cast<allocator_header*>(_trusted_memory);
    std::lock_guard<std::mutex> lock(data->mutex);
    data->mode = mode;
}

std::vector<allocator_test_utils::block_info> allocator_sorted_list::get_blocks_info() const noexcept
{
    if (_trusted_memory == nullptr) return {};
    allocator_header* data = reinterpret_cast<allocator_header*>(_trusted_memory);
    std::lock_guard<std::mutex> lock(data->mutex);
    return get_blocks_info_inner();
}


std::vector<allocator_test_utils::block_info> allocator_sorted_list::get_blocks_info_inner() const
{
    if (_trusted_memory == nullptr) return {};
    std::vector<allocator_test_utils::block_info> result;

    for (auto it = begin(); it != end(); ++it){
        allocator_test_utils::block_info b;
        b.block_size = it.size();
        b.is_block_occupied = it.occupied();
        result.push_back(b);
    }
    return result;
}

allocator_sorted_list::sorted_free_iterator allocator_sorted_list::free_begin() const noexcept
{
    return sorted_free_iterator(_trusted_memory);    
}

allocator_sorted_list::sorted_free_iterator allocator_sorted_list::free_end() const noexcept
{
    return sorted_free_iterator(nullptr);
}

allocator_sorted_list::sorted_iterator allocator_sorted_list::begin() const noexcept
{
    return sorted_iterator(_trusted_memory);
}

allocator_sorted_list::sorted_iterator allocator_sorted_list::end() const noexcept
{
    return sorted_iterator(nullptr);
}


bool allocator_sorted_list::sorted_free_iterator::operator==(
        const allocator_sorted_list::sorted_free_iterator & other) const noexcept
{
    return _free_ptr == other._free_ptr;
}

bool allocator_sorted_list::sorted_free_iterator::operator!=(
        const allocator_sorted_list::sorted_free_iterator &other) const noexcept
{
    return _free_ptr != other._free_ptr;
}

allocator_sorted_list::sorted_free_iterator &allocator_sorted_list::sorted_free_iterator::operator++() & noexcept
{
    if (_free_ptr)
    {
        _free_ptr = reinterpret_cast<block_header*>(_free_ptr)->next_block;
    }
    return *this;
}

allocator_sorted_list::sorted_free_iterator allocator_sorted_list::sorted_free_iterator::operator++(int n)
{
    auto temp = *this;
    ++(*this);
    return temp;
}

size_t allocator_sorted_list::sorted_free_iterator::size() const noexcept
{
    return reinterpret_cast<block_header*>(_free_ptr)->block_size;
}

void *allocator_sorted_list::sorted_free_iterator::operator*() const noexcept
{
    return reinterpret_cast<char*>(_free_ptr) + sizeof(block_header);    
}

allocator_sorted_list::sorted_free_iterator::sorted_free_iterator(): _free_ptr(nullptr)
{

}

allocator_sorted_list::sorted_free_iterator::sorted_free_iterator(void *trusted)
{
    if (trusted)
    {
        allocator_header* data = reinterpret_cast<allocator_header*>(trusted);
        _free_ptr = data->first_block;
    }
    else
    {
        _free_ptr = nullptr;
    }    
}

bool allocator_sorted_list::sorted_iterator::operator==(const allocator_sorted_list::sorted_iterator & other) const noexcept
{
    return _current_ptr == other._current_ptr;
}

bool allocator_sorted_list::sorted_iterator::operator!=(const allocator_sorted_list::sorted_iterator &other) const noexcept
{
    return _current_ptr != other._current_ptr;
}

allocator_sorted_list::sorted_iterator &allocator_sorted_list::sorted_iterator::operator++() & noexcept
{
    if (_current_ptr)
    {
        block_header* block = reinterpret_cast<block_header*>(_current_ptr);
        _current_ptr = reinterpret_cast<char*>(_current_ptr) + block->block_size;

        allocator_header* data = reinterpret_cast<allocator_header*>(_trusted_memory);

        void* end_of_memory = reinterpret_cast<char*>(_trusted_memory) + data->total_size;

        if (_current_ptr >= end_of_memory) { 
            _current_ptr = nullptr;
        }
    }
    return *this;
}

allocator_sorted_list::sorted_iterator allocator_sorted_list::sorted_iterator::operator++(int n)
{
    auto temp = *this;
    ++(*this);
    return temp;
}

size_t allocator_sorted_list::sorted_iterator::size() const noexcept
{
    return reinterpret_cast<block_header*>(_current_ptr)->block_size;
}

void *allocator_sorted_list::sorted_iterator::operator*() const noexcept
{
    return reinterpret_cast<char*>(_current_ptr) + sizeof(block_header);
}

allocator_sorted_list::sorted_iterator::sorted_iterator() : _free_ptr(nullptr), _current_ptr(nullptr), _trusted_memory(nullptr) {}

allocator_sorted_list::sorted_iterator::sorted_iterator(void *trusted): _trusted_memory(trusted)
{
    if (_trusted_memory != nullptr) {
        _current_ptr = reinterpret_cast<char*>(_trusted_memory) + sizeof(allocator_header);
    } else {
        _current_ptr = nullptr;
    }
}

bool allocator_sorted_list::sorted_iterator::occupied() const noexcept
{
    return reinterpret_cast<block_header*>(_current_ptr)->next_block == _trusted_memory;
}

