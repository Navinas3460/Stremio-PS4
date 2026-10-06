#include "json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

const Json NULL_JSON;
const std::string EMPTY_STRING;
const Json::Array EMPTY_ARRAY;
const Json::Object EMPTY_OBJECT;

struct Parser {
	const std::string &s;
	size_t i = 0;
	std::string error;
	int depth = 0;

	void skip_ws() {
		while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) {
			i++;
		}
	}
	bool fail(const char *what) {
		if (error.empty()) {
			char buf[96];
			snprintf(buf, sizeof(buf), "%s at offset %zu", what, i);
			error = buf;
		}
		return false;
	}
	bool literal(const char *word) {
		size_t n = 0;
		while (word[n]) {
			if (i + n >= s.size() || s[i + n] != word[n]) {
				return fail("invalid literal");
			}
			n++;
		}
		i += n;
		return true;
	}
	static void append_utf8(std::string &out, uint32_t cp) {
		if (cp < 0x80) {
			out += (char)cp;
		} else if (cp < 0x800) {
			out += (char)(0xC0 | (cp >> 6));
			out += (char)(0x80 | (cp & 0x3F));
		} else if (cp < 0x10000) {
			out += (char)(0xE0 | (cp >> 12));
			out += (char)(0x80 | ((cp >> 6) & 0x3F));
			out += (char)(0x80 | (cp & 0x3F));
		} else {
			out += (char)(0xF0 | (cp >> 18));
			out += (char)(0x80 | ((cp >> 12) & 0x3F));
			out += (char)(0x80 | ((cp >> 6) & 0x3F));
			out += (char)(0x80 | (cp & 0x3F));
		}
	}
	bool hex4(uint32_t &out) {
		if (i + 4 > s.size()) {
			return fail("bad \\u escape");
		}
		out = 0;
		for (int k = 0; k < 4; k++) {
			const char c = s[i++];
			out <<= 4;
			if (c >= '0' && c <= '9') out |= c - '0';
			else if (c >= 'a' && c <= 'f') out |= c - 'a' + 10;
			else if (c >= 'A' && c <= 'F') out |= c - 'A' + 10;
			else return fail("bad \\u escape");
		}
		return true;
	}
	bool string(std::string &out) {
		i++; // opening quote
		while (i < s.size()) {
			const char c = s[i++];
			if (c == '"') {
				return true;
			}
			if (c != '\\') {
				out += c;
				continue;
			}
			if (i >= s.size()) {
				break;
			}
			const char e = s[i++];
			switch (e) {
				case '"': out += '"'; break;
				case '\\': out += '\\'; break;
				case '/': out += '/'; break;
				case 'b': out += '\b'; break;
				case 'f': out += '\f'; break;
				case 'n': out += '\n'; break;
				case 'r': out += '\r'; break;
				case 't': out += '\t'; break;
				case 'u': {
					uint32_t cp;
					if (!hex4(cp)) return false;
					if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
						i += 2;
						uint32_t lo;
						if (!hex4(lo)) return false;
						cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
					}
					append_utf8(out, cp);
					break;
				}
				default: return fail("bad escape");
			}
		}
		return fail("unterminated string");
	}
	bool value(Json &out) {
		if (++depth > 200) {
			return fail("nesting too deep");
		}
		skip_ws();
		if (i >= s.size()) {
			return fail("unexpected end");
		}
		const char c = s[i];
		bool ok = true;
		if (c == '{') {
			i++;
			Json::Object obj;
			skip_ws();
			if (i < s.size() && s[i] == '}') {
				i++;
			} else {
				while (true) {
					skip_ws();
					if (i >= s.size() || s[i] != '"') { ok = fail("expected key"); break; }
					std::string key;
					if (!string(key)) { ok = false; break; }
					skip_ws();
					if (i >= s.size() || s[i] != ':') { ok = fail("expected ':'"); break; }
					i++;
					Json v;
					if (!value(v)) { ok = false; break; }
					obj[key] = v;
					skip_ws();
					if (i < s.size() && s[i] == ',') { i++; continue; }
					if (i < s.size() && s[i] == '}') { i++; break; }
					ok = fail("expected ',' or '}'");
					break;
				}
			}
			out = Json(obj);
		} else if (c == '[') {
			i++;
			Json::Array arr;
			skip_ws();
			if (i < s.size() && s[i] == ']') {
				i++;
			} else {
				while (true) {
					Json v;
					if (!value(v)) { ok = false; break; }
					arr.push_back(v);
					skip_ws();
					if (i < s.size() && s[i] == ',') { i++; continue; }
					if (i < s.size() && s[i] == ']') { i++; break; }
					ok = fail("expected ',' or ']'");
					break;
				}
			}
			out = Json(arr);
		} else if (c == '"') {
			std::string str;
			ok = string(str);
			out = Json(str);
		} else if (c == 't') {
			ok = literal("true");
			out = Json(true);
		} else if (c == 'f') {
			ok = literal("false");
			out = Json(false);
		} else if (c == 'n') {
			ok = literal("null");
			out = Json();
		} else if (c == '-' || (c >= '0' && c <= '9')) {
			const char *start = s.c_str() + i;
			char *end = nullptr;
			const double d = strtod(start, &end);
			if (end == start) {
				ok = fail("bad number");
			}
			i += end - start;
			out = Json(d);
		} else {
			ok = fail("unexpected character");
		}
		depth--;
		return ok;
	}
};

