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

#include "itex/core/compiler/xla/text_literal_reader.h"

#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "itex/core/compiler/xla/literal.h"
#include "itex/core/compiler/xla/service/hlo_parser.h"
#include "itex/core/compiler/xla/shape_util.h"
#include "itex/core/compiler/xla/status_macros.h"
#include "itex/core/compiler/xla/types.h"
#include "itex/core/compiler/xla/util.h"
#include "itex/core/utils/buffered_inputstream.h"
#include "itex/core/utils/protobuf.h"
#include "itex/core/utils/random_inputstream.h"
#include "protos/xla.pb.h"

namespace itex_xla {

StatusOr<Literal> TextLiteralReader::ReadPath(absl::string_view path) {
  ITEX_CHECK(!absl::EndsWith(path, ".gz"))
      << "TextLiteralReader no longer supports reading .gz files";
  std::unique_ptr<itex::RandomAccessFile> file;
  Status s =
      itex::Env::Default()->NewRandomAccessFile(std::string(path), &file);
  if (!s.ok()) {
    return s;
  }

  TextLiteralReader reader(file.release());
  return reader.ReadAllLines();
}

TextLiteralReader::TextLiteralReader(itex::RandomAccessFile* file)
    : file_(file) {}

StatusOr<Literal> TextLiteralReader::ReadAllLines() {
  itex::io::RandomAccessInputStream stream(file_.get());
  itex::io::BufferedInputStream buf(&stream, 65536);
  std::string shape_string;
  Status s = buf.ReadLine(&shape_string);
  if (!s.ok()) {
    return s;
  }

  absl::StripAsciiWhitespace(&shape_string);
  TF_ASSIGN_OR_RETURN(Shape shape, ParseShape(shape_string));
  if (shape.element_type() != F32) {
    return Unimplemented(
        "unsupported element type for text literal reading: %s",
        ShapeUtil::HumanString(shape));
  }

  Literal result(shape);
  const float fill = std::numeric_limits<float>::quiet_NaN();
  result.PopulateWithValue<float>(fill);
  std::vector<absl::string_view> pieces;
  std::vector<absl::string_view> coordinates;
  std::vector<int64_t> coordinate_values;
  std::string line;
  while (buf.ReadLine(&line).ok()) {
    pieces = absl::StrSplit(line, ':');
    absl::string_view coordinates_string =
        absl::StripAsciiWhitespace(pieces[0]);
    absl::string_view value_string = absl::StripAsciiWhitespace(pieces[1]);
    if (!absl::ConsumePrefix(&coordinates_string, "(")) {
      return InvalidArgument(
          "expected '(' at the beginning of coordinates: \"%s\"", line);
    }
    if (!absl::ConsumeSuffix(&coordinates_string, ")")) {
      return InvalidArgument("expected ')' at the end of coordinates: \"%s\"",
                             line);
    }
    float value;
    if (!absl::SimpleAtof(value_string, &value)) {
      return InvalidArgument("could not parse value as float: \"%s\"",
                             value_string);
    }
    coordinates = absl::StrSplit(coordinates_string, ',');
    coordinate_values.clear();
    for (absl::string_view piece : coordinates) {
      int64_t coordinate_value;
      if (!absl::SimpleAtoi(piece, &coordinate_value)) {
        return InvalidArgument(
            "could not parse coordinate member as int64_t: \"%s\"",
            std::string(piece));
      }
      coordinate_values.push_back(coordinate_value);
    }
    if (coordinate_values.size() != shape.dimensions_size()) {
      return InvalidArgument(
          "line did not have expected number of coordinates; want %d got %u: "
          "\"%s\"",
          shape.dimensions_size(), coordinate_values.size(), line);
    }
    result.Set<float>(coordinate_values, value);
  }
  return std::move(result);
}

}  // namespace itex_xla
