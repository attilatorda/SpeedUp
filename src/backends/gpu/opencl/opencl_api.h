#pragma once
/* The part of the OpenCL 1.2 API that SpeedUp uses, loaded at run time.
 *
 * SpeedUp does not link against OpenCL and needs no OpenCL SDK to build:
 * the ICD loader (OpenCL.dll, libOpenCL.so.1, the OpenCL framework) is
 * opened when a GPU path is first requested, and the types below are the
 * ABI of the Khronos headers. Without a loader or a GPU device,
 * speedup_internal_ocl() returns NULL and every caller stays on the CPU. */
#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#define SU_CL_CALL __stdcall
#else
#define SU_CL_CALL
#endif

typedef int32_t cl_int;
typedef uint32_t cl_uint;
typedef uint64_t cl_ulong;
typedef cl_uint cl_bool;
typedef cl_ulong cl_bitfield;
typedef cl_bitfield cl_device_type;
typedef cl_bitfield cl_mem_flags;
typedef cl_bitfield cl_command_queue_properties;
typedef intptr_t cl_context_properties;
typedef struct _cl_platform_id* cl_platform_id;
typedef struct _cl_device_id* cl_device_id;
typedef struct _cl_context* cl_context;
typedef struct _cl_command_queue* cl_command_queue;
typedef struct _cl_mem* cl_mem;
typedef struct _cl_program* cl_program;
typedef struct _cl_kernel* cl_kernel;
typedef struct _cl_event* cl_event;

#define SU_CL_SUCCESS 0
#define SU_CL_TRUE 1
#define SU_CL_DEVICE_TYPE_GPU ((cl_bitfield)1 << 2)
#define SU_CL_MEM_READ_WRITE ((cl_bitfield)1 << 0)
#define SU_CL_DEVICE_MAX_WORK_GROUP_SIZE 0x1004
#define SU_CL_DEVICE_GLOBAL_MEM_SIZE 0x101F
#define SU_CL_DEVICE_NAME 0x102B
#define SU_CL_PROGRAM_BUILD_LOG 0x1183

typedef struct su_ocl {
    cl_context context;
    cl_device_id device;
    cl_command_queue queue;
    size_t max_work_group;
    cl_ulong global_mem;
    char name[128];
    cl_program (SU_CL_CALL* CreateProgramWithSource)(cl_context, cl_uint, const char**, const size_t*, cl_int*);
    cl_int (SU_CL_CALL* BuildProgram)(cl_program, cl_uint, const cl_device_id*, const char*,
                                      void (SU_CL_CALL*)(cl_program, void*), void*);
    cl_int (SU_CL_CALL* GetProgramBuildInfo)(cl_program, cl_device_id, cl_uint, size_t, void*, size_t*);
    cl_int (SU_CL_CALL* ReleaseProgram)(cl_program);
    cl_kernel (SU_CL_CALL* CreateKernel)(cl_program, const char*, cl_int*);
    cl_int (SU_CL_CALL* ReleaseKernel)(cl_kernel);
    cl_mem (SU_CL_CALL* CreateBuffer)(cl_context, cl_mem_flags, size_t, void*, cl_int*);
    cl_int (SU_CL_CALL* ReleaseMemObject)(cl_mem);
    cl_int (SU_CL_CALL* SetKernelArg)(cl_kernel, cl_uint, size_t, const void*);
    cl_int (SU_CL_CALL* EnqueueNDRangeKernel)(cl_command_queue, cl_kernel, cl_uint, const size_t*, const size_t*,
                                              const size_t*, cl_uint, const cl_event*, cl_event*);
    cl_int (SU_CL_CALL* EnqueueWriteBuffer)(cl_command_queue, cl_mem, cl_bool, size_t, size_t, const void*, cl_uint,
                                            const cl_event*, cl_event*);
    cl_int (SU_CL_CALL* EnqueueReadBuffer)(cl_command_queue, cl_mem, cl_bool, size_t, size_t, void*, cl_uint,
                                           const cl_event*, cl_event*);
    cl_int (SU_CL_CALL* Finish)(cl_command_queue);
} su_ocl;

/* The first GPU device's context and queue, opened on first use; NULL when
 * there is no OpenCL loader or no GPU. Not thread-safe on first use. */
const su_ocl* speedup_internal_ocl(void);

/* After a failed call: stop using OpenCL in this process. */
void speedup_internal_ocl_disable(void);
