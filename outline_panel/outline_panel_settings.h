// outline_panel_settings: the defaults Zed's outline panel reads
// (crates/outline_panel/src/outline_panel_settings.rs,
// assets/settings/default.json "outline_panel").

#ifndef OUTLINE_PANEL_SETTINGS_H
#define OUTLINE_PANEL_SETTINGS_H

#define OP_DEFAULT_WIDTH_PX   300.f   // default_width (docked left here, as mdview's sidebar was)
#define OP_MIN_WIDTH_PX       120.f
#define OP_INDENT_SIZE_PX     20.f    // indent_size
#define OP_ROW_HEIGHT_PX      26.f    // h_6 label box + border_1 top and bottom
#define OP_FILTER_HEIGHT_PX   32.f    // Tab::container_height
#define OP_LABEL_PX           15.f    // buffer_font_size (outline::render_item)
#define OP_UI_TEXT_PX         14.f    // text_ui
#define OP_GUIDE_LEFT_PX      15.f    // LIST_ITEM_INDENT_GUIDE_LEFT_OFFSET
#define OP_RESIZE_HANDLE_PX   6.f     // RESIZE_HANDLE_SIZE

#endif // OUTLINE_PANEL_SETTINGS_H
