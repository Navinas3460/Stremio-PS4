// Stremio data types: C++ ports of stremio-core's LibraryItem, MetaItem and
// add-on Descriptor/Manifest, Stream and Subtitles.
#pragma once

#include "json.h"

#include <cstdint>
#include <string>
#include <vector>

// RFC 3339 ("2024-05-01T12:34:56.789Z") -> unix seconds, 0 if missing.
int64_t parse_time(const Json &v);
std::string format_time(int64_t unix_seconds);

// Percent-encodes like stremio-core's URI_COMPONENT_ENCODE_SET.
std::string uri_encode(const std::string &s);

std::vector<std::string> wide_artwork(const std::string &id, const std::string &background, const std::string &poster);
// Portrait artwork, best first: the poster, then Metahub's for IMDb ids.
std::vector<std::string> poster_artwork(const std::string &id, const std::string &poster);

// stremio-core types::library::LibraryItem
struct LibraryItem {
	std::string id, name, type, poster;
	bool removed = false, temp = false;
	int64_t mtime = 0;
	bool is_live_hint = false;
	std::string default_video_id;
	// LibraryItemState (times in ms)
	int64_t time_offset = 0, duration = 0;
	std::string video_id;
	int64_t last_watched = 0; // unix seconds
	bool no_notif = false;
	int64_t time_watched = 0, overall_time_watched = 0;
	int times_watched = 0, flagged_watched = 0;
	Json raw;

	static LibraryItem from_json(const Json &j);
	// LibraryItem::from(&MetaItemPreview): a temporary item for something not
	// in the library yet (removed + temp: shows only in Continue Watching).
	static LibraryItem from_meta(const struct MetaItem &meta, int64_t now);
	// Back to the API shape, keeping unmodelled fields from `raw`.
	Json to_json() const;
	// The video to resume: last watched, else the default, else the item.
	std::string resume_video_id() const;
	bool is_live() const { return is_live_hint || type == "tv"; }
	bool is_in_continue_watching() const { return type != "other" && (!removed || temp) && time_offset > 0; }
	bool should_pull_notifications() const;
	float progress() const;          // 0..1
	int minutes_left() const;        // -1 if unknown
	std::string episode_label() const; // "S1 · E3" for series
	std::vector<std::string> landscape_images() const { return wide_artwork(id, "", poster); }
	std::vector<std::string> poster_images() const { return poster_artwork(id, poster); }
};

struct Video {
	std::string id, title;
	int64_t released = 0;
	int season = 0, episode = 0;
};

// stremio-core MetaItemPreview / MetaItem
struct MetaItem {
	// MetaItem::next_video: the following released video, never crossing from
	// a regular season into specials (season 0). nullptr if none.
	const Video *next_video(const std::string &video_id, int64_t now) const;
	const Video *find_video(const std::string &video_id) const;
	// The video to play without choosing an episode, or "".
	std::string direct_video_id() const;
	static std::string video_label(const Video &v); // "S1 · E3  Title"

	std::string id, type, name, poster, background, release_info, description, default_video_id;
	std::vector<Video> videos; // sorted by season, episode

	static MetaItem from_json(const Json &j);
	std::vector<std::string> landscape_images() const { return wide_artwork(id, background, poster); }
	std::vector<std::string> poster_images() const { return poster_artwork(id, poster); }
};

// Items of a catalog ResourceResponse: {"metas": [...]} or {"metasDetailed": [...]}.
// Returns false if the response is neither.
bool metas_from_response(const Json &result, std::vector<MetaItem> &out);

struct ExtraProp {
	std::string name;
	bool is_required = false;
	int options_limit = 1;
	std::vector<std::string> options; // e.g. a catalog's genres or years
};

struct Catalog {
	std::string type, id, name;
	std::vector<ExtraProp> extra;
	const ExtraProp *extra_prop(const std::string &name) const;
	// ManifestCatalog::is_extra_supported for a list of extra names.
	bool are_extra_names_supported(const std::vector<std::string> &names) const;
};

// stremio-core Descriptor + the parts of Manifest we use.
struct Addon {
	std::string transport_url, id, name;
        std::string version, description, logo;
        // Keep the original descriptor intact when editing the collection.
        Json descriptor;
        Json addon_catalogs;
        bool is_official = false;
        bool is_protected = false;
        bool configurable = false;
        bool configuration_required = false;
	std::vector<std::string> types;
	bool has_id_prefixes = false;
	std::vector<std::string> id_prefixes;
	Json resources;
	std::vector<Catalog> catalogs;

	static Addon from_json(const Json &descriptor);
	bool catalog_resource_supports(const std::string &item_id, const std::string &item_type) const;
	bool manifest_supports(const std::string &item_id, const std::string &item_type) const;
	bool is_resource_supported(const std::string &resource, const std::string &item_type, const std::string &item_id) const;
	// e.g. catalog/series/last-videos/lastVideosIds=tt1%2Ctt2.json; "" for legacy transports.
	std::string resource_url(const std::string &resource, const std::string &type, const std::string &id,
			const std::vector<std::pair<std::string, std::string>> &extra = {}) const;
};

// stremio-core Subtitles (an add-on's subtitle file for a video).
struct SubtitleItem {
	std::string id, url, lang, addon_name;
	std::string release; // file / release name, when the add-on gives one
	static SubtitleItem from_json(const Json &j, const std::string &addon);
};

// stremio-core Stream: direct http(s) `url` sources, and torrents
// (`infoHash`), which play through the built-in engine (src/torrent) as
// magnet links.
struct StreamItem {
	enum Source { URL, YOUTUBE, TORRENT, EXTERNAL, OTHER };
	Source source = OTHER;
	std::string url, name, description, addon_name;
	// Torrents: the info-hash (lower-case hex), the file (-1 = the engine
	// picks the largest video) and the add-on's trackers ("sources").
	std::string info_hash;
	int file_idx = -1;
	std::vector<std::string> trackers;
	std::vector<std::string> request_headers; // behaviorHints.proxyHeaders.request, "Name: value"
	// behaviorHints passed to subtitle add-ons (matching subtitles to the file).
	std::string filename, video_hash;
	uint64_t video_size = 0;

	static StreamItem from_json(const Json &j, const std::string &addon);
	std::string playable_url() const;
	std::string unsupported_reason() const;

	// Torrent add-ons (Torrentio, ...) put the swarm's stats in the stream's
	// text as an emoji-labelled line: "👤 56 💾 859.37 MB ⚙️ YTS". from_json
	// reads it; details() is the description with that line reduced to the
	// origin ("YTS"; seeders and size are shown on their own).
	int seeders = -1; // -1 = not given
	std::string size_label, origin;
	std::string details() const;
};
