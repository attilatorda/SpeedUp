#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

// Assembly function declarations
int64_t branching_cache_optimized_fixed(int64_t* array, int64_t key, int64_t size);
int64_t branchless_cache_optimized_fixed(int64_t* array, int64_t key, int64_t size);
int64_t branchless_prefetch_optimized_fixed(int64_t* array, int64_t key, int64_t size);

// Reference implementation
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

// Test for correctness
void test_correctness(const char* name, int64_t (*func)(int64_t*, int64_t, int64_t),
                     int64_t* array, int64_t size) {
    printf("Testing %s...\n", name);
    
    int errors = 0;
    
    // Test all existing elements
    for (int64_t i = 0; i < size; i++) {
        int64_t result = func(array, array[i], size);
        if (result != i) {
            printf("  ERROR: Key %lld at index %lld returned %lld\n", 
                   array[i], i, result);
            errors++;
            if (errors > 10) {
                printf("  Too many errors, stopping...\n");
                break;
            }
        }
    }
    
    // Test non-existent elements (odd numbers)
    for (int64_t i = 0; i < 1000; i++) {
        int64_t key = i * 2 + 1;  // Odd numbers don't exist
        if (key > array[size-1]) break;
        
        int64_t result = func(array, key, size);
        if (result != -1) {
            printf("  ERROR: Non-existent key %lld returned %lld (should be -1)\n", 
                   key, result);
            errors++;
        }
    }
    
    // Test boundary cases
    int64_t result;
    
    // Key smaller than all elements
    result = func(array, -1, size);
    if (result != -1) {
        printf("  ERROR: Key -1 returned %lld (should be -1)\n", result);
        errors++;
    }
    
    // Key larger than all elements
    result = func(array, array[size-1] + 100, size);
    if (result != -1) {
        printf("  ERROR: Key %lld returned %lld (should be -1)\n", 
               array[size-1] + 100, result);
        errors++;
    }
    
    // Single element array
    int64_t single_array[1] = {42};
    result = func(single_array, 42, 1);
    if (result != 0) {
        printf("  ERROR: Single element array failed\n");
        errors++;
    }
    
    result = func(single_array, 43, 1);
    if (result != -1) {
        printf("  ERROR: Single element array (non-existent) failed\n");
        errors++;
    }
    
    if (errors == 0) {
        printf("  PASS: All tests passed!\n");
    } else {
        printf("  FAIL: %d errors found\n", errors);
    }
    printf("\n");
}

// Test cache alignment edge cases
void test_cache_alignment() {
    printf("Testing cache alignment edge cases...\n");
    
    // Create array with size not multiple of cache line
    const int64_t size = 100;
    int64_t* array = malloc(size * sizeof(int64_t));
    
    for (int64_t i = 0; i < size; i++) {
        array[i] = i * 2;
    }
    
    // Test all three implementations
    int64_t ref, result1, result2, result3;
    
    for (int64_t key = 0; key < size * 2; key += 1) {
        ref = reference_binary_search(array, key, size);
        result1 = branching_cache_optimized_fixed(array, key, size);
        result2 = branchless_cache_optimized_fixed(array, key, size);
        result3 = branchless_prefetch_optimized_fixed(array, key, size);
        
        if (result1 != ref || result2 != ref || result3 != ref) {
            printf("  MISMATCH: key=%lld, ref=%lld, "
                   "opt1=%lld, opt2=%lld, opt3=%lld\n",
                   key, ref, result1, result2, result3);
        }
    }
    
    free(array);
    printf("  Cache alignment tests completed\n\n");
}

