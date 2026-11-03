// =============================================================================
//  PRIMEQ PRO by Piradex — cliente do PCC (Plugin Control Central, Supabase)
//  Contrato e assinatura: ver Source/Shared/PccProtocol.h. Ligado quando o build define PQ_PCC_URL e PQ_PCC_PUBKEY
//  (cmake -DPQ_PCC_URL=https://xxxx.supabase.co -DPQ_PCC_PUBKEY=<64 hex>). Sem eles o PCC fica desligado.
//  NUNCA chamar nas threads de audio ou de mensagens: faz rede (bloqueante, timeout 8 s).
// =============================================================================
#pragma once
#include <juce_core/juce_core.h>
#include <string>
#include "../Shared/PccProtocol.h"

#ifndef PQ_PCC_URL
 #define PQ_PCC_URL ""
#endif
#ifndef PQ_PCC_PUBKEY
 #define PQ_PCC_PUBKEY ""
#endif

namespace pq { namespace pccnet
{
inline constexpr const char* kProduct = "PRIMEQPRO";
inline bool enabled() { return std::string (PQ_PCC_URL).size() > 8 && std::string (PQ_PCC_PUBKEY).size() == 64; }
inline std::string machineId() { return juce::SystemStats::getUniqueDeviceID().toStdString(); }

enum class Status { Ok, Unreachable, BadReply };

// pergunta ao servidor; `out` so e preenchido com respostas cuja assinatura/nonce/maquina/produto foram verificados
inline Status validateBlocking (const std::string& keyHash, pcc::Reply& out)
{
    if (! enabled()) return Status::Unreachable;
    juce::Random rnd; std::string nonce; for (int i = 0; i < 16; ++i) nonce += juce::String::toHexString ((int) rnd.nextInt (256)).paddedLeft ('0', 2).toStdString();
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    o->setProperty ("product", juce::String (kProduct)); o->setProperty ("version", juce::String (JucePlugin_VersionString));
    o->setProperty ("machineId", juce::String (machineId())); o->setProperty ("keyHash", juce::String (keyHash)); o->setProperty ("nonce", juce::String (nonce));
    const auto body = juce::JSON::toString (juce::var (o.get()), true);
    auto url = juce::URL (juce::String (PQ_PCC_URL).trimCharactersAtEnd ("/") + "/functions/v1/validate").withPOSTData (body);
    auto stream = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                                             .withExtraHeaders ("Content-Type: application/json\r\n").withConnectionTimeoutMs (8000).withHttpRequestCmd ("POST"));
    if (stream == nullptr) return Status::Unreachable;
    const auto text = stream->readEntireStreamAsString().toStdString();
    pcc::Reply r;
    if (! pcc::parseReply (text, r) || ! pcc::verifyReply (r, PQ_PCC_PUBKEY, kProduct, machineId(), &nonce)) return Status::BadReply;
    out = r; return Status::Ok;
}
}} // namespace pq::pccnet
