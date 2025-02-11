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

#ifndef ITEX_CORE_COMPILER_XLA_SERVICE_GPU_LLVM_GPU_BACKEND_DUMP_IR_PASS_H_
#define ITEX_CORE_COMPILER_XLA_SERVICE_GPU_LLVM_GPU_BACKEND_DUMP_IR_PASS_H_

#include <string>
#include <vector>

#include "itex/core/compiler/xla/types.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Pass.h"

namespace itex_xla {
namespace gpu {

// Pass manager which optionally dumps the IR to a sequence of files before each
// pass.
class IrDumpingPassManager : public llvm::legacy::PassManager {
 public:
  IrDumpingPassManager(const std::string& input_filename,
                       const std::string& output_dir, bool dump_ir)
      : llvm::legacy::PassManager(),
        input_filename_(input_filename),
        output_dir_(output_dir),
        dump_ir_(dump_ir) {}
  void add(llvm::Pass* P) { passes_.push_back(P); }
  void run(llvm::Module& module);  // NOLINT(runtime/references)

 private:
  std::string input_filename_;
  std::string output_dir_;
  bool dump_ir_;
  std::vector<llvm::Pass*> passes_;
};

}  // namespace gpu
}  // namespace itex_xla

#endif  // ITEX_CORE_COMPILER_XLA_SERVICE_GPU_LLVM_GPU_BACKEND_DUMP_IR_PASS_H_
