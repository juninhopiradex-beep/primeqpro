// =============================================================================
//  PrimEQ Pro by Piradex — Smart Ops: perfis (presets) por fonte e objetivo
//  Cada perfil e so um conjunto de valores iniciais dos controlos (editaveis) + uma gama de frequencias onde as propostas sao aceites.
//  a/b/c: Deresonate = sensibilidade dB / corte max dB / bandas; De-Ess = percentil % / reducao max dB / -;
//         Unmask = conflito min dB / corte max dB / bandas; Match = forca % / ganho max dB / bandas.
// =============================================================================
#pragma once
#include <vector>

namespace pq
{
struct SmartProfile { const char* name; const char* note; double a, b, c, fmin, fmax; };
namespace profiles
{
// ordem das operacoes = SmartPanel::Op (1 Deres, 2 DeEss, 3 Match, 4 Unmask)
inline const std::vector<SmartProfile>& of (int op)
{
    static const std::vector<SmartProfile> deres = {
        { "Geral (equilibrado)", "Ponto de partida seguro para qualquer fonte.", 4.0, 9.0, 6, 80, 16000 },
        { "Voz falada / locução", "Procura nasal, caixa e sala; deixa as fundamentais em paz.", 4.0, 6.0, 5, 120, 8000 },
        { "Voz cantada (pop / R&B)", "Cortes curtos nas ressonâncias da voz, preservando o corpo.", 4.5, 5.0, 5, 150, 9000 },
        { "Voz rap / trap", "Mais firme na zona 200 Hz–4 kHz onde a voz densa acumula.", 4.0, 6.0, 5, 150, 9000 },
        { "Coros / backing vocals", "Suave: várias vozes sobrepostas têm picos naturais.", 5.0, 4.0, 4, 200, 8000 },
        { "Kick / bombo", "Zona do corpo e da caixa do bombo (50 Hz–1,2 kHz).", 5.0, 8.0, 4, 50, 1200 },
        { "Snare / caixa", "Anel e ressonância do aro (150 Hz–6 kHz).", 4.5, 7.0, 5, 150, 6000 },
        { "Bateria acústica (bus)", "Cortes moderados em toda a bateria.", 5.0, 6.0, 5, 100, 8000 },
        { "Overheads / pratos", "Só acima de 2 kHz: dureza e ressonância de metal.", 4.0, 6.0, 5, 2000, 16000 },
        { "Baixo elétrico / 808", "Evita sobretons e ressonâncias do braço (40 Hz–1,5 kHz).", 6.0, 6.0, 4, 40, 1500 },
        { "Guitarra acústica / violão", "Ressonância do corpo e aspereza de palheta.", 4.0, 7.0, 6, 100, 10000 },
        { "Guitarra elétrica", "Zona de 'caixa' e agudo áspero do amp.", 4.5, 6.0, 5, 150, 8000 },
        { "Piano / teclas", "Notas que saltam por ressonância da caixa.", 5.0, 5.0, 6, 100, 10000 },
        { "Sintetizador / pad", "Picos de filtro e ressonância ressonante.", 5.0, 6.0, 5, 100, 14000 },
        { "Cordas / orquestra", "Muito suave: preserva a naturalidade.", 5.5, 4.0, 5, 150, 12000 },
        { "Percussão (conga, shaker…)", "Anéis metálicos e pele.", 4.5, 6.0, 4, 300, 14000 },
        { "Bus de mix (suave)", "Só os picos óbvios, cortes pequenos.", 6.0, 3.0, 6, 80, 14000 },
        { "Master (cirúrgico)", "Poucos cortes estreitos e pequenos (≤ 2,5 dB).", 7.0, 2.5, 4, 100, 12000 },
        { "Limpeza forte (material difícil)", "Sensível e com cortes grandes; reveja o resultado.", 3.0, 12.0, 8, 80, 16000 } };
    static const std::vector<SmartProfile> deess = {
        { "Geral", "Equilíbrio entre eficácia e naturalidade.", 75, 9.0, 0, 3500, 12000 },
        { "Voz feminina (sibilância alta)", "Centro da sibilância mais alto (5,5–12 kHz).", 72, 9.0, 0, 5500, 12000 },
        { "Voz masculina", "Sibilância mais baixa (3,5–9 kHz).", 75, 8.0, 0, 3500, 9000 },
        { "Voz suave / ASMR", "Atua pouco, preserva o ar.", 70, 6.0, 0, 3500, 12000 },
        { "Locução / podcast", "Fala próxima do microfone (4–10 kHz).", 75, 9.0, 0, 4000, 10000 },
        { "Rap / trap (agressiva)", "Atua mais cedo e mais forte.", 65, 12.0, 0, 3500, 12000 },
        { "Coros / backing vocals", "Suave, várias vozes somadas.", 70, 6.0, 0, 3500, 12000 },
        { "Cirúrgico (só os picos)", "Só as sílabas mais explosivas.", 85, 5.0, 0, 3500, 12000 },
        { "Forte (resolver de vez)", "Limiar baixo e redução alta.", 60, 12.0, 0, 3500, 12000 } };
    static const std::vector<SmartProfile> match = {
        { "Equilibrado (60 %)", "Aproxima o balanço tonal sem forçar.", 60, 6.0, 6, 30, 18000 },
        { "Subtil (30 %)", "Só um empurrão na direção da referência.", 30, 4.0, 4, 30, 18000 },
        { "Forte (85 %)", "Segue a referência de perto; reveja com ABX.", 85, 9.0, 8, 30, 18000 },
        { "Só corpo (100–800 Hz)", "Iguala apenas os graves/médios graves.", 60, 6.0, 5, 100, 800 },
        { "Só presença (1–5 kHz)", "Iguala apenas a zona de presença.", 60, 6.0, 5, 1000, 5000 },
        { "Só brilho (5–16 kHz)", "Iguala apenas os agudos e o ar.", 60, 6.0, 4, 5000, 16000 } };
    static const std::vector<SmartProfile> unmask = {
        { "Geral", "Equilíbrio entre abrir espaço e preservar timbre.", 2.0, 6.0, 4, 80, 12000 },
        { "Voz vs. beat", "Abre espaço à voz (200 Hz–5 kHz).", 2.0, 5.0, 4, 200, 5000 },
        { "Kick vs. baixo", "Só a zona de disputa grave (80–400 Hz).", 2.0, 5.0, 3, 80, 400 },
        { "Guitarra vs. voz", "Disputa de médios (800 Hz–5 kHz).", 2.0, 4.0, 4, 800, 5000 },
        { "Sintetizador / pad vs. voz", "Pads cobrem a voz em 300 Hz–4 kHz.", 2.0, 5.0, 4, 300, 4000 },
        { "Snare vs. voz", "Presença da caixa contra a voz.", 2.5, 4.0, 3, 1500, 6000 },
        { "Mix (suave)", "Poucos cortes pequenos.", 3.0, 3.0, 3, 80, 12000 },
        { "Máximo espaço", "Muito sensível, cortes grandes.", 1.5, 8.0, 6, 80, 12000 } };
    static const std::vector<SmartProfile> none;
    switch (op) { case 1: return deres; case 2: return deess; case 3: return match; case 4: return unmask; default: return none; }
}
} // namespace profiles
} // namespace pq
