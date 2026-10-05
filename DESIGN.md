# CodexSync UI, Windows 11 Settings direction

1. **Theme**: utility-first Microsoft Settings layout, no marketing header or decorative gradients.
2. **Palette**: system theme. Dark canvas #202020, card #2b2b2b, border #353535, text #f3f3f3, secondary #c4c4c4, accent #60cdff. Light canvas #f3f3f3, card #ffffff, border #e4e4e4, text #1b1b1b, secondary #5a5a5a, accent #0067c0.
3. **Typography**: Microsoft YaHei UI for Chinese Windows text (avoids Qt's serif CJK fallback), platform sans on Linux. Heading 28px, navigation/card titles/body 14px semibold, descriptions 13px medium.
4. **Components**: Qt Quick Controls FluentWinUI3. Settings rows use Microsoft's icon/header/description/action hierarchy. Microsoft Fluent system icons, MIT license included.
5. **Layout**: 260px navigation; main padding 36px horizontal, 24px vertical; 4px card gaps; section heading 30px above and 6px below.
6. **Elevation**: background steps and one-pixel borders. Card radius 4px, status panel 8px, no shadows on dark surfaces.
7. **Guardrails**: no invented counts or successful connection states; no raw log dominating the home page; one primary action; secrets masked; offline apply confirmation retained.
8. **Responsive**: sidebar collapses to icons below 920px; cards wrap explanatory text; all pages scroll vertically; keyboard focus remains visible. Desktop minimum 760x580.
9. **Implementation guide**: use native Fluent buttons/fields, SettingsCard for settings and expanders, compact recent activity, details dialog for logs. Do not redesign core sync behavior in this iteration.

## Verified references
- Microsoft WinUI Gallery SettingsPage.xaml: https://github.com/microsoft/WinUI-Gallery/blob/main/WinUIGallery/Pages/SettingsPage.xaml
- Qt FluentWinUI3 controls: https://github.com/qt/qtdeclarative/tree/v6.8.3/src/quickcontrols/fluentwinui3
- Qt cross-platform FluentWinUI3 style: https://doc.qt.io/qt-6/qtquickcontrols-fluentwinui3.html
- Microsoft Fluent icons: https://github.com/microsoft/fluentui-system-icons
- FluentUI theme source, comparison only: https://github.com/zhuzichu520/FluentUI/blob/main/src/FluTheme.cpp

## Original application icon
Flat, deterministic vector artwork: a solid #0F6CBD rounded tile with one white reciprocal sync mark. No gradients, highlights, shadows, document layers, or text. Editable source: ui/codexsync.svg. Native Qt packaging renders each size directly from the SVG, creating transparent PNGs at 16/32/48/64/128/256 px and a six-resolution Windows ICO. The master PNG, EXE icon, window/taskbar icon, and application sidebar share this source.

## Terminal mode and complete local history
The terminal-mode page prioritizes CLI commands, with HTTP/C API still available. `conversations --config FILE` audits active/archived rollout files, including `.jsonl.zst`, plus thread-index references, without reading message bodies or uploading anything. All dates and unindexed archived files are retained; thread/history SQLite databases and session indexes remain part of the full-root backup. Directory aliases resolve to physical roots, including detached linked history directories. Explicit exclusions and missing references are reported rather than disguised as complete coverage. Counts represent files, not unique chats; cloud-only history is outside local coverage.
