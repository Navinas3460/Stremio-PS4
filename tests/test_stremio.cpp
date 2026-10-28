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

namespace {
std::vector<std::string> ids(const std::vector<const LibraryItem *> &items) {
	std::vector<std::string> out;
	for (const LibraryItem *item : items) out.push_back(item->id);
	return out;
}
} // namespace

TEST(stremio_library_filters_and_sorts) {
	Stremio s;
	signed_in(s);
	using Sort = Stremio::LibrarySort;
	// Removed items aren't in the library page.
	CHECK(ids(s.library("", Sort::LAST_WATCHED)) == std::vector<std::string>({ "tt1375666", "tt0903747" }));
	CHECK(ids(s.library("", Sort::NAME)) == std::vector<std::string>({ "tt0903747", "tt1375666" }));
	CHECK(ids(s.library("series", Sort::LAST_WATCHED)) == std::vector<std::string>({ "tt0903747" }));
	CHECK(s.library("channel", Sort::NAME).empty());
	CHECK(s.library_types() == std::vector<std::string>({ "movie", "series" }));
	CHECK(s.in_library("tt1375666"));
	CHECK(!s.in_library("tt0000001"));
	CHECK(!s.in_library("tt9999999"));
}

TEST(stremio_add_and_remove_from_library) {
	Stremio s;
	signed_in(s);
	const int puts = count_puts();
	const MetaItem meta = MetaItem::from_json(Json::parse(
			R"({"id":"tt7777777","type":"movie","name":"New","poster":"https://img.example/new.jpg"})"));
	s.add_to_library(meta);
	CHECK(s.in_library("tt7777777"));
	CHECK_EQ(count_puts(), puts + 1);
	Json put = last_put_item();
	CHECK_EQ(put["_id"].as_string(), "tt7777777");
	CHECK(!put["removed"].as_bool(true));
	CHECK(!put["temp"].as_bool(true));
	CHECK_EQ(put["poster"].as_string(), "https://img.example/new.jpg");
	// Just added counts as watched now: first in "Recent".
	CHECK_EQ(s.library("", Stremio::LibrarySort::LAST_WATCHED)[0]->id, "tt7777777");

	// Removing hides it from the library and Continue watching but keeps the
	// watch state, which re-adding brings back.
	s.remove_from_library("tt1375666");
	CHECK(!s.in_library("tt1375666"));
	CHECK(last_put_item()["removed"].as_bool(false));
	for (const LibraryItem *item : s.continue_watching()) CHECK(item->id != "tt1375666");
	s.add_to_library(MetaItem::from_json(Json::parse(R"({"id":"tt1375666","type":"movie","name":"Inception"})")));
	const LibraryItem *item = s.library_item("tt1375666");
	CHECK(item && !item->removed && item->time_offset == 1800000);
	// Removing something that isn't in the library sends nothing.
	const int before = count_puts();
	s.remove_from_library("tt0000001");
	CHECK_EQ(count_puts(), before);
}

// --- Add-on management regression tests ---

namespace {

using ReviewResult = Stremio::AddonChangeResult;

const char *REVIEW_URL = "https://review.example/manifest.json";

Json review_manifest() {
    return Json::parse(R"({
        "id":"review",
        "name":"Review add-on",
        "version":"1.0.0",
        "types":["movie"],
        "resources":["stream"],
        "catalogs":[]
    })");
}

Addon review_addon() {
    Json descriptor = Json::object();
    descriptor.set("transportUrl", REVIEW_URL)
        .set("manifest", review_manifest());
    return Addon::from_json(descriptor);
}

int review_request_count(const std::string &tail) {
    return (int)std::count_if(
        fake::requests.begin(), fake::requests.end(),
        [&](const http::Request &request) {
            return ends_with(request.url, tail);
        });
}

struct ReviewApi {
    Json collection = Json::parse(ADDONS)["addons"];
    Json manifest = review_manifest();
    Json last_saved;
    bool save_success = true;
    bool fail_manifest = false;
    int saves = 0;

    void use() {
        fake::handler = [this](const http::Request &request) {
            if (request.url == REVIEW_URL) {
                if (fail_manifest) return fake::json("{}", 503);
                return fake::json(manifest.dump());
            }
            if (ends_with(request.url, "/addonCollectionGet")) {
                Json result = Json::object();
                result.set("addons", collection);
                return fake::api_result(result.dump());
            }
            if (ends_with(request.url, "/addonCollectionSet")) {
                ++saves;
                last_saved = Json::parse(request.body);
                if (!save_success) {
                    return fake::api_result(R"({"success":false})");
                }
                collection = last_saved["addons"];
                return fake::api_result(R"({"success":true})");
            }
            return answer(request);
        };
    }
};

} // namespace

TEST(stremio_review_install_preserves_other_descriptors) {
    Stremio s;
    signed_in(s);
    ReviewApi api;

    Json first = api.collection[0];
    first.set("reviewUnknownField", "keep-me");
    Json flags = Json::object();
    flags.set("official", true).set("protected", true);
    first.set("flags", flags);

    Json original_collection = Json::array();
    original_collection.push(first);
    original_collection.push(api.collection[1]);
    api.collection = original_collection;
    const std::string before = api.collection.dump();
    api.use();

    int callbacks = 0;
    ReviewResult result = ReviewResult::SAVE_ERROR;
    s.change_addon(review_addon(), true, [&](ReviewResult value) {
        ++callbacks;
        result = value;
    });

    CHECK(s.addon_change_busy());
    fake::settle();

    CHECK(result == ReviewResult::OK);
    CHECK_EQ(callbacks, 1);
    CHECK_EQ(api.saves, 1);
    CHECK(!s.addon_change_busy());
    CHECK(!s.account_sync_busy());
    CHECK_EQ(api.last_saved["authKey"].as_string(), "key");
    CHECK_EQ(api.collection.size(), (size_t)3);
    CHECK_EQ(api.collection[0].dump(), original_collection[0].dump());
    CHECK_EQ(api.collection[1].dump(), original_collection[1].dump());
    CHECK_EQ(original_collection.dump(), before);
    CHECK_EQ(api.collection[2]["transportUrl"].as_string(), REVIEW_URL);
    CHECK_EQ(api.collection[2]["manifest"]["id"].as_string(), "review");
    CHECK_EQ(s.addons().size(), (size_t)3);
}

TEST(stremio_review_uninstall_preserves_other_descriptors) {
    Stremio s;
    signed_in(s);
    ReviewApi api;
    const std::string untouched = api.collection[1].dump();
    const Addon target = s.addons()[0];
    api.use();

    int callbacks = 0;
    ReviewResult result = ReviewResult::SAVE_ERROR;
    s.change_addon(target, false, [&](ReviewResult value) {
        ++callbacks;
        result = value;
    });
    fake::settle();

    CHECK(result == ReviewResult::OK);
    CHECK_EQ(callbacks, 1);
    CHECK_EQ(api.saves, 1);
    CHECK_EQ(api.collection.size(), (size_t)1);
    CHECK_EQ(api.collection[0].dump(), untouched);
    CHECK_EQ(s.addons().size(), (size_t)1);
    CHECK_EQ(s.addons()[0].id, "streams");
}

TEST(stremio_review_protected_uninstall_sends_no_save) {
    Stremio s;
    signed_in(s);
    ReviewApi api;
    Json protected_descriptor = api.collection[0];
    Json flags = Json::object();
    flags.set("protected", true);
    protected_descriptor.set("flags", flags);
    Json collection = Json::array();
    collection.push(protected_descriptor);
    collection.push(api.collection[1]);
    api.collection = collection;
    api.use();

    ReviewResult result = ReviewResult::OK;
    s.change_addon(s.addons()[0], false, [&](ReviewResult value) {
        result = value;
    });
    fake::settle();

    CHECK(result == ReviewResult::PROTECTED);
    CHECK_EQ(api.saves, 0);
    CHECK(!s.addon_change_busy());
}

TEST(stremio_review_existing_and_missing_send_no_save) {
    Stremio s;
    signed_in(s);
    ReviewApi api;

    Json installed = Json::object();
    installed.set("transportUrl", REVIEW_URL)
        .set("manifest", review_manifest());
    api.collection.push(installed);
    api.use();

    ReviewResult result = ReviewResult::OK;
    s.change_addon(review_addon(), true, [&](ReviewResult value) {
        result = value;
    });
    fake::settle();
    CHECK(result == ReviewResult::ALREADY_INSTALLED);
    CHECK_EQ(api.saves, 0);

    api.collection = Json::parse(ADDONS)["addons"];
    s.change_addon(review_addon(), false, [&](ReviewResult value) {
        result = value;
    });
    fake::settle();
    CHECK(result == ReviewResult::NOT_INSTALLED);
    CHECK_EQ(api.saves, 0);
}

TEST(stremio_review_invalid_configured_and_failed_manifests) {
    Stremio s;
    signed_in(s);
    ReviewApi api;
    api.use();

    ReviewResult result = ReviewResult::OK;

    api.manifest.set("id", "different-addon");
    s.change_addon(review_addon(), true, [&](ReviewResult value) {
        result = value;
    });
    fake::settle();
    CHECK(result == ReviewResult::INVALID_MANIFEST);
    CHECK_EQ(api.saves, 0);

    api.manifest = review_manifest();
    Json hints = Json::object();
    hints.set("configurationRequired", true);
    api.manifest.set("behaviorHints", hints);
    s.change_addon(review_addon(), true, [&](ReviewResult value) {
        result = value;
    });
    fake::settle();
    CHECK(result == ReviewResult::NEEDS_CONFIGURATION);
    CHECK_EQ(api.saves, 0);

    api.fail_manifest = true;
    s.change_addon(review_addon(), true, [&](ReviewResult value) {
        result = value;
    });
    fake::settle();
    CHECK(result == ReviewResult::NETWORK_ERROR);
    CHECK_EQ(api.saves, 0);
    CHECK(!s.addon_change_busy());
}

TEST(stremio_review_busy_blocks_second_change) {
    Stremio s;
    signed_in(s);
    ReviewApi api;
    api.use();

    int first_callbacks = 0;
    int second_callbacks = 0;
    ReviewResult first = ReviewResult::SAVE_ERROR;
    ReviewResult second = ReviewResult::OK;

    s.change_addon(review_addon(), true, [&](ReviewResult value) {
        ++first_callbacks;
        first = value;
    });
    s.change_addon(review_addon(), true, [&](ReviewResult value) {
        ++second_callbacks;
        second = value;
    });

    CHECK(second == ReviewResult::BUSY);
    CHECK_EQ(second_callbacks, 1);
    fake::settle();

    CHECK(first == ReviewResult::OK);
    CHECK_EQ(first_callbacks, 1);
    CHECK_EQ(api.saves, 1);
    CHECK(!s.addon_change_busy());
}

TEST(stremio_review_logout_cancels_before_save) {
    Stremio s;
    signed_in(s);
    ReviewApi api;
    api.use();

    int callbacks = 0;
    ReviewResult result = ReviewResult::OK;
    s.change_addon(review_addon(), true, [&](ReviewResult value) {
        ++callbacks;
        result = value;
    });
    s.logout();
    fake::settle();

    CHECK(result == ReviewResult::SESSION_CHANGED);
    CHECK_EQ(callbacks, 1);
    CHECK_EQ(api.saves, 0);
    CHECK(!s.logged_in());
    CHECK(!s.addon_change_busy());
    CHECK(!s.account_sync_busy());
}

TEST(stremio_review_failed_save_does_not_report_success) {
    Stremio s;
    signed_in(s);
    ReviewApi api;
    api.save_success = false;
    api.use();

    ReviewResult result = ReviewResult::OK;
    s.change_addon(review_addon(), true, [&](ReviewResult value) {
        result = value;
    });
    fake::settle();

    CHECK(result == ReviewResult::SAVE_ERROR);
    CHECK_EQ(api.saves, 1);
    CHECK_EQ(api.collection.size(), (size_t)2);
    CHECK_EQ(s.addons().size(), (size_t)2);
    CHECK(!s.addon_change_busy());
}

TEST(stremio_review_invalid_collection_keeps_local_addons) {
    Stremio s;
    signed_in(s);
    const size_t before = s.addons().size();
    fake::handler = [](const http::Request &request) {
        if (ends_with(request.url, "/addonCollectionGet")) {
            return fake::api_result(R"({"addons":{}})");
        }
        return answer(request);
    };

    s.refresh();
    fake::settle();

    CHECK_EQ(s.addons().size(), before);
    CHECK(!s.addons_error().empty());
    CHECK(!s.account_sync_busy());

    fake::handler = answer;
    s.refresh();
    fake::settle();

    CHECK(s.addons_error().empty());
    CHECK(s.addons_loaded());
    CHECK(!s.account_sync_busy());
}

TEST(stremio_review_refresh_coalesces_overlapping_requests) {
    Stremio s;
    signed_in(s);
    fake::requests.clear();

    s.refresh();
    s.refresh();
    s.refresh();

    CHECK(s.account_sync_busy());
    CHECK_EQ(review_request_count("/datastoreGet"), 1);
    CHECK_EQ(review_request_count("/addonCollectionGet"), 1);

    http::poll();

    CHECK(s.account_sync_busy());
    CHECK_EQ(review_request_count("/datastoreGet"), 2);
    CHECK_EQ(review_request_count("/addonCollectionGet"), 2);

    fake::settle();

    CHECK(!s.account_sync_busy());
    CHECK_EQ(review_request_count("/datastoreGet"), 2);
    CHECK_EQ(review_request_count("/addonCollectionGet"), 2);
    CHECK(s.addons_error().empty());
}

TEST(stremio_review_old_board_answers_are_ignored) {
    Stremio s;
    signed_in(s);

    s.refresh();
    http::poll(); // Completes account sync; leaves board requests pending.
    CHECK_EQ(s.board().size(), (size_t)2);

    s.refresh(); // Invalidates those pending board responses.
    fake::handler = [](const http::Request &request) {
        if (ends_with(request.url, "/catalog/movie/top.json")
                || ends_with(request.url, "/catalog/series/top.json")) {
            return fake::json(metas({ "stale-board-item" }));
        }
        return answer(request);
    };
    http::poll();

    CHECK_EQ(s.board().size(), (size_t)2);
    for (const BoardRow &row : s.board()) {
        CHECK(row.items.empty());
        CHECK(row.loading);
    }

    fake::handler = answer;
    fake::settle();

    CHECK(!s.account_sync_busy());
    CHECK_EQ(s.board()[0].items.size(), (size_t)3);
    if (!s.board()[0].items.empty()) {
        CHECK_EQ(s.board()[0].items[0].id, "m1");
    }
}

TEST(stremio_review_http_error_preserves_revoked_session_message) {
    Stremio s;
    fake::reset();
    fake::files["/fake/session.json"] =
        R"({"authKey":"old","user":{"email":"me@example.com"}})";
    fake::handler = [](const http::Request &request) {
        if (ends_with(request.url, "/datastoreGet")) {
            return fake::json(
                R"({"error":{"code":1,"message":"Session does not exist"}})",
                401);
        }
        return answer(request);
    };

    s.init();
    s.refresh();
    fake::settle();

    CHECK(!s.logged_in());
    CHECK(!s.account_sync_busy());
    CHECK(!fake::files.count("/fake/session.json"));
}

// --- Community add-on search matching ---
#include "../src/core/addon_search.h"

TEST(stremio_addon_search_name_and_identifier) {
    CHECK(addon_search::matches("My Streams", "org.example.addon", "streams"));
    CHECK(addon_search::matches("My Streams", "org.example.addon", "EXAMPLE"));
    CHECK(!addon_search::matches("My Streams", "org.example.addon", "subtitles"));
    CHECK(addon_search::matches("My Streams", "org.example.addon", ""));
}

TEST(stremio_addon_search_spanish_accents) {
    CHECK(addon_search::matches("Pel\xC3\xAD""culas", "films", "peliculas"));
    CHECK(addon_search::matches("ESPA\xC3\x91""OL", "language", "espanol"));
    CHECK(addon_search::matches("M\xC3\x9C""SICA", "audio", "musica"));
}

TEST(stremio_addon_search_keeps_original_text) {
    const std::string name = "Community Add-on";
    const std::string id = "org.Example";
    CHECK(addon_search::matches(name, id, "COMMUNITY"));
    CHECK_EQ(name, "Community Add-on");
    CHECK_EQ(id, "org.Example");
}

// --- Local QR support for phone-based add-on configuration ---
#include "../src/core/addon_config.h"

TEST(stremio_addon_config_standard_routes) {
    CHECK_EQ(
        addon_config::configure_url("https://addon.example/manifest.json"),
        "https://addon.example/configure");
    CHECK_EQ(
        addon_config::configure_url("https://addon.example/service/manifest.json"),
        "https://addon.example/service/configure");
    CHECK_EQ(
        addon_config::configure_url("http://addon.example/manifest.json"),
        "http://addon.example/configure");
}

TEST(stremio_addon_config_rejects_ambiguous_routes) {
    CHECK(addon_config::configure_url("stremio://addon.example/manifest.json").empty());
    CHECK(addon_config::configure_url("https:///manifest.json").empty());
    CHECK(addon_config::configure_url("https://addon.example/manifest.json?key=secret").empty());
    CHECK(addon_config::configure_url("https://addon.example/manifest.json#fragment").empty());
    CHECK(addon_config::configure_url("https://user:secret@addon.example/manifest.json").empty());
    CHECK(addon_config::configure_url("https://addon.example/other.json").empty());
    CHECK(addon_config::configure_url("https://addon.example/a b/manifest.json").empty());
}

TEST(stremio_addon_config_qr_is_generated_locally) {
    const auto code = addon_config::make_qr("https://addon.example/configure");
    const int size = addon_config::qr_size(code);
    CHECK(size >= 21 && size <= 177);
    CHECK(size % 4 == 1);
    CHECK(addon_config::qr_dark(code, 0, 0));
    CHECK(addon_config::qr_dark(code, 6, 0));
    CHECK(addon_config::qr_dark(code, 0, 6));
    CHECK(!addon_config::qr_dark(code, 1, 1));
    CHECK(!addon_config::qr_dark(code, -1, 0));
    CHECK(!addon_config::qr_dark(code, size, 0));
    CHECK(addon_config::make_qr("").empty());
    CHECK(addon_config::make_qr(std::string("a\0b", 3)).empty());
    CHECK(addon_config::make_qr(std::string(10000, 'x')).empty());
}

TEST(stremio_addon_config_rejects_invalid_qr_buffers) {
    auto code = addon_config::make_qr("https://addon.example/configure");
    CHECK(!code.empty());
    if (code.empty()) return;

    code[0] = 0;
    CHECK_EQ(addon_config::qr_size(code), 0);
    CHECK(!addon_config::qr_dark(code, 0, 0));

    code[0] = 22;
    CHECK_EQ(addon_config::qr_size(code), 0);

    code[0] = 255;
    CHECK_EQ(addon_config::qr_size(code), 0);

    code.clear();
    CHECK_EQ(addon_config::qr_size(code), 0);
}
