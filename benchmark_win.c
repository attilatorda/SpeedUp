#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <windows.h>

// Feature flags
#define ENABLE_SIMD 1  // Set to 0 to disable SIMD implementations

#if ENABLE_SIMD
#include <immintrin.h>  // For AVX2 intrinsics
#endif

// Assembly function declarations using Windows calling convention
__declspec(dllexport) int64_t branching_binary_search_win64(int64_t* array, int64_t key, int64_t size);
__declspec(dllexport) int64_t branchless_binary_search_win64(int64_t* array, int64_t key, int64_t size);
__declspec(dllexport) int64_t optimized_binary_search_win64(int64_t* array, int64_t key, int64_t size);
__declspec(dllexport) int64_t measure_cycles_win64(int64_t (*func)(int64_t*, int64_t, int64_t), 
                                                   int64_t* array, int64_t key, int64_t size);

// Reference C implementation
int64_t reference_binary_search(int64_t* array, int64_t key, int64_t size) {
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

#if ENABLE_SIMD
// SIMD-optimized binary search using AVX2
// Strategy: Check 4 positions simultaneously to narrow the search range faster
int64_t simd_binary_search(int64_t* array, int64_t key, int64_t size) {
    int64_t left = 0, right = size - 1;
    
    // For small arrays, use standard binary search
    if (size < 32) {
        return reference_binary_search(array, key, size);
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
int64_t simd_optimized_binary_search(int64_t* array, int64_t key, int64_t size) {
    int64_t left = 0, right = size - 1;
    
    // For very small arrays, use standard binary search
    if (size < 32) {
        return reference_binary_search(array, key, size);
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
#endif

// Windows high-precision timer
double get_time_ns() {
    static LARGE_INTEGER frequency = {0};
    if (frequency.QuadPart == 0) {
        QueryPerformanceFrequency(&frequency);
    }
    
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    
    return (double)counter.QuadPart * 1e9 / (double)frequency.QuadPart;
}

// Benchmark function with warmup
double benchmark_search(int64_t (*search_func)(int64_t*, int64_t, int64_t),
                       int64_t* array, int64_t size, 
                       int64_t* keys, int64_t num_keys, int iterations) {
    // Warmup: 10% of iterations
    int warmup = iterations / 10;
    for (int iter = 0; iter < warmup; iter++) {
        for (int64_t k = 0; k < num_keys; k++) {
            search_func(array, keys[k], size);
        }
    }
    
    // Actual benchmark
    double total_time = 0;
    for (int iter = 0; iter < iterations; iter++) {
        double start = get_time_ns();
        
        for (int64_t k = 0; k < num_keys; k++) {
            search_func(array, keys[k], size);
        }
        
        double end = get_time_ns();
        total_time += (end - start);
    }
    
    return total_time / (iterations * num_keys);
}

// Verification function
int verify_function(int64_t (*search_func)(int64_t*, int64_t, int64_t),
                   int64_t* array, int64_t size,
                   int64_t* keys, int64_t num_test_keys) {
    for (int64_t k = 0; k < num_test_keys; k++) {
        int64_t expected = reference_binary_search(array, keys[k], size);
        int64_t result = search_func(array, keys[k], size);
        if (result != expected) {
            printf("Error: got %lld, expected %lld for key %lld\n",
                   result, expected, keys[k]);
            return 0;
        }
    }
    return 1;
}

int main() {
    printf("Binary Search Benchmark Suite (Windows x64)\n");
    printf("============================================\n");
    printf("CPU: AMD Ryzen 5 2600X\n");
    
#if ENABLE_SIMD
    printf("SIMD: Enabled (AVX2)\n\n");
#else
    printf("SIMD: Disabled\n\n");
#endif
    
    // Test configurations
    int64_t test_sizes[] = {10000, 100000, 1000000, 10000000};
    int num_sizes = sizeof(test_sizes) / sizeof(test_sizes[0]);
    
    // Test functions
    struct {
        const char* name;
        int64_t (*func)(int64_t*, int64_t, int64_t);
    } functions[] = {
        {"Reference C", reference_binary_search},
        {"Branching ASM", branching_binary_search_win64},
        {"Branchless ASM", branchless_binary_search_win64},
        {"Optimized ASM", optimized_binary_search_win64},
#if ENABLE_SIMD
        {"SIMD AVX2", simd_binary_search},
        {"SIMD + Optimized", simd_optimized_binary_search},
#endif
    };
    int num_funcs = sizeof(functions) / sizeof(functions[0]);
    
    // Open CSV file
    FILE* csv = fopen("results.csv", "w");
    if (!csv) {
        fprintf(stderr, "Failed to open results.csv\n");
        return 1;
    }
    
    // Write CSV header
    fprintf(csv, "Array Size,Elements,");
    for (int i = 0; i < num_funcs; i++) {
        fprintf(csv, "%s (ns)", functions[i].name);
        if (i < num_funcs - 1) fprintf(csv, ",");
    }
    fprintf(csv, "\n");
    
    // Run benchmarks for each array size
    for (int size_idx = 0; size_idx < num_sizes; size_idx++) {
        int64_t size = test_sizes[size_idx];
        
        // Adjust test parameters based on array size
        int64_t num_keys = (size < 100000) ? 10000 : 1000;
        int iterations = (size < 1000000) ? 1000 : 500;
        
        printf("\n========================================\n");
        printf("Array Size: %lld elements (%.2f MB)\n", 
               size, size * sizeof(int64_t) / (1024.0 * 1024.0));
        printf("Test keys: %lld, Iterations: %d\n", num_keys, iterations);
        printf("========================================\n");
        
        // Allocate and initialize array
        int64_t* array = (int64_t*)_aligned_malloc(size * sizeof(int64_t), 64);
        int64_t* keys = (int64_t*)malloc(num_keys * sizeof(int64_t));
        
        if (!array || !keys) {
            fprintf(stderr, "Memory allocation failed for size %lld\n", size);
            fclose(csv);
            return 1;
        }
        
        // Initialize array with even numbers
        for (int64_t i = 0; i < size; i++) {
            array[i] = i * 2;
        }
        
        // Generate test keys (mix of existing and non-existing)
        srand(12345);  // Fixed seed for reproducibility
        for (int64_t i = 0; i < num_keys; i++) {
            if (rand() % 2 == 0) {
                keys[i] = (rand() % size) * 2;      // Existing key
            } else {
                keys[i] = (rand() % size) * 2 + 1;  // Non-existing key
            }
        }
        
        printf("\n%-20s %15s %12s\n", "Function", "Time (ns)", "Correct");
        printf("------------------------------------------------\n");
        
        // Write array size to CSV
        fprintf(csv, "%lld,%lld,", size, size);
        
        // Benchmark each function
        for (int i = 0; i < num_funcs; i++) {
            // Verify correctness
            int correct = verify_function(functions[i].func, array, size, keys, 
                                         (num_keys < 100) ? num_keys : 100);
            
            if (!correct) {
                printf("%-20s %15s %12s\n", functions[i].name, "FAILED", "✗");
                fprintf(csv, "FAILED");
            } else {
                // Measure performance
                double avg_time = benchmark_search(functions[i].func, array, size, 
                                                  keys, num_keys, iterations);
                
                printf("%-20s %15.2f %12s\n", functions[i].name, avg_time, "✓");
                fprintf(csv, "%.2f", avg_time);
            }
            
            if (i < num_funcs - 1) fprintf(csv, ",");
        }
        fprintf(csv, "\n");
        fflush(csv);
        
        // Cleanup
        _aligned_free(array);
        free(keys);
    }
    
    fclose(csv);
    
    printf("\n========================================\n");
    printf("Benchmark completed!\n");
    printf("Results saved to: results.csv\n");
    printf("========================================\n");
    
    return 0;
}