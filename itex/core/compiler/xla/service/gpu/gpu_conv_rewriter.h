/* Copyright (c) 2023 Intel Corporation

Copyright 2018 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#ifndef ITEX_CORE_COMPILER_XLA_SERVICE_GPU_GPU_CONV_REWRITER_H_
#define ITEX_CORE_COMPILER_XLA_SERVICE_GPU_GPU_CONV_REWRITER_H_

#include <tuple>

#include "itex/core/compiler/xla/service/hlo_module.h"
#include "itex/core/compiler/xla/service/hlo_pass_interface.h"

namespace itex_xla {
namespace gpu {

// Rewrites plain convolutions, backwards-filter convolutions, and
// backwards-input convolutions into CustomCall HLOs that call into
// Cudnn/Miopen.
// For integer convolution, it requires the following pattern:
// conv<InputT=int32_t, ResultT=int32_t>(
//   convert<int32_t>(int8_x), convert<int32_t>(int8_y))
// We transform it to:
// custom_call<int32_t>(int8_x, int8_y, target=cudnnForwardConvolution)
// Note that this pattern is necessary but not sufficient to map convolutions
// to CuDNN. More patterns will be matched in cudnn_fused_conv_rewriter.

class GpuConvRewriter : public HloModulePass {
 public:
  absl::string_view name() const override { return "gpu-conv-rewriter"; }

  StatusOr<bool> Run(HloModule* module) override;
};

namespace conv_matchers {

bool CanImplementAsGpuForwardConv(HloInstruction* conv);
std::tuple<bool, Window, ConvolutionDimensionNumbers, HloInstruction*>
MatchBackwardFilter(HloInstruction* conv);
std::tuple<bool, Window, ConvolutionDimensionNumbers, HloInstruction*>
MatchBackwardInput(HloInstruction* conv);

}  // namespace conv_matchers

}  // namespace gpu
}  // namespace itex_xla

#endif  // ITEX_CORE_COMPILER_XLA_SERVICE_GPU_GPU_CONV_REWRITER_H_
