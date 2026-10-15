#include "../src/core/json.h"
#include "test.h"

TEST(json_parses_values) {
	std::string err;
	const Json j = Json::parse(R"({"a": 1.5, "b": [true, false, null], "c": "x\"y\\z", "d": {"e": -20}})", &err);
	CHECK_EQ(err, "");
	CHECK(j.is_object());
	CHECK_NEAR(j["a"].as_number(), 1.5, 1e-9);
	CHECK_EQ(j["b"].size(), (size_t)3);
	CHECK(j["b"][0].as_bool());
	CHECK(j["b"][2].is_null());
	CHECK_EQ(j["c"].as_string(), "x\"y\\z");
	CHECK_EQ(j["d"]["e"].as_int(), (int64_t)-20);
}

TEST(json_unicode_escapes) {
	const Json j = Json::parse(R"(["é", "😀", "\n\t"])");
	CHECK_EQ(j[0].as_string(), "\xC3\xA9");         // é
	CHECK_EQ(j[1].as_string(), "\xF0\x9F\x98\x80"); // 😀 (surrogate pair)
	CHECK_EQ(j[2].as_string(), "\n\t");
}

TEST(json_missing_and_mistyped_values_fall_back) {
	const Json j = Json::parse(R"({"n": "not a number"})");
	CHECK(j["missing"].is_null());
	CHECK(j["missing"]["deeper"].is_null());
	CHECK_EQ(j["n"].as_int(7), (int64_t)7);
	CHECK_EQ(j["missing"].as_string("fallback"), "fallback");
	CHECK_EQ(j["n"].size(), (size_t)0);
	CHECK(!j.has("missing"));
}

TEST(json_reports_errors) {
	for (const char *bad : { "", "{", "[1,]", "{\"a\" 1}", "tru", "\"unterminated", "[1] x" }) {
		std::string err;
		const Json j = Json::parse(bad, &err);
		if (err.empty()) test::fail(__FILE__, __LINE__, std::string("no error for: ") + bad);
		CHECK(j.is_null());
	}
}

TEST(json_round_trips) {
	Json o = Json::object();
	o.set("s", "a\"b\n").set("i", 42).set("big", (int64_t)1700000000000).set("f", 0.25).set("t", true).set("n", nullptr);
	Json arr = Json::array();
	arr.push(1);
	arr.push("two");
	o.set("arr", arr);
	std::string err;
	const Json back = Json::parse(o.dump(), &err);
	CHECK_EQ(err, "");
	CHECK_EQ(back["s"].as_string(), "a\"b\n");
	CHECK_EQ(back["i"].as_int(), (int64_t)42);
	CHECK_EQ(back["big"].as_int(), (int64_t)1700000000000); // ms timestamps must survive
	CHECK_NEAR(back["f"].as_number(), 0.25, 1e-12);
	CHECK(back["t"].as_bool());
	CHECK(back.has("n") && back["n"].is_null());
	CHECK_EQ(back["arr"][1].as_string(), "two");
	CHECK_EQ(back.dump(), o.dump());
}
