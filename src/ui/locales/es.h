// Spanish translations. Return nullptr for missing keys.
#pragma once

#include "keys.h"

namespace i18n {
namespace locales {

inline const char *es(Text key) {
    switch (key) {
        case Text::HOME: return "Inicio";
        case Text::DISCOVER: return "Explorar";
        case Text::SEARCH: return "Buscar";
        case Text::LIBRARY: return "Biblioteca";
        case Text::SETTINGS: return "Ajustes";
        case Text::SIGN_OUT: return "Cerrar sesión";
        case Text::SIGN_OUT_CONFIRM: return "Pulsa \xC3\x97 otra vez para cerrar sesión";
        case Text::UI_SCALE: return "Escala de interfaz";
        case Text::VIDEO_PLAYER: return "Reproductor de vídeo";
        case Text::PLAYER_HARDWARE: return "Hardware de PS4 (solo MP4)";
        case Text::PLAYER_FFMPEG: return "FFmpeg (reproduce todo)";
        case Text::PLAYER_HARDWARE_TOAST: return "Se usará el reproductor de PS4 (solo MP4)";
        case Text::PLAYER_FFMPEG_TOAST: return "Se usará FFmpeg";
        case Text::INTERFACE_LANGUAGE: return "Idioma de la interfaz";
        case Text::CONTINUE_WATCHING: return "Continuar viendo";
        case Text::FILTERS: return "Filtros";

        case Text::LOADING: return "Cargando\xE2\x80\xA6";
        case Text::LOADING_MORE: return "Cargando más\xE2\x80\xA6";
        case Text::LOADING_LIBRARY: return "Cargando tu biblioteca\xE2\x80\xA6";
        case Text::LOADING_ADDONS: return "Cargando tus complementos\xE2\x80\xA6";
        case Text::LIBRARY_LOAD_ERROR: return "No se pudo cargar tu biblioteca: ";
        case Text::ADDONS_LOAD_ERROR: return "No se pudieron cargar tus complementos: ";
        case Text::CATALOG_LOAD_ERROR: return "No se pudo cargar este catálogo: ";
        case Text::CATALOG_SEARCH_ERROR: return "No se pudo buscar en este catálogo: ";
        case Text::NO_BROWSABLE_CATALOGS: return "Ninguno de tus complementos tiene catálogos para explorar.";
        case Text::EMPTY_CATALOG: return "Este catálogo está vacío.";
        case Text::EMPTY_CONTINUE_WATCHING: return "Todavía no hay nada para continuar. Empieza a ver algo en Stremio y aparecerá aquí.";
        case Text::EMPTY_LIBRARY: return "Tu biblioteca está vacía. Abre un título y pulsa \xE2\x96\xB3 para añadirlo.";
        case Text::ADDONS: return "Complementos";

        case Text::MINUTES_LEFT_SUFFIX: return " min restantes";
        case Text::ONE_NEW_EPISODE: return "1 episodio nuevo";
        case Text::NEW_EPISODES_SUFFIX: return " episodios nuevos";
        case Text::NEW_SINGULAR_SUFFIX: return " nuevo";
        case Text::NEW_PLURAL_SUFFIX: return " nuevos";

        case Text::SEARCHING: return "Buscando\xE2\x80\xA6";
        case Text::SEARCH_HINT: return "Busca películas y series con el teclado";
        case Text::NO_RESULTS_PREFIX: return "No hay resultados para \"";
        case Text::TYPE_A_TITLE: return "Escribe un título";
        case Text::SPACE: return "Espacio";
        case Text::DELETE_KEY: return "Borrar";
        case Text::CLEAR: return "Limpiar";
        case Text::RESULTS: return "Resultados";

        case Text::SORT_RECENT: return "Recientes";
        case Text::SORT_ALPHABETICAL: return "A\xE2\x80\x93Z";
        case Text::SORT_MOST_WATCHED: return "Más vistos";

        case Text::TYPE_MOVIES: return "Películas";
        case Text::TYPE_SERIES: return "Series";
        case Text::TYPE_CHANNELS: return "Canales";
        case Text::POPULAR: return "Popular";
        case Text::FEATURED: return "Destacados";
        case Text::FEATURED_MOVIES: return "Películas destacadas";
        case Text::FEATURED_SERIES: return "Series destacadas";
        case Text::CATALOG_NEW: return "Novedades";
        case Text::TRENDING: return "Tendencias";
        case Text::TOP_AIRING: return "Lo mejor en emisión";
        case Text::MOST_POPULAR: return "Lo más popular";
        case Text::HIGHEST_RATED: return "Lo mejor valorado";
        case Text::KITSU_TRENDING: return "Tendencias en Kitsu";
        case Text::KITSU_TOP_AIRING: return "Lo mejor en emisión en Kitsu";
        case Text::KITSU_MOST_POPULAR: return "Lo más popular en Kitsu";
        case Text::KITSU_HIGHEST_RATED: return "Lo mejor valorado en Kitsu";
        case Text::TOP_SEEDED: return "Con más fuentes";
        case Text::FILTER_TYPE: return "Tipo";
        case Text::FILTER_CATALOG: return "Catálogo";
        case Text::FILTER_YEAR: return "Año";
        case Text::FILTER_GENRE: return "Género";
        case Text::FILTER_ALL: return "Todos";

        case Text::GENRE_ACTION: return "Acción";
        case Text::GENRE_ADVENTURE: return "Aventura";
        case Text::GENRE_ANIMATION: return "Animación";
        case Text::GENRE_COMEDY: return "Comedia";
        case Text::GENRE_CRIME: return "Crimen";
        case Text::GENRE_DOCUMENTARY: return "Documental";
        case Text::GENRE_DRAMA: return "Drama";
        case Text::GENRE_FAMILY: return "Familiar";
        case Text::GENRE_FANTASY: return "Fantasía";
        case Text::GENRE_HISTORY: return "Historia";
        case Text::GENRE_HORROR: return "Terror";
        case Text::GENRE_MUSIC: return "Música";
        case Text::GENRE_MYSTERY: return "Misterio";
        case Text::GENRE_ROMANCE: return "Romance";
        case Text::GENRE_SCIENCE_FICTION: return "Ciencia ficción";
        case Text::GENRE_THRILLER: return "Suspenso";
        case Text::GENRE_WAR: return "Bélico";
        case Text::GENRE_WESTERN: return "Oeste";
        default: return nullptr;
    }
}

} // namespace locales
} // namespace i18n
