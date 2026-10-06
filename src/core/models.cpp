#include "models.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

const char *METAHUB = "https://images.metahub.space";

std::string str(const Json &v) {
	return v.is_string() ? v.as_string() : std::string();
}

// Days since 1970-01-01 for a civil date (Howard Hinnant's algorithm).
int64_t days_from_civil(int64_t y, unsigned m, unsigned d) {
	y -= m <= 2;
	const int64_t era = (y >= 0 ? y : y - 399) / 400;
	const unsigned yoe = (unsigned)(y - era * 400);
	const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + (int64_t)doe - 719468;
}

bool prefix_ok(bool has_prefixes, const std::vector<std::string> &prefixes, const std::string &id) {
	if (!has_prefixes || prefixes.empty()) {
		return true;
	}
	for (const std::string &p : prefixes) {
		if (id.compare(0, p.size(), p) == 0) {
			return true;
		}
	}
	return false;
}

std::vector<std::string> strings(const Json &arr) {
	std::vector<std::string> out;
	for (const Json &v : arr.elements()) {
		if (v.is_string()) out.push_back(v.as_string());
	}
	return out;
}

bool contains(const std::vector<std::string> &v, const std::string &s) {
	return std::find(v.begin(), v.end(), s) != v.end();
}

} // namespace

int64_t parse_time(const Json &v) {
	if (!v.is_string()) {
		return 0;
	}
	int y, mo, d, h = 0, mi = 0, s = 0;
	if (sscanf(v.as_string().c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) < 3) {
		return 0;
	}
	return days_from_civil(y, (unsigned)mo, (unsigned)d) * 86400 + h * 3600 + mi * 60 + s;
}

std::string format_time(int64_t t) {
	int64_t days = t / 86400, rem = t % 86400;
	if (rem < 0) { rem += 86400; days--; }
	// civil_from_days
	days += 719468;
	const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
	const unsigned doe = (unsigned)(days - era * 146097);
	const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	int64_t y = (int64_t)yoe + era * 400;
	const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	const unsigned mp = (5 * doy + 2) / 153;
	const unsigned d = doy - (153 * mp + 2) / 5 + 1;
	const unsigned m = mp < 10 ? mp + 3 : mp - 9;
	y += m <= 2;
	char buf[40];
	snprintf(buf, sizeof(buf), "%04lld-%02u-%02uT%02d:%02d:%02d.000Z", (long long)y, m, d, (int)(rem / 3600),
			(int)(rem / 60 % 60), (int)(rem % 60));
	return buf;
}

std::string uri_encode(const std::string &s) {
	static const char *keep = "-_.!~*'()";
	std::string out;
	char buf[4];
	for (unsigned char c : s) {
		if (isalnum(c) || strchr(keep, c)) {
			out += (char)c;
		} else {
			snprintf(buf, sizeof(buf), "%%%02X", c);
			out += buf;
		}
	}
	return out;
}

std::vector<std::string> wide_artwork(const std::string &id, const std::string &background, const std::string &poster) {
	std::vector<std::string> urls;
	if (!background.empty()) urls.push_back(background);
	if (id.compare(0, 2, "tt") == 0) urls.push_back(std::string(METAHUB) + "/background/medium/" + id + "/img");
	if (!poster.empty()) urls.push_back(poster);
	return urls;
}

// --- LibraryItem --------------------------------------------------------------

LibraryItem LibraryItem::from_json(const Json &j) {
	LibraryItem item;
	item.raw = j;
	item.id = str(j["_id"]);
	item.name = str(j["name"]);
	item.type = str(j["type"]);
	item.poster = str(j["poster"]);
	item.removed = j["removed"].as_bool();
	item.temp = j["temp"].as_bool();
	item.mtime = parse_time(j["_mtime"]);
	item.is_live_hint = j["behaviorHints"]["isLive"].as_bool();
	item.default_video_id = str(j["behaviorHints"]["defaultVideoId"]);
	const Json &st = j["state"];
	item.time_offset = st["timeOffset"].as_int();
	item.duration = st["duration"].as_int();
	item.video_id = str(st["video_id"]);
	item.last_watched = parse_time(st["lastWatched"]);
	item.no_notif = st["noNotif"].as_bool();
	item.time_watched = st["timeWatched"].as_int();
	item.overall_time_watched = st["overallTimeWatched"].as_int();
	item.times_watched = (int)st["timesWatched"].as_int();
	item.flagged_watched = (int)st["flaggedWatched"].as_int();
	return item;
}

