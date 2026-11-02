// =============================================================================
//  PRIMEQ pro by Piradex — acesso: login (Master / Beta com validade)
//  Mesmo motor dos outros plugins Piradex (PBKDF2-HMAC-SHA256, 60000 iteracoes, master unica + betas com validade,
//  periodo aberto ate 31/12/2026, guarda contra recuo do relogio, sessao lembrada por maquina).
//  A verificacao e lenta de proposito: nunca chamar na thread de audio nem na de mensagens.
// =============================================================================
#pragma once
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <atomic>
#include <memory>
#include "../Shared/Licensing.h"

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
    void checkExpiry();                                              // periodicamente (thread de mensagens)
    int  today() const;                                              // dia atual, com guarda contra recuo do relógio

private:
    AccessManager();
    void setRole (int index);
    juce::String tokenFor (int index) const;
    mutable juce::CriticalSection lock;
    std::unique_ptr<juce::PropertiesFile> props;
    std::atomic<bool> unlocked { false };
    std::atomic<int> roleValue { 0 };
    int currentIndex = -1;
    mutable int lastSeenDay = 0;
};
