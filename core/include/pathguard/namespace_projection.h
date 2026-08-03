#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace pathguard::namespace_projection {

inline constexpr std::string_view kLayoutPrefix = "_pg/v1/ns_";
inline constexpr std::size_t kNamespaceIdSize = 26;

std::string ComputeNamespaceIdV1(std::string_view canonical_identity);

bool ValidNamespaceIdV1(std::string_view namespace_id) noexcept;

std::string BuildNamespaceTargetV1(
    std::string_view target_root, std::string_view namespace_id);

bool NamespaceTargetMatchesV1(
    std::string_view namespace_target_root,
    std::string_view namespace_id) noexcept;

bool SameRelativeTail(
    std::string_view visible_path, std::string_view visible_root,
    std::string_view backing_path, std::string_view backing_root) noexcept;

}  // namespace pathguard::namespace_projection