LibraryItem LibraryItem::from_meta(const MetaItem &meta, int64_t now) {
	const std::string t = format_time(now);
	Json state = Json::object();
	state.set("lastWatched", t).set("timeWatched", 0).set("timeOffset", 0).set("overallTimeWatched", 0)
			.set("timesWatched", 0).set("flaggedWatched", 0).set("duration", 0).set("video_id", nullptr)
			.set("watched", nullptr).set("noNotif", false);
	Json hints = Json::object();
	hints.set("defaultVideoId", meta.default_video_id.empty() ? Json() : Json(meta.default_video_id))
			.set("featuredVideoId", nullptr).set("hasScheduledVideos", false);
	Json d = Json::object();
	d.set("_id", meta.id).set("name", meta.name).set("type", meta.type)
			.set("poster", meta.poster.empty() ? Json() : Json(meta.poster)).set("posterShape", "poster")
			.set("removed", true).set("temp", true).set("_ctime", t).set("_mtime", t).set("state", state)
			.set("behaviorHints", hints);
	return from_json(d);
}

Json LibraryItem::to_json() const {
	Json d = raw.is_object() ? raw : Json::object();
	d.set("_id", id).set("name", name).set("type", type).set("removed", removed).set("temp", temp)
			.set("_mtime", format_time(mtime));
	Json state = raw["state"].is_object() ? raw["state"] : Json::object();
	state.set("timeOffset", time_offset).set("duration", duration)
			.set("video_id", video_id.empty() ? Json() : Json(video_id))
			.set("lastWatched", last_watched > 0 ? Json(format_time(last_watched)) : Json())
			.set("timeWatched", time_watched).set("overallTimeWatched", overall_time_watched)
			.set("timesWatched", times_watched).set("flaggedWatched", flagged_watched);
	d.set("state", state);
	return d;
}

std::string LibraryItem::resume_video_id() const {
	if (!video_id.empty()) return video_id;
	if (!default_video_id.empty()) return default_video_id;
	return type != "series" ? id : "";
}

bool LibraryItem::should_pull_notifications() const {
	return !no_notif && !is_live() && type != "other" && type != "movie" && default_video_id.empty() && !removed && !temp;
}

float LibraryItem::progress() const {
	if (time_offset > 0 && duration > 0) {
		return std::min(1.0f, (float)time_offset / (float)duration);
	}
	return 0.0f;
}

int LibraryItem::minutes_left() const {
	if (duration <= 0) return -1;
	return (int)std::max<int64_t>(0, (int64_t)std::ceil((duration - time_offset) / 60000.0));
}

std::string LibraryItem::episode_label() const {
	if (type != "series" || video_id.empty()) return "";
	// "tt0903747:1:3" -> "S1 · E3"
	const size_t b = video_id.rfind(':');
	if (b == std::string::npos || b == 0) return "";
	const size_t a = video_id.rfind(':', b - 1);
	if (a == std::string::npos) return "";
	const std::string season = video_id.substr(a + 1, b - a - 1), episode = video_id.substr(b + 1);
	auto numeric = [](const std::string &s) { return !s.empty() && std::all_of(s.begin(), s.end(), ::isdigit); };
	if (!numeric(season) || !numeric(episode)) return "";
	return "S" + season + " \xC2\xB7 E" + episode;
}

// --- MetaItem -----------------------------------------------------------------

