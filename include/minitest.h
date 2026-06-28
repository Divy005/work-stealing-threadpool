#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <cstdlib>

namespace minitest {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> cases;
    return cases;
}

inline int& fail_count() {
    static int c = 0;
    return c;
}

inline int& test_count() {
    static int c = 0;
    return c;
}

struct Registrar {
    Registrar(const char* suite, const char* name, std::function<void()> fn) {
        registry().push_back({std::string(suite) + "." + name, std::move(fn)});
    }
};

inline void expect_fail(const char* expr, const char* file, int line) {
    std::cerr << file << ":" << line << ": FAILED: " << expr << "\n";
    ++fail_count();
}

inline int run_all() {
    int passed = 0;
    for (auto& tc : registry()) {
        int before = fail_count();
        tc.fn();
        ++test_count();
        if (fail_count() == before) {
            std::cout << "[ PASS ] " << tc.name << "\n";
            ++passed;
        } else {
            std::cout << "[ FAIL ] " << tc.name << "\n";
        }
    }
    int total = test_count();
    std::cout << "\n" << passed << "/" << total << " tests passed.\n";
    return fail_count() > 0 ? 1 : 0;
}

} // namespace minitest

#define TEST(suite, name)                                                       \
    static void suite##_##name##_body();                                        \
    static minitest::Registrar suite##_##name##_reg(#suite, #name,              \
        suite##_##name##_body);                                                 \
    static void suite##_##name##_body()

#define EXPECT_EQ(a, b)                                                         \
    do { if (!((a) == (b))) minitest::expect_fail(                              \
        #a " == " #b, __FILE__, __LINE__); } while(0)

#define EXPECT_NE(a, b)                                                         \
    do { if (!((a) != (b))) minitest::expect_fail(                              \
        #a " != " #b, __FILE__, __LINE__); } while(0)