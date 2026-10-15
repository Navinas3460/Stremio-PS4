#include "stremio.h"

#include "../net/http.h"
#include "../platform/platform.h"

#include <algorithm>
#include <cstdlib>
#include <memory>

namespace {

const char *API_URL = "https://api.strem.io/api/";
const char *LINK_API_URL = "https://link.stremio.com/api/v2/";
const char *LIBRARY_COLLECTION = "libraryItem";
const size_t CATALOG_PREVIEW_SIZE = 100;
const int NOTIFICATION_ITEMS_COUNT = 100;
const size_t BOARD_ROW_SIZE = 30;
const double LINK_POLL_SECONDS = 3.0;
const double LINK_CODE_TTL_SECONDS = 300.0;

std::string session_path() {
	return platform::data_dir() + "session.json";
}

// Parses JSON on the worker thread (libraries can be several MB).
void parse_on_worker(http::Response &res) {
	if (!res.error.empty()) return;
	std::string err;
	auto json = std::make_shared<Json>(Json::parse(res.body, &err));
	if (!err.empty()) {
		res.error = "invalid JSON (" + err + ")";
		return;
	}
	res.body.clear();
	res.payload = json;
}

std::string capitalize(const std::string &s) {
	if (s.empty()) return s;
	std::string out = s;
	out[0] = (char)toupper((unsigned char)out[0]);
	return out;
}

} // namespace

Stremio &stremio() {
	static Stremio instance;
	return instance;
}

// --- plumbing -------------------------------------------------------------------

void Stremio::fetch_json(const std::string &url, std::function<void(bool, const Json &, const std::string &)> cb) {
	http::get(
			url,
			[cb](http::Response &res) {
				if (!res.error.empty()) return cb(false, Json(), res.error);
				auto json = std::static_pointer_cast<Json>(res.payload);
				if (!json) return cb(false, Json(), "HTTP " + std::to_string(res.status));
				cb(true, *json, "");
			},
			parse_on_worker);
}

// APIResult envelope: {"result": ...} or {"error": {message, code}}.
static void unwrap(http::Response &res, const std::function<void(bool, const Json &, const std::string &)> &cb) {
	if (!res.error.empty()) return cb(false, Json(), res.error);
	auto json = std::static_pointer_cast<Json>(res.payload);
	if (!json || !json->is_object()) return cb(false, Json(), "unexpected response (HTTP " + std::to_string(res.status) + ")");
	if (json->has("error")) {
		const Json &e = (*json)["error"];
		return cb(false, Json(), e.is_object() ? e["message"].as_string("unknown error") : e.dump());
	}
	if (json->has("result")) return cb(true, (*json)["result"], "");
	cb(false, Json(), "malformed response");
}

void Stremio::call_api(const std::string &method, const Json &body, ApiCallback cb) {
	http::Request req;
	req.method = "POST";
	req.url = std::string(API_URL) + method;
	req.headers.push_back("Content-Type: application/json");
	req.body = body.dump();
	http::send(req, [cb](http::Response &res) { unwrap(res, cb); }, parse_on_worker);
}

void Stremio::call_link_api(const std::string &method, const std::string &query, ApiCallback cb) {
	http::get(std::string(LINK_API_URL) + method + "?" + query, [cb](http::Response &res) { unwrap(res, cb); },
			parse_on_worker);
}

// --- session ----------------------------------------------------------------------

void Stremio::init() {
	std::string text;
	if (platform::read_file(session_path(), text)) {
		Json s = Json::parse(text);
		if (s["authKey"].is_string()) {
			auth_key_ = s["authKey"].as_string();
			user_ = s["user"];
		}
	}
	platform::log("session: %s", logged_in() ? "signed in" : "signed out");
}

#if defined(STREMIO_DEV)
// Development builds: a sample session that never talks to an account.
void Stremio::start_demo() {
	demo_ = true;
	auth_key_ = "demo";
	user_ = Json::object();
	user_.set("email", "demo@example.com");
	auth_rev++;
	platform::log("demo mode (no account)");
}
#endif

void Stremio::save_session() {
	if (demo_) return;
	Json s = Json::object();
	s.set("authKey", auth_key_).set("user", user_);
	platform::write_file(session_path(), s.dump());
}

