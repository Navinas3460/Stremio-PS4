#include "../src/core/languages.h"
#include "../src/core/models.h"
#include "test.h"

TEST(models_time_round_trip) {
	const int64_t t = parse_time(Json("2024-05-01T12:34:56.789Z"));
	CHECK_EQ(t, (int64_t)1714566896);
	CHECK_EQ(parse_time(Json(format_time(t))), t);
	CHECK_EQ(parse_time(Json()), (int64_t)0);
	CHECK_EQ(parse_time(Json("garbage")), (int64_t)0);
}

TEST(models_uri_encode) {
	CHECK_EQ(uri_encode("tt1,tt2"), "tt1%2Ctt2");
	CHECK_EQ(uri_encode("star wars: a/b"), "star%20wars%3A%20a%2Fb");
	CHECK_EQ(uri_encode("-_.!~*'()"), "-_.!~*'()");
	CHECK_EQ(uri_encode("\xC3\xA9"), "%C3%A9");
}

TEST(models_library_item) {
	const Json j = Json::parse(R"({"_id":"tt0903747","name":"Breaking Bad","type":"series","removed":false,"temp":false,
		"_mtime":"2026-10-01T00:00:00.000Z","state":{"video_id":"tt0903747:2:5","timeOffset":1800000,"duration":2700000,
		"lastWatched":"2026-10-01T00:00:00.000Z","timesWatched":0},"extraField":"kept"})");
	const LibraryItem item = LibraryItem::from_json(j);
	CHECK_EQ(item.id, "tt0903747");
	CHECK_NEAR(item.progress(), 2.0 / 3.0, 1e-5);
	CHECK_EQ(item.minutes_left(), 15);
	CHECK_EQ(item.episode_label(), "S2 \xC2\xB7 E5");
	CHECK_EQ(item.resume_video_id(), "tt0903747:2:5");
	CHECK(item.is_in_continue_watching());
	CHECK(item.should_pull_notifications());
	// Unmodelled fields survive a round trip to the API.
	CHECK_EQ(item.to_json()["extraField"].as_string(), "kept");
	CHECK_EQ(item.to_json()["state"]["timeOffset"].as_int(), (int64_t)1800000);

	LibraryItem movie = item;
	movie.type = "movie";
	movie.video_id = "tt1";
	CHECK_EQ(movie.episode_label(), "");
	CHECK(!movie.should_pull_notifications());
	movie.time_offset = 0;
	CHECK(!movie.is_in_continue_watching());
	movie.duration = 0;
	CHECK_EQ(movie.minutes_left(), -1);
	CHECK_EQ(movie.progress(), 0.0f);
}

TEST(models_meta_next_video) {
	const MetaItem m = MetaItem::from_json(Json::parse(R"({"id":"tt9","type":"series","name":"Show","videos":[
		{"id":"tt9:1:2","season":1,"episode":2,"released":"2020-01-08T00:00:00Z"},
		{"id":"tt9:0:1","season":0,"episode":1,"title":"Special"},
		{"id":"tt9:1:1","season":1,"episode":1,"title":"Pilot","released":"2020-01-01T00:00:00Z"},
		{"id":"tt9:2:1","season":2,"episode":1,"released":"2030-01-01T00:00:00Z"}]})"));
	CHECK_EQ(m.videos.size(), (size_t)4);
	CHECK_EQ(m.videos[0].id, "tt9:0:1"); // sorted by season, episode
	const int64_t now = parse_time(Json("2026-01-01T00:00:00Z"));
	CHECK_EQ(m.next_video("tt9:1:1", now)->id, "tt9:1:2");
	CHECK(m.next_video("tt9:1:2", now) == nullptr); // next one isn't released yet
	CHECK(m.next_video("tt9:0:1", now) != nullptr); // specials may lead into season 1
	CHECK_EQ(MetaItem::video_label(m.videos[1]), "S1 \xC2\xB7 E1  Pilot");
	CHECK_EQ(m.direct_video_id(), ""); // a series: pick an episode

	const MetaItem movie = MetaItem::from_json(Json::parse(R"({"id":"tt1","type":"movie","name":"Film"})"));
	CHECK_EQ(movie.direct_video_id(), "tt1");
}

