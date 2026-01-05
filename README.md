# Optimizing Binary Search: A Comparative Performance Analysis

*Performance Study on x86-64 Architecture*  
**January 2026**

---

## Abstract

Binary search is a fundamental algorithm with *O(log n)* time complexity, yet its practical performance varies significantly based on implementation details and data size. This study presents a comprehensive performance analysis of six binary search implementations on modern x86-64 architecture (AMD Ryzen 5 2600X), ranging from standard C implementations to hand-optimized assembly with SIMD instructions.

Contrary to common assumptions, we demonstrate that the optimal implementation varies with array size:

- **Branchless assembly** excels for small arrays (*31.2% faster at 10K elements*)
- **Branching assembly** performs best for large arrays (*12.3% faster at 10M elements*)
- **Software-pipelined assembly** provides the most consistent performance overall
- **SIMD implementations** deliver mixed results and often underperform at scale

---

## 1. Introduction

Binary search is one of the most fundamental algorithms in computer science, with applications ranging from database indexing to compiler symbol tables. While its algorithmic complexity of *O(log n)* is well-established, real-world performance is dominated by constant factors such as:

- Branch prediction
- Cache behavior
- Instruction-level parallelism
- Memory latency

This paper explores how low-level implementation choices influence observed performance.

---

## 2.3 Implementation Summaries

### 1. Reference C

**Approach:** Standard textbook implementation  
**Optimization:** Compiler optimizations (`-O3`)

```c
while (left <= right) {
    mid = left + (right - left)/2;
    if (array[mid] == key) return mid;
    if (array[mid] < key) 
        left = mid + 1;
    else 
        right = mid - 1;
}
```

**Best for:** Baseline comparison, portability

---

### 2. Branching ASM

**Approach:** Hand-optimized x86-64 assembly  
**Optimization:** Minimal instruction count, branch prediction

```asm
.search_loop:
    mov rcx, rax
    add rcx, rbx
    shr rcx, 1
    mov rdx, [r12 + rcx*8]
    cmp rdx, r13
    je .found
    jl .greater
.less:
    mov rbx, rcx
    dec rbx
    jmp .search_loop
```

**Best for:** Large arrays (≥1M elements)

---

### 3. Branchless ASM

**Approach:** Conditional moves (`CMOV`)  
**Optimization:** Eliminates branch misprediction penalties

```asm
lea rdi, [rcx + 1]
lea rsi, [rcx - 1]
cmovl rax, rdi
cmovg rbx, rsi
```

**Best for:** Small arrays (≤100K elements)

---

### 4. Optimized ASM (Software-Pipelined)

**Approach:** Software pipelining  
**Optimization:** Hides memory latency

```asm
.greater_opt:
    mov rax, rcx
    inc rax
    mov rcx, rax
    add rcx, rbx
    shr rcx, 1
    mov rdx, [r12 + rcx*8]
    jmp .search_loop_opt
```

**Best for:** Consistent performance across all sizes

---

### 5. SIMD AVX2

**Approach:** Vectorized comparisons

```c
__m256i values = _mm256_set_epi64x(
    array[pos[3]], array[pos[2]], 
    array[pos[1]], array[pos[0]]
);
__m256i equal_mask =
    _mm256_cmpeq_epi64(values, key_vec);
```

---

### 6. SIMD + Optimized Hybrid

**Approach:** SIMD for large ranges, scalar finish

```c
while (right - left > 32) {
    // SIMD comparison
}

while (left <= right) {
    // Scalar search
}
```

---

## 3. Results

### 3.1 Performance Comparison

| Implementation | 10K | 100K | 1M | 10M |
|---------------|-----|------|----|-----|
| Reference C | 64.99 ns | 57.90 ns | 68.77 ns | 78.89 ns |
| Branching ASM | 61.85 ns | 56.47 ns | 61.87 ns | **69.18 ns** |
| Branchless ASM | **44.73 ns** | 58.31 ns | 65.79 ns | 78.58 ns |
| Optimized ASM | 60.15 ns | **52.60 ns** | **57.53 ns** | 75.46 ns |
| SIMD AVX2 | 69.08 ns | 56.73 ns | 64.76 ns | 70.91 ns |
| SIMD + Opt | 70.89 ns | 57.21 ns | 66.03 ns | 80.58 ns |

---

## Conclusion

Binary search performance is dominated by microarchitectural effects rather than asymptotic complexity. There is no universally optimal implementation; performance engineering must be context-aware.

---
