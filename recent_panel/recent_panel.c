////////////////////////////////
//~ Recent Files
//
// One path per line in <config>/mdview/recent_files, most recent first. The
// last_file an older mdview wrote seeds the list.

internal String8
rp_config_path(Arena *arena, String8 name)
{
  return push_str8f(arena, "%S/mdview/%S", get_process_info()->user_program_config_data_path, name);
}

internal B32
rp_path_match(String8 a, String8 b)
{
  return str8_match(a, b, StringMatchFlag_CaseInsensitive|StringMatchFlag_SlashInsensitive);
}

// Replaces the list; the paths may point into the panel's arena.
internal void
rp_recent_set(RP_Panel *panel, String8 *paths, U64 count)
{
  Temp scratch = scratch_begin(0, 0);
  count = Min(count, RP_MAX_FILES);
  String8 *copies = push_array(scratch.arena, String8, count);
  for EachIndex(i, count) { copies[i] = push_str8_copy(scratch.arena, paths[i]); }
  arena_clear(panel->arena);
  for EachIndex(i, count) { panel->paths[i] = push_str8_copy(panel->arena, copies[i]); }
  panel->path_count = count;
  scratch_end(scratch);
}

internal void
rp_recent_save(RP_Panel *panel)
{
  Temp scratch = scratch_begin(0, 0);
  String8List lines = {0};
  for EachIndex(i, panel->path_count) { str8_list_push(scratch.arena, &lines, panel->paths[i]); }
  StringJoin join = {str8_zero(), str8_lit("\n"), str8_lit("\n")};
  String8 file = rp_config_path(scratch.arena, str8_lit("recent_files"));
  make_directory(str8_chop_last_slash(file));
  write_data_to_file_path(file, str8_list_join(scratch.arena, &lines, &join));
  scratch_end(scratch);
}

internal void
rp_recent_load(RP_Panel *panel)
{
  Temp scratch = scratch_begin(0, 0);
  String8 data = data_from_file_path(scratch.arena, rp_config_path(scratch.arena, str8_lit("recent_files")));
  if(data.size == 0) { data = data_from_file_path(scratch.arena, rp_config_path(scratch.arena, str8_lit("last_file"))); }
  U8 splits[] = {'\n', '\r'};
  String8List lines = str8_split(scratch.arena, data, splits, ArrayCount(splits), 0);
  String8 *paths = push_array(scratch.arena, String8, RP_MAX_FILES);
  U64 count = 0;
  for(String8Node *n = lines.first; n != 0 && count < RP_MAX_FILES; n = n->next)
  {
    String8 path = str8_skip_chop_whitespace(n->string);
    if(path.size) { paths[count++] = path; }
  }
  rp_recent_set(panel, paths, count);
  scratch_end(scratch);
}

// Moves the path to the top of the list, adding it if it is new.
internal void
rp_recent_push(RP_Panel *panel, String8 path)
{
  if(path.size == 0) { return; }
  Temp scratch = scratch_begin(0, 0);
  String8 *paths = push_array(scratch.arena, String8, panel->path_count + 1);
  U64 count = 0;
  paths[count++] = path;
  for EachIndex(i, panel->path_count) { if(!rp_path_match(panel->paths[i], path)) { paths[count++] = panel->paths[i]; } }
  rp_recent_set(panel, paths, count);
  rp_recent_save(panel);
  scratch_end(scratch);
}

internal void
rp_recent_remove(RP_Panel *panel, String8 path)
{
  Temp scratch = scratch_begin(0, 0);
  String8 *paths = push_array(scratch.arena, String8, panel->path_count + 1);
  U64 count = 0;
  for EachIndex(i, panel->path_count) { if(!rp_path_match(panel->paths[i], path)) { paths[count++] = panel->paths[i]; } }
  rp_recent_set(panel, paths, count);
  rp_recent_save(panel);
  scratch_end(scratch);
}

////////////////////////////////
//~ Layout

typedef struct RP_Layout RP_Layout;
struct RP_Layout
{
  F32 s;
  Rng2F32 rect;                  // the panel, its right border included
  Rng2F32 handle;                // resize handle, centered on the right edge
  Rng2F32 header;
  Rng2F32 list;
  F32 row_h;
  F32 max_scroll;
};

internal F32
rp_scale(WM_Window window)
{
  F32 dpi = wm_dpi_from_window(window);
  return (dpi > 0) ? dpi/96.f : 1.f;
}