void Stremio::apply_auth(const Json &result) {
	auth_key_ = result["authKey"].as_string();
	user_ = result["user"];
	save_session();
	auth_rev++;
}

void Stremio::logout() {
	stop_link_login();
	if (logged_in() && !demo_) {
		Json body = Json::object(); // best effort: invalidate the key server-side
		body.set("type", "Logout").set("authKey", auth_key_);
		call_api("logout", body, [](bool, const Json &, const std::string &) {});
	}
	auth_key_.clear();
	user_ = Json();
	gen_++;
	library_.clear();
	library_loaded_ = false;
	addons_.clear();
	addons_loaded_ = false;
	notifications_.clear();
	board_.clear();
	if (!demo_) platform::remove_file(session_path());
	demo_ = false;
	auth_rev++;
	library_rev++;
	board_rev++;
}

// --- link (QR) login ----------------------------------------------------------------

void Stremio::start_link_login() {
	stop_link_login();
	const int gen = link_gen_;
	link_.active = true;
	link_.status = "Getting a code\xE2\x80\xA6";
	link_polling_ = true; // request in flight: update() must not restart it
	call_link_api("create", "type=Create", [this, gen](bool ok, const Json &result, const std::string &error) {
		if (gen != link_gen_) return;
		link_polling_ = false;
		platform::log("link create: %s", ok ? result["code"].as_string("?").c_str() : error.c_str());
		if (!ok || !result["code"].is_string()) {
			link_.status = ok ? "Unexpected link response" : error;
			link_.error = true;
			link_poll_in_ = 5; // retry
			link_.code.clear();
			return;
		}
		link_.code = result["code"].as_string();
		link_.link = result["link"].as_string("");
		link_.qr_url = result["qrcode"].as_string("");
		link_.status = "Waiting for you to approve the sign-in\xE2\x80\xA6";
		link_.error = false;
		link_age_ = 0;
		link_poll_in_ = LINK_POLL_SECONDS;
	});
}

void Stremio::stop_link_login() {
	link_gen_++;
	link_ = LinkLogin();
	link_polling_ = false;
}

void Stremio::update(double dt) {
	if (!link_.active) return;
	link_age_ += dt;
	link_poll_in_ -= dt;
	if (link_poll_in_ > 0 || link_polling_) return;
	if (link_.code.empty() || link_age_ > LINK_CODE_TTL_SECONDS) {
		start_link_login(); // first code failed, or this one expired
		return;
	}
	poll_link();
}

void Stremio::poll_link() {
	const int gen = link_gen_;
	link_polling_ = true;
	call_link_api("read", "type=Read&code=" + uri_encode(link_.code), [this, gen](bool ok, const Json &result, const std::string &) {
		if (gen != link_gen_) return;
		link_polling_ = false;
		link_poll_in_ = LINK_POLL_SECONDS;
		// Until the code is approved, `read` answers with an error: keep polling.
		if (!ok || !result["authKey"].is_string()) return;
		const std::string token = result["authKey"].as_string();
		stop_link_login();
		link_.status = "Signing in\xE2\x80\xA6";
		Json body = Json::object();
		body.set("type", "LoginWithToken").set("token", token);
		call_api("loginWithToken", body, [this](bool ok2, const Json &auth, const std::string &error) {
			if (!ok2 || !auth["authKey"].is_string()) {
				platform::log("loginWithToken failed: %s", error.c_str());
				start_link_login();
				link_.status = "Sign-in failed: " + error;
				link_.error = true;
				return;
			}
			apply_auth(auth);
			platform::log("signed in as %s", user_email().c_str());
		});
	});
}

// --- data ---------------------------------------------------------------------------

