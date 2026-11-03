# Licenciamento e PCC
**Online (PCC, Supabase):** o plugin envia `sha256(chave|PRIMEQPRO)`, `machineId` e um nonce para `POST /functions/v1/validate`. A resposta vem assinada (Ed25519); o plugin só confia se a assinatura (chave pública embebida), o nonce, a máquina e o produto baterem certo. Sem rede, usa a última resposta assinada durante 14 dias. A chave em claro nunca sai do computador. Protocolo: `Source/Shared/PccProtocol.h`. Servidor: `pcc/supabase/` (migração SQL + Edge Function).
**Offline (fallback):** PBKDF2-HMAC-SHA256, 60000 iterações. Os hashes **já não estão no repositório**: `Source/Shared/Secrets.h` é gerado por `tools/gen_secrets.py`, ignorado pelo git e injectado no CI pelo segredo `PRIMEQPRO_SECRETS_B64`. Sem ele o build compila sem senhas offline (só período aberto e PCC).
**Deploy do PCC (uma vez):**
1. `python3 tools/gen_pcc_keys.py` → guarda `PRIVADO_pcc_chave.txt` (nunca no GitHub). A chave pública (64 hex) vai para `PQ_PCC_PUBKEY` no CMakeLists.
2. Supabase → SQL Editor: correr `pcc/supabase/migrations/*.sql`.
3. `supabase secrets set PCC_SIGNING_KEY=<privada base64>` e `supabase functions deploy validate --no-verify-jwt`.
4. Criar licenças: `select * from pcc_new_primeqpro_license('Nome','email','País','venda', null);` (ou pelo painel PCC, plugin `primeqpro`).
**Atenção:** os hashes antigos continuam no histórico do git. Considere-os comprometidos: gere novos (`gen_secrets.py`), actualize o segredo `PRIMEQPRO_SECRETS_B64` e use PCC. Tornar o repositório privado é decisão do proprietário.
