; Windows x64 compatible binary search implementations - FIXED VERSION
; Calling convention: RCX = first param, RDX = second param, R8 = third param
; Volatile registers: RAX, RCX, RDX, R8, R9, R10, R11
; Non-volatile registers: RBX, RBP, RDI, RSI, R12-R15 must be preserved

section .text

; ============================================================================
; Branching Binary Search for Windows x64
; ============================================================================
global branching_binary_search_win64
branching_binary_search_win64:
    ; RCX = array pointer, RDX = key, R8 = size
    push rbx
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15
    push rbp
    mov rbp, rsp
    
    ; Save parameters
    mov r12, rcx        ; r12 = array pointer
    mov r13, rdx        ; r13 = key
    mov r14, r8         ; r14 = size
    
    xor rax, rax        ; left = 0
    mov rbx, r14        ; right = size - 1
    dec rbx
    
.search_loop:
    cmp rax, rbx
    jg .not_found
    
    ; Calculate mid = (left + right) / 2
    mov rcx, rax
    add rcx, rbx
    shr rcx, 1          ; rcx = mid
    
    ; Load array[mid]
    mov rdx, [r12 + rcx*8]
    cmp rdx, r13
    je .found
    jl .greater
    
.less:
    ; array[mid] > key
    mov rbx, rcx
    dec rbx
    jmp .search_loop

.greater:
    ; array[mid] < key
    mov rax, rcx
    inc rax
    jmp .search_loop

.found:
    mov rax, rcx
    jmp .done

.not_found:
    mov rax, -1

.done:
    mov rsp, rbp
    pop rbp
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbx
    ret

; ============================================================================
; Branchless Binary Search for Windows x64 (CMOV)
; ============================================================================
global branchless_binary_search_win64
branchless_binary_search_win64:
    ; RCX = array pointer, RDX = key, R8 = size
    push rbx
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15
    push rbp
    mov rbp, rsp
    
    ; Save parameters
    mov r12, rcx        ; r12 = array pointer
    mov r13, rdx        ; r13 = key
    mov r14, r8         ; r14 = size
    
    xor rax, rax        ; left = 0
    mov rbx, r14        ; right = size - 1
    dec rbx
    
    ; Initialize result
    mov r15, -1         ; r15 = result (default -1)
    
.search_loop_cmov:
    cmp rax, rbx
    jg .done_cmov
    
    ; Calculate mid = (left + right) / 2
    mov rcx, rax
    add rcx, rbx
    shr rcx, 1          ; rcx = mid
    
    ; Load array[mid]
    mov rdx, [r12 + rcx*8]
    cmp rdx, r13
    je .found_cmov      ; Exit immediately on match
    
    ; Prepare new bounds
    lea rdi, [rcx + 1]  ; mid + 1
    lea rsi, [rcx - 1]  ; mid - 1
    
    ; Branchless updates
    cmovl rax, rdi      ; if array[mid] < key: left = mid + 1
    cmovg rbx, rsi      ; if array[mid] > key: right = mid - 1
    
    jmp .search_loop_cmov

.found_cmov:
    mov rax, rcx        ; return index
    mov rsp, rbp
    pop rbp
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbx
    ret

.done_cmov:
    mov rax, r15        ; return result (not found)
    
    mov rsp, rbp
    pop rbp
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbx
    ret

; ============================================================================
; Optimized Binary Search with Software Pipelining for Windows x64
; ============================================================================
global optimized_binary_search_win64
optimized_binary_search_win64:
    ; RCX = array pointer, RDX = key, R8 = size
    push rbx
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15
    push rbp
    mov rbp, rsp
    
    ; Save parameters
    mov r12, rcx        ; r12 = array pointer
    mov r13, rdx        ; r13 = key
    mov r14, r8         ; r14 = size
    
    xor rax, rax        ; left = 0
    mov rbx, r14        ; right = size - 1
    dec rbx
    
    ; Check for empty/single element
    cmp rax, rbx
    jg .not_found_opt
    
    ; Prime the pipeline - load first mid value
    mov rcx, rax
    add rcx, rbx
    shr rcx, 1
    mov rdx, [r12 + rcx*8]  ; Prefetch first value
    
