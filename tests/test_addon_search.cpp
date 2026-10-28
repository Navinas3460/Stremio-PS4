#include "test.h"

#include "../src/core/addon_search.h"

TEST(addon_search_empty_query) {
	CHECK(addon_search::matches("Cinema", "org.example.cinema", ""));
	CHECK(addon_search::matches("", "", ""));
	CHECK(!addon_search::matches("", "", "cinema"));
}

TEST(addon_search_name_and_id) {
	CHECK(addon_search::matches("Cinema Catalog", "org.example.movies", "catalog"));
	CHECK(addon_search::matches("Cinema Catalog", "org.example.movies", "example.movies"));
	CHECK(!addon_search::matches("Cinema Catalog", "org.example.movies", "sports"));
}

TEST(addon_search_case_insensitive) {
	CHECK_EQ(addon_search::normalize("AbC XYZ 019"), "abc xyz 019");
	CHECK(addon_search::matches("CINEMA", "org.example", "CiNe"));
	CHECK(addon_search::matches("Catalog", "ORG.EXAMPLE.MOVIES", "Example.MOV"));
}

TEST(addon_search_spanish_accents) {
	CHECK_EQ(
		addon_search::normalize(
			"\xC3\x81\xC3\x89\xC3\x8D\xC3\x93\xC3\x9A"
			"\xC3\x9C\xC3\x91"),
		"aeiouun"
	);
	CHECK_EQ(
		addon_search::normalize(
			"\xC3\xA1\xC3\xA9\xC3\xAD\xC3\xB3\xC3\xBA"
			"\xC3\xBC\xC3\xB1"),
		"aeiouun"
	);
	CHECK_EQ(
		addon_search::normalize("\xC3\xA1\xC3\xA1\xC3\x91\xC3\xB1"),
		"aann"
	);
	CHECK(addon_search::matches(
		"Pel\xC3\xAD" "culas", "org.example", "PELICULAS"
	));
	CHECK(addon_search::matches(
		"Catalog", "org.ni\xC3\xB1" "os", "NINOS"
	));
	CHECK(addon_search::matches(
		"Ninos", "org.example", "NI\xC3\x91" "OS"
	));
}

TEST(addon_search_preserves_spaces_and_punctuation) {
	CHECK_EQ(
		addon_search::normalize("  CINEMA-HD_2  "),
		"  cinema-hd_2  "
	);
	CHECK(addon_search::matches("Cinema HD", "org.example", "cinema hd"));
	CHECK(!addon_search::matches("Cinema HD", "org.example", "cinemahd"));
	CHECK(!addon_search::matches("Cinema", "org.example", " cinema "));
	CHECK(addon_search::matches("Cinema-HD", "org.example", "cinema-hd"));
	CHECK(!addon_search::matches("Cinema-HD", "org.example", "cinema hd"));
}
