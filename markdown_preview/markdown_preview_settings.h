// markdown_preview_settings: the defaults Zed's preview reads
// (crates/markdown_preview/src/markdown_preview_settings.rs,
// assets/settings/default.json, crates/theme_settings).

#ifndef MARKDOWN_PREVIEW_SETTINGS_H
#define MARKDOWN_PREVIEW_SETTINGS_H

#define MDP_MAX_WIDTH_PX          800.f   // markdown_preview.max_width
#define MDP_DEFAULT_FONT_SIZE_PX  16.f    // ui_font_size
#define MDP_MIN_FONT_SIZE_PX      6.f     // clamp_font_size
#define MDP_MAX_FONT_SIZE_PX      100.f
#define MDP_BUFFER_LINE_HEIGHT    1.618f  // buffer_line_height: comfortable
#define MDP_REPARSE_DEBOUNCE_US   200000  // REPARSE_DEBOUNCE
#define MDP_LINES_PER_WHEEL_NOTCH 3.f     // Windows' default wheel scroll lines

#define MDP_SCROLLBAR_WIDTH_PX    6.f     // ScrollbarStyle::Regular
#define MDP_SCROLLBAR_PADDING_PX  4.f     // SCROLLBAR_PADDING
#define MDP_SCROLLBAR_MIN_THUMB_PX 25.f   // MINIMUM_THUMB_SIZE
#define MDP_SCROLLBAR_HIDE_DELAY_US 1000000 // SCROLLBAR_HIDE_DELAY_INTERVAL
#define MDP_SCROLLBAR_HIDE_DURATION_US 400000 // SCROLLBAR_HIDE_DURATION

#endif // MARKDOWN_PREVIEW_SETTINGS_H
