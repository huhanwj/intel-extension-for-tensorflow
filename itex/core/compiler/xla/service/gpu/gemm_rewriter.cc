/* Copyright (c) 2023 Intel Corporation

Copyright 2019 The TensorFlow Authors. All Rights Reserved.

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

#include "itex/core/compiler/xla/service/gpu/gemm_rewriter.h"

#include <memory>
#include <utility>

#include "itex/core/compiler/xla/service/dfs_hlo_visitor_with_default.h"
#include "itex/core/compiler/xla/service/gpu/ir_emission_utils.h"
#include "itex/core/compiler/xla/service/gpu/mkl.h"
#include "itex/core/compiler/xla/service/hlo_casting_utils.h"
#include "itex/core/compiler/xla/service/hlo_instructions.h"
#include "itex/core/compiler/xla/service/hlo_opcode.h"
#include "itex/core/compiler/xla/service/pattern_matcher.h"
#include "itex/core/compiler/xla/status_macros.h"
#include "itex/core/compiler/xla/statusor.h"
#include "itex/core/utils/errors.h"
#include "protos/backend_configs.pb.h"
#include "protos/xla_data.pb.h"

namespace itex_xla {
namespace gpu {
namespace {

namespace m = match;

// Give this instruction a more useful name than "custom-call.42".
Status SetName(HloModule* module, HloInstruction* gemm) {
  GemmBackendConfig config;
  TF_ASSIGN_OR_RETURN(config, gemm->backend_config<GemmBackendConfig>());
  const DotDimensionNumbers& dot_dims = config.dot_dimension_numbers();
  bool is_batch_dot = !dot_dims.lhs_batch_dimensions().empty() ||
                      !dot_dims.rhs_batch_dimensions().empty();

  module->SetAndUniquifyInstrName(
      gemm, is_batch_dot ? "cublas-batch-gemm" : "cublas-gemm");
  return Status::OK();
}

// The rewriting proceeds in a bottom-up way:
//
// (kDot A B) is rewritten into a (kCustomCall:gemm A B)
//
// (kMultiply (kCustomCall:gemm A B) C) is folding C (provided it's a constant)
// into an alpha parameter of the custom call.
//
// (kAdd (kCustomCall:gemm A B) C) is rewritten into (kCustomCall:gemm A B C),
// where the "beta" parameter is set to 1 (provided it was zero before,
// and provided C has no other users).
// We then guide the buffer assignment to alias the buffer of the custom call
// and C.
class GemmRewriterVisitor : public DfsHloRewriteVisitor {
 public:
  Status HandleDot(HloInstruction* instr) override {
    if (IsMatrixMultiplication(*instr)) {
      ITEX_CHECK(!instr->IsRank2Transpose());
      HloInstruction* lhs = instr->mutable_operand(0);
      HloInstruction* rhs = instr->mutable_operand(1);
      ITEX_CHECK(!lhs->IsRank2Transpose());
      ITEX_CHECK(!rhs->IsRank2Transpose());
      const Shape& output_shape = instr->shape();
      std::unique_ptr<HloInstruction> gemm_call =
          HloInstruction::CreateCustomCall(output_shape, {lhs, rhs},
                                           kGemmCallTarget);
      GemmBackendConfig gemm_config;
      gemm_config.set_alpha_real(1.0);
      gemm_config.set_alpha_imag(0.0);
      gemm_config.set_beta(0.0);
      *gemm_config.mutable_dot_dimension_numbers() =
          instr->dot_dimension_numbers();

      TF_RETURN_IF_ERROR(gemm_call->set_backend_config(gemm_config));
      TF_RETURN_IF_ERROR(SetName(instr->GetModule(), gemm_call.get()));
      TF_RETURN_IF_ERROR(
          ReplaceWithNewInstruction(instr, std::move(gemm_call)));
    }
    return Status::OK();
  }

  Status HandleMultiply(HloInstruction* instr) override {
    HloInstruction *alpha, *existing_gemm;
    if (Match(instr,
              m::MultiplyAnyOrder(
                  m::Op(&existing_gemm).WithCustomCallTarget(kGemmCallTarget),
                  m::Broadcast(m::ConstantScalar(&alpha))))) {
      TF_ASSIGN_OR_RETURN(auto config,
                          existing_gemm->backend_config<GemmBackendConfig>());

      // Do not fuse alpha into S32 GEMM, as they only support fixed values for
      // alpha/beta.
      if (existing_gemm->shape().element_type() == S32) {
        return Status::OK();
      }

      if (config.beta() == 0.0 && existing_gemm->user_count() == 1) {
        complex128 prev_alpha = {config.alpha_real(), config.alpha_imag()};
        complex128 new_alpha =
            *alpha->literal().GetAsComplex128({}) * prev_alpha;
        config.set_alpha_real(new_alpha.real());
        config.set_alpha_imag(new_alpha.imag());
        TF_RETURN_IF_ERROR(existing_gemm->set_backend_config(config));
        TF_RETURN_IF_ERROR(ReplaceInstruction(instr, existing_gemm));
      }
    }
    return Status::OK();
  }

  Status HandleAdd(HloInstruction* instr) override {
    HloInstruction *bias, *existing_gemm;
    if (Match(instr,
              m::AddAnyOrder(
                  m::Op(&existing_gemm).WithCustomCallTarget(kGemmCallTarget),
                  m::Op(&bias)))) {
      return FuseBiasedGemm(instr, bias, existing_gemm);
    }
    return Status::OK();
  }

  Status HandleConvert(HloInstruction* instr) override {
    HloInstruction *bias, *existing_gemm;
    if (Match(
            instr,
            m::Convert(m::AddAnyOrder(
                           m::Convert(m::Op(&existing_gemm)
                                          .WithCustomCallTarget(kGemmCallTarget)
                                          .WithElementType(BF16)),
                           m::Convert(m::Op(&bias).WithElementType(BF16))))
                .WithElementType(BF16))) {
      return FuseBiasedGemm(instr, bias, existing_gemm);
    }
    return Status::OK();
  }

  Status FuseBiasedGemm(HloInstruction* instr, HloInstruction* bias,
                        HloInstruction* existing_gemm) {
    // Do not fuse bias into S32 GEMM, as for this datatype cuBLAS only
    // supports fixed values for alpha/beta.
    if (existing_gemm->shape().element_type() == S32) {
      return Status::OK();
    }
    auto config =
        existing_gemm->backend_config<GemmBackendConfig>().ValueOrDie();
    if (config.beta() == 0 && bias->user_count() == 1 &&
        existing_gemm->user_count() == 1 &&
        bias->shape() == existing_gemm->shape()) {
      config.set_beta(1.0);
      ITEX_CHECK_EQ(existing_gemm->operand_count(), 2);
      std::unique_ptr<HloInstruction> gemm_call =
          existing_gemm->CloneWithNewOperands(
              instr->shape(), {existing_gemm->mutable_operand(0),
                               existing_gemm->mutable_operand(1), bias});
      TF_RETURN_IF_ERROR(gemm_call->set_backend_config(config));

      itex_xla::Cast<HloCustomCallInstruction>(gemm_call.get())
          ->set_output_to_operand_aliasing({{{}, {2, {}}}});
      TF_RETURN_IF_ERROR(SetName(instr->GetModule(), gemm_call.get()));
      TF_RETURN_IF_ERROR(
          ReplaceWithNewInstruction(instr, std::move(gemm_call)));
    }
    return Status::OK();
  }
};

StatusOr<bool> RunOnComputation(HloComputation* computation) {
  GemmRewriterVisitor visitor;
  TF_RETURN_IF_ERROR(computation->Accept(&visitor));
  return visitor.changed();
}

}  // anonymous namespace

StatusOr<bool> GemmRewriter::Run(HloModule* module) {
  bool changed = false;
  for (HloComputation* computation : module->MakeNonfusionComputations()) {
    TF_ASSIGN_OR_RETURN(bool result, RunOnComputation(computation));
    changed |= result;
  }
  return changed;
}

}  // namespace gpu
}  // namespace itex_xla
