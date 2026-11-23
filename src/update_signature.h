#pragma once
// Release signatures. Every release carries SHA256SUMS.txt (hashes of the setup and the portable zip,
// written by CI) and SHA256SUMS.txt.sig, made by the maintainer on their own PC with
// scripts/sign-release.ps1. The private key never goes to GitHub, so a hacked account or workflow
// cannot publish an update this app would install: the setup's hash has to be in a list signed by
// one of the public keys in update_public_key.h.
//
// Signature: ECDSA P-256 over SHA-256 of the file's bytes, 64 bytes r || s, base64 in the .sig file.
#include <cstddef>
#include <string>

namespace updates {
constexpr std::size_t MaxSumsBytes = 64 * 1024;
constexpr std::size_t MaxSignatureBytes = 1024;

// keyBase64: a BCRYPT_ECCPUBLIC_BLOB (ECS1, 32-byte X and Y), base64.
bool VerifySignature(const std::string& data, const std::string& signatureText, const std::string& keyBase64);
// Tries every key in update_public_key.h. False when none is configured.
bool VerifyReleaseSignature(const std::string& data, const std::string& signatureText);
bool ReleaseKeyConfigured();
// The lowercase hex SHA-256 listed for fileName in a sha256sum-style list ("<hex>  <name>" or
// "<hex> *<name>" per line). False unless exactly one well-formed line names it.
bool SignedHash(const std::string& sums, const std::string& fileName, std::string& hex);
bool Base64Decode(const std::string& text, std::string& out);
}
