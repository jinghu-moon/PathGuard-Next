#include "pathguard/namespace_projection.h"

#include <array>
#include <cstdint>
#include <vector>

namespace pathguard::namespace_projection {
namespace {

constexpr std::array<std::uint32_t, 64> kSha256Round{
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b,
    0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
    0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7,
    0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152,
    0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
    0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
    0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
    0x2748774c,
    0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa,
    0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

std::uint32_t RotateRight(std::uint32_t value, unsigned bits) {
    return (value >> bits) | (value << (32U - bits));
}

std::array<std::uint8_t, 32> Sha256(std::string_view input) {
    std::vector<std::uint8_t> bytes(input.begin(), input.end());
    const std::uint64_t bit_count =
        static_cast<std::uint64_t>(bytes.size()) * 8;
    bytes.push_back(0x80);
    while (bytes.size() % 64 != 56) bytes.push_back(0);
    for (int shift = 56; shift >= 0; shift -= 8) {
        bytes.push_back(static_cast<std::uint8_t>(bit_count >> shift));
    }
    std::array<std::uint32_t, 8> hash{
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    for (std::size_t offset = 0; offset < bytes.size(); offset += 64) {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t index = 0; index < 16; ++index) {
            const std::size_t at = offset + index * 4;
            words[index] = static_cast<std::uint32_t>(bytes[at]) << 24
                | static_cast<std::uint32_t>(bytes[at + 1]) << 16
                | static_cast<std::uint32_t>(bytes[at + 2]) << 8
                | bytes[at + 3];
        }
        for (std::size_t index = 16; index < words.size(); ++index) {
            const std::uint32_t s0 = RotateRight(words[index - 15], 7)
                ^ RotateRight(words[index - 15], 18)
                ^ (words[index - 15] >> 3);
            const std::uint32_t s1 = RotateRight(words[index - 2], 17)
                ^ RotateRight(words[index - 2], 19)
                ^ (words[index - 2] >> 10);
            words[index] = words[index - 16] + s0 + words[index - 7] + s1;
        }
        std::uint32_t a = hash[0], b = hash[1], c = hash[2], d = hash[3];
        std::uint32_t e = hash[4], f = hash[5], g = hash[6], h = hash[7];
        for (std::size_t index = 0; index < words.size(); ++index) {
            const std::uint32_t sigma1 = RotateRight(e, 6)
                ^ RotateRight(e, 11) ^ RotateRight(e, 25);
            const std::uint32_t choose = (e & f) ^ (~e & g);
            const std::uint32_t temp1 = h + sigma1 + choose
                + kSha256Round[index] + words[index];
            const std::uint32_t sigma0 = RotateRight(a, 2)
                ^ RotateRight(a, 13) ^ RotateRight(a, 22);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = sigma0 + majority;
            h = g; g = f; f = e; e = d + temp1;
            d = c; c = b; b = a; a = temp1 + temp2;
        }
        hash[0] += a; hash[1] += b; hash[2] += c; hash[3] += d;
        hash[4] += e; hash[5] += f; hash[6] += g; hash[7] += h;
    }
    std::array<std::uint8_t, 32> output{};
    for (std::size_t index = 0; index < hash.size(); ++index) {
        output[index * 4] = static_cast<std::uint8_t>(hash[index] >> 24);
        output[index * 4 + 1] = static_cast<std::uint8_t>(hash[index] >> 16);
        output[index * 4 + 2] = static_cast<std::uint8_t>(hash[index] >> 8);
        output[index * 4 + 3] = static_cast<std::uint8_t>(hash[index]);
    }
    return output;
}

std::string Base32First128(const std::array<std::uint8_t, 32>& digest) {
    constexpr char kAlphabet[] = "abcdefghijklmnopqrstuvwxyz234567";
    std::string output;
    output.reserve(kNamespaceIdSize);
    std::uint32_t accumulator = 0;
    unsigned bits = 0;
    for (std::size_t index = 0; index < 16; ++index) {
        accumulator = (accumulator << 8) | digest[index];
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            output.push_back(kAlphabet[(accumulator >> bits) & 31U]);
        }
    }
    if (bits != 0) {
        output.push_back(kAlphabet[(accumulator << (5 - bits)) & 31U]);
    }
    return output;
}

bool RelativeTail(std::string_view path, std::string_view root,
                  std::string_view* tail) noexcept {
    if (tail == nullptr || path.size() < root.size()
        || path.compare(0, root.size(), root) != 0) {
        return false;
    }
    if (path.size() == root.size()) {
        *tail = {};
        return true;
    }
    if (path[root.size()] != '/') return false;
    *tail = path.substr(root.size() + 1);
    return !tail->empty();
}

}  // namespace

std::string ComputeNamespaceIdV1(std::string_view canonical_identity) {
    return Base32First128(Sha256(canonical_identity));
}

bool ValidNamespaceIdV1(std::string_view namespace_id) noexcept {
    if (namespace_id.size() != kNamespaceIdSize) return false;
    for (const char value : namespace_id) {
        if (!((value >= 'a' && value <= 'z')
              || (value >= '2' && value <= '7'))) {
            return false;
        }
    }
    return true;
}

std::string BuildNamespaceTargetV1(
        std::string_view target_root, std::string_view namespace_id) {
    if (target_root.empty() || !ValidNamespaceIdV1(namespace_id)) return {};
    std::string output(target_root);
    output.append("/").append(kLayoutPrefix).append(namespace_id);
    return output;
}

bool NamespaceTargetMatchesV1(
        std::string_view namespace_target_root,
        std::string_view namespace_id) noexcept {
    if (!ValidNamespaceIdV1(namespace_id)) return false;
    std::string suffix("/");
    suffix.append(kLayoutPrefix).append(namespace_id);
    return namespace_target_root.size() > suffix.size()
        && namespace_target_root.ends_with(suffix);
}

bool SameRelativeTail(
        std::string_view visible_path, std::string_view visible_root,
        std::string_view backing_path, std::string_view backing_root) noexcept {
    std::string_view visible_tail;
    std::string_view backing_tail;
    return RelativeTail(visible_path, visible_root, &visible_tail)
        && RelativeTail(backing_path, backing_root, &backing_tail)
        && visible_tail == backing_tail;
}

}  // namespace pathguard::namespace_projection
