// The Stremio facade end to end against a fake API and fake add-ons: session,
// library and add-on sync, the board, new-episode notifications, Continue
// watching, search, and the player's watch-progress rules (stremio-core's
// player.rs: watched at 70%, past the credits at 90%, saved every 90 s).
#include "../src/core/stremio.h"
#include "fakes.h"
#include "test.h"

#include <algorithm>

namespace {

const char *API = "https://api.strem.io/api/";

// "2026-10-14T00:00:00Z": "now" for these tests.
const int64_t NOW = 1791936000;

const char *LIBRARY = R"([
 {"_id":"tt1375666","name":"Inception","type":"movie","removed":false,"temp":false,"_mtime":"2026-10-03T00:00:00.000Z",
  "state":{"video_id":"tt1375666","timeOffset":1800000,"duration":8880000,"lastWatched":"2026-10-03T00:00:00.000Z"}},
 {"_id":"tt0903747","name":"Breaking Bad","type":"series","removed":false,"temp":false,"_mtime":"2026-10-01T00:00:00.000Z",
  "state":{"video_id":"tt0903747:2:5","timeOffset":0,"duration":0,"lastWatched":"2026-10-01T00:00:00.000Z"}},
 {"_id":"tt0000001","name":"Removed","type":"movie","removed":true,"temp":false,"_mtime":"2026-10-02T00:00:00.000Z",
  "state":{"video_id":"tt0000001","timeOffset":5000,"duration":10000}}
])";

const char *ADDONS = R"({"addons":[
 {"transportUrl":"https://meta.example/manifest.json","manifest":{"id":"meta","name":"Meta","types":["movie","series"],
  "idPrefixes":["tt"],"resources":["catalog","meta"],"catalogs":[
   {"type":"movie","id":"top","name":"Popular","extra":[{"name":"search"}]},
   {"type":"series","id":"top","name":"Popular","extra":[{"name":"search"}]},
   {"type":"series","id":"last-videos","extra":[{"name":"lastVideosIds","isRequired":true,"optionsLimit":100}]}]}},
 {"transportUrl":"https://streams.example/manifest.json","manifest":{"id":"streams","name":"Streams",
  "types":["movie","series"],"resources":["stream"],"catalogs":[]}}
]})";

const char *LAST_VIDEOS = R"({"metasDetailed":[{"id":"tt0903747","type":"series","name":"Breaking Bad","videos":[
 {"id":"tt0903747:2:5","season":2,"episode":5,"released":"2009-04-12T00:00:00Z"},
 {"id":"tt0903747:5:1","season":5,"episode":1,"released":"2026-10-05T00:00:00Z"},
 {"id":"tt0903747:5:2","season":5,"episode":2,"released":"2026-10-08T00:00:00Z"},
 {"id":"tt0903747:5:3","season":5,"episode":3,"released":"2027-01-01T00:00:00Z"}]}]})";

bool ends_with(const std::string &s, const std::string &tail) {
	return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

// A metas response with one item per name.
std::string metas(const std::vector<std::string> &ids) {
	std::string out = "{\"metas\":[";
	for (size_t i = 0; i < ids.size(); i++) {
		out += (i ? "," : "") + std::string("{\"id\":\"") + ids[i] + "\",\"type\":\"movie\",\"name\":\"" + ids[i] + "\"}";
	}
	return out + "]}";
}

http::Response answer(const http::Request &r) {
	if (r.url == std::string(API) + "datastoreGet") return fake::api_result(LIBRARY);
	if (r.url == std::string(API) + "addonCollectionGet") return fake::api_result(ADDONS);
	if (r.url == std::string(API) + "datastorePut") return fake::api_result(R"({"success":true})");
	if (ends_with(r.url, "/catalog/movie/top.json")) return fake::json(metas({ "m1", "m2", "m3" }));
	if (ends_with(r.url, "/catalog/series/top.json")) return fake::json(metas({ "s1" }));
	if (r.url.find("/catalog/series/last-videos/lastVideosIds=") != std::string::npos) return fake::json(LAST_VIDEOS);
	const size_t search = r.url.find("/search=");
	if (search != std::string::npos) {
		const std::string q = r.url.substr(search + 8, r.url.size() - search - 8 - 5); // minus ".json"
		return fake::json(metas({ "result-" + q }));
	}
	return fake::json("not found", 404);
}

// A signed-in Stremio with the library and add-ons synced.
void signed_in(Stremio &s) {
	fake::reset();
	fake::unix_time = NOW;
	fake::files["/fake/session.json"] = R"({"authKey":"key","user":{"email":"me@example.com"}})";
	fake::handler = answer;
	s.init();
	s.refresh();
	fake::settle();
}

int count_puts() {
	return (int)std::count_if(fake::requests.begin(), fake::requests.end(),
			[](const http::Request &r) { return ends_with(r.url, "/datastorePut"); });
}

Json last_put_item() {
	for (auto it = fake::requests.rbegin(); it != fake::requests.rend(); ++it) {
		if (ends_with(it->url, "/datastorePut")) return Json::parse(it->body)["changes"][0];
	}
	return Json();
}

} // namespace

