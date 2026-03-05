#include <not_implemented.h>
#include "../include/allocator_global_heap.h"

allocator_global_heap::allocator_global_heap(){

}

[[nodiscard]] void *allocator_global_heap::do_allocate_sm(
    size_t size)
{
    std::lock_guard<std::mutex> lock(_lock);
    size_t result_size = size + size_t_size;

    void* result;

    try{
        result = ::operator new(result_size);
    } catch(std::bad_alloc &e)
        {
        throw;
    }


    *static_cast<size_t*>(result) = size;

    return static_cast<char*>(result) + size_t_size;

}

void allocator_global_heap::do_deallocate_sm(
    void *at){
    if (at == nullptr) return;

    std::lock_guard<std::mutex> lock(_lock);
    ::operator delete(static_cast<char*>(at) - size_t_size);
}

allocator_global_heap::~allocator_global_heap(){

}

allocator_global_heap::allocator_global_heap(const allocator_global_heap &other){

}

allocator_global_heap &allocator_global_heap::operator=(const allocator_global_heap &other){
    return *this;
}

bool allocator_global_heap::do_is_equal(const std::pmr::memory_resource &other) const noexcept{
    return this == &other;
}

allocator_global_heap::allocator_global_heap(allocator_global_heap &&other) noexcept{

}

allocator_global_heap &allocator_global_heap::operator=(allocator_global_heap &&other) noexcept{
    return *this;
}
