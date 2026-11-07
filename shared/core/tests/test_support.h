#pragma once
// Minimal test registry shared by the core test files; main() lives in core_tests.cpp.

#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include "musicunlock/audio.h"

struct TestCase {
    const char* name;
    std::function<void()> body;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> body) { registry().push_back({name, std::move(body)}); }
};

inline int failures = 0;

inline void report(const char* file, int line, const std::string& message) {
    ++failures;
    std::cerr << file << ":" << line << ": " << message << "\n";
}

#define TEST(name)                                       \
    void name();                                         \
    const Registrar name##_registrar(#name, name);       \
    void name()

#define CHECK(expr) \
    do { if (!(expr)) report(__FILE__, __LINE__, "CHECK(" #expr ") failed"); } while (0)

#define CHECK_EQ(a, b, label) \
    do { if (!((a) == (b))) report(__FILE__, __LINE__, std::string("mismatch: ") + (label)); } while (0)

#define CHECK_THROWS_AS(expr, type, label)                                                              \
    do {                                                                                                \
        bool thrown = false;                                                                            \
        try { (void)(expr); } catch (const type&) { thrown = true; }                                    \
        if (!thrown) report(__FILE__, __LINE__, std::string("expected " #type ": ") + (label));         \
    } while (0)

#define CHECK_THROWS(expr, label) CHECK_THROWS_AS(expr, mu::DecodeError, label)