#if defined(STREMIO_DEV)
// Sample library for demo mode: in-progress items and a series watched long
// ago (so the real Cinemeta add-on reports new episodes for it).
static const char *DEMO_LIBRARY = R"([
 {"_id":"tt0903747","name":"Breaking Bad","type":"series","poster":"https://images.metahub.space/poster/medium/tt0903747/img","removed":false,"temp":false,"_mtime":"2026-10-05T20:00:00.000Z","state":{"video_id":"tt0903747:2:5","timeOffset":1200000,"duration":2820000,"lastWatched":"2026-10-05T20:00:00.000Z"}},
 {"_id":"tt15239678","name":"Dune: Part Two","type":"movie","poster":"https://images.metahub.space/poster/medium/tt15239678/img","removed":false,"temp":false,"_mtime":"2026-10-04T20:00:00.000Z","state":{"video_id":"tt15239678","timeOffset":5400000,"duration":9960000,"lastWatched":"2026-10-04T20:00:00.000Z"}},
 {"_id":"tt14452776","name":"The Bear","type":"series","poster":"https://images.metahub.space/poster/medium/tt14452776/img","removed":false,"temp":false,"_mtime":"2026-10-03T20:00:00.000Z","state":{"video_id":"tt14452776:2:3","timeOffset":0,"duration":0,"lastWatched":"2023-07-01T00:00:00.000Z"}}
])";
#endif

void Stremio::refresh() {
	if (!logged_in()) return;
#if defined(STREMIO_DEV)
	if (demo_) {
		pending_syncs_ = 1;
		library_.clear();
		const Json demo = Json::parse(DEMO_LIBRARY); // keep alive for the loop
		for (const Json &d : demo.elements()) {
			LibraryItem item = LibraryItem::from_json(d);
			library_[item.id] = item;
		}
		library_loaded_ = true;
		library_rev++;
		addons_.clear();
		add_extra_addons({ "https://v3-cinemeta.strem.io/manifest.json" }, [this] {
			addons_loaded_ = true;
			after_sync();
		});
		return;
	}
#endif
	pending_syncs_ = 2;
	sync_library();
	sync_addons();
}

void Stremio::sync_library() {
	const int gen = gen_;
	Json body = Json::object();
	body.set("authKey", auth_key_).set("collection", LIBRARY_COLLECTION).set("ids", Json::array()).set("all", true);
	call_api("datastoreGet", body, [this, gen](bool ok, const Json &result, const std::string &error) {
		if (gen != gen_) return;
		if (!ok) {
			library_error_ = error;
			platform::log("library sync failed: %s", error.c_str());
			// The API answers a revoked key with "Session does not exist".
			std::string e = error;
			for (char &c : e) c = (char)tolower((unsigned char)c);
			if (e.find("session") != std::string::npos || e.find("authkey") != std::string::npos) {
				logout(); // the key was revoked
				return;
			}
		} else {
			library_.clear();
			for (const Json &d : result.elements()) {
				if (d.is_object()) {
					LibraryItem item = LibraryItem::from_json(d);
					library_[item.id] = item;
				}
			}
			library_error_.clear();
			platform::log("library: %zu items", library_.size());
		}
		library_loaded_ = true;
		library_rev++;
		after_sync();
	});
}

void Stremio::sync_addons() {
	const int gen = gen_;
	Json body = Json::object();
	body.set("type", "AddonCollectionGet").set("authKey", auth_key_).set("update", true);
	call_api("addonCollectionGet", body, [this, gen](bool ok, const Json &result, const std::string &error) {
		if (gen != gen_) return;
		if (!ok) {
			addons_error_ = error;
			platform::log("add-ons sync failed: %s", error.c_str());
		} else {
			addons_.clear();
			for (const Json &d : result["addons"].elements()) {
				if (d.is_object()) addons_.push_back(Addon::from_json(d));
			}
			addons_error_.clear();
			platform::log("add-ons: %zu", addons_.size());
		}
		auto done = [this, gen] {
			if (gen != gen_) return;
			addons_loaded_ = true;
			after_sync();
		};
#if defined(STREMIO_DEV)
		add_extra_addons({}, done);
#else
		done();
#endif
	});
}

