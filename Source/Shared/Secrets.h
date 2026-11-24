// GERADO por tools/gen_secrets.py. So salt + hash PBKDF2 (nunca a senha em claro).
#pragma once
#include "Licensing.h"

namespace pq { namespace secrets
{
inline constexpr pq::lic::PwEntry kPasswords[] =
{
    { "master", pq::lic::Role::Master,
      "f66c3ec8febf16a6c6eb5a8a327e8db5",
      "b834a72caccf1d9126f2e29f4d94edc2f21ee4a1476019474f9576fd99a71b9a", 0, 0, 0 },

    { "beta-01", pq::lic::Role::Beta,
      "7ac02900317309e4cb998e2eb2adeb1e",
      "67caab7ccabcf21d0a33381a6952cf23aa2ce9530f5bf855eb9e5892625d207a", 2026, 12, 31 },
};
inline constexpr int kPasswordCount = (int) (sizeof (kPasswords) / sizeof (kPasswords[0]));
} } // namespace pq::secrets
