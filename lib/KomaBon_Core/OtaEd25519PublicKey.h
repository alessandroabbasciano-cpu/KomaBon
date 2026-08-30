#pragma once
// Book32 v1.11.0 — public half of the OTA signing keypair.
//
// The matching private key never leaves the OTA_ED25519_PRIVATE_KEY GitHub
// Actions secret; release.yml uses it to sign the SHA-256 digest of each
// published asset (see docs/plans/2026-08-23-ota-ed25519-signing-design.md).
// This key is meant to be long-lived — rotating it means every device already
// in the field needs a USB reflash to trust the new key, since the whole
// point is that OTA cannot silently swap it out from under them.

#include <cstdint>

inline constexpr uint8_t BOOK32_OTA_ED25519_PUBLIC_KEY[32] = {
    0xfe, 0xf6, 0xdd, 0x62, 0x58, 0x09, 0x08, 0xd3, 0x9f, 0x16, 0xb2, 0x36, 0xc1, 0xbb, 0x2f, 0xef,
    0x34, 0x0b, 0xc1, 0x2f, 0x16, 0x47, 0x5e, 0x74, 0xee, 0xa1, 0xb8, 0x04, 0x23, 0xac, 0x00, 0x3c,
};
