// English translations and fallback language.
#pragma once

#include "keys.h"

namespace i18n {
namespace locales {

inline const char *en(Text key) {
    switch (key) {
        case Text::HOME: return "Home";
        case Text::DISCOVER: return "Discover";
        case Text::SEARCH: return "Search";
        case Text::LIBRARY: return "Library";
        case Text::SETTINGS: return "Settings";
        case Text::SIGN_OUT: return "Sign out";
        case Text::SIGN_OUT_CONFIRM: return "Press \xC3\x97 again to sign out";
        case Text::UI_SCALE: return "UI scale";
        case Text::VIDEO_PLAYER: return "Video player";
        case Text::PLAYER_HARDWARE: return "PS4 hardware (MP4 only)";
        case Text::PLAYER_FFMPEG: return "FFmpeg (plays everything)";
        case Text::PLAYER_HARDWARE_TOAST: return "Videos will use the PS4's hardware player (MP4 only)";
        case Text::PLAYER_FFMPEG_TOAST: return "Videos will use FFmpeg";
        case Text::INTERFACE_LANGUAGE: return "Interface language";
        case Text::CONTINUE_WATCHING: return "Continue watching";
        case Text::FILTERS: return "Filters";

        case Text::LOADING: return "Loading\xE2\x80\xA6";
        case Text::LOADING_MORE: return "Loading more\xE2\x80\xA6";
        case Text::LOADING_LIBRARY: return "Loading your library\xE2\x80\xA6";
        case Text::LOADING_ADDONS: return "Loading your add-ons\xE2\x80\xA6";
        case Text::LIBRARY_LOAD_ERROR: return "Couldn't load your library: ";
        case Text::ADDONS_LOAD_ERROR: return "Couldn't load your add-ons: ";
        case Text::CATALOG_LOAD_ERROR: return "Couldn't load this catalog: ";
        case Text::CATALOG_SEARCH_ERROR: return "Couldn't search this catalog: ";
        case Text::NO_BROWSABLE_CATALOGS: return "None of your add-ons have catalogs to browse.";
        case Text::EMPTY_CATALOG: return "Nothing in this catalog.";
        case Text::EMPTY_CONTINUE_WATCHING: return "Nothing to continue yet. Start something in Stremio and it will show up here.";
        case Text::EMPTY_LIBRARY: return "Your library is empty. Open a title and press \xE2\x96\xB3 to add it.";
        case Text::ADDONS: return "Add-ons";

        case Text::MINUTES_LEFT_SUFFIX: return " min left";
        case Text::ONE_NEW_EPISODE: return "1 new episode";
        case Text::NEW_EPISODES_SUFFIX: return " new episodes";
        case Text::NEW_SINGULAR_SUFFIX: return " new";
        case Text::NEW_PLURAL_SUFFIX: return " new";

        case Text::SEARCHING: return "Searching\xE2\x80\xA6";
        case Text::SEARCH_HINT: return "Search for movies and series with the keyboard";
        case Text::NO_RESULTS_PREFIX: return "No results for \"";
        case Text::TYPE_A_TITLE: return "Type a title";
        case Text::SPACE: return "Space";
        case Text::DELETE_KEY: return "Delete";
        case Text::CLEAR: return "Clear";
        case Text::RESULTS: return "Results";

        case Text::SORT_RECENT: return "Recent";
        case Text::SORT_ALPHABETICAL: return "A\xE2\x80\x93Z";
        case Text::SORT_MOST_WATCHED: return "Most watched";

        case Text::TYPE_MOVIES: return "Movies";
        case Text::TYPE_SERIES: return "Series";
        case Text::TYPE_CHANNELS: return "Channels";
        case Text::POPULAR: return "Popular";
        case Text::FEATURED: return "Featured";
        case Text::FEATURED_MOVIES: return "Featured Movies";
        case Text::FEATURED_SERIES: return "Featured Series";
        case Text::CATALOG_NEW: return "New";
        case Text::TRENDING: return "Trending";
        case Text::TOP_AIRING: return "Top Airing";
        case Text::MOST_POPULAR: return "Most Popular";
        case Text::HIGHEST_RATED: return "Highest Rated";
        case Text::KITSU_TRENDING: return "Kitsu Trending";
        case Text::KITSU_TOP_AIRING: return "Kitsu Top Airing";
        case Text::KITSU_MOST_POPULAR: return "Kitsu Most Popular";
        case Text::KITSU_HIGHEST_RATED: return "Kitsu Highest Rated";
        case Text::TOP_SEEDED: return "Top Seeded";
        case Text::FILTER_TYPE: return "Type";
        case Text::FILTER_CATALOG: return "Catalog";
        case Text::FILTER_YEAR: return "Year";
        case Text::FILTER_GENRE: return "Genre";
        case Text::FILTER_ALL: return "All";

        case Text::GENRE_ACTION: return "Action";
        case Text::GENRE_ADVENTURE: return "Adventure";
        case Text::GENRE_ANIMATION: return "Animation";
        case Text::GENRE_COMEDY: return "Comedy";
        case Text::GENRE_CRIME: return "Crime";
        case Text::GENRE_DOCUMENTARY: return "Documentary";
        case Text::GENRE_DRAMA: return "Drama";
        case Text::GENRE_FAMILY: return "Family";
        case Text::GENRE_FANTASY: return "Fantasy";
        case Text::GENRE_HISTORY: return "History";
        case Text::GENRE_HORROR: return "Horror";
        case Text::GENRE_MUSIC: return "Music";
        case Text::GENRE_MYSTERY: return "Mystery";
        case Text::GENRE_ROMANCE: return "Romance";
        case Text::GENRE_SCIENCE_FICTION: return "Science Fiction";
        case Text::GENRE_THRILLER: return "Thriller";
        case Text::GENRE_WAR: return "War";
        case Text::GENRE_WESTERN: return "Western";
        case Text::EPISODES: return "Episodes";
        case Text::STREAMS: return "Streams";
        case Text::EPISODE: return "Episode";
        case Text::PLAY: return "Play";
        case Text::BACK: return "Back";
        case Text::SHOW_STREAMS: return "Show streams";
        case Text::ADD_TO_LIBRARY: return "Add to library";
        case Text::REMOVE_FROM_LIBRARY: return "Remove from library";
        case Text::IN_LIBRARY: return "In library";
        case Text::ADDED_TO_LIBRARY: return "Added to your library";
        case Text::REMOVED_FROM_LIBRARY: return "Removed from your library";
        case Text::LOADING_EPISODES: return "Loading episodes\xE2\x80\xA6";
        case Text::FINDING_STREAMS: return "Finding streams\xE2\x80\xA6";
        case Text::NO_STREAMS_FOUND: return "No streams found. Install a streaming add-on in Stremio to watch this.";
        case Text::DETAILS_LOAD_ERROR: return "Couldn't load details: ";
        case Text::SEEDER_SUFFIX: return " seeder";
        case Text::SEEDERS_SUFFIX: return " seeders";
        case Text::COUNT: return nullptr;
    }
    return nullptr;
}

} // namespace locales
} // namespace i18n
