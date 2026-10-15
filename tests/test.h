// A minimal test runner (no dependencies): TEST(name) { CHECK(...); }.
// Failures are reported with file and line; the process exits non-zero if
// any check failed. Run with a name filter: stremio_ps4_tests nv12
#pragma once

#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace test {

struct Case {
	const char *name;
	void (*fn)();
};
std::vector<Case> &registry();
void fail(const char *file, int line, const std::string &message);

struct Register {
	Register(const char *name, void (*fn)()) { registry().push_back({ name, fn }); }
};

template <class T>
std::string show(const T &v) {
	std::ostringstream s;
	s << v;
	return s.str();
}
inline std::string show(const std::string &v) {
	return "\"" + v + "\"";
}
inline std::string show(const char *v) {
	return v ? show(std::string(v)) : "null";
}

} // namespace test

#define TEST(name)                                                   \
	static void test_##name();                                       \
	static test::Register register_##name(#name, test_##name);       \
	static void test_##name()

#define CHECK(cond)                                                            \
	do {                                                                       \
		if (!(cond)) test::fail(__FILE__, __LINE__, "CHECK(" #cond ") failed"); \
	} while (0)

#define CHECK_EQ(actual, expected)                                                                          \
	do {                                                                                                    \
		const auto &a_ = (actual);                                                                          \
		const auto &e_ = (expected);                                                                        \
		if (!(a_ == e_)) {                                                                                  \
			test::fail(__FILE__, __LINE__, #actual " == " #expected ": got " + test::show(a_) + ", expected " + \
					test::show(e_));                                                                       \
		}                                                                                                   \
	} while (0)

#define CHECK_NEAR(actual, expected, tolerance)                                                                   \
	do {                                                                                                          \
		const double a_ = (actual), e_ = (expected);                                                              \
		if (std::fabs(a_ - e_) > (tolerance)) {                                                                   \
			test::fail(__FILE__, __LINE__, #actual " ~ " #expected ": got " + test::show(a_) + ", expected " +        \
					test::show(e_));                                                                             \
		}                                                                                                         \
	} while (0)
