#!/usr/bin/env python3
"""
Gera o par de chaves Ed25519 do PCC.
  - PRIVADA (base64 do seed de 32 bytes)  -> segredo da Edge Function:  supabase secrets set PCC_SIGNING_KEY=<valor>
  - PUBLICA (64 hex)                      -> vai para o plugin:         cmake -DPQ_PCC_PUBKEY=<valor>   (ou editar CMakeLists.txt)
A privada fica em PRIVADO_pcc_chave.txt (no .gitignore). NUNCA a coloques no GitHub nem no plugin.
Para rodar a chave: executa de novo, atualiza o segredo no Supabase e recompila os plugins.
"""
import base64, os
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives import serialization as s

k = Ed25519PrivateKey.generate()
seed = k.private_bytes(s.Encoding.Raw, s.PrivateFormat.Raw, s.NoEncryption())
pub = k.public_key().public_bytes(s.Encoding.Raw, s.PublicFormat.Raw)
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "PRIVADO_pcc_chave.txt")
with open(out, "w") as f:
    f.write("PCC_SIGNING_KEY (privada, base64 do seed) = %s\nPQ_PCC_PUBKEY (publica, hex) = %s\n" % (base64.b64encode(seed).decode(), pub.hex()))
print("Chave PUBLICA (hex):", pub.hex())
print("A privada foi gravada em", os.path.abspath(out))
