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

#include "itex/core/compiler/xla/client/lib/testing.h"

#include "absl/strings/str_cat.h"
#include "itex/core/compiler/xla/client/xla_builder.h"
#include "itex/core/compiler/xla/execution_options_util.h"
#include "itex/core/compiler/xla/literal.h"
#include "itex/core/compiler/xla/shape_util.h"
#include "itex/core/compiler/xla/statusor.h"
#include "itex/core/compiler/xla/tests/test_utils.h"
#include "itex/core/compiler/xla/types.h"
#include "itex/core/compiler/xla/util.h"
#include "itex/core/utils/protobuf.h"

namespace itex_xla {
namespace {

// Calculates the number of bytes required to store the data within the
// specified shape. In case of a (nested) tuple shape this is the total byte
// size of all sub-shapes within the tuple.
int64_t DataSizeOfShape(const Shape& shape) {
  if (shape.IsArray()) {
    return ShapeUtil::ByteSizeOf(shape);
  }

  int64_t total_size = 0;
  for (const Shape& s : shape.tuple_shapes()) {
    total_size += DataSizeOfShape(s);
  }
  return total_size;
}

// Creates a XlaOp for an op what generates fake data with the given shape.
XlaOp BuildFakeDataOpOnDevice(const Shape& shape, XlaBuilder* builder) {
  if (shape.IsArray()) {
    return Broadcast(
        ConstantLiteral(builder, LiteralUtil::One(shape.element_type())),
        shape.dimensions());
  }
  std::vector<XlaOp> parts;
  const auto& tuple_shapes = shape.tuple_shapes();
  parts.reserve(tuple_shapes.size());
  for (const Shape& s : tuple_shapes) {
    parts.push_back(BuildFakeDataOpOnDevice(s, builder));
  }
  return Tuple(builder, parts);
}

std::unique_ptr<GlobalData> MakeFakeDataViaDeviceOrDie(
    const Shape& shape, Client* client, DebugOptions* debug_opts) {
  XlaBuilder b(absl::StrCat("make_fake_", ShapeUtil::HumanString(shape)));
  BuildFakeDataOpOnDevice(shape, &b);
  XlaComputation computation = b.Build().ConsumeValueOrDie();

  auto execution_options = CreateDefaultExecutionOptions();
  *execution_options.mutable_shape_with_output_layout() = shape.ToProto();
  if (debug_opts) {
    *execution_options.mutable_debug_options() = *debug_opts;
  }
  return client->Execute(computation, /*arguments=*/{}, &execution_options)
      .ConsumeValueOrDie();
}

}  // namespace

std::unique_ptr<GlobalData> MakeFakeDataOrDie(
    const Shape& shape, Client* client, DebugOptions* debug_opts /*=nullptr*/) {
  if (DataSizeOfShape(shape) < (1LL << 20)) {
    StatusOr<Literal> literal_status = MakeFakeLiteral(shape);
    if (!literal_status.ok()) {
      // If we got an Unimplemented error, fall back to making the fake data via
      // an on-device computation.
      ITEX_CHECK_EQ(literal_status.status().code(), itex::error::UNIMPLEMENTED);
      return MakeFakeDataViaDeviceOrDie(shape, client, debug_opts);
    }
    return client->TransferToServer(literal_status.ValueOrDie()).ValueOrDie();
  }

  // If the data is large, generate it on-device.
  return MakeFakeDataViaDeviceOrDie(shape, client, debug_opts);
}

std::vector<std::unique_ptr<GlobalData>> MakeFakeArgumentsOrDie(
    const XlaComputation& computation, Client* client,
    DebugOptions* debug_opts /*=nullptr*/) {
  ITEX_CHECK(computation.proto().has_host_program_shape())
      << "Computation should have program shape.";
  auto program_shape = computation.proto().host_program_shape();

  std::vector<std::unique_ptr<GlobalData>> results;
  for (const ShapeProto& shape : program_shape.parameters()) {
    results.push_back(MakeFakeDataOrDie(Shape(shape), client, debug_opts));
  }
  return results;
}

}  // namespace itex_xla
