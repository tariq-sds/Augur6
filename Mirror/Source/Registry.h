#pragma once
#include "Analysis.h"
#include <mutex>

// All Mirror instances in the same host process share this registry: stems publish their
// analysis here; the master instance reads them and writes back gain / transpose targets.
namespace mr
{
constexpr int kMaxStems = 48;

enum Role { roleAuto = 0, roleDrums, roleBass, roleTonal, roleVocal, roleFx };
inline const juce::StringArray& roleNames() { static juce::StringArray r { "Auto", "Drums", "Bass", "Tonal", "Vocal", "FX" }; return r; }

struct StemSlot
{
    std::atomic<bool> used { false };
    std::atomic<double> heartbeat { 0 };
    std::atomic<int> role { roleAuto }, resolvedRole { roleTonal };
    std::atomic<bool> locked { false };
    std::atomic<int> analysisVersion { 0 };
    std::array<std::atomic<float>, kBands> power {};       // mean band power over the last listen (pre-gain)
    std::array<std::atomic<float>, 12> chroma {};
    std::atomic<float> level { 0 };                        // live peak, for meters
    std::atomic<float> gainDb { 0 };                       // current gain parameter value
    std::atomic<float> transpose { 0 };
    std::atomic<float> targetGainDb { 0 };
    std::atomic<int> targetSeq { 0 };
    std::atomic<int> targetTranspose { 0 };
    std::atomic<int> transposeSeq { 0 };
    std::atomic<int> keyShift { 0 };                       // suggested by master
    std::atomic<int> keyState { 0 };                       // 0 unknown, 1 in key, 2 out of key, 3 not tonal
    juce::String name;                                     // guarded by Registry::mutex
};

struct Registry
{
    StemSlot slots[kMaxStems];
    std::mutex mutex;
    std::atomic<bool> listening { false };
    std::atomic<int> listenSeq { 0 };
    std::atomic<int> masters { 0 };

    static Registry& get() { static Registry r; return r; }

    int claim()
    {
        for (int i = 0; i < kMaxStems; ++i)
        {
            bool expected = false;
            if (slots[i].used.compare_exchange_strong (expected, true))
            {
                auto& s = slots[i];
                s.role = roleAuto; s.locked = false; s.analysisVersion = 0;
                for (auto& p : s.power) p = 0; for (auto& c : s.chroma) c = 0;
                s.gainDb = 0; s.transpose = 0; s.keyState = 0; s.keyShift = 0;
                s.heartbeat = juce::Time::getMillisecondCounterHiRes();
                std::lock_guard<std::mutex> lk (mutex);
                s.name = "Stem " + juce::String (i + 1);
                return i;
            }
        }
        return -1;
    }

    void release (int i) { if (i >= 0 && i < kMaxStems) slots[i].used = false; }

    bool alive (int i) const
    {
        return slots[i].used.load() && juce::Time::getMillisecondCounterHiRes() - slots[i].heartbeat.load() < 4000.0;
    }

    juce::String nameOf (int i) { std::lock_guard<std::mutex> lk (mutex); return slots[i].name; }
    void setName (int i, const juce::String& n) { std::lock_guard<std::mutex> lk (mutex); slots[i].name = n; }
};
} // namespace mr
