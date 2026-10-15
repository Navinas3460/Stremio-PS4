#include "test.h"

#include <cstring>

namespace test {

namespace {
int g_failures = 0;
const char *g_current = "";
} // namespace

std::vector<Case> &registry() {
	static std::vector<Case> cases;
	return cases;
}

void fail(const char *file, int line, const std::string &message) {
	g_failures++;
	fprintf(stderr, "FAIL %s (%s:%d): %s\n", g_current, file, line, message.c_str());
}

} // namespace test

int main(int argc, char **argv) {
	const char *filter = argc > 1 ? argv[1] : nullptr;
	int run = 0;
	for (const test::Case &c : test::registry()) {
		if (filter && !strstr(c.name, filter)) continue;
		test::g_current = c.name;
		const int before = test::g_failures;
		c.fn();
		run++;
		printf("%s %s\n", test::g_failures == before ? "ok  " : "FAIL", c.name);
	}
	printf("\n%d tests, %d failed checks\n", run, test::g_failures);
	return test::g_failures == 0 && run > 0 ? 0 : 1;
}
