# Design

Stremio PS4 is designed for a 1920×1080 canvas viewed from across the room.
The values below live in `src/ui/theme.h`. Every size there is the base value
below × 1.15 (`UI_SCALE`), which reads better on a TV.

## Typography

Font: Inter (`assets/fonts`), with Noto fallbacks for other scripts (Arabic, Hebrew, Indic, Thai, CJK...).

| Role | Base size | Weight |
|---|---|---|
| Page header / title | 48 px | Bold |
| Section / category header | 32 px | Semi-Bold |
| Card title / menu item | 24 px | Medium |
| Description / muted metadata | 20 px | Regular |
| Subtitles | 42 px | Semi-Bold |

## Colours

The colour tokens use [daisyUI](https://daisyui.com)'s semantic theme roles,
so a screen asks for a role ("a surface", "the focus colour") and never a raw
colour. The values are the brand palette.

| Token | Value | Use |
|---|---|---|
| `BASE_100` | `#051923` | Backdrop canvas (Ink Black) |
| `BASE_200` | `#0C2A38` | Cards, list rows, keys, fields, the tab box |
| `BASE_300` | `#163D50` | Elevated parts on `BASE_200`: key caps, avatar |
| `BASE_CONTENT` | `#EEF1F4` | Text (muted: 60% opacity) |
| `PRIMARY` | `#0070D1` | Focus rings, active tab, progress (Sapphire Sky) |
| `SECONDARY` | `#685CEE` | Focus ring on an already-active item (Majorelle Blue) |
| `ACCENT` | `#84DD63` | Badges, "in use" dots, toasts (Radioactive Grass) |
| `WARNING` | `#F2C14E` | Torrents with 5–14 seeders |
| `ERROR` | `#FF8A80` | Errors, the sign-out confirmation, torrents with under 5 seeders |

As in daisyUI, `PRIMARY` is kept for what has the focus or is selected;
headings use `BASE_CONTENT`.

## Shape

| Token | Base value | Use |
|---|---|---|
| `RADIUS_FIELD` | 12 px | Cards, buttons, keys, list rows |
| `RADIUS_BOX` | 24 px | Large panels |
| `RADIUS_SELECTOR` | pill | Tabs, filter chips, badges |

## Components

`src/ui/components.h` has the shared pieces, each modelled on a daisyUI
component:

| Component | daisyUI | Where |
|---|---|---|
| Focus chrome (`begin_focus` / `end_focus`) | btn, card | Every focusable element |
| `chip` | btn, filter | Tabs, the Library's type and sort filters, the details page's add-on filter |
| `badge` | badge | "3 new episodes" on cards, "In library", a torrent's seeders (green 15+, amber 5–14, red under 5) |
| `progress` | progress | Watch progress, the player's timeline |
| `status` | status | The playing track, the current episode |
| `avatar` | avatar | The signed-in account on Home |
| `hints`, `kbd` | kbd | Controller hints (× Select, ○ Back, L1 / R1) |
| `Toast` | toast + alert | Short confirmations and errors |
| Tab bar (`tab_bar.h`) | tabs-box | The four main tabs |
| Add-on filter (`DetailsScreen::draw_filter`) | tabs-box, smaller | All / one add-on above the streams; L1 / R1 or Up from the first stream |
| Card rows (`card.h`) | carousel | Home and Search |
| Poster cards (`draw_poster`) | card | The Library grid: artwork, caption below |

## Focus

- Focused cards, buttons and list items animate in about 150 ms. They scale
  to 1.1× (lists 1.03×) and become fully opaque, with a 4 px ring.
- Unfocused items return to 1.0× and dim to 60% opacity.