TEST(stremio_session_sync_and_board) {
	Stremio s;
	signed_in(s);
	CHECK(s.logged_in());
	CHECK_EQ(s.user_email(), "me@example.com");
	CHECK(s.library_loaded());
	CHECK_EQ(s.library_error(), "");
	CHECK_EQ(s.addons_error(), "");
	// Board: catalogs that need no extra (not last-videos), in add-on order.
	CHECK_EQ(s.board().size(), (size_t)2);
	CHECK_EQ(s.board()[0].title, "Popular - Movie");
	CHECK_EQ(s.board()[0].items.size(), (size_t)3);
	CHECK(!s.board()[0].loading);
	CHECK_EQ(s.board()[1].title, "Popular - Series");
	// The API calls carry the auth key.
	CHECK(fake::requests[0].body.find("\"authKey\":\"key\"") != std::string::npos);
}

TEST(stremio_notifications_and_continue_watching) {
	Stremio s;
	signed_in(s);
	// Only series are pulled, and only for lastVideosIds catalogs.
	bool pulled = false;
	for (const http::Request &r : fake::requests) {
		if (r.url.find("lastVideosIds=") == std::string::npos) continue;
		pulled = true;
		CHECK(r.url.find("tt0903747") != std::string::npos);
		CHECK(r.url.find("tt1375666") == std::string::npos);
	}
	CHECK(pulled);
	// Released after it was last watched and not in the future: 5x01, 5x02.
	CHECK_EQ(s.notification_count("tt0903747"), 2);
	CHECK_EQ(s.notification_count("tt1375666"), 0);

	// In progress + new episodes, newest first (new episode 2026-10-08 beats
	// the movie watched 2026-10-03); removed items are left out.
	const std::vector<const LibraryItem *> cw = s.continue_watching();
	CHECK_EQ(cw.size(), (size_t)2);
	if (cw.size() == 2) {
		CHECK_EQ(cw[0]->id, "tt0903747");
		CHECK_EQ(cw[1]->id, "tt1375666");
	}
}

TEST(stremio_revoked_session_signs_out) {
	Stremio s;
	fake::reset();
	fake::files["/fake/session.json"] = R"({"authKey":"old","user":{"email":"me@example.com"}})";
	fake::handler = [](const http::Request &r) {
		// The API's real answer for a revoked key.
		if (ends_with(r.url, "/datastoreGet")) return fake::json(R"({"error":{"code":1,"message":"Session does not exist"}})");
		return answer(r);
	};
	s.init();
	CHECK(s.logged_in());
	s.refresh();
	fake::settle();
	CHECK(!s.logged_in());
	CHECK(!fake::files.count("/fake/session.json"));
}

