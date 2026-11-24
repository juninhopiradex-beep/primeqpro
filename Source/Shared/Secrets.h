// GERADO por tools/gen_secrets.py. So salt + hash PBKDF2 (nunca a senha em claro).
#pragma once
#include "Licensing.h"

namespace pq { namespace secrets
{
inline constexpr pq::lic::PwEntry kPasswords[] =
{
    { "master", pq::lic::Role::Master, "242c6eca2c7d216d206c341e92dbd4e3", "3d758c5bb1514bb703c7d9c5a68425b2396c350e13a29e46594d940182d50a66", 0, 0, 0 },
    { "beta-01", pq::lic::Role::Beta,   "6e6283ed0d52313328bbd9c812d5ad26", "27d4eec6d7defb105f9f320ae30c8e08e58f111b7c84c0b488623006cb620b94", 2027, 6, 30 },
};
inline constexpr int kPasswordCount = (int) (sizeof (kPasswords) / sizeof (kPasswords[0]));
} } // namespace pq::secrets