void escape_to(std::string &out, const std::string &s) {
	out += '"';
	for (unsigned char c : s) {
		switch (c) {
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			case '\b': out += "\\b"; break;
			case '\f': out += "\\f"; break;
			default:
				if (c < 0x20) {
					char buf[8];
					snprintf(buf, sizeof(buf), "\\u%04x", c);
					out += buf;
				} else {
					out += (char)c;
				}
		}
	}
	out += '"';
}

} // namespace

Json Json::parse(const std::string &text, std::string *error) {
	Parser p{ text };
	Json out;
	if (!p.value(out)) {
		if (error) *error = p.error;
		return Json();
	}
	p.skip_ws();
	if (p.i != text.size()) {
		if (error) *error = "trailing characters";
		return Json();
	}
	return out;
}

const std::string &Json::as_string() const {
	return type_ == STRING ? *string_ : EMPTY_STRING;
}

const Json &Json::operator[](const std::string &key) const {
	if (type_ != OBJECT) {
		return NULL_JSON;
	}
	auto it = object_->find(key);
	return it == object_->end() ? NULL_JSON : it->second;
}

bool Json::has(const std::string &key) const {
	return type_ == OBJECT && object_->count(key) > 0;
}

Json &Json::set(const std::string &key, const Json &value) {
	if (type_ != OBJECT) {
		*this = Json(Object());
	} else if (object_.use_count() > 1) {
		object_ = std::make_shared<Object>(*object_); // copy on write
	}
	(*object_)[key] = value;
	return *this;
}

const Json::Object &Json::items() const {
	return type_ == OBJECT ? *object_ : EMPTY_OBJECT;
}

size_t Json::size() const {
	if (type_ == ARRAY) return array_->size();
	if (type_ == OBJECT) return object_->size();
	return 0;
}

const Json &Json::operator[](size_t index) const {
	return (type_ == ARRAY && index < array_->size()) ? (*array_)[index] : NULL_JSON;
}

const Json::Array &Json::elements() const {
	return type_ == ARRAY ? *array_ : EMPTY_ARRAY;
}

void Json::push(const Json &value) {
	if (type_ != ARRAY) {
		*this = Json(Array());
	} else if (array_.use_count() > 1) {
		array_ = std::make_shared<Array>(*array_);
	}
	array_->push_back(value);
}

void Json::dump_to(std::string &out) const {
	switch (type_) {
		case NUL: out += "null"; break;
		case BOOL: out += bool_ ? "true" : "false"; break;
		case NUMBER: {
			char buf[32];
			if (std::isfinite(number_) && number_ == std::floor(number_) && std::fabs(number_) < 9e15) {
				snprintf(buf, sizeof(buf), "%lld", (long long)number_);
			} else {
				snprintf(buf, sizeof(buf), "%.17g", number_);
			}
			out += buf;
			break;
		}
		case STRING: escape_to(out, *string_); break;
		case ARRAY: {
			out += '[';
			bool first = true;
			for (const Json &v : *array_) {
				if (!first) out += ',';
				first = false;
				v.dump_to(out);
			}
			out += ']';
			break;
		}
		case OBJECT: {
			out += '{';
			bool first = true;
			for (const auto &kv : *object_) {
				if (!first) out += ',';
				first = false;
				escape_to(out, kv.first);
				out += ':';
				kv.second.dump_to(out);
			}
			out += '}';
			break;
		}
	}
}

std::string Json::dump() const {
	std::string out;
	dump_to(out);
	return out;
}
