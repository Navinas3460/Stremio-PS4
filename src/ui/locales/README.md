# Interface translations

This folder contains the interface translation keys and language files.

- `keys.h`: shared text keys.
- `en.h`: English translations and fallback language.
- `es.h`: Spanish translations.
- `../i18n.h`: language registry, selection, persistence and lookup.
- `../settings_screen.cpp`: uses the registry to cycle through languages.

These translations are compiled into the application. Adding or changing a
language requires rebuilding the application; copying a language file onto
the console does not install it.

## Add a language

For example, to add French:

1. Copy `es.h` to `fr.h`.
2. Rename the lookup function from `es(Text key)` to `fr(Text key)`.
3. Translate the returned strings. Keep every `Text::...` key unchanged.
4. Keep `default: return nullptr;` for missing translations.
5. In `../i18n.h`:
   - Add `#include "locales/fr.h"`.
   - Add `FRENCH` to `enum class Language`.
   - Add this entry to the `languages` array:

     { Language::FRENCH, "fr", "Français", locales::fr }

6. Rebuild and test the application.

No changes to individual screens or to the Settings language selector are
needed when adding translations for existing keys.

Keep English as the first registry entry. Use a unique, stable language code:
the selected code is saved in `ui_language.txt`.

## Translation rules

- Save files as UTF-8.
- Keep translation keys and C++ function names unchanged.
- Preserve C++ escapes such as `\"`, `\n` and `\xE2\x80\xA6`.
- Preserve leading and trailing spaces in prefixes and suffixes.
- Translate interface text, not movie titles, series titles or add-on names.
- Keep labels concise and check their appearance at different UI scales.
- Use `nullptr` for a missing translation; an empty string intentionally
  displays nothing and does not trigger the English fallback.

## Add a new interface message

1. Add a key before `COUNT` in `keys.h`.
2. Add its English text in `en.h`.
3. Add translations in the other language files where available.
4. Request the message from the screen:

   i18n::tr(i18n::Text::YOUR_KEY)

Do not place language-specific translations directly inside screen code.

## External catalog and filter labels

Known catalog and filter labels are matched to keys by the alias table in
`../i18n.h`. Unknown labels remain unchanged.

The alias table identifies source labels; translations belong in the
language files. Do not translate identifiers or filter values sent to add-ons.

## Fallback and plural forms

If the selected language returns `nullptr`, the lookup falls back to English.

The current notification code selects singular for 1 and plural for other
counts. Languages requiring additional plural categories need an extension
to that selection logic; adding translations alone does not provide it.

## Testing checklist

- Select the language in Settings.
- Restart the app and check that the selection is retained.
- Check Home, Discover, Search, Library and Settings.
- Check catalog labels, genres, loading states and empty states.
- Test search keyboard actions and filter selection.
- Check singular and plural notification messages.
- Check accented characters, truncation and overlapping text.
- Switch back to English and verify that it still works.
- Verify English fallback for a missing translation.