MetaItem MetaItem::from_json(const Json &j) {
	MetaItem m;
	m.id = str(j["id"]);
	m.type = str(j["type"]);
	m.name = str(j["name"]);
	m.poster = str(j["poster"]);
	m.background = str(j["background"]);
	m.release_info = str(j["releaseInfo"]);
	m.description = str(j["description"]);
	m.default_video_id = str(j["behaviorHints"]["defaultVideoId"]);
	for (const Json &v : j["videos"].elements()) {
		Video video;
		video.id = str(v["id"]);
		video.title = v.has("title") ? str(v["title"]) : str(v["name"]);
		video.released = parse_time(v["released"]);
		video.season = (int)v["season"].as_int();
		video.episode = (int)(v.has("episode") ? v["episode"].as_int() : v["number"].as_int());
		m.videos.push_back(video);
	}
	std::sort(m.videos.begin(), m.videos.end(), [](const Video &a, const Video &b) {
		if (a.season != b.season) return a.season < b.season;
		if (a.episode != b.episode) return a.episode < b.episode;
		return a.released < b.released;
	});
	return m;
}

bool metas_from_response(const Json &result, std::vector<MetaItem> &out) {
	const char *key = result.has("metas") ? "metas" : result.has("metasDetailed") ? "metasDetailed" : nullptr;
	if (!key) return false;
	for (const Json &m : result[key].elements()) { // null means empty
		if (m.is_object()) out.push_back(MetaItem::from_json(m));
	}
	return true;
}

// --- Add-ons --------------------------------------------------------------------

const ExtraProp *Catalog::extra_prop(const std::string &n) const {
	for (const ExtraProp &e : extra) {
		if (e.name == n) return &e;
	}
	return nullptr;
}

bool Catalog::are_extra_names_supported(const std::vector<std::string> &names) const {
	for (const std::string &n : names) {
		if (!extra_prop(n)) return false;
	}
	for (const ExtraProp &e : extra) {
		if (e.is_required && !contains(names, e.name)) return false;
	}
	return true;
}

Addon Addon::from_json(const Json &d) {
	Addon a;
	a.transport_url = str(d["transportUrl"]);
	const Json &m = d["manifest"];
	a.id = str(m["id"]);
	a.name = m.has("name") ? str(m["name"]) : a.id;
	a.types = strings(m["types"]);
	a.has_id_prefixes = m["idPrefixes"].is_array();
	a.id_prefixes = strings(m["idPrefixes"]);
	a.resources = m["resources"];
	for (const Json &c : m["catalogs"].elements()) {
		Catalog cat;
		cat.type = str(c["type"]);
		cat.id = str(c["id"]);
		cat.name = str(c["name"]);
		if (c["extra"].is_array()) { // full form
			for (const Json &e : c["extra"].elements()) {
				if (!e["name"].is_string()) continue;
				cat.extra.push_back({ e["name"].as_string(), e["isRequired"].as_bool(), (int)e["optionsLimit"].as_int(1) });
			}
		} else { // legacy short form
			const std::vector<std::string> required = strings(c["extraRequired"]);
			for (const std::string &n : strings(c["extraSupported"])) {
				cat.extra.push_back({ n, contains(required, n), 1 });
			}
		}
		a.catalogs.push_back(cat);
	}
	return a;
}

bool Addon::catalog_resource_supports(const std::string &item_id, const std::string &item_type) const {
	for (const Json &r : resources.elements()) {
		if (r.is_string() && r.as_string() == "catalog") return true;
		if (r.is_object() && str(r["name"]) == "catalog") {
			const bool type_ok = r["types"].is_null() || contains(strings(r["types"]), item_type);
			if (type_ok && prefix_ok(r["idPrefixes"].is_array(), strings(r["idPrefixes"]), item_id)) return true;
		}
	}
	return false;
}

bool Addon::manifest_supports(const std::string &item_id, const std::string &item_type) const {
	return prefix_ok(has_id_prefixes, id_prefixes, item_id) && contains(types, item_type);
}

bool Addon::is_resource_supported(const std::string &resource, const std::string &item_type, const std::string &item_id) const {
	for (const Json &r : resources.elements()) {
		if (r.is_string() && r.as_string() == resource) {
			return contains(types, item_type) && prefix_ok(has_id_prefixes, id_prefixes, item_id);
		}
		if (r.is_object() && str(r["name"]) == resource) {
			// A full resource without `types` supports no types.
			return r["types"].is_array() && contains(strings(r["types"]), item_type) &&
					prefix_ok(r["idPrefixes"].is_array(), strings(r["idPrefixes"]), item_id);
		}
	}
	return false;
}

