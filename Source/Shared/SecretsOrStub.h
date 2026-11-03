// Usa Source/Shared/Secrets.h (gerado por tools/gen_secrets.py; NAO vai para o GitHub — no CI vem do segredo
// PRIMEQPRO_SECRETS_B64). Sem ele, compila com uma tabela vazia: nenhuma senha offline serve e o acesso
// depende do periodo aberto ate 31/12/2026 ou do PCC.
#pragma once
#if __has_include("Secrets.h")
 #include "Secrets.h"
#else
 #include "Licensing.h"
 namespace pq { namespace secrets
 {
 inline constexpr bool kStub = true;
 inline constexpr pq::lic::PwEntry kPasswords[] = { { "none", pq::lic::Role::Beta, "00000000000000000000000000000000", "0000000000000000000000000000000000000000000000000000000000000000", 2000, 1, 1 } };
 inline constexpr int kPasswordCount = 1;
 }}
#endif
