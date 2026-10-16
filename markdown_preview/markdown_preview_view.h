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

#ifndef MARKDOWN_PREVIEW_VIEW_H
#define MARKDOWN_PREVIEW_VIEW_H

typedef enum MDP_LinkTargetKind
{
  MDP_LinkTargetKind_None,
  MDP_LinkTargetKind_Heading,
  MDP_LinkTargetKind_Position,
}
MDP_LinkTargetKind;

typedef struct MDP_View MDP_View;
struct MDP_View
{
  Arena *arena;

  // source
  Arena *doc_arena;
  String8 path;
  String8 name;
  String8 base_dir;
  String8 text;
  String8 error;
  U64 error_until_us;
  MD_Document doc;

  // element cache, rebuilt when the document, width, rem or theme change
  Arena *element_arena;
  MD_Element *element;
  B32 element_dirty;
  F32 element_width;
  F32 element_rem;
  TH_Kind element_theme;

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

  // file watching
  U64 file_modified;
  U64 reload_at_us;
};

internal void mdp_view_init(MDP_View *view);
internal void mdp_view_load_text(MDP_View *view, String8 name, String8 path, String8 text);
internal B32  mdp_view_open_path(MDP_View *view, String8 path, B32 keep_scroll);
internal void mdp_view_poll_file(MDP_View *view);
internal B32  mdp_view_handle_event(MDP_View *view, WM_Window window, WM_Event *ev);
internal void mdp_view_draw(MDP_View *view, WM_Window window);
internal B32  mdp_view_is_animating(MDP_View *view);

#endif // MARKDOWN_PREVIEW_VIEW_H
