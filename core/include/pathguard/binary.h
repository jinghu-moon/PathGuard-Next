#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "pathguard/policy.h"

namespace pathguard {

bool EncodePolicy(const PolicyDocument& document, std::uint64_t generation,
                 std::vector<std::uint8_t>* output, ParseError* error);
bool DecodePolicy(const std::vector<std::uint8_t>& input, PolicyDocument* document,
                 std::uint64_t* generation, ParseError* error);

}  // namespace pathguard
