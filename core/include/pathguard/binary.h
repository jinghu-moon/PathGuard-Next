#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "pathguard/policy.h"

namespace pathguard {

bool EncodePolicy(const PolicyDocument& document, std::vector<std::uint8_t>* output,
                  ParseError* error);
bool DecodePolicy(const std::vector<std::uint8_t>& input, PolicyDocument* document,
                  std::uint64_t* content_generation, ParseError* error);
std::uint64_t ComputeContentGeneration(const PolicyDocument& document);
std::uint64_t ComputePlanGeneration(const AppPolicy& policy, FailureMode failure_mode);

}  // namespace pathguard
