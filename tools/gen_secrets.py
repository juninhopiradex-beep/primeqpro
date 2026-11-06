#!/usr/bin/env python3
"""
PrimEQ Pro by Piradex — gera as senhas e o ficheiro de segredos do build (mesmo motor dos outros plugins Piradex).

Cria (e as senhas em claro NUNCA devem ir para o GitHub):
  Source/Shared/Secrets.h          usado pelo compilador: so salt + hash PBKDF2-HMAC-SHA256 (60000 iteracoes)
  PRIVADO_senhas_PRIMEQPRO.txt     as senhas em claro: master e beta (esta no .gitignore)
  secrets.b64                      conteudo para o secret PRIMEQPRO_SECRETS_B64 do GitHub Actions (opcional)

A senha MASTER e unica e partilhada por todos os plugins Piradex: usa --master "<a tua master>" para manter a mesma.

Uso:   python3 tools/gen_secrets.py --master "MinhaMaster"
       python3 tools/gen_secrets.py --master "MinhaMaster" --beta-until 2026-12-31
       python3 tools/gen_secrets.py --out /tmp/x          # escreve noutra pasta (testes)
"""
import argparse, base64, datetime, hashlib, os, secrets as sec

ALPHA = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"
ITER = 60000   # tem de ser igual a pq::lic::kPbkdf2Iterations (Source/Shared/Licensing.h)


def group(prefix, n_groups):
    return prefix + "".join("-" + "".join(sec.choice(ALPHA) for _ in range(4)) for _ in range(n_groups))


def pbkdf2(pw: str, salt: bytes) -> bytes:
    return hashlib.pbkdf2_hmac("sha256", pw.strip().encode(), salt, ITER, 32)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--master"); ap.add_argument("--beta")
    ap.add_argument("--beta-until", default="2026-12-31")
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
    a = ap.parse_args()
    root = os.path.abspath(a.out)
    until = datetime.date.fromisoformat(a.beta_until)

    master = a.master or group("LXM", 4)
    beta = a.beta or group("LXB", 3)
    s_m, s_b = sec.token_bytes(16), sec.token_bytes(16)
    h_m, h_b = pbkdf2(master, s_m), pbkdf2(beta, s_b)

    header = f'''// GERADO por tools/gen_secrets.py. So salt + hash PBKDF2 (nunca a senha em claro).
#pragma once
#include "Licensing.h"

namespace pq {{ namespace secrets
{{
inline constexpr pq::lic::PwEntry kPasswords[] =
{{
    {{ "master", pq::lic::Role::Master, "{s_m.hex()}", "{h_m.hex()}", 0, 0, 0 }},
    {{ "beta-01", pq::lic::Role::Beta,   "{s_b.hex()}", "{h_b.hex()}", {until.year}, {until.month}, {until.day} }},
}};
inline constexpr int kPasswordCount = (int) (sizeof (kPasswords) / sizeof (kPasswords[0]));
}} }} // namespace pq::secrets
'''
    os.makedirs(os.path.join(root, "Source", "Shared"), exist_ok=True)
    with open(os.path.join(root, "Source", "Shared", "Secrets.h"), "w") as f:
        f.write(header)
    with open(os.path.join(root, "secrets.b64"), "w") as f:
        f.write(base64.b64encode(header.encode()).decode())
    priv = f'''PrimEQ Pro by Piradex — SENHAS PRIVADAS  (guarda num gestor de senhas; NUNCA no GitHub)
=====================================================================================

Senha MASTER (acesso total, nao expira):
    {master}

Senha BETA (abre o plugin ate ao fim de {until.strftime("%d/%m/%Y")}):
    {beta}

Para trocar uma senha ou a data: corre de novo  python3 tools/gen_secrets.py  e faz commit do novo Source/Shared/Secrets.h.
'''
    with open(os.path.join(root, "PRIVADO_senhas_PRIMEQPRO.txt"), "w") as f:
        f.write(priv)
    print("Gerado em", root, ": Source/Shared/Secrets.h, secrets.b64, PRIVADO_senhas_PRIMEQPRO.txt")


if __name__ == "__main__":
    main()
