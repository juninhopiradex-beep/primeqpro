// =============================================================================
//  PRIMEQ PRO by Piradex — ponto de ligacao ao PCC (Plugin Control Central)
//
//  ESTADO: DESLIGADO por omissao (PQ_PCC_ENABLED = 0). Nenhum plugin Piradex chama o PCC hoje e o contrato da API do
//  PCC ainda nao foi definido, por isso NAO ha rede neste build: o acesso e 100 % offline (Access.cpp), como os outros plugins.
//
//  Quando o PCC existir, este e o unico sitio a ligar: implementar IPccValidator e devolver-lo em makeValidator().
//  Contrato PROPOSTO (a confirmar contra o PCC real, nao e uma API existente):
//     POST {PCC_URL}/functions/v1/validate
//       { "product":"PRIMEQPRO", "version":"1.0.0", "machineId":"<SystemStats::getUniqueDeviceID()>", "keyId":"<id da senha>" }
//     -> { "valid":true, "role":"master|beta", "expires":"YYYY-MM-DD", "message":"..." }
//  Regras: nunca bloquear a thread de audio nem a de mensagens; cache local com validade; se o servidor estiver
//  inacessivel, manter o ultimo estado valido (modo offline) em vez de bloquear o utilizador.
// =============================================================================
#pragma once
#include <string>

#ifndef PQ_PCC_ENABLED
 #define PQ_PCC_ENABLED 0
#endif

namespace pq
{
struct PccResult { bool reachable = false, valid = false; std::string role, expires, message; };

class IPccValidator
{
public:
    virtual ~IPccValidator() = default;
    virtual PccResult validate (const std::string& machineId, const std::string& keyId) = 0;   // sincrono: chamar numa thread de fundo
};
}
