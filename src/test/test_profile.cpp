#include <stdint.h>
#include <vector>
#include <map>
#include <string>
#include <gtest/gtest.h>
#include "dlib/hash.h"
#include "dlib/profile.h"
#include "dlib/time.h"
#include "dlib/thread.h"

#if not defined(_WIN32)

void ProfileSampleCallback(void* context, const dmProfile::Sample* sample)
{
    std::vector<dmProfile::Sample>* samples = (std::vector<dmProfile::Sample>*) context;
    samples->push_back(*sample);
}

void ProfileScopeCallback(void* context, const dmProfile::Scope* scope)
{
    std::map<std::string, const dmProfile::Scope*>* scopes = (std::map<std::string, const dmProfile::Scope*>*) context;
    (*scopes)[std::string(scope->m_Name)] = scope;
}

void ProfileCounterCallback(void* context, const dmProfile::CounterData* counter)
{
    std::map<std::string, const dmProfile::CounterData*>* counters = (std::map<std::string, const dmProfile::CounterData*>*) context;
    (*counters)[std::string(counter->m_Counter->m_Name)] = counter;
}

TEST(dmProfile, Profile)
{
    dmProfile::Initialize(128, 1024, 0);

    for (int i = 0; i < 2; ++i)
    {
        {
            dmProfile::Begin();
            {
                DM_PROFILE(A, "a")
                dmTime::Sleep(100000);
                {
                    {
                        DM_PROFILE(B, "a_b1")
                        dmTime::Sleep(50000);
                        {
                            DM_PROFILE(C, "a_b1_c")
                            dmTime::Sleep(40000);
                        }
                    }
                    {
                        DM_PROFILE(B, "b2")
                        dmTime::Sleep(50000);
                        {
                            DM_PROFILE(C, "a_b2_c1")
                            dmTime::Sleep(40000);
                        }
                        {
                            DM_PROFILE(C, "a_b2_c2")
                            dmTime::Sleep(60000);
                        }
                    }
                }
            }
            {
                DM_PROFILE(D, "a_d")
                dmTime::Sleep(80000);
            }
        }

        std::vector<dmProfile::Sample> samples;
        std::map<std::string, const dmProfile::Scope*> scopes;

        dmProfile::IterateSamples(&samples, &ProfileSampleCallback);
        dmProfile::IterateScopes(&scopes, &ProfileScopeCallback);
        ASSERT_EQ(7U, samples.size());

        double ticks_per_sec = dmProfile::GetTicksPerSecond();

        ASSERT_STREQ("a", samples[0].m_Name);
        ASSERT_STREQ("a_b1", samples[1].m_Name);
        ASSERT_STREQ("a_b1_c", samples[2].m_Name);
        ASSERT_STREQ("b2", samples[3].m_Name);
        ASSERT_STREQ("a_b2_c1", samples[4].m_Name);
        ASSERT_STREQ("a_b2_c2", samples[5].m_Name);
        ASSERT_STREQ("a_d", samples[6].m_Name);

    // TODO: TOL increased due to valgrind... Command line option or detect valgrind.
    #define TOL (40.0 / 1000.0)
        ASSERT_NEAR((100000 + 50000 + 40000 + 50000 + 40000 + 60000) / 1000000.0, samples[0].m_Elapsed / ticks_per_sec, TOL);
        ASSERT_NEAR((50000 + 40000) / 1000000.0, samples[1].m_Elapsed / ticks_per_sec, TOL);
        ASSERT_NEAR((40000) / 1000000.0, samples[2].m_Elapsed / ticks_per_sec, TOL);
        ASSERT_NEAR((50000 + 40000 + 60000) / 1000000.0, samples[3].m_Elapsed / ticks_per_sec, TOL);
        ASSERT_NEAR((40000) / 1000000.0, samples[4].m_Elapsed / ticks_per_sec, TOL);
        ASSERT_NEAR((60000) / 1000000.0, samples[5].m_Elapsed / ticks_per_sec, TOL);
        ASSERT_NEAR((80000) / 1000000.0, samples[6].m_Elapsed / ticks_per_sec, TOL);

        ASSERT_TRUE(scopes.end() != scopes.find("A"));
        ASSERT_TRUE(scopes.end() != scopes.find("B"));
        ASSERT_TRUE(scopes.end() != scopes.find("C"));
        ASSERT_TRUE(scopes.end() != scopes.find("D"));

        ASSERT_NEAR((100000 + 50000 + 40000 + 50000 + 40000 + 60000) / 1000000.0,
                    scopes["A"]->m_Elapsed / ticks_per_sec, TOL);

        ASSERT_NEAR((50000 + 40000 + 50000 + 40000 + 60000) / 1000000.0,
                    scopes["B"]->m_Elapsed / ticks_per_sec, TOL);

        ASSERT_NEAR((40000 + 40000 + 60000) / 1000000.0,
                    scopes["C"]->m_Elapsed / ticks_per_sec, TOL);

        ASSERT_NEAR((80000) / 1000000.0,
                    scopes["D"]->m_Elapsed / ticks_per_sec, TOL);

        dmProfile::End();
    }
    dmProfile::Finalize();
}

