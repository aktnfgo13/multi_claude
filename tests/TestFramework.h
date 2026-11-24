// Minimal self-contained test framework (no external dependency).
#pragma once

#include <cmath>
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace mfxtest {

struct TestCase {
    std::string group, name;
    std::function<void()> fn;
};

std::vector<TestCase>& registry();
struct Registrar {
    Registrar(const char* group, const char* name, std::function<void()> fn) { registry().push_back({group, name, std::move(fn)}); }
};

struct Failure { std::string msg; };
extern int gChecks;
/// Records a measurement for the test report (tests/results).
void record(const std::string& key, double value, const std::string& unit = "");

#define MFX_CAT2(a, b) a##b
#define MFX_CAT(a, b) MFX_CAT2(a, b)
#define TEST(group, name)                                                              \
    static void MFX_CAT(test_, MFX_CAT(group, MFX_CAT(_, name)))();                    \
    static mfxtest::Registrar MFX_CAT(reg_, MFX_CAT(group, MFX_CAT(_, name)))(#group, #name, \
        MFX_CAT(test_, MFX_CAT(group, MFX_CAT(_, name))));                             \
    static void MFX_CAT(test_, MFX_CAT(group, MFX_CAT(_, name)))()

#define CHECK(cond)                                                                    \
    do { ++mfxtest::gChecks; if (!(cond)) { std::ostringstream os_;                    \
        os_ << __FILE__ << ":" << __LINE__ << " CHECK(" #cond ") failed";              \
        throw mfxtest::Failure{os_.str()}; } } while (0)

#define CHECK_MSG(cond, msg)                                                           \
    do { ++mfxtest::gChecks; if (!(cond)) { std::ostringstream os_;                    \
        os_ << __FILE__ << ":" << __LINE__ << " CHECK(" #cond ") failed: " << msg;     \
        throw mfxtest::Failure{os_.str()}; } } while (0)

#define CHECK_NEAR(a, b, tol)                                                          \
    do { ++mfxtest::gChecks; const double a_ = (a), b_ = (b);                          \
        if (!(std::fabs(a_ - b_) <= (tol))) { std::ostringstream os_;                  \
        os_ << __FILE__ << ":" << __LINE__ << " CHECK_NEAR(" #a ", " #b ") " << a_      \
            << " vs " << b_ << " tol " << (tol);                                       \
        throw mfxtest::Failure{os_.str()}; } } while (0)

} // namespace mfxtest
