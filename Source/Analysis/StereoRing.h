// =============================================================================
//  PRIMEQ pro by Piradex — anel SPSC lock-free (audio -> GUI), pre-alocado, sem mutex
// =============================================================================
#pragma once
#include <atomic>
#include <cstdint>
#include <vector>

namespace pq
{
class StereoRing
{
public:
    StereoRing() : l (kSize), r (kSize) {}
    // thread de audio
    void push (const float* a, const float* b, int n) noexcept
    {
        uint32_t w = wr.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) { l[(w + (uint32_t) i) & kMask] = a[i]; r[(w + (uint32_t) i) & kMask] = b ? b[i] : a[i]; }
        wr.store (w + (uint32_t) n, std::memory_order_release);
    }
    // thread da GUI: devolve quantas amostras novas foram copiadas (descarta o excesso se houve overrun)
    int pull (float* a, float* b, int maxN) noexcept
    {
        const uint32_t w = wr.load (std::memory_order_acquire); uint32_t rd = rp;
        uint32_t avail = w - rd;
        if (avail > kSize - 64) { rd = w - (kSize - 64); avail = kSize - 64; }
        const int n = (int) std::min<uint32_t> (avail, (uint32_t) maxN);
        for (int i = 0; i < n; ++i) { a[i] = l[(rd + (uint32_t) i) & kMask]; b[i] = r[(rd + (uint32_t) i) & kMask]; }
        rp = rd + (uint32_t) n; return n;
    }
    static constexpr uint32_t kSize = 1u << 16, kMask = kSize - 1;
private:
    std::vector<float> l, r; std::atomic<uint32_t> wr { 0 }; uint32_t rp = 0;
};
} // namespace pq