internal F32
rp_panel_width_px(RP_Panel *panel, WM_Window window)
{
  if(!panel->open) { return 0; }
  F32 s = rp_scale(window);
  Rng2F32 client = wm_client_rect_from_window(window);
  F32 max_w = Max(RP_MIN_WIDTH_PX*s, (client.x1 - client.x0)*0.5f);
  return round_f32(Clamp(RP_MIN_WIDTH_PX*s, panel->width*s, max_w));
}

internal RP_Layout
rp_layout(RP_Panel *panel, MDP_View *view, WM_Window window)
{
  RP_Layout l = {0};
  F32 s = l.s = rp_scale(window);
  Rng2F32 client = wm_client_rect_from_window(window);
  F32 w = rp_panel_width_px(panel, window);
  l.rect = r2f32p(client.x0, Min(client.y0 + view->top_inset, client.y1), client.x0 + w, client.y1);
  F32 border = Max(1.f, round_f32(s));
  F32 half = round_f32(RP_RESIZE_HANDLE_PX*s/2);
  l.handle = r2f32p(l.rect.x1 - half, l.rect.y0, l.rect.x1 + half, l.rect.y1);
  F32 inner_x1 = l.rect.x1 - border;
  l.header = r2f32p(l.rect.x0, l.rect.y0, inner_x1, Min(l.rect.y0 + round_f32(RP_HEADER_HEIGHT_PX*s), l.rect.y1));
  l.list = r2f32p(l.rect.x0, l.header.y1, inner_x1, l.rect.y1);
  l.row_h = round_f32(RP_ROW_HEIGHT_PX*s);
  l.max_scroll = ClampBot(0, panel->path_count*l.row_h - (l.list.y1 - l.list.y0));
  panel->scroll_y = Clamp(0, panel->scroll_y, l.max_scroll);
  return l;
}

internal Rng2F32
rp_row_rect(RP_Layout *l, RP_Panel *panel, U64 row)
{
  F32 y = l->list.y0 - round_f32(panel->scroll_y) + row*l->row_h;
  return r2f32p(l->list.x0, y, l->list.x1, y + l->row_h);
}

internal U64
rp_row_at(RP_Layout *l, RP_Panel *panel, Vec2F32 pos)
{
  if(!contains_2f32(l->list, pos)) { return max_U64; }
  F32 y = pos.y - l->list.y0 + round_f32(panel->scroll_y);
  U64 row = (U64)(y / l->row_h);
  return (row < panel->path_count) ? row : max_U64;
}

// the hovered row's close button, at its right end
internal Rng2F32
rp_remove_rect(RP_Layout *l, Rng2F32 row)
{
  F32 s = l->s;
  F32 b = round_f32(20.f*s);
  F32 x1 = row.x1 - round_f32(6.f*s);
  F32 y0 = round_f32((row.y0 + row.y1 - b)/2);
  return r2f32p(x1 - b, y0, x1, y0 + b);
}

////////////////////////////////
//~ Panel

internal void
rp_panel_init(RP_Panel *panel)
{
  MemoryZeroStruct(panel);
  panel->arena = arena_alloc();
  panel->open = 1;
  panel->width = RP_DEFAULT_WIDTH_PX;
  panel->activated = max_U64;
}

internal B32
rp_panel_handle_event(RP_Panel *panel, MDP_View *view, WM_Window window, WM_Event *ev)
{
  if(!panel->open) { return 0; }
  RP_Layout l = rp_layout(panel, view, window);
  B32 taken = 0;
  switch(ev->kind)
  {
    default:{}break;

    case WM_EventKind_MouseMove:
    {
      if(panel->resizing)
      {
        panel->width = ClampBot(RP_MIN_WIDTH_PX, (ev->pos.x - l.rect.x0)/l.s);
        taken = 1;
      }
    }break;

    case WM_EventKind_Release:
    {
      if(ev->key == WM_Key_LeftMouseButton && panel->resizing) { panel->resizing = 0; taken = 1; }
    }break;

    case WM_EventKind_Scroll:
    {
      if(contains_2f32(l.rect, ev->pos) && !(ev->modifiers & WM_Modifier_Ctrl))
      {
        panel->scroll_y = Clamp(0, panel->scroll_y - ev->delta.y/120.f*3.f*l.row_h, l.max_scroll);
        panel->scroll_activity_us = now_time_us();
        taken = 1;
      }
    }break;

    case WM_EventKind_Press:
    {
      //- the resize handle, then a row: its close button forgets it, the rest opens it
      if(ev->key == WM_Key_LeftMouseButton)
      {
        if(contains_2f32(l.handle, ev->pos)) { panel->resizing = 1; taken = 1; break; }
        if(!contains_2f32(l.rect, ev->pos)) { break; }
        view->search_focused = 0;
        view->menu_open = 0;
        taken = 1;
        U64 row = rp_row_at(&l, panel, ev->pos);
        if(row == max_U64) { break; }
        if(contains_2f32(rp_remove_rect(&l, rp_row_rect(&l, panel, row)), ev->pos)) { rp_recent_remove(panel, panel->paths[row]); }
        else { panel->activated = row; }
        break;
      }
      if(ev->key == WM_Key_RightMouseButton && contains_2f32(l.rect, ev->pos)) { taken = 1; }
    }break;
  }
  return taken;
}

