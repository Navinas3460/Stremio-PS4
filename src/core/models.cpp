#include "models.h"

#include "../torrent/magnet.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

// Emoji labels torrent add-ons use for their stats line.
const char *const SEEDERS_MARK = "\xF0\x9F\x91\xA4"; // 👤
const char *const SIZE_MARK = "\xF0\x9F\x92\xBE";    // 💾
const char *const ORIGIN_MARK = "\xE2\x9A\x99";       // ⚙ (often followed by U+FE0F)

bool is_stats_line(const std::string &line) {
	return line.find(SEEDERS_MARK) != std::string::npos || line.find(SIZE_MARK) != std::string::npos;
}

// The text after `mark` up to the next mark (or the line's end), trimmed.
std::string after_mark(const std::string &line, const char *mark) {
	size_t i = line.find(mark);
	if (i == std::string::npos) return "";
	i += strlen(mark);
	if (line.compare(i, 3, "\xEF\xB8\x8F") == 0) i += 3; // variation selector
	size_t end = line.size();
	for (const char *m : { SEEDERS_MARK, SIZE_MARK, ORIGIN_MARK }) {
		const size_t k = line.find(m, i);
		if (k != std::string::npos) end = std::min(end, k);
	}
	std::string s = line.substr(i, end - i);
	const size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
	return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

void parse_stats(const std::string &text, int &seeders, std::string &size, std::string &origin) {
	size_t start = 0;
	while (start <= text.size()) {
		size_t end = text.find('\n', start);
		if (end == std::string::npos) end = text.size();
		const std::string line = text.substr(start, end - start);
		if (is_stats_line(line)) {
			const std::string n = after_mark(line, SEEDERS_MARK);
			if (!n.empty() && isdigit((unsigned char)n[0])) seeders = atoi(n.c_str());
			size = after_mark(line, SIZE_MARK);
			origin = after_mark(line, ORIGIN_MARK);
			return;
		}
		start = end + 1;
	}
}

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
	if (arr.is_string() && !arr.as_string().empty()) {
		out.push_back(arr.as_string());
		return out;
	}
	for (const Json &v : arr.elements()) {
		if (v.is_string() && !v.as_string().empty()) out.push_back(v.as_string());
	}
	return out;
}

bool contains(const std::vector<std::string> &v, const std::string &s) {
	return std::find(v.begin(), v.end(), s) != v.end();
}

std::string to_lower_ascii(std::string s) {
	for (char &c : s) c = (char)tolower((unsigned char)c);
	return s;
}

