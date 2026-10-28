// The app-facing Stremio facade: session, link (QR) login, library, add-ons,
// Continue Watching, new-episode notifications and the catalog board.
// A C++ port of the parts of stremio-core the app needs.
//
// Network results arrive on the main thread (http::poll). Screens read the
// public state each frame and compare the *_rev counters to notice changes.
#pragma once

#include "json.h"
#include "models.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

struct BoardRow {
	std::string title, url;
	std::vector<MetaItem> items;
	bool loading = true;
	std::string error;
};

// Discover (stremio-core CatalogWithFilters): one add-on catalog at a time,
// picked by type, catalog and its filter (genre, year...), loaded a page at
// a time.
struct Discover {
	std::vector<std::string> types;          // movie and series first, then A-Z
	std::string type;
	std::vector<std::string> catalogs;       // the type's catalogs (names)
	int catalog = 0;
	std::vector<std::string> filter_options; // the catalog's genres, years...; "" = All
	std::string filter;
	std::vector<MetaItem> items;
	bool loading = false, has_more = false;
	std::string error;
};

struct LinkLogin {
	bool active = false;
	std::string code, link, qr_url;
	std::string status;
	bool error = false;
};

class Stremio {
public:
	void init(); // loads the saved session

#if defined(STREMIO_DEV)
	// Desktop testing without an account: no Stremio API calls; a small
	// sample library and the public Cinemeta add-on. Never saves a session.
	void start_demo();
#endif

	bool logged_in() const { return !auth_key_.empty(); }
	std::string user_email() const { return user_["email"].as_string(""); }
	void logout();

	// QR / link login (stremio-core models/link.rs).
	void start_link_login();
	void stop_link_login();
	const LinkLogin &link() const { return link_; }

	// Library + add-ons, then the board and notifications.
	void refresh();
	bool account_sync_busy() const { return sync_in_progress_; }

	// stremio-core ContinueWatchingPreview: in-progress items plus items with
	// new episodes, newest first.
	std::vector<const LibraryItem *> continue_watching() const;
	int notification_count(const std::string &meta_id) const;

	const std::vector<BoardRow> &board() const { return board_; }

	// Search (stremio-core CatalogsWithExtra with the "search" extra): one row
	// per add-on catalog that supports searching. "" clears the results.
	void search(const std::string &query);
	const std::vector<BoardRow> &search_results() const { return search_; }
	const std::string &search_query() const { return search_query_; }
	// Discover: discover_open() loads the first catalog the first time;
	// discover_select() switches type / catalog / filter ("" type: keep).
	const Discover &discover() const { return discover_; }
	void discover_open();
	void discover_select(const std::string &type, int catalog, const std::string &filter);
	void discover_load_more();
        const std::vector<Addon> &addons() const { return addons_; }
        bool addons_loaded() const { return addons_loaded_; }

	bool library_loaded() const { return library_loaded_; }
	const std::string &library_error() const { return library_error_; }
	const std::string &addons_error() const { return addons_error_; }

	// Account collection changes. The UI must confirm the exact target first.
        enum class AddonChangeResult {
                OK,
                NOT_SIGNED_IN,
                BUSY,
                ALREADY_INSTALLED,
                NOT_INSTALLED,
                PROTECTED,
                NEEDS_CONFIGURATION,
                INVALID_MANIFEST,
                NETWORK_ERROR,
                SAVE_ERROR,
                SESSION_CHANGED
        };
        using AddonChangeCallback = std::function<void(AddonChangeResult)>;
        void change_addon(const Addon &addon, bool install, AddonChangeCallback cb);
        bool addon_change_busy() const { return addon_change_busy_; }

        // Meta details from the first add-on that provides them.
	using MetaCallback = std::function<void(bool ok, const MetaItem &meta, const std::string &error)>;
	void request_meta(const std::string &type, const std::string &id, MetaCallback cb);
	// Streams for one video from every add-on that provides them:
	// `on_addon` once per add-on, then `on_done`.
	using StreamsCallback = std::function<void(const std::string &addon, std::vector<StreamItem> &streams, const std::string &error)>;
	// Subtitles from every add-on with the "subtitles" resource; `stream`
	// (optional) adds its filename / size / hash so add-ons can match it.
	using SubtitlesCallback = std::function<void(std::vector<SubtitleItem> &subtitles)>;
	void request_subtitles(const std::string &type, const std::string &video_id, const StreamItem *stream,
			SubtitlesCallback on_addon);
	void request_streams(const std::string &type, const std::string &video_id, StreamsCallback on_addon,
			std::function<void()> on_done);
	const LibraryItem *library_item(const std::string &id) const;

