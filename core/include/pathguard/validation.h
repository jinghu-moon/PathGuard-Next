#pragma once

#include "pathguard/policy.h"

namespace pathguard {

bool ValidatePolicy(AppPolicy* policy, ParseError* error);

}  // namespace pathguard