.search_loop_opt:
    ; Value already loaded in rdx from previous iteration
    cmp rdx, r13
    je .found_opt
    jl .greater_opt
    
.less_opt:
    ; array[mid] > key: search left half
    mov rbx, rcx
    dec rbx
    
    ; Check if done
    cmp rax, rbx
    jg .not_found_opt
    
    ; Calculate next mid and load while updating bounds
    mov rcx, rax
    add rcx, rbx
    shr rcx, 1
    mov rdx, [r12 + rcx*8]  ; Load for next iteration
    
    jmp .search_loop_opt

.greater_opt:
    ; array[mid] < key: search right half
    mov rax, rcx
    inc rax
    
    ; Check if done
    cmp rax, rbx
    jg .not_found_opt
    
    ; Calculate next mid and load while updating bounds
    mov rcx, rax
    add rcx, rbx
    shr rcx, 1
    mov rdx, [r12 + rcx*8]  ; Load for next iteration
    
    jmp .search_loop_opt

.found_opt:
    mov rax, rcx
    jmp .done_opt

.not_found_opt:
    mov rax, -1

.done_opt:
    mov rsp, rbp
    pop rbp
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbx
    ret

; ============================================================================
; Measure CPU Cycles for Windows x64 - FIXED
; ============================================================================
global measure_cycles_win64
measure_cycles_win64:
    ; RCX = function pointer, RDX = array, R8 = key, R9 = size
    push rbx
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15
    push rbp
    mov rbp, rsp
    
    ; Save parameters
    mov r12, rcx        ; r12 = function pointer
    mov r13, rdx        ; r13 = array
    mov r14, r8         ; r14 = key
    mov r15, r9         ; r15 = size
    
    ; Serialize and measure start time
    cpuid               ; Serialize execution
    rdtsc               ; Read Time-Stamp Counter
    shl rdx, 32
    or rdx, rax         ; rdx = start timestamp (64-bit)
    mov rsi, rdx        ; Save start time in rsi
    
    ; Call the search function
    mov rcx, r13        ; array
    mov rdx, r14        ; key
    mov r8, r15         ; size
    call r12            ; Call the search function
    
    ; Save result
    mov rdi, rax        ; Save search result
    
    ; Serialize and measure end time
    rdtsc               ; Read Time-Stamp Counter again
    shl rdx, 32
    or rdx, rax         ; rdx = end timestamp (64-bit)
    
    ; Calculate cycles elapsed
    sub rdx, rsi        ; cycles = end - start
    mov rax, rdx        ; Return cycles in rax
    
    ; Note: search result is lost - if needed, modify to return both
    ; For now, cycles are in rax
    
    mov rsp, rbp
    pop rbp
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbx
    ret

; ============================================================================
; Alternative: Return both cycles and result
; ============================================================================
global measure_cycles_with_result_win64
measure_cycles_with_result_win64:
    ; RCX = function pointer, RDX = array, R8 = key, R9 = size
    ; Returns: RAX = cycles, RDX = search result
    push rbx
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15
    push rbp
    mov rbp, rsp
    
    ; Save parameters
    mov r12, rcx        ; r12 = function pointer
    mov r13, rdx        ; r13 = array
    mov r14, r8         ; r14 = key
    mov r15, r9         ; r15 = size
    
    ; Serialize and measure start time
    xor rax, rax
    cpuid               ; Serialize execution
    rdtsc               ; Read Time-Stamp Counter
    shl rdx, 32
    or rdx, rax         ; rdx = start timestamp (64-bit)
    mov rsi, rdx        ; Save start time in rsi
    
    ; Call the search function
    mov rcx, r13        ; array
    mov rdx, r14        ; key
    mov r8, r15         ; size
    call r12            ; Call the search function
    
    ; Save result
    mov rdi, rax        ; Save search result
    
    ; Serialize and measure end time
    rdtsc               ; Read Time-Stamp Counter again
    shl rdx, 32
    or rdx, rax         ; rdx = end timestamp (64-bit)
    
    ; Calculate cycles elapsed
    sub rdx, rsi        ; cycles = end - start
    mov rax, rdx        ; Return cycles in rax
    mov rdx, rdi        ; Return search result in rdx
    
    mov rsp, rbp
    pop rbp
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbx
    ret