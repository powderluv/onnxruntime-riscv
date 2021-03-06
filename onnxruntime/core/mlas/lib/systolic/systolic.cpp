// See LICENSE for license details.

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdexcept>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#include "systolic_include.h"
#pragma GCC diagnostic pop

/**
 * Perform a matmul and subsequent quantization.
 * Switch between TILED_OS and TILED_CPU
 * 
 * Elements are accumulated internally into acc_t (int32) and subsequently rounded/saturated to elem_t (int8).
 * The given divisor *must* be a power of 2.
 */

#define ROTATED_MATMUL_TYPE(x)

/**
 * Interally CPU is last in tiled_matmul_type_t but we want to expose CPU as accelerator mode 0
 * So just rotate everything by one
 */
inline int positive_mod(int i, int n) {
  return (i % n + n) % n;
}
inline tiled_matmul_type_t get_accelerator_mode(int mode) {
  return static_cast<tiled_matmul_type_t>(positive_mod(mode - 1, (int)CPU + 1));
}

/* Internal -- no need to touch */

void tiled_matmul_auto(size_t dim_I, size_t dim_J, size_t dim_K,
                       size_t strideA,
                       size_t strideB,
                       size_t strideD,
                       size_t strideC,
                       const elem_t* A, const elem_t* B,
                       const acc_t* D, elem_t* C,
                       int act, acc_scale_t scale, size_t relu6_shift, bool repeating_bias,
                       enum tiled_matmul_type_t tiled_matmul_type) {
  tiled_matmul_auto(dim_I, dim_J, dim_K,
                    A, B, D, C,
                    strideA, strideB, strideD, strideC,
                    MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
                    act, scale, relu6_shift, repeating_bias,
                    tiled_matmul_type);
}

void tiled_matmul_auto(size_t dim_I, size_t dim_J, size_t dim_K,
                       const elem_t* A, const elem_t* B,
                       const acc_t* D, elem_t* C,
                       int act, acc_scale_t scale, size_t relu6_shift, bool repeating_bias,
                       enum tiled_matmul_type_t tiled_matmul_type) {
  tiled_matmul_auto(dim_I, dim_J, dim_K, dim_K, dim_J, dim_J, dim_J, A, B, D, C, act, scale, relu6_shift, repeating_bias, tiled_matmul_type);
}

/* End internal */

void SystolicMultiply(char accelerator_mode, bool relu, int dimI, int dimJ, int dimK,
                      const elem_t* in1, const elem_t* in2, elem_t* out, acc_scale_t real_multiplier, const acc_t* bias) {
  printf("Called into systolic matmul!\n");
  printf("Using accelerated matmul with dimensions (%d, %d, %d)\n", dimI, dimJ, dimK);
  tiled_matmul_auto(dimI, dimJ, dimK, in1, in2, bias, out, /*activation= */ relu,
                    real_multiplier, /*relu6_shift= */ 0, /* repeating_bias= */ 0, get_accelerator_mode(accelerator_mode));
}

void SystolicMultiply(char accelerator_mode, bool relu,
                      int dimI, int dimJ, int dimK,
                      const elem_t* in1, int strideIn1,
                      const elem_t* in2, int strideIn2,
                      elem_t* out, int strideOut,
                      acc_scale_t real_multiplier,
                      const acc_t* bias, int strideBias, bool repeating_bias) {
  printf("Called into systolic matmul!\n");
  printf("Using accelerated matmul with dimensions (%d, %d, %d)\n", dimI, dimJ, dimK);
  tiled_matmul_auto(dimI, dimJ, dimK,
                    strideIn1, strideIn2, strideBias, strideOut,
                    in1, in2, bias, out, /*activation= */ relu,
                    real_multiplier, /*relu6_shift= */ 0, /* repeating_bias= */ repeating_bias, get_accelerator_mode(accelerator_mode));
}

void SystolicAdd(char accelerator_mode __attribute__((unused)), bool relu, const int8_t* A, float A_scale, const int8_t* B,
                 float B_scale,
                 int8_t* C, float C_scale, int dim) {
  printf("Called into systolic add\n");
  // To most efficiently use systolic, instead of using 1xdim, use 16xResizedDim.
  // Systolic can load multiple blocks in a given row

  // Note that it's more accurate to use A_scale/C_scale and B_scale/C_scale as the A, B scales (with C_scale = 1)
  // Since that way we don't blow up rounding error by dividing by C_scale

  // Equivalent to:
  // for (int i = 0; i < dim; i++) {
  //   int32_t tmp1 = (int) MVIN_SCALE(*A, A_scale/C_scale);
  //   int32_t tmp2 = (int) MVIN_SCALE(*B, B_scale/C_scale);
  //   *C = scale_and_sat(tmp1 + tmp2, relu ? RELU : 0, 1, 0);

  //   A++;
  //   B++;
  //   C++;
  // }

  int resizedDim = dim - dim % DIM;
  tiled_resadd_auto(DIM, resizedDim / DIM, A_scale / C_scale, B_scale / C_scale,
                    /*C_scale= */ 1, A, B, C, relu, get_accelerator_mode(accelerator_mode));
  if (dim % DIM > 0) {
    printf("Some extra leftover\n");
    tiled_resadd_auto(1, dim % DIM, A_scale / C_scale, B_scale / C_scale,
                      /*C_scale= */ 1, A + resizedDim, B + resizedDim, C + resizedDim, relu, get_accelerator_mode(accelerator_mode));
  }
}

void SystolicConv(char accelerator_mode, int batch_size, int in_dim, int in_channels,
                  int out_channels, int out_dim,
                  int stride, int padding, int kernel_dim,
                  const elem_t* input,
                  const elem_t* weights,
                  const acc_t* bias,
                  elem_t* output,
                  bool relu,
                  float output_scale) {
  printf("Called into systolic conv\n");
  // printf("Debugging info\n");
  // printf("Batch size, in_w/h, in_channel %d %d %d\n", batch_size, in_dim, in_channels);
  // printf("Out_channels, out_w/h %d %d\n", out_channels, out_dim);
  // printf("Stride, padding %d %d\n", stride, padding);
  // printf("kernel_w/h %d\n", kernel_dim);
  // if (bias) {
  //   printf("Bias values: %d\n", bias[0]);
  // }
  // printf("Relu? %d\n", relu);


  tiled_conv_auto(batch_size, in_dim, in_channels, out_channels, out_dim,
                  stride, padding, kernel_dim, input, weights, bias, output,
                  relu, output_scale, /*relu6_shift= */ 0,
                  /*pool_size = */ 0, /*pool_stride = */ 0, /*pool_padding = */ 0,
                  get_accelerator_mode(accelerator_mode));

  // printf("Output\n");
  // for (int i = 0; i < out_dim * out_dim * out_channels * batch_size; i++) {
  //   printf("%d ", output[i]);
  // }
  // printf("\n");
}