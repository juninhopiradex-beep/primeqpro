#include "Access.h"
#include "AppPaths.h"
#include "../Shared/SecretsOrStub.h"

using namespace pq;

AccessManager& AccessManager::get() { static AccessManager a; return a; }

namespace
{
    // marca do dia visto: HMAC com um segredo derivado do hash da senha master (só quem tem o Secrets.h gera uma marca válida)
    std::string macFor (const std::string& day)
    {
        const std::string key = std::string (secrets::kPasswords[0].hashHex) + "|primeqpro-lastseen";
        const crypto::Bytes k (key.begin(), key.end()), m (day.begin(), day.end());
        return crypto::toHex (crypto::hmacSha256 (k, m)).substr (0, 16);
    }
}

AccessManager::AccessManager()
{
    juce::PropertiesFile::Options o; o.storageFormat = juce::PropertiesFile::storeAsXML; o.millisecondsBeforeSaving = 0;
    props = std::make_unique<juce::PropertiesFile> (pqpaths::settingsFile(), o);

    const auto ls = props->getValue ("lastSeen").toStdString(), lm = props->getValue ("lastSeenMac").toStdString();
    if (! ls.empty() && lm == macFor (ls)) lastSeenDay = std::atoi (ls.c_str());

    restorePcc();
    // sessão lembrada neste computador
    const auto id = props->getValue ("loginId"), tok = props->getValue ("loginToken");
    if (id.isNotEmpty() && tok.isNotEmpty())
        for (int i = 0; i < secrets::kPasswordCount; ++i)
            if (id == secrets::kPasswords[i].id && tok == tokenFor (i) && ! lic::pwExpired (secrets::kPasswords[i], today())) { setRole (i); break; }
}

int AccessManager::today() const
{
    const juce::ScopedLock sl (lock);
    const int now = lic::todayDays2026();
    if (now > lastSeenDay)
    {
        lastSeenDay = now; const auto s = std::to_string (now);
        props->setValue ("lastSeen", juce::String (s)); props->setValue ("lastSeenMac", juce::String (macFor (s)));
        props->saveIfNeeded();
    }
    return lic::effectiveToday (now, lastSeenDay);
}

juce::String AccessManager::tokenFor (int index) const
{
    const std::string s = std::string (juce::SystemStats::getUniqueDeviceID().toRawUTF8()) + "|" + secrets::kPasswords[index].id + "|" + secrets::kPasswords[index].hashHex + "|PRIMEQPRO";
    return juce::String (crypto::sha256Hex (crypto::Bytes (s.begin(), s.end())));
}

void AccessManager::setRole (int index)
{
    currentIndex = index; roleValue.store ((int) secrets::kPasswords[index].role); unlocked.store (true);
}

AccessManager::Match AccessManager::matchPassword (const juce::String& password)
{
    Match m; bool ex = false;
    m.index = lic::matchPassword (secrets::kPasswords, secrets::kPasswordCount, password.toStdString(), AccessManager::get().today(), ex);
    m.expired = ex; return m;
}

AccessManager::LoginResult AccessManager::applyLogin (Match m, bool remember)
{
    if (m.index < 0) return m.expired ? LoginResult::Expired : LoginResult::Wrong;
    const juce::ScopedLock sl (lock);
    setRole (m.index);
    if (remember) { props->setValue ("loginId", juce::String (secrets::kPasswords[m.index].id)); props->setValue ("loginToken", tokenFor (m.index)); }
    else          { props->removeValue ("loginId"); props->removeValue ("loginToken"); }
    props->saveIfNeeded();
    return LoginResult::Ok;
}

void AccessManager::logout()
{
    const juce::ScopedLock sl (lock);
    unlocked.store (false); roleValue.store (0); currentIndex = -1; pccActive = false;
    for (auto k : { "loginId", "loginToken", "pccHash", "pccValid", "pccRole", "pccExpires", "pccNonce", "pccTs", "pccSig", "pccMsg" }) props->removeValue (k);
    props->saveIfNeeded();
}

// ------------------------------------------------------------------------------------------------ PCC
namespace { long long unixNow() { return juce::Time::currentTimeMillis() / 1000; } }

void AccessManager::setPcc (const pq::pcc::Reply& r, const std::string& keyHash)
{
    pccReply = r; pccHash = keyHash; pccActive = true; currentIndex = -1;
    int y = 0, m = 0, d = 0; pccExpDays = pcc::parseDate (r.expires, y, m, d) ? lic::daysSince2026 (y, m, d) : -1;
    roleValue.store ((int) (r.role == "master" ? lic::Role::Master : lic::Role::Beta)); unlocked.store (true);
}

void AccessManager::storePcc (const pq::pcc::Reply& r, const std::string& keyHash)
{
    props->setValue ("pccHash", juce::String (keyHash)); props->setValue ("pccValid", r.valid ? "1" : "0"); props->setValue ("pccRole", juce::String (r.role));
    props->setValue ("pccExpires", juce::String (r.expires)); props->setValue ("pccNonce", juce::String (r.nonce)); props->setValue ("pccTs", juce::String ((juce::int64) r.ts));
    props->setValue ("pccSig", juce::String (r.sig)); props->saveIfNeeded();
}

