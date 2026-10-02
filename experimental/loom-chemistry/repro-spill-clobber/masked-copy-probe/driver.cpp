#include <hip/hip_runtime.h>
#include <cstdio>
#include <cstdlib>
#include <vector>

#define HIP(call) do { auto e = (call); if (e != hipSuccess) { \
  std::fprintf(stderr, "%s: %s\n", #call, hipGetErrorString(e)); return 2; \
} } while (0)

int main(int argc, char **argv) {
  if (argc != 3) return 2;
  int mode = std::atoi(argv[2]);
  std::vector<int> input(64 * 3), output(64 * 3, -999);
  for (int i = 0; i < 64 * 3; ++i) input[i] = i % 3 + 1;
  int *di, *dout, *dm;
  HIP(hipMalloc(&di, input.size() * sizeof(int)));
  HIP(hipMalloc(&dout, output.size() * sizeof(int)));
  HIP(hipMalloc(&dm, sizeof(int)));
  HIP(hipMemcpy(di, input.data(), input.size() * sizeof(int), hipMemcpyHostToDevice));
  HIP(hipMemcpy(dout, output.data(), output.size() * sizeof(int), hipMemcpyHostToDevice));
  HIP(hipMemcpy(dm, &mode, sizeof(int), hipMemcpyHostToDevice));
  hipModule_t module;
  hipFunction_t kernel;
  HIP(hipModuleLoad(&module, argv[1]));
  HIP(hipModuleGetFunction(&kernel, module, "rotate"));
  void *args[] = {&di, &dout, &dm};
  HIP(hipModuleLaunchKernel(kernel, 1, 1, 1, 64, 1, 1, 0, nullptr, args, nullptr));
  HIP(hipDeviceSynchronize());
  HIP(hipMemcpy(output.data(), dout, output.size() * sizeof(int), hipMemcpyDeviceToHost));
  int bad = 0;
  for (int lane = 0; lane < 64; ++lane) {
    bool lane_bad = false;
    for (int j = 0; j < 3; ++j) {
      bad += output[3 * lane + j] != 0;
      lane_bad |= output[3 * lane + j] != 0;
    }
    if (lane_bad) std::printf("lane=%d residuals=%d,%d,%d\n", lane,
        output[3 * lane], output[3 * lane + 1], output[3 * lane + 2]);
  }
  std::printf("mode=%d mismatches=%d\n", mode, bad);
  HIP(hipModuleUnload(module));
  HIP(hipFree(di)); HIP(hipFree(dout)); HIP(hipFree(dm));
  return bad ? 1 : 0;
}
