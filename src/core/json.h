// Minimal JSON value, parser and serialiser (no exceptions). Enough for the
// Stremio API and add-on responses.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

class Json {
public:
	enum Type { NUL, BOOL, NUMBER, STRING, ARRAY, OBJECT };
	using Array = std::vector<Json>;
	using Object = std::map<std::string, Json>;

	Json() = default;
	Json(std::nullptr_t) {}
	Json(bool b) : type_(BOOL), bool_(b) {}
	Json(int n) : type_(NUMBER), number_(n) {}
	Json(int64_t n) : type_(NUMBER), number_((double)n) {}
	Json(uint64_t n) : type_(NUMBER), number_((double)n) {}
	Json(double n) : type_(NUMBER), number_(n) {}
	Json(const char *s) : type_(STRING), string_(std::make_shared<std::string>(s)) {}
	Json(const std::string &s) : type_(STRING), string_(std::make_shared<std::string>(s)) {}
	Json(const Array &a) : type_(ARRAY), array_(std::make_shared<Array>(a)) {}
	Json(const Object &o) : type_(OBJECT), object_(std::make_shared<Object>(o)) {}

	static Json array() { return Json(Array()); }
	static Json object() { return Json(Object()); }

	// Parses `text`. On error returns null and sets `error` (if given).
	static Json parse(const std::string &text, std::string *error = nullptr);
	std::string dump() const;

	Type type() const { return type_; }
	bool is_null() const { return type_ == NUL; }
	bool is_bool() const { return type_ == BOOL; }
	bool is_number() const { return type_ == NUMBER; }
	bool is_string() const { return type_ == STRING; }
	bool is_array() const { return type_ == ARRAY; }
	bool is_object() const { return type_ == OBJECT; }

	// Lenient accessors: return the fallback for missing / mistyped values.
	bool as_bool(bool fallback = false) const { return type_ == BOOL ? bool_ : fallback; }
	double as_number(double fallback = 0) const { return type_ == NUMBER ? number_ : fallback; }
	int64_t as_int(int64_t fallback = 0) const { return type_ == NUMBER ? (int64_t)number_ : fallback; }
	const std::string &as_string() const;
	std::string as_string(const std::string &fallback) const { return type_ == STRING ? *string_ : fallback; }

	// Object access. `get` returns a shared null for missing keys.
	const Json &operator[](const std::string &key) const;
	bool has(const std::string &key) const;
	Json &set(const std::string &key, const Json &value); // makes this an object if needed
	const Object &items() const;

	// Array access.
	size_t size() const;
	const Json &operator[](size_t index) const;
	const Array &elements() const;
	void push(const Json &value); // makes this an array if needed

private:
	void dump_to(std::string &out) const;

	Type type_ = NUL;
	bool bool_ = false;
	double number_ = 0;
	std::shared_ptr<std::string> string_;
	std::shared_ptr<Array> array_;
	std::shared_ptr<Object> object_;
};