bool name_matches(const std::string &entry, const std::string &query) {
	if (entry.empty() || query.empty()) return false;
	const std::string a = to_lower_ascii(entry);
	const std::string b = to_lower_ascii(query);
	return a == b || a.find(b) != std::string::npos;
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

std::vector<std::string> LibraryItem::poster_images() const {
	std::vector<std::string> urls;
	if (!poster.empty()) urls.push_back(poster);
	if (id.compare(0, 2, "tt") == 0) urls.push_back(std::string(METAHUB) + "/poster/medium/" + id + "/img");
	return urls;
}

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
	if (!poster.empty()) d.set("poster", poster);
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
	if (m.id.empty()) m.id = str(j["imdb_id"]);
	m.imdb_id = str(j["imdb_id"]);
	if (m.imdb_id.empty() && m.id.compare(0, 2, "tt") == 0) m.imdb_id = m.id;
	m.type = str(j["type"]);
	m.name = str(j["name"]);
	m.poster = str(j["poster"]);
	if (m.poster.empty() && m.id.compare(0, 2, "tt") == 0) {
		m.poster = std::string(METAHUB) + "/poster/medium/" + m.id + "/img";
	}
	m.background = str(j["background"]);
	m.release_info = str(j["releaseInfo"]);
	if (m.release_info.empty()) m.release_info = str(j["year"]);
	m.description = str(j["description"]);
	m.default_video_id = str(j["behaviorHints"]["defaultVideoId"]);

	// Ratings & extended metadata fields from Cinemeta / add-ons
	m.imdb_rating = str(j["imdbRating"]);
	if (m.imdb_rating == "N/A" || m.imdb_rating == "0" || m.imdb_rating == "0.0") m.imdb_rating.clear();
	m.runtime = str(j["runtime"]);
	if (m.runtime == "N/A") m.runtime.clear();
	m.awards = str(j["awards"]);
	if (m.awards == "N/A") m.awards.clear();
	m.country = str(j["country"]);
	if (m.country == "N/A") m.country.clear();

	m.genres = strings(j["genres"]);
	if (m.genres.empty()) m.genres = strings(j["genre"]);
	m.cast = strings(j["cast"]);
	m.directors = strings(j["director"]);
	if (m.directors.empty()) m.directors = strings(j["directors"]);
	m.writers = strings(j["writer"]);
	if (m.writers.empty()) m.writers = strings(j["writers"]);

	// Also inspect Stremio v3 `links` array (Cast, Directors, Writers, Genres, imdb)
	for (const Json &link : j["links"].elements()) {
		if (!link.is_object()) continue;
		const std::string cat = to_lower_ascii(str(link["category"]));
		const std::string name = str(link["name"]);
		if (name.empty()) continue;
		if (cat == "imdb" && m.imdb_rating.empty()) {
			m.imdb_rating = name;
		} else if (cat == "cast" && !contains(m.cast, name)) {
			m.cast.push_back(name);
		} else if ((cat == "directors" || cat == "director") && !contains(m.directors, name)) {
			m.directors.push_back(name);
		} else if ((cat == "writers" || cat == "writer") && !contains(m.writers, name)) {
			m.writers.push_back(name);
		} else if ((cat == "genres" || cat == "genre") && !contains(m.genres, name)) {
			m.genres.push_back(name);
		}
	}

	for (const Json &v : j["videos"].elements()) {
		Video video;
		video.id = str(v["id"]);
		video.title = v.has("title") ? str(v["title"]) : str(v["name"]);
		video.released = parse_time(v["released"]);
		if (v["released"].is_string() && v["released"].as_string().size() >= 10) {
			video.released_date = v["released"].as_string().substr(0, 10);
		}
		video.season = (int)v["season"].as_int();
		video.episode = (int)(v.has("episode") ? v["episode"].as_int() : v["number"].as_int());
		video.overview = v.has("overview") ? str(v["overview"]) : str(v["description"]);
		video.thumbnail = str(v["thumbnail"]);
		if (video.thumbnail.empty() && !m.imdb_id.empty() && video.season > 0 && video.episode > 0) {
			video.thumbnail = "https://episodes.metahub.space/" + m.imdb_id + "/" +
					std::to_string(video.season) + "/" + std::to_string(video.episode) + "/w780.jpg";
		}
		m.videos.push_back(video);
	}
	std::sort(m.videos.begin(), m.videos.end(), [](const Video &a, const Video &b) {
		if (a.season != b.season) return a.season < b.season;
		if (a.episode != b.episode) return a.episode < b.episode;
		return a.released < b.released;
	});
	return m;
}

void MetaItem::apply_ratings(const RatingsInfo &info) {
	if (!info.imdb_rating.empty() && imdb_rating.empty()) imdb_rating = info.imdb_rating;
	if (!info.rotten_tomatoes.empty()) rotten_tomatoes = info.rotten_tomatoes;
	if (!info.metacritic.empty()) metacritic = info.metacritic;
	if (!info.rated.empty()) rated = info.rated;
	if (!info.runtime.empty() && runtime.empty()) runtime = info.runtime;
	if (!info.awards.empty() && awards.empty()) awards = info.awards;
	if (!info.box_office.empty() && box_office.empty()) box_office = info.box_office;
	if (!info.full_plot.empty() && (description.empty() || description.size() < info.full_plot.size() || description.find("...") != std::string::npos)) {
		description = info.full_plot;
	}
	for (const std::string &c : info.cast) {
		if (!contains(cast, c)) cast.push_back(c);
	}
	for (const std::string &d : info.directors) {
		if (!contains(directors, d)) directors.push_back(d);
	}
	for (const std::string &w : info.writers) {
		if (!contains(writers, w)) writers.push_back(w);
	}
}

