# Licenciamento e PCC
Motor offline: PBKDF2-HMAC-SHA256, 60000 iterações, hashes master e beta em `Source/Shared/Secrets.h` (apenas hashes). Gerar novas com `tools/gen_secrets.py` (cria `PRIVADO_senhas_PRIMEQPRO.txt`, ignorado pelo git).
PCC: nenhum plugin actual chama o PCC, por isso `Source/Plugin/PccClient.h` é um stub desactivado (`PQ_PCC_ENABLED=0`) com o contrato proposto em comentários. Falta a API real. **Recomenda-se repositório privado** (os hashes estão no código).