// The path of the row clicked since the last call, or empty.
internal String8
rp_panel_take_activated(Arena *arena, RP_Panel *panel)
{
  String8 path = {0};
  if(panel->activated < panel->path_count) { path = push_str8_copy(arena, panel->paths[panel->activated]); }
  panel->activated = max_U64;
  return path;
}

internal B32
rp_panel_is_animating(RP_Panel *panel)
{
  return panel->open && panel->scroll_activity_us && now_time_us() < panel->scroll_activity_us + MDP_SCROLLBAR_HIDE_DELAY_US + 50000;
}

////////////////////////////////
//~ Drawing

internal void
rp_panel_draw(RP_Panel *panel, MDP_View *view, WM_Window window)
{
  if(!panel->open) { return; }
  RP_Layout l = rp_layout(panel, view, window);
  TH_Colors *colors = &view->theme.colors;
  MD_Fonts *fonts = &view->fonts;
  F32 s = l.s;
  F32 line = Max(1.f, round_f32(s));
  Vec2F32 mouse = wm_mouse_from_window(window);
  Vec4F32 bg = colors->panel_background;
  Vec4F32 hover_bg = th_over(bg, colors->ghost_element_hover);
  F32 ui_px = round_f32(RP_UI_TEXT_PX*s);
  F32 pad = round_f32(8.f*s);

  //- the dock: panel_background, border_r_1
  dr_rect(l.rect, th_linear(bg), 0, 0, 0);
  dr_rect(r2f32p(l.rect.x1 - line, l.rect.y0, l.rect.x1, l.rect.y1), th_linear(colors->border), 0, 0, 0);

  //- header, border_b_1
  dr_rect(r2f32p(l.header.x0, l.header.y1 - line, l.header.x1, l.header.y1), th_linear(colors->border), 0, 0, 0);
  DR_ClipScope(l.header)
  {
    mdp_draw_text_centered_y(fonts->sans, ui_px, r2f32p(l.header.x0, l.header.y0, l.header.x1, l.header.y1 - line), l.header.x0 + pad, str8_lit("Recent Files"), colors->text_muted);
  }

  //- rows: ghost hover, or ghost_element_selected for the open document; a
  // file icon, the name, its folder muted, and on hover a close button
  B32 list_hovered = contains_2f32(l.list, mouse) && !panel->resizing && !contains_2f32(l.handle, mouse);
  U64 hovered_row = list_hovered ? rp_row_at(&l, panel, mouse) : max_U64;
  U64 first = (U64)(panel->scroll_y / l.row_h);
  U64 last = Min(panel->path_count, (U64)((panel->scroll_y + (l.list.y1 - l.list.y0)) / l.row_h) + 1);
  DR_ClipScope(l.list)
  {
    for(U64 r = first; r < last; r += 1)
    {
      String8 path = panel->paths[r];
      Rng2F32 row = rp_row_rect(&l, panel, r);
      B32 active = (view->path.size && rp_path_match(path, view->path));
      B32 hovered = (r == hovered_row);
      Vec4F32 row_bg = active ? colors->ghost_element_selected : hovered ? hover_bg : bg;
      if(active) { dr_rect(r2f32p(row.x0 + line, row.y0 + line, row.x1 - 2*line, row.y1 - line), th_linear(row_bg), 0, 0, 0); }
      else if(hovered) { dr_rect(row, th_linear(row_bg), 0, 0, 0); }

      F32 size = round_f32(16.f*s);
      F32 x0 = row.x0 + line + round_f32(6.f*s);
      F32 y0 = round_f32((row.y0 + row.y1 - size)/2);
      ic_draw(IC_Icon_File, r2f32p(x0, y0, x0 + size, y0 + size), IC_SIZE_SMALL_PX*s, active ? colors->text : colors->text_muted);

      Rng2F32 label = r2f32p(x0 + size + round_f32(6.f*s), row.y0, row.x1 - 2*line - round_f32(6.f*s), row.y1);
      if(hovered)
      {
        Rng2F32 remove = rp_remove_rect(&l, row);
        label.x1 = remove.x0 - round_f32(4.f*s);
        if(contains_2f32(remove, mouse)) { dr_rect(remove, th_linear(th_over(row_bg, colors->ghost_element_hover)), 4.f*s, 0, 0.5f); }
        ic_draw(IC_Icon_Close, remove, IC_SIZE_SMALL_PX*s, colors->text_muted);
      }
      DR_ClipScope(intersect_2f32(label, l.list))
      {
        String8 name = str8_skip_last_slash(path);
        String8 folder = str8_chop_last_slash(path);
        mdp_draw_text_centered_y(fonts->sans, ui_px, label, label.x0, name, colors->text);
        F32 x = label.x0 + md_text_width(fonts->sans, ui_px, name) + round_f32(6.f*s);
        mdp_draw_text_centered_y(fonts->sans, ui_px, label, x, folder, colors->text_muted);
      }
    }

    //- scrollbar: the thumb while the panel is hovered or just scrolled
    if(l.max_scroll > 0 && (contains_2f32(l.rect, mouse) || rp_panel_is_animating(panel)))
    {
      F32 spad = MDP_SCROLLBAR_PADDING_PX*s;
      F32 width = MDP_SCROLLBAR_WIDTH_PX*s;
      F32 list_h = l.list.y1 - l.list.y0;
      F32 space = list_h - 2*spad;
      F32 total = panel->path_count*l.row_h;
      F32 thumb_h = Min(space, Max(MDP_SCROLLBAR_MIN_THUMB_PX*s, list_h*(list_h/total)));
      F32 thumb_y = l.list.y0 + spad + (panel->scroll_y/l.max_scroll)*(space - thumb_h);
      Rng2F32 thumb = r2f32p(l.list.x1 - spad - width, thumb_y, l.list.x1 - spad, thumb_y + thumb_h);
      Vec4F32 c = th_over(bg, contains_2f32(thumb, mouse) ? colors->scrollbar_thumb_hover_background : colors->scrollbar_thumb_background);
      dr_rect(thumb, th_linear(c), width/2, 0, 0.5f);
    }
  }

  //- empty state: the header, and how to open a file
  if(panel->path_count == 0)
  {
    String8 header = str8_lit("No recent files");
    String8 hint = str8_lit("Open a File With");
    String8 binding = str8_lit("Ctrl+O");
    F32 line_h = round_f32(ui_px*1.43f);
    F32 gap = round_f32(2.f*s);
    F32 y = round_f32((l.list.y0 + l.list.y1 - (2*line_h + gap))/2);
    F32 cx = (l.list.x0 + l.list.x1)/2;
    DR_ClipScope(l.list)
    {
      Rng2F32 r = r2f32p(l.list.x0, y, l.list.x1, y + line_h);
      mdp_draw_text_centered_y(fonts->sans, ui_px, r, cx - md_text_width(fonts->sans, ui_px, header)/2, header, colors->text);
      y += line_h + gap;
      r = r2f32p(l.list.x0, y, l.list.x1, y + line_h);
      F32 hint_w = md_text_width(fonts->sans, ui_px, hint);
      F32 binding_w = md_text_width(fonts->sans, ui_px, binding);
      F32 x = cx - (hint_w + 4.f*s + binding_w)/2;
      mdp_draw_text_centered_y(fonts->sans, ui_px, r, x, hint, colors->text_muted);
      mdp_draw_text_centered_y(fonts->sans, ui_px, r, x + hint_w + 4.f*s, binding, colors->text);
    }
  }

  //- cursor
  if(panel->resizing || contains_2f32(l.handle, mouse)) { wm_set_cursor(WM_Cursor_LeftRight); }
  else if(hovered_row != max_U64) { wm_set_cursor(WM_Cursor_HandPoint); }
  else if(contains_2f32(l.rect, mouse)) { wm_set_cursor(WM_Cursor_Pointer); }
}