bool MetaItem::involves_person(const std::string &person_name) const {
	for (const std::string &c : cast) if (name_matches(c, person_name)) return true;
	for (const std::string &d : directors) if (name_matches(d, person_name)) return true;
	for (const std::string &w : writers) if (name_matches(w, person_name)) return true;
	return false;
}

std::string MetaItem::person_role(const std::string &person_name) const {
	std::vector<std::string> roles;
	for (const std::string &d : directors) {
		if (name_matches(d, person_name)) { roles.push_back("Director"); break; }
	}
	for (const std::string &w : writers) {
		if (name_matches(w, person_name)) { roles.push_back("Writer"); break; }
	}
	for (const std::string &c : cast) {
		if (name_matches(c, person_name)) { roles.push_back("Cast"); break; }
	}
	std::string out;
	for (size_t i = 0; i < roles.size(); i++) out += (i ? " / " : "") + roles[i];
	return out;
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
	a.raw = d;
	a.transport_url = str(d["transportUrl"]);
	const Json &m = d["manifest"].is_object() ? d["manifest"] : d;
	a.id = str(m["id"]);
	a.name = m.has("name") ? str(m["name"]) : a.id;
	a.version = str(m["version"]);
	a.description = str(m["description"]);
	a.logo = str(m["logo"]);
	a.types = strings(m["types"]);
	a.has_id_prefixes = m["idPrefixes"].is_array();
	a.id_prefixes = strings(m["idPrefixes"]);
	a.resources = m["resources"];
	a.is_protected = d["flags"]["protected"].as_bool() || a.id == "com.linvo.cinemeta";
	a.is_official = d["flags"]["official"].as_bool() || a.id == "com.linvo.cinemeta";
	a.configurable = m["behaviorHints"]["configurable"].as_bool();
	a.configuration_required = m["behaviorHints"]["configurationRequired"].as_bool();
	for (const Json &c : m["catalogs"].elements()) {
		Catalog cat;
		cat.type = str(c["type"]);
		cat.id = str(c["id"]);
		cat.name = str(c["name"]);
		if (c["extra"].is_array()) { // full form
			for (const Json &e : c["extra"].elements()) {
				if (!e["name"].is_string()) continue;
				std::vector<std::string> opts = strings(e["options"]);
				if (e["name"].as_string() == "genre" && opts.empty()) opts = strings(c["genres"]);
				cat.extra.push_back({ e["name"].as_string(), e["isRequired"].as_bool(), (int)e["optionsLimit"].as_int(1),
						std::move(opts) });
			}
		} else { // legacy short form
			const std::vector<std::string> required = strings(c["extraRequired"]);
			for (const std::string &n : strings(c["extraSupported"])) {
				std::vector<std::string> opts = n == "genre" ? strings(c["genres"]) : std::vector<std::string>();
				cat.extra.push_back({ n, contains(required, n), 1, std::move(opts) });
			}
		}
		a.catalogs.push_back(cat);
	}
	return a;
}

Json Addon::to_descriptor_json() const {
	if (raw.is_object() && raw.has("transportUrl") && raw.has("manifest")) {
		Json d = raw;
		if (!d.has("flags") || !d["flags"].is_object()) {
			Json flags = Json::object();
			flags.set("official", is_official).set("protected", is_protected);
			d.set("flags", flags);
		}
		return d;
	}
	Json d = Json::object();
	d.set("transportUrl", transport_url);
	d.set("transportName", "http");
	if (raw.is_object()) {
		d.set("manifest", raw);
	} else {
		Json m = Json::object();
		m.set("id", id).set("name", name).set("version", version).set("description", description);
		if (!logo.empty()) m.set("logo", logo);
		d.set("manifest", m);
	}
	Json flags = Json::object();
	flags.set("official", is_official).set("protected", is_protected);
	d.set("flags", flags);
	return d;
}

