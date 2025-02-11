/* Copyright (c) 2023 Intel Corporation

Copyright 2017 The TensorFlow Authors. All Rights Reserved.

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

#ifndef ITEX_CORE_COMPILER_XLA_SERVICE_GPU_INSTRUCTION_FUSION_H_
#define ITEX_CORE_COMPILER_XLA_SERVICE_GPU_INSTRUCTION_FUSION_H_

#include "absl/container/flat_hash_map.h"
#include "itex/core/compiler/xla/service/fusion_node_indexing_evaluation.h"
#include "itex/core/compiler/xla/service/hlo_instruction.h"
#include "itex/core/compiler/xla/service/instruction_fusion.h"

namespace itex_xla {
namespace gpu {

class GpuInstructionFusion : public InstructionFusion {
 public:
  explicit GpuInstructionFusion(bool may_duplicate)
      : InstructionFusion(GpuInstructionFusion::IsExpensive, may_duplicate) {}

  static bool IsExpensive(const HloInstruction& instruction);

  StatusOr<bool> Run(HloModule* module) override {
    fusion_node_evaluations_.clear();
    return InstructionFusion::Run(module);
  }

 protected:
  FusionDecision ShouldFuse(HloInstruction* consumer,
                            int64_t operand_index) override;

  HloInstruction::FusionKind ChooseKind(
      const HloInstruction* producer, const HloInstruction* consumer) override;

 private:
  // This method is called by ShouldFuse() to do all the computationally
  // inexpensive checks whether we should fuse the operand into 'consumer'.
  FusionDecision ShouldFuseInexpensiveChecks(HloInstruction* consumer,
                                             int64_t operand_index);

  HloInstruction* FuseInstruction(HloInstruction* fusion_instruction,
                                  HloInstruction* producer) override;

  // Keep track of the number of times each instruction inside a fusion node is
  // indexed with different index vectors.
  absl::flat_hash_map<const HloInstruction*, FusionNodeIndexingEvaluation>
      fusion_node_evaluations_;
};

}  // namespace gpu
}  // namespace itex_xla

#endif  // ITEX_CORE_COMPILER_XLA_SERVICE_GPU_INSTRUCTION_FUSION_H_