// Test with various array sizes
void test_various_sizes() {
    printf("Testing various array sizes...\n");
    
    int64_t sizes[] = {1, 2, 3, 7, 8, 9, 15, 16, 17, 
                       31, 32, 33, 63, 64, 65, 127, 128, 129,
                       1000, 10000, 100000};
    
    int num_sizes = sizeof(sizes) / sizeof(sizes[0]);
    
    for (int s = 0; s < num_sizes; s++) {
        int64_t size = sizes[s];
        int64_t* array = malloc(size * sizeof(int64_t));
        
        for (int64_t i = 0; i < size; i++) {
            array[i] = i * 2;
        }
        
        // Test a few keys
        int errors = 0;
        for (int64_t k = 0; k < size; k += size / 10 + 1) {
            int64_t key = array[k];
            int64_t ref = reference_binary_search(array, key, size);
            int64_t res1 = branching_cache_optimized_fixed(array, key, size);
            int64_t res2 = branchless_cache_optimized_fixed(array, key, size);
            int64_t res3 = branchless_prefetch_optimized_fixed(array, key, size);
            
            if (res1 != ref || res2 != ref || res3 != ref) {
                errors++;
            }
        }
        
        if (errors > 0) {
            printf("  Size %lld: FAILED (%d errors)\n", size, errors);
        } else {
            printf("  Size %lld: OK\n", size);
        }
        
        free(array);
    }
    printf("\n");
}

// Performance comparison
void benchmark_fixed() {
    printf("Benchmarking corrected implementations...\n");
    
    const int64_t size = 1000000;
    const int64_t num_keys = 10000;
    const int iterations = 100;
    
    int64_t* array = malloc(size * sizeof(int64_t));
    int64_t* keys = malloc(num_keys * sizeof(int64_t));
    
    // Initialize
    for (int64_t i = 0; i < size; i++) {
        array[i] = i * 2;
    }
    
    srand(time(NULL));
    for (int64_t i = 0; i < num_keys; i++) {
        if (rand() % 2 == 0) {
            keys[i] = (rand() % size) * 2;  // Existing
        } else {
            keys[i] = (rand() % size) * 2 + 1;  // Non-existing
        }
    }
    
    // Test each implementation
    int64_t (*functions[])(int64_t*, int64_t, int64_t) = {
        reference_binary_search,
        branching_cache_optimized_fixed,
        branchless_cache_optimized_fixed,
        branchless_prefetch_optimized_fixed
    };
    
    const char* names[] = {
        "Reference Binary Search",
        "Branching + Cache Opt (Fixed)",
        "Branchless + Cache Opt (Fixed)",
        "Ultra Optimized Prefetch (Fixed)"
    };
    
    struct timespec start, end;
    
    for (int f = 0; f < 4; f++) {
        // Warm-up
        for (int64_t i = 0; i < 1000; i++) {
            functions[f](array, keys[i % num_keys], size);
        }
        
        // Measure
        clock_gettime(CLOCK_MONOTONIC, &start);
        
        for (int iter = 0; iter < iterations; iter++) {
            for (int64_t k = 0; k < num_keys; k++) {
                functions[f](array, keys[k], size);
            }
        }
        
        clock_gettime(CLOCK_MONOTONIC, &end);
        
        double elapsed = (end.tv_sec - start.tv_sec) * 1e9;
        elapsed += (end.tv_nsec - start.tv_nsec);
        double avg_time = elapsed / (iterations * num_keys);
        
        printf("  %-35s: %8.2f ns/search\n", names[f], avg_time);
    }
    
    free(array);
    free(keys);
}

int main() {
    printf("Testing Corrected Binary Search Implementations\n");
    printf("===============================================\n\n");
    
    // Create a test array
    const int64_t test_size = 100000;
    int64_t* test_array = malloc(test_size * sizeof(int64_t));
    
    for (int64_t i = 0; i < test_size; i++) {
        test_array[i] = i * 2;
    }
    
    // Run tests
    test_correctness("Reference Binary Search", reference_binary_search, test_array, test_size);
    test_correctness("Branching + Cache Opt (Fixed)", branching_cache_optimized_fixed, test_array, test_size);
    test_correctness("Branchless + Cache Opt (Fixed)", branchless_cache_optimized_fixed, test_array, test_size);
    test_correctness("Ultra Optimized Prefetch (Fixed)", branchless_prefetch_optimized_fixed, test_array, test_size);
    
    test_cache_alignment();
    test_various_sizes();
    benchmark_fixed();
    
    free(test_array);
    
    printf("All tests completed!\n");
    return 0;
}