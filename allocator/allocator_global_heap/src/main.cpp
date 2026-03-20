#include <iostream>
#include <vector>
#include <memory_resource>
#include "../include/allocator_global_heap.h"

int main() {
    allocator_global_heap my_res;
    std::pmr::polymorphic_allocator<int> alloc(&my_res);
    
    std::pmr::vector<int> numbers(alloc);
        
    std::cout << "Adding elements to vector" << std::endl;
    for (int i = 1; i <= 5; ++i) {
        numbers.push_back(i * 10);
    }
    std::cout << "Vector contents: ";
    for (int n : numbers) {
        std::cout << n << " ";
    }
    std::cout << "\nCheck" << std::endl;

    std::cout << "Memory was safely deallocated" << std::endl;

    return 0;
}