#include <algorithm>
#include <string.h>
#include "profile.h"
#include "hashtable.h"
#include "hash.h"
#include "spinlock.h"
#include "math.h"

namespace dmProfile
{
    dmArray<Sample> g_Samples;
    dmArray<Scope> g_Scopes;

    dmHashTable32<uint32_t> g_CountersTable;
    dmArray<Counter> g_Counters;
    dmArray<CounterData> g_CountersData;

    uint32_t g_BeginTime = 0;
    uint64_t g_TicksPerSecond = 1000000;
    float g_FrameTime = 0.0f;
    float g_MaxFrameTime = 0.0f;
    uint32_t g_MaxFrameTimeCounter = 0;
    bool g_OutOfScopes = false;
    bool g_OutOfSamples = false;
    bool g_OutOfCounters = false;
    dmSpinlock::lock_t g_CounterLock;
    dmSpinlock::lock_t g_ProfileLock;

    struct InitSpinLocks
    {
        InitSpinLocks()
        {
            dmSpinlock::Init(&g_CounterLock);
            dmSpinlock::Init(&g_ProfileLock);
        }
    };

    InitSpinLocks g_InitSpinlocks;

    void Initialize(uint32_t max_scopes, uint32_t max_samples, uint32_t max_counters)
    {
        g_Scopes.SetCapacity(max_scopes);
        g_Scopes.SetSize(0);

        g_Samples.SetCapacity(max_samples);
        g_Samples.SetSize(0);

        g_CountersTable.SetCapacity(dmMath::Max(16U,  2 * max_counters / 3), max_counters);
        g_CountersTable.Clear();

        g_CountersData.SetCapacity(max_counters);
        g_CountersData.SetSize(0);

        g_Counters.SetCapacity(max_counters);
        g_Counters.SetSize(0);

#if defined(_WIN32)
        QueryPerformanceFrequency((LARGE_INTEGER *) &g_TicksPerSecond);
#endif
    }

    void Finalize()
    {
        g_Samples.SetCapacity(0);
    }

    void Begin()
    {
        uint32_t n = g_Scopes.Size();
        for (uint32_t i = 0; i < n; ++i)
        {
            g_Scopes[i].m_Elapsed = 0;
            g_Scopes[i].m_Count = 0;
        }

        n = g_CountersData.Size();
        for (uint32_t i = 0; i < n; ++i)
        {
            g_CountersData[i].m_Value = 0;
        }

        g_Samples.SetSize(0);

#if defined(_WIN32)
        uint64_t pcnt;
        QueryPerformanceCounter((LARGE_INTEGER *) &pcnt);
        g_BeginTime = (uint32_t) pcnt;
#else
        timeval tv;
        gettimeofday(&tv, 0);
        g_BeginTime = tv.tv_sec * 1000000 + tv.tv_usec;
#endif
        g_FrameTime = 0.0f;
        g_OutOfScopes = false;
        g_OutOfSamples = false;
        g_OutOfCounters = false;
    }

    void End()
    {
        if (g_Scopes.Size() > 0)
        {
            g_FrameTime = g_Scopes[0].m_Elapsed/(0.001f * g_TicksPerSecond);
            for (uint32_t i = 1; i < g_Scopes.Size(); ++i)
            {
                float time = g_Scopes[i].m_Elapsed/(0.001f * g_TicksPerSecond);
                g_FrameTime = dmMath::Select(g_FrameTime - time, g_FrameTime, time);
            }
            ++g_MaxFrameTimeCounter;
            if (g_MaxFrameTimeCounter > 60 || g_FrameTime > g_MaxFrameTime)
            {
                g_MaxFrameTimeCounter = 0;
                g_MaxFrameTime = g_FrameTime;
            }
        }
    }

