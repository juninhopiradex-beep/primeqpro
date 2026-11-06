// =============================================================================
//  PRIMEQ PRO by Piradex — acesso: login (Master / Beta com validade)
//  Mesmo motor dos outros plugins Piradex (PBKDF2-HMAC-SHA256, 60000 iteracoes, master unica + betas com validade,
//  periodo aberto ate 31/12/2026, guarda contra recuo do relogio, sessao lembrada por maquina).
//  A verificacao e lenta de proposito: nunca chamar na thread de audio nem na de mensagens.
// =============================================================================
#pragma once
#include <algorithm>
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <atomic>
#include <memory>
#include "../Shared/Licensing.h"
#include "PccClient.h"

class AccessManager
{
public:
    static AccessManager& get();

    static bool inTrial()          { return pq::lic::todayDays2026() <= pq::lic::daysSince2026 (2026, 12, 31); }   // aberto ate 31/12/2026
    bool isUnlocked() const        { return unlocked.load() || inTrial(); }      // seguro na thread de audio (atomico)
    pq::lic::Role role() const     { auto r = (pq::lic::Role) roleValue.load(); return (r == pq::lic::Role::None && inTrial()) ? pq::lic::Role::Beta : r; }
    juce::String roleText() const;                                   // "MESTRE" / "BETA · até 31/12/2026"
    int  betaDaysLeft() const;                                       // -1 se não for beta

    struct Match { int index = -1; bool expired = false; };
    static Match matchPassword (const juce::String& password);       // lento: chamar numa thread de fundo
    enum class LoginResult { Ok, Wrong, Expired };
    LoginResult applyLogin (Match m, bool remember);
    void logout();

    // ---- PCC (validacao online assinada; ver Shared/PccProtocol.h). Funcoes BLOQUEANTES: so em threads de fundo.
    enum class PccOutcome { Ok, Invalid, Unreachable, BadReply, Disabled };
    PccOutcome loginPcc (const juce::String& password, bool remember, juce::String& message);
    void revalidatePcc();                                            // 1x por dia, em segundo plano; revogacao desliga o acesso
    bool pccConfigured() const { return pq::pccnet::enabled(); }
    void checkExpiry();                                              // periodicamente (thread de mensagens)
    bool onboardingSeen() const { return props->getBoolValue ("onboardDone", false); }
    // idioma da interface: 0 = PT, 1 = EN. Sem valor guardado, segue o idioma do sistema (pt* -> PT, resto -> EN).
    int language() const { const auto v = props->getIntValue ("lang", -1); if (v == 0 || v == 1) return v; return juce::SystemStats::getUserLanguage().startsWithIgnoreCase ("pt") ? 0 : 1; }
    void setLanguage (int l) { props->setValue ("lang", l == 1 ? 1 : 0); props->saveIfNeeded(); }
    int skin() const { return std::clamp (props->getIntValue ("skin", 0), 0, 2); }
    void setSkin (int i) { props->setValue ("skin", std::clamp (i, 0, 2)); props->saveIfNeeded(); }
    void setOnboardingSeen() { props->setValue ("onboardDone", true); props->saveIfNeeded(); }
    int  today() const;                                              // dia atual, com guarda contra recuo do relógio

private:
    AccessManager();
    void setRole (int index);
    void setPcc (const pq::pcc::Reply& r, const std::string& keyHash);
    void restorePcc();
    void storePcc (const pq::pcc::Reply& r, const std::string& keyHash);
    juce::String tokenFor (int index) const;
    mutable juce::CriticalSection lock;
    std::unique_ptr<juce::PropertiesFile> props;
    std::atomic<bool> unlocked { false };
    std::atomic<int> roleValue { 0 };
    int currentIndex = -1;
    bool pccActive = false; pq::pcc::Reply pccReply; std::string pccHash; int pccExpDays = -1;
    mutable int lastSeenDay = 0;
};
