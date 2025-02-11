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

#ifndef ITEX_CORE_COMPILER_XLA_SERVICE_PLATFORM_UTIL_H_
#define ITEX_CORE_COMPILER_XLA_SERVICE_PLATFORM_UTIL_H_

#include <set>
#include <string>
#include <vector>

#include "itex/core/compiler/xla/statusor.h"
#include "itex/core/compiler/xla/stream_executor/stream_executor.h"
#include "itex/core/compiler/xla/types.h"

namespace itex_xla {

// Utilities for querying platforms and devices used by XLA.
class PlatformUtil {
 public:
  // Returns the platforms present on the system and supported by XLA.
  //
  // Note that, even if a platform is present with zero devices, if we *do* have
  // compilation support for it, it will be returned in this sequence.
  static StatusOr<std::vector<se::Platform*>> GetSupportedPlatforms();

  // Convenience function which returns the default supported platform for
  // tests. If exactly one supported platform is present, then this platform is
  // the default platform. If exactly two platforms are present and one of them
  // is the interpreter platform, then the other platform is the default
  // platform. Otherwise returns an error.
  static StatusOr<se::Platform*> GetDefaultPlatform();

  // Returns the platform according to the given name. Returns error if there is
  // no such platform.
  static StatusOr<se::Platform*> GetPlatform(const std::string& platform_name);

  // Returns a vector of StreamExecutors for the given platform.
  // If populated, only the devices in allowed_devices will have
  // their StreamExecutors initialized, otherwise all StreamExecutors will be
  // initialized and returned.
  //
  // If the platform has no visible devices, a not-found error is returned.
  static StatusOr<std::vector<se::StreamExecutor*>> GetStreamExecutors(
      se::Platform* platform,
      const std::optional<std::set<int>>& allowed_devices = std::nullopt);

 private:
  PlatformUtil(const PlatformUtil&) = delete;
  PlatformUtil& operator=(const PlatformUtil&) = delete;
};

}  // namespace itex_xla

#endif  // ITEX_CORE_COMPILER_XLA_SERVICE_PLATFORM_UTIL_H_
