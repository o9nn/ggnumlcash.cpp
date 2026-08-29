#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

// ============================================================================
// Financial Memory Pool - Phase 1, Issue #002 (Task 2.3)
//
// A cache-friendly, thread-safe pool allocator tuned for financial workloads
// that allocate/free many same-sized blocks at high frequency (ticks, order
// book updates, risk scenario buffers). It provides:
//
//   - O(1) acquire/release from a free-list
//   - Cache-line-aligned (64-byte) block allocation to avoid false sharing
//   - Pre-reserved capacity to eliminate allocation on the hot path
//   - Growth by chunked slabs (geometric) rather than per-object new/delete
//   - Usage statistics for monitoring (hits, misses, high-water mark)
//
// The pool is used for the fixed-size working buffers in market-data and
// risk pipelines where allocation latency must be bounded (<1us) and
// predictable. It is NOT a general-purpose allocator.
// ============================================================================

namespace ggnucash {
namespace tensors {

// ============================================================================
// Pool Statistics
// ============================================================================

struct MemoryPoolStats {
    uint64_t total_blocks;       // Blocks currently allocated from the OS
    uint64_t free_blocks;        // Blocks on the free list
    uint64_t used_blocks;        // Blocks currently checked out
    uint64_t high_water_mark;    // Max simultaneously used blocks
    uint64_t acquires;           // Total acquire() calls
    uint64_t releases;           // Total release() calls
    uint64_t growth_events;      // Times the pool had to grow

    MemoryPoolStats()
        : total_blocks(0), free_blocks(0), used_blocks(0), high_water_mark(0),
          acquires(0), releases(0), growth_events(0) {}
};

// ============================================================================
// Financial Memory Pool
// ============================================================================

class FinancialMemoryPool {
public:
    // block_size     size in bytes of each pooled block (rounded up to a
    //                multiple of the cache line)
    // initial_blocks number of blocks to pre-allocate
    explicit FinancialMemoryPool(size_t block_size, size_t initial_blocks = 64);
    ~FinancialMemoryPool() = default;

    // Non-copyable, non-movable (owns raw memory).
    FinancialMemoryPool(const FinancialMemoryPool &) = delete;
    FinancialMemoryPool & operator=(const FinancialMemoryPool &) = delete;

    // Acquire a block. Grows the pool if exhausted. Returns aligned memory.
    void * acquire();

    // Release a block back to the pool. The pointer must have come from
    // acquire() and not already be released.
    void release(void * ptr);

    // Current statistics.
    MemoryPoolStats stats() const;

    // Configured (cache-line-aligned) block size.
    size_t block_size() const { return block_size_; }

    // Number of blocks currently available without growing.
    size_t available() const;

    // Pre-warm the pool to at least `n` total blocks.
    void reserve(size_t n);

private:
    static constexpr size_t kCacheLine = 64;

    struct FreeNode {
        FreeNode * next;
    };

    size_t block_size_;             // aligned block payload size
    size_t aligned_block_stride_;   // stride including bookkeeping
    FreeNode * free_list_;          // intrusive singly-linked free list

    // Owned slabs of memory (each a vector of bytes).
    std::vector<std::unique_ptr<uint8_t[]>> slabs_;

    mutable std::mutex mutex_;

    // Stats (guarded by mutex_).
    uint64_t total_blocks_;
    uint64_t used_blocks_;
    uint64_t high_water_mark_;
    uint64_t acquires_;
    uint64_t releases_;
    uint64_t growth_events_;

    static size_t align_up(size_t value, size_t alignment);
    // Caller must hold mutex_. Adds at least min_blocks new blocks.
    void grow(size_t min_blocks);
};

} // namespace tensors
} // namespace ggnucash
