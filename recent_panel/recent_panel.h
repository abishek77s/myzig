// recent_panel: the files mdview has opened, in a sidebar at the window's left
// edge, before the outline panel. It takes the place of the title bar's tab,
// and is toggled like Zed's threads sidebar (its icon, mirrored to the left,
// and ctrl-alt-j).
//
// A dock panel (panel_background, a border on its right edge, a 6px resize
// handle over it) holding a header row and a uniform list of files, most
// recent first: a file icon, the file name, and its folder, muted. The open
// document's row is selected. Clicking a row opens that file; the close button
// on a hovered row forgets it. The list is kept in the user's config folder,
// like raddbg's last_user, and its first file is reopened when mdview starts
// without a path.

#ifndef RECENT_PANEL_H
#define RECENT_PANEL_H

typedef struct RP_Panel RP_Panel;
struct RP_Panel
{
  B32 open;
  F32 width;                     // logical px
  B32 resizing;

  // most recent first, in arena
  Arena *arena;
  String8 paths[RP_MAX_FILES];
  U64 path_count;

  // list
  F32 scroll_y;                  // physical px
  U64 scroll_activity_us;
  U64 activated;                 // row clicked, until taken; max_U64 = none
};

internal void    rp_panel_init(RP_Panel *panel);
internal void    rp_recent_load(RP_Panel *panel);
internal void    rp_recent_push(RP_Panel *panel, String8 path);
internal void    rp_recent_remove(RP_Panel *panel, String8 path);
internal F32     rp_panel_width_px(RP_Panel *panel, WM_Window window);
internal B32     rp_panel_handle_event(RP_Panel *panel, MDP_View *view, WM_Window window, WM_Event *ev);
internal String8 rp_panel_take_activated(Arena *arena, RP_Panel *panel);
internal void    rp_panel_draw(RP_Panel *panel, MDP_View *view, WM_Window window);
internal B32     rp_panel_is_animating(RP_Panel *panel);

#endif // RECENT_PANEL_H