TEST(models_catalog_responses) {
	std::vector<MetaItem> metas;
	CHECK(metas_from_response(Json::parse(R"({"metas":[{"id":"a","type":"movie","name":"A"},"junk"]})"), metas));
	CHECK_EQ(metas.size(), (size_t)1);
	metas.clear();
	CHECK(metas_from_response(Json::parse(R"({"metasDetailed":null})"), metas));
	CHECK(metas.empty());
	CHECK(!metas_from_response(Json::parse(R"({"err":"x"})"), metas));
}

TEST(models_addon_resources) {
	const Addon a = Addon::from_json(Json::parse(R"({"transportUrl":"https://addon.example/abc/manifest.json",
		"manifest":{"id":"x","name":"X","types":["movie","series"],"idPrefixes":["tt"],
		"resources":["catalog",{"name":"stream","types":["movie"],"idPrefixes":["tt"]},{"name":"subtitles"}],
		"catalogs":[{"type":"movie","id":"top","name":"Top","extra":[{"name":"search"},{"name":"genre"}]},
		            {"type":"series","id":"last","extra":[{"name":"lastVideosIds","isRequired":true,"optionsLimit":50}]}]}})"));
	CHECK(a.is_resource_supported("stream", "movie", "tt123"));
	CHECK(!a.is_resource_supported("stream", "series", "tt123")); // the resource's own types win
	CHECK(!a.is_resource_supported("stream", "movie", "kitsu:1"));
	CHECK(!a.is_resource_supported("subtitles", "movie", "tt1")); // full resource without types
	CHECK(!a.is_resource_supported("meta", "movie", "tt1"));
	CHECK_EQ(a.resource_url("stream", "movie", "tt1"), "https://addon.example/abc/stream/movie/tt1.json");
	CHECK_EQ(a.resource_url("catalog", "movie", "top", { { "search", "the matrix" } }),
			"https://addon.example/abc/catalog/movie/top/search=the%20matrix.json");

	CHECK(a.catalogs[0].are_extra_names_supported({}));
	CHECK(a.catalogs[0].are_extra_names_supported({ "search" }));
	CHECK(!a.catalogs[0].are_extra_names_supported({ "skip" }));
	CHECK(!a.catalogs[1].are_extra_names_supported({})); // required extra missing
	CHECK(a.catalogs[1].are_extra_names_supported({ "lastVideosIds" }));
	CHECK_EQ(a.catalogs[1].extra_prop("lastVideosIds")->options_limit, 50);

	Addon legacy = a;
	legacy.transport_url = "https://old.example/stremio/v1";
	CHECK_EQ(legacy.resource_url("stream", "movie", "tt1"), "");
}

TEST(models_streams) {
	const StreamItem direct = StreamItem::from_json(Json::parse(R"({"url":"https://cdn.example/v.mkv","name":"1080p",
		"title":"Film.mkv","behaviorHints":{"filename":"Film.mkv","videoSize":123456789,
		"proxyHeaders":{"request":{"Referer":"https://x"}}}})"), "Debrid");
	CHECK_EQ(direct.playable_url(), "https://cdn.example/v.mkv");
	CHECK_EQ(direct.unsupported_reason(), "");
	CHECK_EQ(direct.description, "Film.mkv");
	CHECK_EQ(direct.video_size, (uint64_t)123456789);
	CHECK_EQ(direct.request_headers.size(), (size_t)1);
	CHECK_EQ(direct.request_headers[0], "Referer: https://x");

	const StreamItem torrent = StreamItem::from_json(Json::parse(R"({"infoHash":"abc"})"), "Torrentio");
	CHECK_EQ(torrent.playable_url(), "");
	CHECK_EQ(torrent.unsupported_reason(), "Torrent: needs a streaming server");
	const StreamItem rtmp = StreamItem::from_json(Json::parse(R"({"url":"rtmp://x/y"})"), "A");
	CHECK_EQ(rtmp.playable_url(), "");
	CHECK_EQ(rtmp.unsupported_reason(), "Unsupported link");
}

TEST(languages) {
	CHECK_EQ(language_name("en"), "English");
	CHECK_EQ(language_name("ENG"), "English");
	CHECK_EQ(language_name("fra"), "French");
	CHECK_EQ(language_name("fre"), "French");
	CHECK_EQ(language_name("pob"), "Portuguese (Brazil)");
	CHECK_EQ(language_name("xyz"), "XYZ");
	CHECK_EQ(language_name(""), "");
}
