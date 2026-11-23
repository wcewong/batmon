#pragma once
#include <cstdint>
#include <string>

namespace updates {
constexpr std::uint64_t MaxDownloadBytes = 100ULL * 1024 * 1024;
constexpr const char* Repository = "krisgfx9/Wireless-Device-Battery-Monitor";

struct Version {
    unsigned major = 0, minor = 0, patch = 0;
};
bool ParseVersion(const std::string& text, Version& out);
bool Newer(const Version& candidate, const Version& current);
std::string VersionText(const Version& version);

struct ReleaseInfo {
    Version version;
    std::string tag, page, download, sha256;
    std::uint64_t size = 0;
    // SHA256SUMS.txt and its maintainer signature (see update_signature.h); both are required.
    std::string sumsUrl, signatureUrl;
    std::uint64_t sumsSize = 0, signatureSize = 0;
    bool CanInstall() const {
        return !download.empty() && sha256.size() == 64 && size > 0 && size <= MaxDownloadBytes &&
               !sumsUrl.empty() && sumsSize > 0 && sumsSize <= 64 * 1024 &&
               !signatureUrl.empty() && signatureSize > 0 && signatureSize <= 1024;
    }
};
enum class ReleaseResult { Invalid, NoUpdate, Available };
ReleaseResult ParseRelease(const std::string& json, const Version& current, ReleaseInfo& out);
bool ValidDigest(const std::string& digest, std::string& hex);
bool AllowedDownloadUrl(const std::wstring& url);
struct CheckSchedule {
    std::uint64_t lastAttempt = 0, nextAutomatic = 0, retryAfter = 0;
    bool Due(std::uint64_t now) const;
    bool ManualAllowed(std::uint64_t now) const;
    void Finished(std::uint64_t now, bool success, std::uint64_t serverRetry = 0);
};
}