TEST(dmProfile, ProfileOverflow1)
{
    dmProfile::Initialize(4, 2, 0);
    {
        dmProfile::Begin();
        {
            { DM_PROFILE(X, "a") }
            { DM_PROFILE(X, "b") }
            { DM_PROFILE(X, "c") }
            { DM_PROFILE(X, "d") }
        }
        dmProfile::End();
    }

    std::vector<dmProfile::Sample> samples;
    dmProfile::IterateSamples(&samples, &ProfileSampleCallback);
    ASSERT_EQ(2U, samples.size());

    dmProfile::Finalize();
}

TEST(dmProfile, ProfileOverflow2)
{
    dmProfile::Initialize(0, 0, 0);
    {
        dmProfile::Begin();
        {
            { DM_PROFILE(X, "a") }
            { DM_PROFILE(X, "b") }
            { DM_PROFILE(X, "c") }
            { DM_PROFILE(X, "d") }
        }
        dmProfile::End();
    }

    dmProfile::Finalize();
}

TEST(dmProfile, Counter1)
{
    dmProfile::Initialize(0, 0, 16);
    {
        for (int i = 0; i < 2; ++i)
        {
            dmProfile::Begin();
            { DM_COUNTER("c1", 1); }
            { DM_COUNTER("c1", 2); }
            { DM_COUNTER("c1", 4); }
            { DM_COUNTER("c2", 123); }
            dmProfile::End();

            std::map<std::string, dmProfile::CounterData*> counters;
            dmProfile::IterateCounters(&counters, ProfileCounterCallback);

            ASSERT_EQ(7, counters["c1"]->m_Value);
            ASSERT_EQ(123, counters["c2"]->m_Value);
            ASSERT_EQ(2U, counters.size());
        }
    }

    dmProfile::Finalize();
}

uint32_t g_c1hash = dmHashString32("c1");
void CounterThread(void* arg)
{
    for (int i = 0; i < 2000; ++i)
    {
        DM_COUNTER_HASH("c1", g_c1hash, 1);
    }
}

TEST(dmProfile, Counter2)
{
    dmProfile::Initialize(0, 0, 16);

    dmProfile::Begin();
    dmThread::Thread t1 = dmThread::New(CounterThread, 0xf0000, 0);
    dmThread::Thread t2 = dmThread::New(CounterThread, 0xf0000, 0);

    dmThread::Join(t1);
    dmThread::Join(t2);
    dmProfile::End();

    std::map<std::string, dmProfile::CounterData*> counters;
    dmProfile::IterateCounters(&counters, ProfileCounterCallback);

    ASSERT_EQ(2000 * 2, counters["c1"]->m_Value);
    ASSERT_EQ(1U, counters.size());

    dmProfile::Finalize();
}

void ProfileThread(void* arg)
{
    for (int i = 0; i < 20000; ++i)
    {
        DM_PROFILE(X, "a")
    }
}

TEST(dmProfile, ThreadProfile)
{
    dmProfile::Initialize(32, 1024 * 1024, 16);

    dmProfile::Begin();
    uint64_t start = dmTime::GetTime();
    dmThread::Thread t1 = dmThread::New(ProfileThread, 0xf0000, 0);
    dmThread::Thread t2 = dmThread::New(ProfileThread, 0xf0000, 0);
    dmThread::Join(t1);
    dmThread::Join(t2);
    uint64_t end = dmTime::GetTime();
    dmProfile::End();

    printf("Elapsed: %f ms\n", (end-start) / 1000.0f);

    std::vector<dmProfile::Sample> samples;
    std::map<std::string, const dmProfile::Scope*> scopes;

    dmProfile::IterateSamples(&samples, &ProfileSampleCallback);
    dmProfile::IterateScopes(&scopes, &ProfileScopeCallback);

    ASSERT_EQ(20000 * 2, scopes["X"]->m_Count);
    ASSERT_EQ(20000U * 2U, samples.size());

    dmProfile::Finalize();
}

#else
#endif

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
