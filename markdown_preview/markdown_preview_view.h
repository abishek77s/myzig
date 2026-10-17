// markdown_preview_view: the counterpart of Zed's
// crates/markdown_preview/src/markdown_preview_view.rs.
//
// The view owns the document and draws it as Zed's `Render` impl does: an
// `editor_background` pane, rem overridden to the preview font size
// (`WithRemSize`), a vertically scrolling container with `p_4` padding, a
// content column at most 800px wide and centered, the overlay scrollbar, and
// the hovered link's URL in a `LinkPreview` at the bottom left. Instead of an
// editor buffer it watches the file on disk, reparsing after the same 200ms
// debounce.
//
// Zed's preview parses and lays out the whole buffer. Here only what is on
// screen is in memory: the file is read in pieces by a scanner that keeps
// just where the root blocks fall (in chunks of about MDP_CHUNK_BYTES), the
// headings and the reference definitions; a chunk is read, parsed, built and
// laid out when it comes near the viewport, and dropped when it leaves.
// Chunks not laid out yet take an estimated height, so the scroll position
// is kept relative to a chunk (anchoring) as estimates turn into heights.

#ifndef MARKDOWN_PREVIEW_VIEW_H
#define MARKDOWN_PREVIEW_VIEW_H

typedef enum MDP_LinkTargetKind
{
  MDP_LinkTargetKind_None,
  MDP_LinkTargetKind_Heading,
  MDP_LinkTargetKind_Position,
}
MDP_LinkTargetKind;

// Selection-space offsets carry their chunk in the high bits, so chunks
// number their texts without knowing how much text comes before them.
#define MDP_CHUNK_TEXT_BITS 40
#define mdp_text_base_from_chunk(i) ((U64)(i) << MDP_CHUNK_TEXT_BITS)
#define mdp_chunk_from_text_offset(o) ((o) >> MDP_CHUNK_TEXT_BITS)

typedef struct MDP_Chunk MDP_Chunk;
struct MDP_Chunk
{
  // while near the viewport: parsed, built and laid out
  Arena *arena;
  MD_Document doc;
  MD_Element *element;
  U64 build_key;                 // view->build_key it was built for

  // size: measured at measure_width/measure_rem, or estimated
  F32 height;
  F32 lead;
  F32 trail;
  F32 measure_width;
  F32 measure_rem;
  F32 y;                         // element coordinates, this frame

  // search: the match count, and the matches while resident
  U64 search_key;
  U64 search_count;
  U64 matches_key;
  Rng1U64 *matches;
  U64 match_count;
};

typedef struct MDP_View MDP_View;
struct MDP_View
{
  Arena *arena;

  // source: a file read in pieces, or text in memory (the welcome page)
  Arena *doc_arena;
  String8 path;
  String8 name;
  String8 base_dir;
  String8 text;                  // the source when it is in memory
  B32 in_memory;
  U64 source_size;
  String8 error;
  U64 error_until_us;

  // the document, chunked
  MD_Scanner scanner;
  U64 scan_read_size;
  B32 scan_urgent;               // the viewport reaches past what is scanned
  Arena *chunk_arena;
  MDP_Chunk *chunks;
  U64 chunk_count;
  U64 chunk_cap;
  U64 build_key;                 // bumps when rem, theme or the references change
  F32 build_rem;
  TH_Kind build_theme;
  B32 build_scan_done;
  F32 layout_width;              // the content column's width, this frame
  F32 content_height;            // chunks and the unscanned rest, this frame
  F32 tail_y;                    // where the unscanned rest begins
  F32 px_per_byte;               // height estimate for chunks not measured

  // the window's title bar above the preview and the outline panel to its
  // left, drawn by the app, physical px
  F32 top_inset;
  F32 left_inset;

  // settings
  F32 font_size;                 // markdown_preview_font_size, logical px
  TH_Kind theme_kind;
  TH_Theme theme;
  MD_Fonts fonts;
  MD_Style style;                // resolved for the current frame

  // scroll container
  F32 scroll_y;
  U64 scroll_activity_us;
  B32 thumb_dragging;
  F32 thumb_drag_offset;
  B32 thumb_hovered;

  // interaction
  Arena *interaction_arena;
  MD_Interaction ix;
  B32 selecting;
  U64 select_anchor;
  B32 press_on_link;
  U64 press_link;
  MD_Element *press_element;
  Vec2F32 press_pos;
  U64 last_click_us;
  Vec2F32 last_click_pos;
  String8 hovered_url;
  
  // link target to scroll to once the newly opened file is laid out
  MDP_LinkTargetKind pending_target;
  U8 pending_slug[256];
  U64 pending_slug_size;
  U64 pending_row;
  U64 pending_column;

  // the scroll position to restore once a reloaded file is scanned that far
  B32 pending_offset_valid;
  U64 pending_offset;
  F32 pending_offset_dy;

  // file watching
  U64 file_modified;
  U64 reload_at_us;

  // search (BufferSearchBar over the SearchableItem impl)
  B32 search_open;
  B32 search_focused;
  B32 search_case;               // SearchOptions::CASE_SENSITIVE
  B32 search_word;               // SearchOptions::WHOLE_WORD
  U8 search_query[512];
  U64 search_query_size;
  B32 search_dirty;
  B32 search_scroll_pending;
  U64 search_key;                // bumps when the query, options or document change
  U64 search_anchor;             // selection-space offset the active match follows
  U64 search_next;               // the next chunk to count: from the anchor's to the end, then from the start
  B32 search_wrapped;
  U64 search_total;              // matches in the chunks counted
  B32 search_has_active;
  Rng1U64 search_active_range;   // selection space

  // right-click menu (ContextMenu)
  B32 menu_open;
  Vec2F32 menu_pos;
  U32 menu_items[3];
  U64 menu_item_count;
  U64 menu_selected;             // keyboard-highlighted item, max_U64 = none
  U8 menu_link[2048];
  U64 menu_link_size;
};

#define MDP_CHUNK_BYTES       KB(64)
#define MDP_SCAN_READ_BYTES   MB(1)
#define MDP_SCAN_BUDGET_US    6000      // per frame, while the document is scanned

internal void mdp_view_init(MDP_View *view);
internal void mdp_view_load_text(MDP_View *view, String8 name, String8 path, String8 text);
internal B32  mdp_view_open_path(MDP_View *view, String8 path, B32 keep_scroll);
internal void mdp_view_poll_file(MDP_View *view);
internal B32  mdp_view_handle_event(MDP_View *view, WM_Window window, WM_Event *ev);
internal void mdp_view_draw(MDP_View *view, WM_Window window);
internal B32  mdp_view_is_animating(MDP_View *view);
internal void mdp_view_scroll_to_heading_index(MDP_View *view, WM_Window window, U64 index);
internal U64  mdp_view_heading_index_at_top(MDP_View *view, WM_Window window);
internal MD_ScanHeading *mdp_view_headings(MDP_View *view, U64 *count_out);

#endif // MARKDOWN_PREVIEW_VIEW_H