#if defined(STREMIO_DEV)
// Fetches `urls` plus the add-ons in STREMIO_TV_DEMO_ADDONS (comma-separated
// manifest URLs), used on top of the account's but never saved to it, and
// appends them to addons_ in that order, then calls `done`.
void Stremio::add_extra_addons(std::vector<std::string> urls, std::function<void()> done) {
	if (const char *env = getenv("STREMIO_TV_DEMO_ADDONS")) {
		const std::string list = env;
		for (size_t start = 0; start < list.size();) {
			size_t end = list.find(',', start);
			if (end == std::string::npos) end = list.size();
			std::string u = list.substr(start, end - start);
			u.erase(0, u.find_first_not_of(' '));
			u.erase(u.find_last_not_of(' ') + 1);
			if (!u.empty()) urls.push_back(u);
			start = end + 1;
		}
	}
	if (urls.empty()) {
		done();
		return;
	}
	auto remaining = std::make_shared<int>((int)urls.size());
	auto loaded = std::make_shared<std::vector<std::pair<size_t, Addon>>>();
	for (size_t i = 0; i < urls.size(); i++) {
		const std::string url = urls[i];
		fetch_json(url, [this, url, i, remaining, loaded, done](bool ok, const Json &manifest, const std::string &error) {
			if (ok) {
				Json d = Json::object();
				d.set("transportUrl", url).set("manifest", manifest);
				loaded->push_back({ i, Addon::from_json(d) });
				platform::log("extra add-on: %s", url.c_str());
			} else {
				platform::log("extra add-on %s failed: %s", url.c_str(), error.c_str());
				if (demo_) addons_error_ = error;
			}
			if (--*remaining > 0) return;
			std::sort(loaded->begin(), loaded->end(), [](const auto &a, const auto &b) { return a.first < b.first; });
			for (auto &entry : *loaded) addons_.push_back(entry.second);
			done();
		});
	}
}
#endif

void Stremio::after_sync() {
	if (--pending_syncs_ > 0) return;
	load_board();
	pull_notifications();
}

// --- Continue watching ----------------------------------------------------------------

int Stremio::notification_count(const std::string &meta_id) const {
	auto it = notifications_.find(meta_id);
	return it == notifications_.end() ? 0 : it->second.count;
}

int64_t Stremio::cw_time(const LibraryItem &item) const {
	auto it = notifications_.find(item.id);
	return (it != notifications_.end() && it->second.count > 0) ? it->second.newest : item.mtime;
}

std::vector<const LibraryItem *> Stremio::continue_watching() const {
	std::vector<const LibraryItem *> items;
	for (const auto &kv : library_) {
		const LibraryItem &item = kv.second;
		if (!item.is_live() && (item.is_in_continue_watching() || notification_count(item.id) > 0)) {
			items.push_back(&item);
		}
	}
	std::sort(items.begin(), items.end(), [this](const LibraryItem *a, const LibraryItem *b) { return cw_time(*a) > cw_time(*b); });
	if (items.size() > CATALOG_PREVIEW_SIZE) items.resize(CATALOG_PREVIEW_SIZE);
	return items;
}

// --- Notifications: port of models/ctx/update_notifications.rs -------------------------