std::string Addon::resource_url(const std::string &resource, const std::string &type, const std::string &id,
		const std::vector<std::pair<std::string, std::string>> &extra) const {
	static const std::string manifest = "/manifest.json";
	if (transport_url.size() < manifest.size() ||
			transport_url.compare(transport_url.size() - manifest.size(), manifest.size(), manifest) != 0) {
		return ""; // legacy /stremio/v1 transports are not supported
	}
	std::string path = "/" + uri_encode(resource) + "/" + uri_encode(type) + "/" + uri_encode(id);
	if (!extra.empty()) {
		path += "/";
		for (size_t i = 0; i < extra.size(); i++) {
			if (i) path += "&";
			path += uri_encode(extra[i].first) + "=" + uri_encode(extra[i].second);
		}
	}
	return transport_url.substr(0, transport_url.size() - manifest.size()) + path + ".json";
}

// --- MetaItem helpers ---------------------------------------------------------------

const Video *MetaItem::find_video(const std::string &video_id) const {
	for (const Video &v : videos) {
		if (v.id == video_id) return &v;
	}
	return nullptr;
}

const Video *MetaItem::next_video(const std::string &video_id, int64_t now) const {
	if (type == "tv") return nullptr;
	for (size_t i = 0; i + 1 < videos.size(); i++) {
		if (videos[i].id != video_id) continue;
		const Video &cur = videos[i], &next = videos[i + 1];
		const bool released = next.released == 0 || next.released <= now;
		return ((next.season != 0 || cur.season == next.season) && released) ? &next : nullptr;
	}
	return nullptr;
}

std::string MetaItem::direct_video_id() const {
	if (!default_video_id.empty()) return default_video_id;
	if (videos.empty() && type != "series") return id;
	if (videos.size() == 1) return videos[0].id;
	return "";
}

std::string MetaItem::video_label(const Video &v) {
	if (v.season > 0 || v.episode > 0) {
		return "S" + std::to_string(v.season) + " \xC2\xB7 E" + std::to_string(v.episode) + (v.title.empty() ? "" : "  " + v.title);
	}
	return v.title;
}

// --- StreamItem -----------------------------------------------------------------------

StreamItem StreamItem::from_json(const Json &j, const std::string &addon) {
	StreamItem s;
	s.addon_name = addon;
	s.name = str(j["name"]);
	s.description = j.has("description") ? str(j["description"]) : str(j["title"]);
	if (j["url"].is_string()) {
		s.source = URL;
		s.url = j["url"].as_string();
	} else if (j["ytId"].is_string()) {
		s.source = YOUTUBE;
	} else if (j["infoHash"].is_string()) {
		s.source = TORRENT;
	} else if (j["externalUrl"].is_string()) {
		s.source = EXTERNAL;
	}
	for (const auto &kv : j["behaviorHints"]["proxyHeaders"]["request"].items()) {
		if (kv.second.is_string()) s.request_headers.push_back(kv.first + ": " + kv.second.as_string());
	}
	const Json &hints = j["behaviorHints"];
	s.filename = str(hints["filename"]);
	s.video_hash = str(hints["videoHash"]);
	s.video_size = (uint64_t)hints["videoSize"].as_int(0);
	return s;
}

SubtitleItem SubtitleItem::from_json(const Json &j, const std::string &addon) {
	SubtitleItem s;
	s.addon_name = addon;
	s.id = j["id"].is_number() ? std::to_string(j["id"].as_int()) : str(j["id"]);
	s.url = str(j["url"]);
	s.lang = str(j["lang"]);
	s.release = str(j["movieReleaseName"]);
	if (s.release.empty()) s.release = str(j["subtitleFileName"]);
	return s;
}

std::string StreamItem::playable_url() const {
	if (source == URL && (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0)) return url;
	return "";
}

std::string StreamItem::unsupported_reason() const {
	switch (source) {
		case TORRENT: return "Torrent: needs a streaming server";
		case YOUTUBE: return "YouTube isn't supported yet";
		case EXTERNAL: return "Opens in an external app";
		case URL: return playable_url().empty() ? "Unsupported link" : "";
		default: return "Unsupported stream type";
	}
}
