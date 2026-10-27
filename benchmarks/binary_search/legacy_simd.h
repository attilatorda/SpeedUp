// The two AVX2 searches from the first prototype (legacy/benchmark_win.c),
// unchanged apart from const and the target attribute, so the study measures
// them as they were published.
#pragma once
#include <stdint.h>
#include <immintrin.h>

#if defined(__GNUC__) || defined(__clang__)
#define LEGACY_AVX2 __attribute__((target("avx2")))
#else
#define LEGACY_AVX2
#endif

static int64_t legacy_reference_binary_search(const int64_t* array, int64_t key, int64_t size) {
    int64_t left = 0, right = size - 1;
    while (left <= right) {
        int64_t mid = left + (right - left) / 2;
        if (array[mid] == key) {
            return mid;
        } else if (array[mid] < key) {
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }
    return -1;
}

// SIMD-optimized binary search using AVX2
// Strategy: Check 4 positions simultaneously to narrow the search range faster
LEGACY_AVX2 static int64_t simd_binary_search(const int64_t* array, int64_t key, int64_t size) {
    int64_t left = 0, right = size - 1;
    
    // For small arrays, use standard binary search
    if (size < 32) {
        return legacy_reference_binary_search(array, key, size);
    }
    
    __m256i key_vec = _mm256_set1_epi64x(key);
    
    while (right - left > 16) {
        // Calculate 4 candidate positions
        int64_t quarter = (right - left + 1) / 4;
        int64_t pos[4] = {
            left + quarter,
            left + quarter * 2,
            left + quarter * 3,
            right
        };
        
        // Load 4 values (non-contiguous, so we have to do this manually)
        __m256i values = _mm256_set_epi64x(
            array[pos[3]],
            array[pos[2]],
            array[pos[1]],
            array[pos[0]]
        );
        
        // Compare: result[i] = 0xFFFFFFFFFFFFFFFF if array[pos[i]] == key
        __m256i equal_mask = _mm256_cmpeq_epi64(values, key_vec);
        
        // Check if we found the key
        int mask = _mm256_movemask_pd(_mm256_castsi256_pd(equal_mask));
        if (mask != 0) {
            // Found it! Determine which position
            for (int i = 0; i < 4; i++) {
                if (mask & (1 << i)) {
                    return pos[i];
                }
            }
        }
        
        // Compare: result[i] = 0xFFFFFFFFFFFFFFFF if array[pos[i]] < key
        __m256i less_mask = _mm256_cmpgt_epi64(key_vec, values);
        int less_bits = _mm256_movemask_pd(_mm256_castsi256_pd(less_mask));
        
        // Find the narrowest range based on comparisons
        // If all values < key, search right of pos[3]
        if (less_bits == 0xF) {
            left = pos[3] + 1;
        }
        // If pos[2] < key <= pos[3], search between pos[2] and pos[3]
        else if (less_bits == 0x7) {
            left = pos[2] + 1;
            right = pos[3] - 1;
        }
        // If pos[1] < key <= pos[2], search between pos[1] and pos[2]
        else if (less_bits == 0x3) {
            left = pos[1] + 1;
            right = pos[2] - 1;
        }
        // If pos[0] < key <= pos[1], search between pos[0] and pos[1]
        else if (less_bits == 0x1) {
            left = pos[0] + 1;
            right = pos[1] - 1;
        }
        // If key <= pos[0], search left of pos[0]
        else {
            right = pos[0] - 1;
        }
    }
    
    // Finish with standard binary search for remaining elements
    while (left <= right) {
        int64_t mid = left + (right - left) / 2;
        
        if (array[mid] == key) {
            return mid;
        } else if (array[mid] < key) {
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }
    
    return -1;
}

// SIMD binary search with software pipelining
// Combines the best of both optimizations
LEGACY_AVX2 static int64_t simd_optimized_binary_search(const int64_t* array, int64_t key, int64_t size) {
    int64_t left = 0, right = size - 1;
    
    // For very small arrays, use standard binary search
    if (size < 32) {
        return legacy_reference_binary_search(array, key, size);
    }
    
    __m256i key_vec = _mm256_set1_epi64x(key);
    
    // Phase 1: SIMD for large ranges
    while (right - left > 32) {
        int64_t quarter = (right - left + 1) / 4;
        int64_t pos[4] = {
            left + quarter,
            left + quarter * 2,
            left + quarter * 3,
            right
        };
        
        // Prefetch next potential positions
        _mm_prefetch((const char*)&array[left + quarter/2], _MM_HINT_T0);
        _mm_prefetch((const char*)&array[right - quarter/2], _MM_HINT_T0);
        
        __m256i values = _mm256_set_epi64x(
            array[pos[3]],
            array[pos[2]],
            array[pos[1]],
            array[pos[0]]
        );
        
        __m256i equal_mask = _mm256_cmpeq_epi64(values, key_vec);
        int mask = _mm256_movemask_pd(_mm256_castsi256_pd(equal_mask));
        
        if (mask != 0) {
            for (int i = 0; i < 4; i++) {
                if (mask & (1 << i)) {
                    return pos[i];
                }
            }
        }
        
        __m256i less_mask = _mm256_cmpgt_epi64(key_vec, values);
        int less_bits = _mm256_movemask_pd(_mm256_castsi256_pd(less_mask));
        
        if (less_bits == 0xF) {
            left = pos[3] + 1;
        } else if (less_bits == 0x7) {
            left = pos[2] + 1;
            right = pos[3] - 1;
        } else if (less_bits == 0x3) {
            left = pos[1] + 1;
            right = pos[2] - 1;
        } else if (less_bits == 0x1) {
            left = pos[0] + 1;
            right = pos[1] - 1;
        } else {
            right = pos[0] - 1;
        }
    }
    
    // Phase 2: Software-pipelined binary search for remaining elements
    if (left > right) return -1;
    
    int64_t mid = left + (right - left) / 2;
    int64_t value = array[mid];
    
    while (left <= right) {
        if (value == key) {
            return mid;
        }
        
        if (value < key) {
            left = mid + 1;
        } else {
            right = mid - 1;
        }
        
        if (left > right) break;
        
        mid = left + (right - left) / 2;
        value = array[mid];  // Load for next iteration
    }
    
    return -1;
}