TEST(stremio_player_resume_watched_and_credits) {
	Stremio s;
	signed_in(s);
	const int64_t D = 8880000;
	CHECK_EQ(s.player_start("tt1375666", "tt1375666", nullptr), (int64_t)1800000); // resume

	// Progress is saved at most every 90 s.
	const int puts = count_puts();
	s.player_time_changed(1801000, D);
	CHECK_EQ(count_puts(), puts);
	fake::now += 91;
	s.player_time_changed(1802000, D);
	CHECK_EQ(count_puts(), puts + 1);
	CHECK_EQ(last_put_item()["state"]["timeOffset"].as_int(), (int64_t)1802000);

	// Watching past 70% of the duration marks it watched (once).
	for (int64_t t = 1802000; t <= 8100000; t += 60000) s.player_time_changed(t, D);
	s.player_time_changed(8150000, D);
	// Stopping past 90% (the credits) resets the position.
	s.player_stop();
	const LibraryItem *item = s.library_item("tt1375666");
	CHECK(item != nullptr);
	if (item) {
		CHECK_EQ(item->times_watched, 1);
		CHECK_EQ(item->flagged_watched, 1);
		CHECK_EQ(item->time_offset, (int64_t)0);
		CHECK(!item->is_in_continue_watching());
	}
	CHECK_EQ(last_put_item()["state"]["timeOffset"].as_int(), (int64_t)0); // always pushed on stop
}

TEST(stremio_player_seek_isnt_watching) {
	Stremio s;
	signed_in(s);
	const int64_t D = 8880000;
	s.player_start("tt1375666", "tt1375666", nullptr);
	s.player_seeked(7000000, D); // jumping ahead...
	s.player_time_changed(7001000, D);
	s.player_stop();
	const LibraryItem *item = s.library_item("tt1375666");
	CHECK(item != nullptr);
	if (item) {
		CHECK_EQ(item->time_watched, (int64_t)1000); // ...doesn't count as watched time
		CHECK_EQ(item->times_watched, 0);
		CHECK_EQ(item->time_offset, (int64_t)7001000);
	}
}

TEST(stremio_player_credits_advance_to_next_episode) {
	Stremio s;
	signed_in(s);
	const MetaItem meta = MetaItem::from_json(Json::parse(R"({"id":"tt0903747","type":"series","name":"Breaking Bad","videos":[
		{"id":"tt0903747:2:5","season":2,"episode":5,"released":"2009-04-12T00:00:00Z"},
		{"id":"tt0903747:2:6","season":2,"episode":6,"released":"2009-04-19T00:00:00Z"}]})"));
	CHECK_EQ(s.player_start("tt0903747", "tt0903747:2:5", &meta), (int64_t)0);
	s.player_time_changed(2600000, 2700000);
	s.player_stop();
	const LibraryItem *item = s.library_item("tt0903747");
	CHECK(item != nullptr);
	if (item) {
		CHECK_EQ(item->video_id, "tt0903747:2:6");
		CHECK_EQ(item->time_offset, (int64_t)1); // shows in Continue watching on the next episode
		CHECK_EQ(item->time_watched, (int64_t)0);
	}
}

TEST(stremio_player_new_title_is_temporary) {
	Stremio s;
	signed_in(s);
	const MetaItem meta = MetaItem::from_json(Json::parse(R"({"id":"tt7777777","type":"movie","name":"New"})"));
	CHECK_EQ(s.player_start("tt7777777", "tt7777777", &meta), (int64_t)0);
	s.player_time_changed(60000, 6000000);
	s.player_stop();
	const LibraryItem *item = s.library_item("tt7777777");
	CHECK(item != nullptr);
	if (item) {
		CHECK(item->temp);
		CHECK(item->is_in_continue_watching()); // temporary items still show there
	}
	CHECK_EQ(last_put_item()["_id"].as_string(), "tt7777777");
}

TEST(stremio_search_drops_stale_answers) {
	Stremio s;
	signed_in(s);
	s.search("du"); // typing...
	s.search("dune");
	fake::settle(); // both answers arrive now; only the latest search's count
	CHECK_EQ(s.search_query(), "dune");
	CHECK_EQ(s.search_results().size(), (size_t)2); // the two searchable catalogs
	for (const BoardRow &row : s.search_results()) {
		CHECK(!row.loading);
		CHECK_EQ(row.items.size(), (size_t)1);
		if (!row.items.empty()) CHECK_EQ(row.items[0].id, "result-dune");
	}
	s.search("");
	CHECK(s.search_results().empty());
}