void Stremio::pull_notifications() {
	std::vector<const LibraryItem *> pull;
	for (const auto &kv : library_) {
		if (kv.second.should_pull_notifications()) pull.push_back(&kv.second);
	}
	std::sort(pull.begin(), pull.end(), [](const LibraryItem *a, const LibraryItem *b) { return a->mtime > b->mtime; });
	if (pull.empty()) return;

	// AggrRequest::CatalogsFiltered over `lastVideosIds` catalogs.
	auto results = std::make_shared<std::map<std::string, MetaItem>>();
	auto remaining = std::make_shared<int>(0);
	std::vector<std::string> urls;
	for (const Addon &addon : addons_) {
		for (const Catalog &cat : addon.catalogs) {
			if (!cat.are_extra_names_supported({ "lastVideosIds" })) continue;
			std::vector<std::string> ids;
			for (const LibraryItem *item : pull) {
				if (cat.type == item->type && addon.catalog_resource_supports(item->id, item->type) &&
						addon.manifest_supports(item->id, item->type)) {
					ids.push_back(item->id);
				}
			}
			if (ids.empty()) continue;
			const ExtraProp *prop = cat.extra_prop("lastVideosIds");
			const size_t limit = (size_t)std::min(prop ? prop->options_limit : 1, NOTIFICATION_ITEMS_COUNT);
			if (ids.size() > limit) ids.resize(limit);
			std::sort(ids.begin(), ids.end()); // stable order improves add-on caching
			std::string joined;
			for (size_t i = 0; i < ids.size(); i++) joined += (i ? "," : "") + ids[i];
			const std::string url = addon.resource_url("catalog", cat.type, cat.id, { { "lastVideosIds", joined } });
			if (!url.empty()) urls.push_back(url);
		}
	}
	*remaining = (int)urls.size();
	const int gen = gen_;
	for (const std::string &url : urls) {
		fetch_json(url, [this, gen, results, remaining](bool ok, const Json &json, const std::string &) {
			if (gen != gen_) return;
			std::vector<MetaItem> metas;
			if (ok && metas_from_response(json, metas)) {
				for (MetaItem &m : metas) {
					if (!m.videos.empty() && !results->count(m.id)) (*results)[m.id] = m;
				}
			}
			if (--*remaining > 0) return;
			// Videos released after the item was last watched (and not in the future).
			const int64_t now = platform::unix_time();
			notifications_.clear();
			for (const auto &kv : library_) {
				const LibraryItem &item = kv.second;
				auto it = results->find(item.id);
				if (!item.should_pull_notifications() || it == results->end() || item.last_watched == 0) continue;
				Notif n;
				std::vector<std::string> seen;
				for (const Video &v : it->second.videos) {
					if (v.released > 0 && item.last_watched < v.released && v.released <= now &&
							std::find(seen.begin(), seen.end(), v.id) == seen.end()) {
						seen.push_back(v.id);
						n.count++;
						n.newest = std::max(n.newest, v.released);
					}
				}
				if (n.count > 0) notifications_[item.id] = n;
			}
			platform::log("notifications: %zu items with new episodes", notifications_.size());
			notifications_rev++;
		});
	}
}

// --- Board: port of models/catalogs_with_extra.rs with no extra --------------------------

void Stremio::load_board() {
	board_.clear();
	for (const Addon &addon : addons_) {
		for (const Catalog &cat : addon.catalogs) {
			if (!cat.are_extra_names_supported({})) continue;
			const std::string url = addon.resource_url("catalog", cat.type, cat.id);
			if (url.empty()) continue;
			BoardRow row;
			row.title = (cat.name.empty() ? cat.id : cat.name) + " - " + capitalize(cat.type);
			row.url = url;
			board_.push_back(row);
		}
	}
	board_rev++;
	const int gen = gen_;
	fetch_rows(board_, board_rev, [this, gen] { return gen == gen_; });
}

void Stremio::fetch_rows(std::vector<BoardRow> &rows, int &rev, std::function<bool()> current) {
	for (size_t i = 0; i < rows.size(); i++) {
		fetch_json(rows[i].url, [&rows, &rev, current, i](bool ok, const Json &json, const std::string &error) {
			if (!current() || i >= rows.size()) return;
			BoardRow &row = rows[i];
			row.loading = false;
			std::vector<MetaItem> metas;
			if (ok && metas_from_response(json, metas)) {
				if (metas.size() > BOARD_ROW_SIZE) metas.resize(BOARD_ROW_SIZE);
				row.items = std::move(metas);
			} else {
				row.error = ok ? "unexpected catalog response" : error;
			}
			rev++;
		});
	}
}

// --- Search (models/catalogs_with_extra.rs) ---------------------------------------------

void Stremio::search(const std::string &query) {
	search_query_ = query;
	search_.clear();
	const int gen = ++search_gen_;
	search_rev++;
	if (query.empty()) return;
	for (const Addon &addon : addons_) {
		for (const Catalog &cat : addon.catalogs) {
			if (!cat.are_extra_names_supported({ "search" })) continue;
			const std::string url = addon.resource_url("catalog", cat.type, cat.id, { { "search", query } });
			if (url.empty()) continue;
			BoardRow row;
			row.title = (cat.name.empty() ? addon.name : cat.name) + " - " + capitalize(cat.type);
			row.url = url;
			search_.push_back(row);
		}
	}
	fetch_rows(search_, search_rev, [this, gen] { return gen == search_gen_; });
}

// --- Meta details & streams (models/meta_details.rs) -----------------------------------