void AccessManager::restorePcc()
{
    if (! pccnet::enabled()) return;
    pcc::Reply r; r.valid = props->getValue ("pccValid") == "1"; r.role = props->getValue ("pccRole").toStdString(); r.expires = props->getValue ("pccExpires").toStdString();
    r.nonce = props->getValue ("pccNonce").toStdString(); r.ts = props->getValue ("pccTs").getLargeIntValue(); r.sig = props->getValue ("pccSig").toStdString();
    r.product = pccnet::kProduct; r.machineId = pccnet::machineId();
    const auto h = props->getValue ("pccHash").toStdString(); if (h.empty() || r.sig.empty()) return;
    if (! pcc::verifyReply (r, PQ_PCC_PUBKEY, pccnet::kProduct, r.machineId, nullptr)) return;   // ficheiro adulterado ou de outra maquina
    int y = 0, m = 0, d = 0; const int ex = pcc::parseDate (r.expires, y, m, d) ? lic::daysSince2026 (y, m, d) : -1;
    if (pcc::stillGood (r, ex, today(), unixNow())) setPcc (r, h);
}

AccessManager::PccOutcome AccessManager::loginPcc (const juce::String& password, bool remember, juce::String& message)
{
    if (! pccnet::enabled()) return PccOutcome::Disabled;
    const auto h = pcc::keyHashOf (password.toStdString(), pccnet::kProduct); pcc::Reply r;
    switch (pccnet::validateBlocking (h, r))
    {
        case pccnet::Status::Unreachable: return PccOutcome::Unreachable;
        case pccnet::Status::BadReply:    return PccOutcome::BadReply;
        case pccnet::Status::Ok: break;
    }
    message = juce::String (r.message);
    int y = 0, m = 0, d = 0; const int ex = pcc::parseDate (r.expires, y, m, d) ? lic::daysSince2026 (y, m, d) : -1;
    if (! pcc::stillGood (r, ex, today(), unixNow())) return PccOutcome::Invalid;
    const juce::ScopedLock sl (lock);
    setPcc (r, h); if (remember) storePcc (r, h); else for (auto k : { "pccHash", "pccSig" }) props->removeValue (k);
    props->saveIfNeeded(); return PccOutcome::Ok;
}

void AccessManager::revalidatePcc()
{
    if (! pccnet::enabled()) return;
    std::string h; { const juce::ScopedLock sl (lock); if (! pccActive) return; h = pccHash; }
    const auto last = props->getValue ("pccCheck").getLargeIntValue();
    if (unixNow() - last < (juce::int64) pcc::kRecheckHours * 3600) return;
    pcc::Reply r; if (pccnet::validateBlocking (h, r) != pccnet::Status::Ok) return;        // sem rede / resposta invalida: mantem o estado (periodo de graca)
    const juce::ScopedLock sl (lock); props->setValue ("pccCheck", juce::String ((juce::int64) unixNow()));
    if (! r.valid) { logout(); return; }                                                    // revogada ou desativada no PCC
    setPcc (r, h); storePcc (r, h);                                                         // renova validade/ts
}

void AccessManager::checkExpiry()
{
    int idx; bool pc; { const juce::ScopedLock sl (lock); idx = currentIndex; pc = pccActive; }
    if (pc) { if (! pcc::stillGood (pccReply, pccExpDays, today(), unixNow())) logout(); return; }
    if (unlocked.load() && idx >= 0 && lic::pwExpired (secrets::kPasswords[idx], today())) logout();
}

int AccessManager::betaDaysLeft() const
{
    int idx; bool pc; int ex; { const juce::ScopedLock sl (lock); idx = currentIndex; pc = pccActive; ex = pccExpDays; }
    if (pc) return (unlocked.load() && role() == lic::Role::Beta && ex >= 0) ? std::max (0, ex - today()) : -1;
    if (! unlocked.load() || role() != lic::Role::Beta || idx < 0) return -1;
    return lic::daysLeft (secrets::kPasswords[idx], today());
}

juce::String AccessManager::roleText() const
{
    int idx; bool pc; std::string exp; { const juce::ScopedLock sl (lock); idx = currentIndex; pc = pccActive; exp = pccReply.expires; }
    if (unlocked.load() && pc)
    {
        if (role() == lic::Role::Master) return "MESTRE";
        int y = 0, m = 0, d = 0; if (! pcc::parseDate (exp, y, m, d)) return "LICENCIADO";
        return juce::String (juce::CharPointer_UTF8 ("LICEN\xC3\x87""A \xC2\xB7 at\xC3\xA9 ")) + juce::String (d).paddedLeft ('0', 2) + "/" + juce::String (m).paddedLeft ('0', 2) + "/" + juce::String (y);
    }
    if (! unlocked.load() || idx < 0) return {};
    if (role() == lic::Role::Master) return "MESTRE";
    const auto& e = secrets::kPasswords[idx];
    return juce::String (juce::CharPointer_UTF8 ("BETA \xC2\xB7 at\xC3\xA9 ")) + juce::String (e.expDay).paddedLeft ('0', 2) + "/" + juce::String (e.expMonth).paddedLeft ('0', 2) + "/" + juce::String (e.expYear);
}