std::vector<std::string> Addon::resource_names() const {
	std::vector<std::string> out;
	if (!catalogs.empty()) out.push_back("catalog");
	for (const Json &r : resources.elements()) {
		std::string name;
		if (r.is_string()) name = r.as_string();
		else if (r.is_object()) name = str(r["name"]);
		if (!name.empty() && !contains(out, name)) out.push_back(name);
	}
	return out;
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
		uint8_t hash[20];
		if (torrent::parse_info_hash(j["infoHash"].as_string(), hash)) {
			s.info_hash.clear();
			static const char *hex = "0123456789abcdef";
			for (uint8_t b : hash) {
				s.info_hash += hex[b >> 4];
				s.info_hash += hex[b & 15];
			}
		}
		if (j["fileIdx"].is_number()) s.file_idx = (int)j["fileIdx"].as_int(-1);
		// "sources": "tracker:<url>" entries (and "dht:<hash>", which the engine does anyway).
		for (const Json &src : j["sources"].elements()) {
			if (src.is_string() && src.as_string().rfind("tracker:", 0) == 0) s.trackers.push_back(src.as_string().substr(8));
		}
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
	parse_stats(s.description, s.seeders, s.size_label, s.origin);
	return s;
}

std::string StreamItem::details() const {
	std::string out;
	size_t start = 0;
	while (start <= description.size()) {
		size_t end = description.find('\n', start);
		if (end == std::string::npos) end = description.size();
		std::string line = description.substr(start, end - start);
		if (is_stats_line(line)) line = origin;
		if (!line.empty()) out += (out.empty() ? "" : "\n") + line;
		start = end + 1;
	}
	return out;
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
	if (source == TORRENT && !info_hash.empty()) return torrent::make_magnet(info_hash, file_idx, trackers);
	return "";
}

std::string StreamItem::unsupported_reason() const {
	switch (source) {
		case TORRENT: return info_hash.empty() ? "Torrent: invalid info-hash" : "";
		case YOUTUBE: return "YouTube isn't supported yet";
		case EXTERNAL: return "Opens in an external app";
		case URL: return playable_url().empty() ? "Unsupported link" : "";
		default: return "Unsupported stream type";
	}
}

bool StreamItem::is_compatible() const {
	if (source != URL && source != TORRENT) {
		return false;
	}
	std::string lower = name + " " + description + " " + filename + " " + url;
	for (auto &c : lower) c = tolower((unsigned char)c);
	
	// Filter out all 4K / 2160p / UHD / 8K / 3D / sample / trailer / live streams so they never waste time loading
	if (lower.find("2160p") != std::string::npos ||
	    lower.find("2160i") != std::string::npos ||
	    lower.find("4k") != std::string::npos ||
	    lower.find("uhd") != std::string::npos ||
	    lower.find("3840x") != std::string::npos ||
	    lower.find("4096x") != std::string::npos ||
	    lower.find("4320p") != std::string::npos ||
	    lower.find(" 8k") != std::string::npos ||
	    lower.find(".m3u8") != std::string::npos ||
	    lower.find("rtmp://") != std::string::npos ||
	    lower.find("rtsp://") != std::string::npos ||
	    lower.find("sample") != std::string::npos ||
	    lower.find("trailer") != std::string::npos ||
	    lower.find("extras") != std::string::npos ||
	    lower.find("featurette") != std::string::npos ||
	    lower.find("hsbs") != std::string::npos ||
	    lower.find("3d sbs") != std::string::npos ||
	    lower.find(".3d.") != std::string::npos) {
		return false;
	}

	// Filter out oversized (> 18 GB) unlabeled 4K / BluRay Remux files
	if (video_size > (18ULL << 30)) {
		return false;
	}
	
	if (!filename.empty()) {
		std::string fn = filename;
		for (auto &c : fn) c = tolower((unsigned char)c);
		if (fn.rfind(".rar") != std::string::npos ||
		    fn.rfind(".zip") != std::string::npos ||
		    fn.rfind(".7z") != std::string::npos ||
		    fn.rfind(".iso") != std::string::npos ||
		    fn.rfind(".exe") != std::string::npos ||
		    fn.rfind(".txt") != std::string::npos ||
		    fn.rfind(".nfo") != std::string::npos ||
		    fn.rfind(".jpg") != std::string::npos ||
		    fn.rfind(".png") != std::string::npos ||
		    fn.rfind(".m3u8") != std::string::npos) {
			return false;
		}
	}
	return true;
}
