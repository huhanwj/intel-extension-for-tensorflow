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

#include "itex/core/compiler/mlir/xla/utils/literal_util.h"

#include <vector>

#include "itex/core/compiler/mlir/xla/utils/shape_util.h"
#include "itex/core/compiler/mlir/xla/utils/type_util.h"
#include "itex/core/compiler/xla/literal.h"

namespace itex {

Status HostTensorToBorrowingLiteral(const Tensor& host_tensor,
                                    itex_xla::BorrowingLiteral* literal) {
  itex_xla::Shape xla_shape;
  TF_RETURN_IF_ERROR(TensorShapeToXLAShape(host_tensor.dtype(),
                                           host_tensor.shape(), &xla_shape));
  return HostTensorToBorrowingLiteral(xla_shape, host_tensor, literal);
}

Status HostTensorToBorrowingLiteral(const itex_xla::Shape& xla_shape,
                                    const Tensor& host_tensor,
                                    itex_xla::BorrowingLiteral* literal) {
  const auto& tshape = host_tensor.shape();
  TF_RET_CHECK(tshape.IsFullyDefined() &&
               tshape.dims() == xla_shape.dimensions_size() &&
               tshape.dim_sizes() == xla_shape.dimensions())
      << "Provided itex_xla::Shape must have the same dims as the Tensor "
         "shape.";
  *literal = itex_xla::BorrowingLiteral(
      static_cast<const char*>(DMAHelper::base(&host_tensor)), xla_shape);
  return Status::OK();
}

StatusOr<itex_xla::Literal> HostTensorToLiteral(const Tensor& host_tensor) {
  itex_xla::BorrowingLiteral literal;
  TF_RETURN_IF_ERROR(HostTensorToBorrowingLiteral(host_tensor, &literal));
  return literal.Clone();
}

Status HostTensorToMutableBorrowingLiteral(
    Tensor* host_tensor, itex_xla::MutableBorrowingLiteral* literal) {
  itex_xla::Shape xla_shape;
  TF_RETURN_IF_ERROR(TensorShapeToXLAShape(host_tensor->dtype(),
                                           host_tensor->shape(), &xla_shape));
  return HostTensorToMutableBorrowingLiteral(xla_shape, host_tensor, literal);
}

Status HostTensorToMutableBorrowingLiteral(
    const itex_xla::Shape& xla_shape, Tensor* host_tensor,
    itex_xla::MutableBorrowingLiteral* literal) {
  *literal = itex_xla::MutableBorrowingLiteral(
      static_cast<const char*>(DMAHelper::base(host_tensor)), xla_shape);

  return Status::OK();
}

Status HostTensorsToBorrowingLiteralTuple(absl::Span<const Tensor> host_tensors,
                                          itex_xla::BorrowingLiteral* literal) {
  std::vector<const char*> buf_ptrs;
  buf_ptrs.reserve(host_tensors.size());
  std::vector<itex_xla::Shape> tensor_shapes(host_tensors.size());

  for (int i = 0, end = host_tensors.size(); i < end; i++) {
    // Validate runtime shapes and fail if it doesn't match the contract.
    const Tensor* tensor = &host_tensors[i];
    buf_ptrs.emplace_back(static_cast<const char*>(DMAHelper::base(tensor)));
    TF_RETURN_IF_ERROR(TensorShapeToXLAShape(tensor->dtype(), tensor->shape(),
                                             &tensor_shapes[i]));
  }

  *literal = itex_xla::BorrowingLiteral(
      buf_ptrs, itex_xla::ShapeUtil::MakeTupleShape(tensor_shapes));

  return Status::OK();
}

Status CopyLiteralToHostTensor(const itex_xla::LiteralSlice& literal,
                               Tensor* host_tensor) {
  TF_RET_CHECK(literal.shape().IsArray() &&
               itex_xla::ShapeUtil::ElementsIn(literal.shape()) ==
                   host_tensor->NumElements());
  itex_xla::PrimitiveType primitive_type;
  TF_RETURN_IF_ERROR(
      DataTypeToPrimitiveType(host_tensor->dtype(), &primitive_type));
  if (literal.shape().element_type() != primitive_type) {
    return errors::InvalidArgument(
        "Cannot convert literal of type ",
        itex_xla::PrimitiveType_Name(literal.shape().element_type()),
        " to tensor of type ", DataTypeString(host_tensor->dtype()));
  }
  size_t total_bytes = host_tensor->TotalBytes();
  if (total_bytes > 0) {
    const void* src_ptr = literal.untyped_data();
    void* dst_ptr = DMAHelper::base(host_tensor);
    memcpy(dst_ptr, src_ptr, total_bytes);
  }
  return Status::OK();
}

Status LiteralToHostTensor(const itex_xla::LiteralSlice& literal,
                           DataType target_type, Tensor* host_tensor) {
  TensorShape shape;
  TF_RETURN_IF_ERROR(XLAShapeToTensorShape(literal.shape(), &shape));
  *host_tensor = Tensor(target_type, shape);
  return CopyLiteralToHostTensor(literal, host_tensor);
}

}  // namespace itex
