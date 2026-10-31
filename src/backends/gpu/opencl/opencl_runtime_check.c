/* Opens the OpenCL ICD loader at run time and picks the first GPU device
 * (opencl_api.h). */
#include <string.h>
#include "opencl_api.h"

#if defined(_WIN32)
#include <windows.h>
static void* su_dl_open(void) { return (void*)LoadLibraryA("OpenCL.dll"); }
static void* su_dl_sym(void* lib, const char* name) { return (void*)GetProcAddress((HMODULE)lib, name); }
#else
#include <dlfcn.h>
static void* su_dl_open(void) {
    void* lib = dlopen("libOpenCL.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!lib) lib = dlopen("libOpenCL.so", RTLD_NOW | RTLD_LOCAL);
#if defined(__APPLE__)
    if (!lib) lib = dlopen("/System/Library/Frameworks/OpenCL.framework/OpenCL", RTLD_NOW | RTLD_LOCAL);
#endif
    return lib;
}
static void* su_dl_sym(void* lib, const char* name) { return dlsym(lib, name); }
#endif

typedef cl_int (SU_CL_CALL* su_GetPlatformIDs)(cl_uint, cl_platform_id*, cl_uint*);
typedef cl_int (SU_CL_CALL* su_GetDeviceIDs)(cl_platform_id, cl_device_type, cl_uint, cl_device_id*, cl_uint*);
typedef cl_int (SU_CL_CALL* su_GetDeviceInfo)(cl_device_id, cl_uint, size_t, void*, size_t*);
typedef cl_context (SU_CL_CALL* su_CreateContext)(const cl_context_properties*, cl_uint, const cl_device_id*,
                                                  void (SU_CL_CALL*)(const char*, const void*, size_t, void*), void*,
                                                  cl_int*);
typedef cl_command_queue (SU_CL_CALL* su_CreateCommandQueue)(cl_context, cl_device_id, cl_command_queue_properties,
                                                             cl_int*);

static su_ocl g_ocl;
static int g_state = -1; /* -1: not tried, 0: unavailable, 1: ready */

#define SU_LOAD(field, name)                                                  \
    do {                                                                      \
        *(void**)&g_ocl.field = su_dl_sym(lib, name);                         \
        if (!g_ocl.field) return 0;                                           \
    } while (0)

static int su_ocl_open(void) {
    void* lib = su_dl_open();
    if (!lib) return 0;
    su_GetPlatformIDs get_platforms = (su_GetPlatformIDs)su_dl_sym(lib, "clGetPlatformIDs");
    su_GetDeviceIDs get_devices = (su_GetDeviceIDs)su_dl_sym(lib, "clGetDeviceIDs");
    su_GetDeviceInfo get_info = (su_GetDeviceInfo)su_dl_sym(lib, "clGetDeviceInfo");
    su_CreateContext create_context = (su_CreateContext)su_dl_sym(lib, "clCreateContext");
    su_CreateCommandQueue create_queue = (su_CreateCommandQueue)su_dl_sym(lib, "clCreateCommandQueue");
    if (!get_platforms || !get_devices || !get_info || !create_context || !create_queue) return 0;
    SU_LOAD(CreateProgramWithSource, "clCreateProgramWithSource");
    SU_LOAD(BuildProgram, "clBuildProgram");
    SU_LOAD(GetProgramBuildInfo, "clGetProgramBuildInfo");
    SU_LOAD(ReleaseProgram, "clReleaseProgram");
    SU_LOAD(CreateKernel, "clCreateKernel");
    SU_LOAD(ReleaseKernel, "clReleaseKernel");
    SU_LOAD(CreateBuffer, "clCreateBuffer");
    SU_LOAD(ReleaseMemObject, "clReleaseMemObject");
    SU_LOAD(SetKernelArg, "clSetKernelArg");
    SU_LOAD(EnqueueNDRangeKernel, "clEnqueueNDRangeKernel");
    SU_LOAD(EnqueueWriteBuffer, "clEnqueueWriteBuffer");
    SU_LOAD(EnqueueReadBuffer, "clEnqueueReadBuffer");
    SU_LOAD(Finish, "clFinish");

    cl_platform_id platforms[8];
    cl_uint count = 0;
    if (get_platforms(8, platforms, &count) != SU_CL_SUCCESS || count == 0) return 0;
    if (count > 8) count = 8;
    for (cl_uint p = 0; p < count; p++) {
        cl_device_id device;
        cl_uint devices = 0;
        if (get_devices(platforms[p], SU_CL_DEVICE_TYPE_GPU, 1, &device, &devices) != SU_CL_SUCCESS || !devices) {
            continue;
        }
        cl_int err = 0;
        cl_context context = create_context(NULL, 1, &device, NULL, NULL, &err);
        if (err != SU_CL_SUCCESS || !context) continue;
        cl_command_queue queue = create_queue(context, device, 0, &err);
        if (err != SU_CL_SUCCESS || !queue) continue;
        g_ocl.context = context;
        g_ocl.device = device;
        g_ocl.queue = queue;
        size_t wg = 0;
        get_info(device, SU_CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof wg, &wg, NULL);
        g_ocl.max_work_group = wg;
        get_info(device, SU_CL_DEVICE_GLOBAL_MEM_SIZE, sizeof g_ocl.global_mem, &g_ocl.global_mem, NULL);
        get_info(device, SU_CL_DEVICE_NAME, sizeof g_ocl.name - 1, g_ocl.name, NULL);
        return 1;
    }
    return 0;
}

const su_ocl* speedup_internal_ocl(void) {
    if (g_state < 0) g_state = su_ocl_open();
    return g_state == 1 ? &g_ocl : NULL;
}

void speedup_internal_ocl_disable(void) { g_state = 0; }

int speedup_opencl_available(void) { return speedup_internal_ocl() != NULL; }

const char* speedup_opencl_device_name(void) {
    const su_ocl* ocl = speedup_internal_ocl();
    return ocl ? ocl->name : "";
}