    // Used when out of scopes in order to remove conditional branches
    Scope g_DummyScope = { 0 };
    Scope* AllocateScope(const char* name)
    {
        dmSpinlock::Lock(&g_ProfileLock);
        if (g_Scopes.Full())
        {
            g_OutOfScopes = true;
            dmSpinlock::Unlock(&g_ProfileLock);
            return &g_DummyScope;
        }
        else
        {
            // NOTE: Not optimal with O(n) but scopes are allocated only once
            uint32_t n = g_Scopes.Size();
            for (uint32_t i = 0; i < n; ++i)
            {
                if (strcmp(name, g_Scopes[i].m_Name) == 0)
                {
                    dmSpinlock::Unlock(&g_ProfileLock);
                    return &g_Scopes[i];
                }
            }

            uint32_t i = g_Scopes.Size();
            Scope s;
            s.m_Name = name;
            s.m_Elapsed = 0;
            s.m_Index = i;
            s.m_Count = 0;
            g_Scopes.Push(s);
            Scope* ret = &g_Scopes[i];
            dmSpinlock::Unlock(&g_ProfileLock);
            return ret;
        }
    }

    // Used when out of samples in order to remove conditional branches
    Sample g_DummySample = { 0 };
    Sample* AllocateSample()
    {
        dmSpinlock::Lock(&g_ProfileLock);

        if (g_Samples.Full())
        {
            g_OutOfSamples = true;
            dmSpinlock::Unlock(&g_ProfileLock);
            return &g_DummySample;
        }
        else
        {
            uint32_t size = g_Samples.Size();
            g_Samples.SetSize(size + 1);
            Sample* ret = &g_Samples[size];
            dmSpinlock::Unlock(&g_ProfileLock);
            return ret;
        }
    }

    static inline bool CounterPred(const Counter& c1, const Counter& c2)
    {
        return c1.m_NameHash < c2.m_NameHash;
    }

    void AddCounter(const char* name, uint32_t amount)
    {
        uint32_t name_hash = dmHashString32(name);
        AddCounterHash(name, name_hash, amount);
    }

    void AddCounterHash(const char* name, uint32_t name_hash, uint32_t amount)
    {
        dmSpinlock::Lock(&g_CounterLock);
        uint32_t* counter_index = g_CountersTable.Get(name_hash);

        if (!counter_index)
        {
            if (g_Counters.Full())
            {
                g_OutOfCounters = true;
                dmSpinlock::Unlock(&g_CounterLock);
                return;
            }

            uint32_t new_index = g_CountersData.Size();
            g_Counters.SetSize(new_index+1);
            g_CountersData.SetSize(new_index+1);

            Counter* c = &g_Counters[new_index];
            c->m_Name = name;
            c->m_NameHash = dmHashString32(name);

            CounterData* cd = &g_CountersData[new_index];
            cd->m_Counter = c;
            cd->m_Value = 0;

            g_CountersTable.Put(c->m_NameHash, new_index);

            counter_index = g_CountersTable.Get(name_hash);
        }

        dmSpinlock::Unlock(&g_CounterLock);
        dmAtomicAdd32(&g_CountersData[*counter_index].m_Value, amount);
    }

    float GetFrameTime()
    {
        return g_FrameTime;
    }

    float GetMaxFrameTime()
    {
        return g_MaxFrameTime;
    }

    uint64_t GetTicksPerSecond()
    {
        return g_TicksPerSecond;
    }

    bool IsOutOfScopes()
    {
        return g_OutOfScopes;
    }

    bool IsOutOfSamples()
    {
        return g_OutOfSamples;
    }

    void IterateScopes(void* context, void (*call_back)(void* context, const Scope* sample))
    {
        uint32_t n = g_Scopes.Size();
        for (uint32_t i = 0; i < n; ++i)
        {
            call_back(context, &g_Scopes[i]);
        }
    }

    void IterateSamples(void* context, void (*call_back)(void* context, const Sample* sample))
    {
        uint32_t n = g_Samples.Size();
        for (uint32_t i = 0; i < n; ++i)
        {
            call_back(context, &g_Samples[i]);
        }
    }

    void IterateCounters(void* context, void (*call_back)(void* context, const CounterData* counter))
    {
        uint32_t n = g_CountersData.Size();
        for (uint32_t i = 0; i < n; ++i)
        {
            call_back(context, &g_CountersData[i]);
        }
    }
}