const LibraryItem *Stremio::library_item(const std::string &id) const {
	auto it = library_.find(id);
	return it == library_.end() ? nullptr : &it->second;
}

void Stremio::request_meta(const std::string &type, const std::string &id, MetaCallback cb) {
	auto candidates = std::make_shared<std::vector<std::string>>();
	for (const Addon &addon : addons_) {
		if (addon.is_resource_supported("meta", type, id)) {
			const std::string url = addon.resource_url("meta", type, id);
			if (!url.empty()) candidates->push_back(url);
		}
	}
	// Try add-ons in order until one answers with a meta.
	auto attempt = std::make_shared<std::function<void(size_t, std::string)>>();
	const int gen = gen_;
	*attempt = [this, candidates, attempt, cb, gen](size_t i, std::string last_error) {
		if (i >= candidates->size()) {
			cb(false, MetaItem(), last_error.empty() ? "No add-on can show details for this title" : last_error);
			return;
		}
		fetch_json((*candidates)[i], [this, candidates, attempt, cb, gen, i](bool ok, const Json &json, const std::string &error) {
			if (gen != gen_) return;
			if (ok && json["meta"].is_object()) {
				cb(true, MetaItem::from_json(json["meta"]), "");
			} else {
				(*attempt)(i + 1, ok ? "unexpected meta response" : error);
			}
		});
	};
	(*attempt)(0, "");
}

void Stremio::request_streams(const std::string &type, const std::string &video_id, StreamsCallback on_addon,
		std::function<void()> on_done) {
	std::vector<const Addon *> pending;
	for (const Addon &addon : addons_) {
		if (addon.is_resource_supported("stream", type, video_id) && !addon.resource_url("stream", type, video_id).empty()) {
			pending.push_back(&addon);
		}
	}
	if (pending.empty()) {
		on_done();
		return;
	}
	auto remaining = std::make_shared<int>((int)pending.size());
	const int gen = gen_;
	for (const Addon *addon : pending) {
		const std::string name = addon->name;
		fetch_json(addon->resource_url("stream", type, video_id), [this, gen, name, remaining, on_addon, on_done](bool ok, const Json &json, const std::string &error) {
			if (gen != gen_) return;
			std::vector<StreamItem> streams;
			std::string err = error;
			if (ok && json["streams"].is_array()) {
				for (const Json &st : json["streams"].elements()) {
					if (st.is_object()) streams.push_back(StreamItem::from_json(st, name));
				}
			} else if (ok) {
				err = "unexpected streams response";
			}
			on_addon(name, streams, err);
			if (--*remaining == 0) on_done();
		});
	}
}

void Stremio::request_subtitles(const std::string &type, const std::string &video_id, const StreamItem *stream,
		SubtitlesCallback on_addon) {
	std::vector<std::pair<std::string, std::string>> extra;
	if (stream) {
		if (!stream->filename.empty()) extra.push_back({ "filename", stream->filename });
		if (stream->video_size > 0) extra.push_back({ "videoSize", std::to_string(stream->video_size) });
		if (!stream->video_hash.empty()) extra.push_back({ "videoHash", stream->video_hash });
	}
	const int gen = gen_;
	for (const Addon &addon : addons_) {
		if (!addon.is_resource_supported("subtitles", type, video_id)) continue;
		const std::string url = addon.resource_url("subtitles", type, video_id, extra);
		if (url.empty()) continue;
		const std::string name = addon.name;
		fetch_json(url, [this, gen, name, on_addon](bool ok, const Json &json, const std::string &) {
			if (gen != gen_ || !ok || !json["subtitles"].is_array()) return;
			std::vector<SubtitleItem> subs;
			for (const Json &j : json["subtitles"].elements()) {
				if (j.is_object() && j["url"].is_string()) subs.push_back(SubtitleItem::from_json(j, name));
			}
			on_addon(subs);
		});
	}
}

// --- Player session: library parts of models/player.rs ----------------------------------

namespace {
const double PUSH_TO_LIBRARY_EVERY_SECONDS = 90.0; // PUSH_TO_LIBRARY_EVERY
const double WATCHED_THRESHOLD_COEF = 0.7;
const double CREDITS_THRESHOLD_COEF = 0.9;
} // namespace

