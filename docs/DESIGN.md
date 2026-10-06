# Design

Stremio TV is designed for a 1920×1080 canvas viewed from across the room.
The values below live in `src/ui/theme.h`. Every size there is the base value
below × 1.15 (`UI_SCALE`), which reads better on a TV.

## Typography

Font: Inter (`assets/fonts`).

| Role | Base size | Weight |
|---|---|---|
| Page header / title | 48 px | Bold |
| Section / category header | 32 px | Semi-Bold |
| Card title / menu item | 24 px | Medium |
| Description / muted metadata | 20 px | Regular |
| Subtitles | 42 px | Semi-Bold |

## Colours

| Token | Value | Use |
|---|---|---|
| `BRAND_PRIMARY` | `#0070D1` | Accent, focus rings, active tab, progress |
| `BRAND_SECONDARY` | `#685CEE` | Secondary active states |
| `BRAND_TERTIARY` | `#051923` | Backdrop canvas |
| `SURFACE_CARD` | `#6BAA75` | Container outlines |
| `STATUS_ACCENT` | `#84DD63` | Notifications, badges, "in use" markers |
| `TEXT` | `#EEF1F4` | Text (muted: 60% opacity) |

## Focus

- Focused cards, buttons and list items animate in about 150 ms. They scale
  to 1.1× and become fully opaque.
- Unfocused items return to 1.0× and dim to 60% opacity.
- Corner radii are 12 px for cards and buttons, 24 px for large panels, and
  pill-shaped for the active tab.
