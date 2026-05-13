/* Minimal test helper. Header-only; one TU per binary defines the counters
 * via TESTS_MAIN before including. Designed to keep our unit tests free of
 * gtest/catch2 dependencies — host CI uses only cc/g++.
 *
 * Usage:
 *   #define TESTS_MAIN
 *   #include "tests.hpp"
 *   TEST(name) { CHECK(cond); CHECK_EQ(a, b); }
 *   int main() { return tests::run_all(); }
 */
#ifndef EVS_TESTS_HPP
#define EVS_TESTS_HPP

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

namespace tests {

struct Case {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<Case> &registry()
{
    static std::vector<Case> v;
    return v;
}

inline int &pass_count() { static int n; return n; }
inline int &fail_count() { static int n; return n; }
inline const char *&current_test() { static const char *n = ""; return n; }

inline int register_case(const char *name, std::function<void()> fn)
{
    registry().push_back({name, std::move(fn)});
    return 0;
}

inline int run_all()
{
    for (auto &c : registry()) {
        current_test() = c.name.c_str();
        int prev_fail = fail_count();
        c.fn();
        std::printf(" [%s] %s\n",
                    fail_count() == prev_fail ? "ok  " : "FAIL",
                    c.name.c_str());
    }
    std::printf("summary: %d pass, %d fail\n",
                pass_count(), fail_count());
    return fail_count() == 0 ? 0 : 1;
}

inline void record(bool ok, const char *expr, const char *file, int line)
{
    if (ok) {
        ++pass_count();
    } else {
        ++fail_count();
        std::fprintf(stderr, "  FAIL [%s] %s:%d: %s\n",
                     current_test(), file, line, expr);
    }
}

} /* namespace tests */

#define TEST(name) \
    static void test_fn_##name(); \
    static int  test_reg_##name = \
        ::tests::register_case(#name, &test_fn_##name); \
    static void test_fn_##name()

#define CHECK(cond) \
    ::tests::record((cond), #cond, __FILE__, __LINE__)

#define CHECK_EQ(a, b) \
    ::tests::record(((a) == (b)), #a " == " #b, __FILE__, __LINE__)

#define CHECK_NE(a, b) \
    ::tests::record(((a) != (b)), #a " != " #b, __FILE__, __LINE__)

#define CHECK_LT(a, b) \
    ::tests::record(((a) <  (b)), #a " < "  #b, __FILE__, __LINE__)

#define CHECK_GE(a, b) \
    ::tests::record(((a) >= (b)), #a " >= " #b, __FILE__, __LINE__)

#endif /* EVS_TESTS_HPP */
