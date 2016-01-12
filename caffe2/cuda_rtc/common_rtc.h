#ifndef CAFFE2_CUDA_RTC_COMMON_RTC_H_
#define CAFFE2_CUDA_RTC_COMMON_RTC_H_

#include <cuda.h>
#include <nvrtc.h>

#define NVRTC_CHECK(condition)                                                 \
  do {                                                                         \
    nvrtcResult result = condition;                                            \
    if (result != NVRTC_SUCCESS) {                                             \
      CAFFE_LOG_FATAL << "Error at: " << __FILE__ << ":" << __LINE__ << ": "   \
                      << nvrtcGetErrorString(result);                          \
    }                                                                          \
  } while(0)

namespace caffe2 {

template <typename Derived>
class CudaRTCFunction {
 public:
  CudaRTCFunction() : module_loaded_(false) {}
  ~CudaRTCFunction() {
    if (module_loaded_) {
      CUDA_DRIVERAPI_CHECK(cuModuleUnload(module_));
    }
  }

  // TODO: this function is nontrivial and since CudaRTCFunction uses CRTP, it
  // may potentially increase the binary size. In that case, move common parts
  // into a separate function.
  template <typename... Args>
  void Compile(Args... args) {
    string src = static_cast<Derived*>(this)->GetSource(args...);
    string name = static_cast<Derived*>(this)->KernelName(args...);
    CAFFE_VLOG(1) << "function name: " << name;
    CAFFE_VLOG(1) << "function src:\n" << src;
    // Actually do the compiling.
    nvrtcProgram prog;
    NVRTC_CHECK(nvrtcCreateProgram(
        &prog, src.c_str(), nullptr, 0, nullptr, nullptr));
    // Compile the program.
    // TODO(Yangqing): how to find the current gpu architecture instead of hard
    // coding it?
    //const char *nvrtc_opts[] = {"--gpu-architecture=compute_30",
    //                            "--fmad=false"};
    nvrtcResult compile_result = nvrtcCompileProgram(
        prog, 0, nullptr);
    if (compile_result != NVRTC_SUCCESS) {
      size_t log_size;
      NVRTC_CHECK(nvrtcGetProgramLogSize(prog, &log_size));
      char nvrtc_log[log_size];
      NVRTC_CHECK(nvrtcGetProgramLog(prog, nvrtc_log));
      CAFFE_LOG_FATAL << "Compilation failure for nvrtc("
                      << nvrtcGetErrorString(compile_result)
                      << "): \n" << nvrtc_log;
    }
    size_t ptx_size;
    NVRTC_CHECK(nvrtcGetPTXSize(prog, &ptx_size));
    char nvrtc_ptx[ptx_size];
    NVRTC_CHECK(nvrtcGetPTX(prog, nvrtc_ptx));
    NVRTC_CHECK(nvrtcDestroyProgram(&prog));
    // After compilation, load the module.
    if (module_loaded_) {
      CUDA_DRIVERAPI_CHECK(cuModuleUnload(module_));
    }
    CUDA_DRIVERAPI_CHECK(cuModuleLoadDataEx(&module_, nvrtc_ptx, 0, 0, 0));
    module_loaded_ = true;
    CUDA_DRIVERAPI_CHECK(cuModuleGetFunction(&kernel_, module_, name.c_str()));
  }

  template <typename... Args>
  void Launch(unsigned int gx, unsigned int gy, unsigned int gz,
              unsigned int bx, unsigned int by, unsigned int bz,
              unsigned int shared_mem, cudaStream_t stream,
              Args... args) {
    CAFFE_CHECK(module_loaded_)
        << "Cannot call Launch before a module is loaded.";
    void * args_voidp[] = {&args...};
    CUDA_DRIVERAPI_CHECK(cuLaunchKernel(
        kernel_, gx, gy, gz, bx, by, bz, shared_mem, stream,
        args_voidp, 0));
  }

 private:
  bool module_loaded_;
  CUmodule module_;
  CUfunction kernel_;
};

// TODO: this is in no way unique and is just a hack right now.
inline string GetUniqueName() {
  static constexpr int len = 20;
  static const char alpha[] =
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

  std::stringstream ss;
  ss << "_cuda_kernel_";
  for (int i = 0; i < len; ++i) {
    ss << alpha[rand() % (sizeof(alpha) - 1)];
  }
  return ss.str();
}

}  // namepsace caffe2

#endif  // CAFFE2_CUDA_RTC_COMMON_RTC_H_