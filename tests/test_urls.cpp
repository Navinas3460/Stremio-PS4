#include "test.h"

#include "../src/net/urls.h"

TEST(urls_origin) {
	CHECK_EQ(urls::origin("https://Example.COM/a/b?c=d"), "https://example.com");
	CHECK_EQ(urls::origin("http://user:pass@host:8080/x"), "http://host:8080");
	CHECK_EQ(urls::origin("https://host?q=1"), "https://host");
	CHECK(urls::origin("https://a.example/x") != urls::origin("https://b.example/x"));
	CHECK(urls::origin("https://a.example/x") != urls::origin("http://a.example/x"));
}

TEST(urls_credential_headers) {
	CHECK(urls::is_credential_header("Authorization: Bearer x"));
	CHECK(urls::is_credential_header("cookie: a=b"));
	CHECK(urls::is_credential_header("Proxy-Authorization : Basic x"));
	CHECK(!urls::is_credential_header("Referer: https://example.com/"));
	CHECK(!urls::is_credential_header("User-Agent: Authorization"));
	CHECK(!urls::is_credential_header("Authorization"));
}

TEST(urls_redirect_headers) {
	const std::vector<std::string> h = { "Referer: https://site.example/", "Authorization: Bearer x", "Cookie: a=b" };
	CHECK_EQ(urls::redirect_headers("https://a.example/1", "https://A.example/2", h).size(), (size_t)3);
	const std::vector<std::string> other = urls::redirect_headers("https://a.example/1", "https://cdn.example/2", h);
	CHECK_EQ(other.size(), (size_t)1);
	CHECK_EQ(other[0], "Referer: https://site.example/");
	CHECK_EQ(urls::redirect_headers("https://a.example/1", "http://a.example/1", h).size(), (size_t)1);
}

TEST(urls_for_log) {
	// A debrid resolve link: the account key is in the path.
	CHECK_EQ(urls::for_log("https://torrentio.strem.fun/resolve/realdebrid/SECRETKEY/abc123/null/0/Movie.2020.mkv"),
			"https://torrentio.strem.fun/…/Movie.2020.mkv");
	CHECK_EQ(urls::for_log("https://addon.example/key=SECRET/manifest.json"), "https://addon.example/…/manifest.json");
	CHECK_EQ(urls::for_log("https://cdn.example/file.mp4?token=SECRET"), "https://cdn.example/file.mp4");
	CHECK_EQ(urls::for_log("https://u:p@cdn.example/a/"), "https://cdn.example/…/");
	CHECK_EQ(urls::for_log("https://cdn.example/"), "https://cdn.example/");
	CHECK_EQ(urls::for_log("https://cdn.example"), "https://cdn.example");
	CHECK_EQ(urls::for_log("http://127.0.0.1:11470/x/0"), "http://127.0.0.1:11470/…/0");
}
