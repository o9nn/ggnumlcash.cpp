#include "financial-memory-pool.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace ggnucash {
namespace tensors {

FinancialMemoryPool::FinancialMemoryPool(size_t block_size, size_t initial_blocks)
    : block_size_(align_up(std::max(block_size, sizeof(FreeNode)), kCacheLine)),
      aligned_block_stride_(block_size_),
      free_list_(nullptr),
      total_blocks_(0),
      used_blocks_(0),
      high_water_mark_(0),
      acquires_(0),
      releases_(0),
      growth_events_(0) {
    if (initial_blocks > 0) {
        grow(initial_blocks);
    }
}

size_t FinancialMemoryPool::align_up(size_t value, size_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

void FinancialMemoryPool::grow(size_t min_blocks) {
    // Geometric growth: at least double the current capacity, but honor the
    // requested minimum. Caller must hold mutex_.
    size_t new_blocks = std::max(min_blocks, std::max<size_t>(64, total_blocks_));

    auto slab = std::make_unique<uint8_t[]>(new_blocks * aligned_block_stride_);
    uint8_t * base = slab.get();

    // Push all blocks in the new slab onto the free list.
    for (size_t i = 0; i < new_blocks; i++) {
        auto * node = reinterpret_cast<FreeNode *>(base + i * aligned_block_stride_);
        node->next = free_list_;
        free_list_ = node;
    }

    slabs_.push_back(std::move(slab));
    total_blocks_ += new_blocks;
    growth_events_++;
}

void FinancialMemoryPool::reserve(size_t n) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (total_blocks_ < n) {
        grow(n - total_blocks_);
    }
}

void * FinancialMemoryPool::acquire() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!free_list_) {
        grow(64);
    }
    FreeNode * node = free_list_;
    free_list_ = node->next;

    used_blocks_++;
    acquires_++;
    if (used_blocks_ > high_water_mark_) {
        high_water_mark_ = used_blocks_;
    }
    return static_cast<void *>(node);
}

void FinancialMemoryPool::release(void * ptr) {
    if (!ptr) return;
    std::lock_guard<std::mutex> lock(mutex_);
    auto * node = static_cast<FreeNode *>(ptr);
    node->next = free_list_;
    free_list_ = node;
    if (used_blocks_ > 0) used_blocks_--;
    releases_++;
}

MemoryPoolStats FinancialMemoryPool::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    MemoryPoolStats s;
    size_t free_count = 0;
    for (FreeNode * node = free_list_; node; node = node->next) {
        free_count++;
    }
    s.free_blocks = free_count;
    s.used_blocks = used_blocks_;
    s.total_blocks = total_blocks_;
    s.high_water_mark = high_water_mark_;
    s.acquires = acquires_;
    s.releases = releases_;
    s.growth_events = growth_events_;
    return s;
}

size_t FinancialMemoryPool::available() const {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t count = 0;
    for (FreeNode * node = free_list_; node; node = node->next) {
        count++;
    }
    return count;
}

} // namespace tensors
} // namespace ggnucash