	// The library page (stremio-core LibraryWithFilters): items that aren't
	// removed, of `type` ("" = all types), in `sort` order.
	enum class LibrarySort { LAST_WATCHED, NAME, TIMES_WATCHED };
	std::vector<const LibraryItem *> library(const std::string &type, LibrarySort sort) const;
	// The types in the library: movie and series first, then the rest A-Z.
	std::vector<std::string> library_types() const;
	bool in_library(const std::string &id) const;
	// ActionCtx::AddToLibrary / RemoveFromLibrary.
	void add_to_library(const MetaItem &meta);
	void remove_from_library(const std::string &id);

	// Player session: port of the library parts of stremio-core's player.rs.
	// Returns the resume position (ms) for `video_id`, 0 = from the start.
	int64_t player_start(const std::string &meta_id, const std::string &video_id, const MetaItem *meta);
	void player_time_changed(int64_t time_ms, int64_t duration_ms); // ~1/s while playing
	void player_seeked(int64_t time_ms, int64_t duration_ms);
	void player_stop();

	// Main-thread tick (link polling).
	void update(double dt);

	// Bumped whenever the corresponding state changes.
	int auth_rev = 0, library_rev = 0, board_rev = 0, notifications_rev = 0, search_rev = 0, discover_rev = 0;

private:
	using ApiCallback = std::function<void(bool ok, const Json &result, const std::string &error)>;
	void call_api(const std::string &method, const Json &body, ApiCallback cb);
	void call_link_api(const std::string &method, const std::string &query, ApiCallback cb);
	void fetch_json(const std::string &url, std::function<void(bool ok, const Json &json, const std::string &error)> cb);

	void apply_auth(const Json &result);
	void save_session();
	void sync_library();
	void sync_addons();
	void after_sync();
#if defined(STREMIO_DEV)
	void add_extra_addons(std::vector<std::string> urls, std::function<void()> done);
#endif
	void load_board();
	// Loads each row's catalog URL into its items; answers are dropped once
	// `current` is false (a newer board / search). Bumps `rev` per answer.
	void fetch_rows(std::vector<BoardRow> &rows, int &rev, std::function<bool()> current);
	std::vector<BoardRow> search_;
	std::string search_query_;
	int search_gen_ = 0;
	// Discover's selectable lists from the add-ons; keeps the selection when
	// it's still there.
	void discover_rebuild();
	void discover_lists(const std::string &catalog_key);
	std::string discover_catalog_key() const;
	void discover_fetch(bool next_page);
	Discover discover_;
	bool discover_opened_ = false;
	std::vector<std::pair<size_t, size_t>> discover_sources_; // the type's catalogs: (add-on, catalog) indices
	int discover_gen_ = 0;
	void pull_notifications();
	void poll_link();
	int64_t cw_time(const LibraryItem &item) const;

	bool addon_change_busy_ = false;

        bool demo_ = false;
	std::string auth_key_;
	Json user_;
	int gen_ = 0; // bumped on logout: responses for the old session are dropped

	std::map<std::string, LibraryItem> library_;
	bool library_loaded_ = false;
	std::string library_error_;
	std::vector<Addon> addons_;
	bool addons_loaded_ = false;
	std::string addons_error_;
	int pending_syncs_ = 0;
	bool sync_in_progress_ = false;
	bool refresh_requested_ = false;
	int board_gen_ = 0;

	struct Notif {
		int count = 0;
		int64_t newest = 0;
	};
	std::map<std::string, Notif> notifications_;
	std::vector<BoardRow> board_;

	void push_library_item(LibraryItem item);
	void player_push_throttled();
	bool player_active_ = false;
	LibraryItem player_item_;
	std::string player_video_id_;
	bool player_has_meta_ = false;
	MetaItem player_meta_;
	double player_last_push_ = 0;

	LinkLogin link_;
	int link_gen_ = 0;
	double link_poll_in_ = 0;
	double link_age_ = 0;
	bool link_polling_ = false;
};

Stremio &stremio();
