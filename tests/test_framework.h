#pragma once

#include <cstdio>
#include <vector>

struct TestCase {
    const char* name;
    void (*run)();
};

inline std::vector<TestCase>& testCases()
{
    static std::vector<TestCase> cases;
    return cases;
}

struct AutoRegister {
    AutoRegister(const char* name, void (*run)()) { testCases().push_back({ name, run }); }
};

inline int& gCheckCount()
{
    static int n = 0;
    return n;
}
inline int& gFailCount()
{
    static int n = 0;
    return n;
}

#define TEST_CASE(NAME)                                      \
    static void test_##NAME();                               \
    static AutoRegister auto_reg_##NAME(#NAME, test_##NAME); \
    static void test_##NAME()

#define CHECK(EXPR)                                                     \
    do {                                                                \
        ++gCheckCount();                                                \
        if (!(EXPR)) {                                                  \
            ++gFailCount();                                             \
            std::printf("  [FAIL] %s:%d  %s\n", __FILE__, __LINE__, #EXPR); \
        }                                                               \
    } while (0)

#define CHECK_EQ(A, B) CHECK((A) == (B))

inline int runAllTests()
{
    int totalFailures = 0;
    for (const auto& tc : testCases()) {
        gCheckCount() = 0;
        gFailCount() = 0;
        std::printf("[ RUN  ] %s\n", tc.name);
        tc.run();
        if (gFailCount() == 0) {
            std::printf("[  OK  ] %s (%d checks)\n", tc.name, gCheckCount());
        }
        else {
            std::printf("[ FAIL ] %s (%d/%d checks failed)\n", tc.name, gFailCount(), gCheckCount());
            totalFailures += gFailCount();
        }
    }
    std::printf("\n%zu test case(s), %d check failure(s)\n", testCases().size(), totalFailures);
    return totalFailures == 0 ? 0 : 1;
}