int64_t Stremio::player_start(const std::string &meta_id, const std::string &video_id, const MetaItem *meta) {
	const LibraryItem *existing = library_item(meta_id);
	if (existing) {
		player_item_ = *existing;
	} else if (meta) {
		player_item_ = LibraryItem::from_meta(*meta, platform::unix_time());
	} else {
		player_active_ = false;
		return 0;
	}
	player_active_ = true;
	player_video_id_ = video_id;
	player_has_meta_ = meta != nullptr;
	if (meta) player_meta_ = *meta;
	player_last_push_ = platform::now();
	return (player_item_.video_id == video_id && player_item_.time_offset > 0) ? player_item_.time_offset : 0;
}

// ActionPlayer::TimeChanged
void Stremio::player_time_changed(int64_t time_ms, int64_t duration_ms) {
	if (!player_active_) return;
	LibraryItem &item = player_item_;
	const std::string video_id = item.is_live() ? item.id : player_video_id_;
	item.last_watched = platform::unix_time();
	if (item.video_id != video_id) {
		// A different video (e.g. the next episode) restarts the per-video counters.
		item.video_id = video_id;
		item.overall_time_watched += item.time_watched;
		item.time_watched = 0;
		item.flagged_watched = 0;
	} else {
		const int64_t watched = std::max<int64_t>(0, time_ms - item.time_offset);
		item.time_watched += watched;
		item.overall_time_watched += watched;
	}
	if (time_ms > item.time_offset) {
		item.time_offset = time_ms;
		item.duration = duration_ms;
	}
	if (!item.is_live() && item.flagged_watched == 0 && item.duration > 0 &&
			item.time_watched > item.duration * WATCHED_THRESHOLD_COEF) {
		item.flagged_watched = 1;
		item.times_watched++;
	}
	if (item.temp && item.times_watched == 0) item.removed = true;
	if (item.removed) item.temp = true;
	player_push_throttled();
}

// ActionPlayer::Seek: moves the offset but doesn't count as watching.
void Stremio::player_seeked(int64_t time_ms, int64_t duration_ms) {
	if (!player_active_) return;
	player_item_.last_watched = platform::unix_time();
	player_item_.time_offset = time_ms;
	player_item_.duration = duration_ms;
	player_push_throttled();
}

// Action::Unload: final state, including moving past the credits to the next
// episode (item_state_update), always pushed.
void Stremio::player_stop() {
	if (!player_active_) return;
	LibraryItem &item = player_item_;
	if (item.is_live()) {
		item.time_offset = 0;
		item.video_id = item.id;
	} else if (item.duration > 0 && item.time_offset > item.duration * CREDITS_THRESHOLD_COEF) {
		item.time_offset = 0;
		const Video *next = player_has_meta_ ? player_meta_.next_video(player_video_id_, platform::unix_time()) : nullptr;
		if (next) { // LibraryItem::advance_to_video
			item.video_id = next->id;
			item.overall_time_watched += item.time_watched;
			item.time_watched = 0;
			item.flagged_watched = 0;
			item.time_offset = 1;
		}
	}
	push_library_item(item);
	player_active_ = false;
}

void Stremio::player_push_throttled() {
	if (platform::now() - player_last_push_ >= PUSH_TO_LIBRARY_EVERY_SECONDS) {
		player_last_push_ = platform::now();
		push_library_item(player_item_);
	}
}

// Internal::UpdateLibraryItem + push_items_to_api (DatastoreCommand::Put).
void Stremio::push_library_item(LibraryItem item) {
	item.mtime = platform::unix_time();
	library_[item.id] = item;
	library_rev++;
	if (demo_ || !logged_in()) return; // demo mode never writes to an account
	Json changes = Json::array();
	changes.push(item.to_json());
	Json body = Json::object();
	body.set("authKey", auth_key_).set("collection", LIBRARY_COLLECTION).set("changes", changes);
	call_api("datastorePut", body, [](bool ok, const Json &, const std::string &error) {
		if (!ok) platform::log("couldn't save watch progress: %s", error.c_str());
	});
}
