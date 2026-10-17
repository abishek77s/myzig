// outline_panel: the counterpart of Zed's crates/outline_panel, for the one
// document the preview shows.
//
// A dock panel on the left, after the recent files (panel_background, a
// border on its right edge, a 6px resize handle over it) holding the
// "Search…" filter row
// and a uniform list of the document's headings. Each row is an outline item
// as Zed's markdown outline query yields it, without the "#" marks (in the title
// color, in the buffer font), indented 20px per section level, with a
// disclosure chevron when it has children, and indent guides between levels.
// Clicking a row scrolls the preview to it; while the panel is not focused,
// the selection follows the heading at the top of the preview.

#ifndef OUTLINE_PANEL_H
#define OUTLINE_PANEL_H

typedef struct OP_Entry OP_Entry;
struct OP_Entry
{
  U64 heading;                   // index into the scanned headings
  U64 depth;
  B32 has_children;
  B32 collapsed;
  String8 label;                 // the heading's text
  U64 *match_positions;          // byte offsets into label
  U64 match_count;
  U64 match_score;               // higher ranks first
};

typedef struct OP_Panel OP_Panel;
struct OP_Panel
{
  B32 open;
  B32 focused;                   // the panel, or its filter editor, has focus
  F32 width;                     // logical px
  F32 x0;                        // physical px from the client's left edge: the docks before it
  B32 resizing;

  // filter editor
  U8 query[256];
  U64 query_size;

  // list
  F32 scroll_y;                  // physical px
  U64 scroll_activity_us;
  U64 selected;                  // heading index, max_U64 = none
  B32 autoscroll;                // bring the selection into view next draw

  // collapsed outlines, by hash of level and text so they survive reloads
  U64 collapsed[512];
  U64 collapsed_count;
};

internal void op_panel_init(OP_Panel *panel);
internal F32  op_panel_width_px(OP_Panel *panel, WM_Window window);
internal B32  op_panel_handle_event(OP_Panel *panel, MDP_View *view, WM_Window window, WM_Event *ev);
internal void op_panel_update(OP_Panel *panel, MDP_View *view, WM_Window window);
internal void op_panel_draw(OP_Panel *panel, MDP_View *view, WM_Window window);
internal B32  op_panel_is_animating(OP_Panel *panel);

#endif // OUTLINE_PANEL_H
