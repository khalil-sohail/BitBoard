#pragma once

#include "tuning/generated_tuning_values.hpp"

#include <ostream>
#include <string_view>

namespace Tuning {

struct CompiledProfileIdentity {
    const std::string_view profileId;
    const std::string_view canonicalHash;
    const int schemaVersion;
    const std::string_view modelVersion;
};

[[nodiscard]] constexpr CompiledProfileIdentity compiledProfileIdentity() {
    return {
        .profileId = Generated::PROFILE_ID,
        .canonicalHash = Generated::PROFILE_HASH,
        .schemaVersion = Generated::PROFILE_SCHEMA_VERSION,
        .modelVersion = Generated::MODEL_VERSION,
    };
}

inline void reportCompiledProfileIdentity(std::ostream& output) {
    constexpr CompiledProfileIdentity identity = compiledProfileIdentity();
    output << "info string tuning profile=" << identity.profileId
           << " hash=" << identity.canonicalHash
           << " schema=" << identity.schemaVersion
           << " model=" << identity.modelVersion;
}

inline constexpr std::string_view kSha256Prefix = "sha256:";
inline constexpr std::size_t kSha256HexLength = 64;

static_assert(!compiledProfileIdentity().profileId.empty());
static_assert(compiledProfileIdentity().canonicalHash.starts_with(kSha256Prefix));
static_assert(compiledProfileIdentity().canonicalHash.size() ==
              kSha256Prefix.size() + kSha256HexLength);
static_assert(compiledProfileIdentity().schemaVersion > 0);
static_assert(!compiledProfileIdentity().modelVersion.empty());

} // namespace Tuning
