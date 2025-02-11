/* Copyright (c) 2023 Intel Corporation

Copyright 2021 The TensorFlow Authors. All Rights Reserved.

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

#ifndef ITEX_CORE_COMPILER_XLA_SERVICE_SPMD_STATEFUL_RNG_SPMD_PARTITIONER_H_
#define ITEX_CORE_COMPILER_XLA_SERVICE_SPMD_STATEFUL_RNG_SPMD_PARTITIONER_H_

#include <memory>
#include <utility>

#include "itex/core/compiler/xla/service/hlo_computation.h"
#include "itex/core/compiler/xla/service/hlo_instruction.h"
#include "itex/core/compiler/xla/service/hlo_module.h"
#include "itex/core/compiler/xla/service/hlo_pass_interface.h"
#include "itex/core/compiler/xla/service/spmd/spmd_partitioner.h"

namespace itex_xla {
namespace spmd {

class StatefulRngSpmdPartitioningVisitor
    : public spmd::SpmdPartitioningVisitor {
 public:
  StatefulRngSpmdPartitioningVisitor(
      HloComputation* computation, int64_t num_partitions, int64_t num_replicas,
      const spmd::SPMDCollectiveOpsCreator& collective_ops_creator,
      int64_t* next_channel_id, spmd::SpmdLogger* logger,
      spmd::SpmdPartitionerOptions options, spmd::SpmdPartitioner* partitioner)
      : spmd::SpmdPartitioningVisitor(computation, num_partitions, num_replicas,
                                      collective_ops_creator, next_channel_id,
                                      logger, std::move(options), partitioner) {
  }
  Status HandleRngGetAndUpdateState(HloInstruction* hlo) override;
};

class StatefulRngSpmdPartitioner : public spmd::SpmdPartitioner {
 public:
  StatefulRngSpmdPartitioner(int64_t num_partitions, int64_t num_replicas)
      : spmd::SpmdPartitioner(num_partitions, num_replicas,
                              GetSpmdPartitionerOptions()) {}

 protected:
  std::unique_ptr<spmd::SpmdPartitioningVisitor> CreateVisitor(
      HloComputation* computation, int64_t num_partitions, int64_t num_replicas,
      const spmd::SPMDCollectiveOpsCreator& collective_ops_creator,
      int64_t* next_channel_id, spmd::SpmdLogger* logger,
      spmd::SpmdPartitionerOptions options) override;

  Status PreprocessSharding(HloModule* module) override;
  bool CanSideEffectingHaveReplicatedSharding(
      const HloInstruction* hlo) override;

 private:
  static spmd::SpmdPartitionerOptions GetSpmdPartitionerOptions() {
    spmd::SpmdPartitionerOptions options;
    options.allow_module_signature_change = true;
    return options;
  }
};

}  // namespace spmd
}  // namespace itex_xla

#endif  // ITEX_CORE_COMPILER_XLA_SERVICE_SPMD_STATEFUL_RNG_SPMD_PARTITIONER_H_
