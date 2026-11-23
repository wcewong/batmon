#pragma once
// Public keys allowed to sign releases (see update_signature.h). Written by
// scripts/new-release-key.ps1; the matching private key stays on the maintainer's PC.
// More than one key lets a new key be added before an old one is retired.
// Empty means no key yet: official builds then refuse to compile, and updates cannot be installed.
namespace updates {
inline constexpr const char* ReleaseKeys[] = {
    "RUNTMSAAAABHVmMa+jOhQf2lyG+uIwvN9qe/NV1cjLaPUS4q24cZQYGYwwSZneuwZwXglzei+4JLyaXAzpUf48BywGB4SAA5",
};
}
