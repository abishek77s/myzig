////////////////////////////////
//~ Entries
//
// The outline of a Markdown buffer: one item per heading, its text alone,
// nested by section (depth = the number of enclosing headings of a lower
// level). Collapsed items hide their descendants. A query turns the outline
// into a flat list of the items it fuzzy-matches, collapsed ones included,
// best match first.

// The document's headings, from the preview's scan.
typedef struct OP_Headings OP_Headings;
struct OP_Headings
{
  MD_ScanHeading *v;
  U64 count;
};

internal OP_Headings
op_headings(MDP_View *view)
{
  OP_Headings hs = {0};
  hs.v = mdp_view_headings(view, &hs.count);
  return hs;
}

internal U64
op_collapse_key(MD_ScanHeading *h)
{
  Temp scratch = scratch_begin(0, 0);
  U64 key = u64_hash_from_str8(push_str8f(scratch.arena, "%I64u:%S", h->level, h->text));
  scratch_end(scratch);
  return key;
}

internal B32
op_is_collapsed(OP_Panel *panel, MD_ScanHeading *h)
{
  if(panel->collapsed_count == 0) { return 0; }
  U64 key = op_collapse_key(h);
  for EachIndex(i, panel->collapsed_count) { if(panel->collapsed[i] == key) { return 1; } }
  return 0;
}

internal void
op_toggle_collapsed(OP_Panel *panel, MD_ScanHeading *h)
{
  U64 key = op_collapse_key(h);
  for EachIndex(i, panel->collapsed_count)
  {
    if(panel->collapsed[i] == key)
    {
      panel->collapsed[i] = panel->collapsed[--panel->collapsed_count];
      return;
    }
  }
  if(panel->collapsed_count < ArrayCount(panel->collapsed)) { panel->collapsed[panel->collapsed_count++] = key; }
}

internal String8
op_query(OP_Panel *panel)
{
  return str8_skip_chop_whitespace(str8(panel->query, panel->query_size));
}

// fuzzy::match_strings with smart case: every query character, in order;
// case-insensitive unless the query has an uppercase letter. Like Zed, a
// matched character scores for continuing a run and for starting a word, so a
// substring match outranks scattered letters.
internal B32
op_fuzzy_match(Arena *arena, String8 candidate, String8 query, U64 **positions_out, U64 *count_out, U64 *score_out)
{
  B32 case_sensitive = 0;
  for EachIndex(i, query.size) { if(char_is_upper(query.str[i])) { case_sensitive = 1; break; } }
  U64 *positions = push_array(arena, U64, query.size);
  U64 count = 0;
  U64 sub = str8_find_needle(candidate, 0, query, case_sensitive ? 0 : StringMatchFlag_CaseInsensitive);
  if(sub < candidate.size)
  {
    for EachIndex(i, query.size) { positions[i] = sub + i; }
    count = query.size;
  }
  else
  {
    U64 at = 0;
    for EachIndex(qi, query.size)
    {
      U8 q = case_sensitive ? query.str[qi] : lower_from_char(query.str[qi]);
      for(;at < candidate.size; at += 1)
      {
        U8 c = case_sensitive ? candidate.str[at] : lower_from_char(candidate.str[at]);
        if(c == q) { break; }
      }
      if(at == candidate.size) { return 0; }
      positions[count++] = at;
      at += 1;
    }
  }
  U64 score = 0;
  for EachIndex(i, count)
  {
    U64 p = positions[i];
    if(i > 0 && positions[i-1] + 1 == p) { score += 2; }
    if(p == 0 || !(char_is_alpha(candidate.str[p-1]) || char_is_digit(candidate.str[p-1], 10))) { score += 1; }
  }
  *positions_out = positions;
  *count_out = count;
  *score_out = score;
  return 1;
}

typedef struct OP_Outline OP_Outline;
struct OP_Outline
{
  U64 *depths;                   // per heading
  OP_Entry *entries;             // the visible rows
  U64 entry_count;
};

internal OP_Outline
op_outline(Arena *arena, OP_Panel *panel, OP_Headings *el)
{
  OP_Outline o = {0};
  U64 n = el->count;
  o.depths = push_array(arena, U64, n + 1);
  o.entries = push_array(arena, OP_Entry, n + 1);
  {
    U64 stack[8];
    U64 top = 0;
    for EachIndex(i, n)
    {
      U64 level = el->v[i].level;
      while(top > 0 && stack[top-1] >= level) { top -= 1; }
      o.depths[i] = top;
      if(top < ArrayCount(stack)) { stack[top++] = level; }
    }
  }
  String8 query = op_query(panel);
  if(query.size)
  {
    for EachIndex(i, n)
    {
      OP_Entry e = {0};
      e.heading = i;
      e.label = el->v[i].text;
      if(!op_fuzzy_match(arena, e.label, query, &e.match_positions, &e.match_count, &e.match_score)) { continue; }
      // stable insertion by score: ties keep document order
      U64 r = o.entry_count++;
      for(;r > 0 && o.entries[r-1].match_score < e.match_score; r -= 1) { o.entries[r] = o.entries[r-1]; }
      o.entries[r] = e;
    }
    return o;
  }
  U64 hidden_below = max_U64;
  for EachIndex(i, n)
  {
    MD_ScanHeading *h = &el->v[i];
    U64 d = o.depths[i];
    if(hidden_below != max_U64 && d > hidden_below) { continue; }
    hidden_below = max_U64;
    OP_Entry e = {0};
    e.heading = i;
    e.depth = d;
    e.has_children = (i+1 < n && o.depths[i+1] > d);
    e.collapsed = e.has_children && op_is_collapsed(panel, h);
    if(e.collapsed) { hidden_below = d; }
    e.label = h->text;
    o.entries[o.entry_count++] = e;
  }
  return o;
}

// The row that shows the selection: the selected heading's, or its nearest
// visible ancestor's when it is collapsed away.
internal U64
op_selected_row(OP_Panel *panel, OP_Outline *o, OP_Headings *el)
{
  U64 h = panel->selected;
  while(h < el->count)
  {
    for EachIndex(r, o->entry_count) { if(o->entries[r].heading == h) { return r; } }
    U64 d = o->depths[h];
    if(d == 0) { break; }
    U64 parent = max_U64;
    for(U64 j = h; j > 0; j -= 1) { if(o->depths[j-1] < d) { parent = j-1; break; } }
    h = parent;
  }
  return max_U64;
}

////////////////////////////////
//~ Layout

typedef struct OP_Layout OP_Layout;
struct OP_Layout
{
  F32 s;
  Rng2F32 rect;                  // the panel, its right border included
  Rng2F32 handle;                // resize handle, centered on the right edge
  Rng2F32 filter;                // filter row
  Rng2F32 search_icon;
  Rng2F32 editor;
  Rng2F32 clear;                 // clear button, with a query
  Rng2F32 list;
  F32 row_h;
  F32 max_scroll;
  OP_Outline outline;
};

internal F32
op_scale(WM_Window window)
{
  F32 dpi = wm_dpi_from_window(window);
  return (dpi > 0) ? dpi/96.f : 1.f;
}

internal F32
op_panel_width_px(OP_Panel *panel, WM_Window window)
{
  if(!panel->open) { return 0; }
  F32 s = op_scale(window);
  Rng2F32 client = wm_client_rect_from_window(window);
  F32 max_w = Max(OP_MIN_WIDTH_PX*s, (client.x1 - client.x0)*0.75f);
  return round_f32(Clamp(OP_MIN_WIDTH_PX*s, panel->width*s, max_w));
}

internal OP_Layout
op_layout(Arena *arena, OP_Panel *panel, MDP_View *view, WM_Window window)
{
  OP_Layout l = {0};
  F32 s = l.s = op_scale(window);
  mdp_geometry(view, window);                      // lays the element out
  Rng2F32 client = wm_client_rect_from_window(window);
  F32 w = op_panel_width_px(panel, window);
  F32 x0 = Min(client.x0 + panel->x0, client.x1);
  l.rect = r2f32p(x0, Min(client.y0 + view->top_inset, client.y1), x0 + w, client.y1);
  F32 border = Max(1.f, round_f32(s));
  F32 half = round_f32(OP_RESIZE_HANDLE_PX*s/2);
  l.handle = r2f32p(l.rect.x1 - half, l.rect.y0, l.rect.x1 + half, l.rect.y1);
  F32 inner_x1 = l.rect.x1 - border;

  //- filter row: p_2, gap_1p5, the magnifying glass, the editor, the clear button
  l.filter = r2f32p(l.rect.x0, l.rect.y0, inner_x1, Min(l.rect.y0 + round_f32(OP_FILTER_HEIGHT_PX*s), l.rect.y1));
  F32 p = round_f32(8.f*s);
  Rng2F32 content = r2f32p(l.filter.x0 + p, l.filter.y0 + p, l.filter.x1 - p, l.filter.y1 - p - border);
  l.search_icon = r2f32p(content.x0, content.y0, content.x0 + round_f32(IC_SIZE_SMALL_PX*s), content.y1);
  F32 editor_x1 = content.x1;
  if(op_query(panel).size)
  {
    F32 b = round_f32(20.f*s);
    F32 cy = round_f32((content.y0 + content.y1 - b)/2);
    l.clear = r2f32p(content.x1 - b, cy, content.x1, cy + b);
    editor_x1 = l.clear.x0 - round_f32(4.f*s);
  }
  l.editor = r2f32p(l.search_icon.x1 + round_f32(6.f*s), content.y0, Max(l.search_icon.x1, editor_x1), content.y1);

  //- the list
  l.list = r2f32p(l.rect.x0, l.filter.y1, inner_x1, l.rect.y1);
  l.row_h = round_f32(OP_ROW_HEIGHT_PX*s);
  OP_Headings headings = op_headings(view);
  l.outline = op_outline(arena, panel, &headings);
  F32 content_h = l.outline.entry_count*l.row_h;
  l.max_scroll = ClampBot(0, content_h - (l.list.y1 - l.list.y0));
  panel->scroll_y = Clamp(0, panel->scroll_y, l.max_scroll);
  return l;
}

internal Rng2F32
op_row_rect(OP_Layout *l, OP_Panel *panel, U64 row)
{
  F32 y = l->list.y0 - round_f32(panel->scroll_y) + row*l->row_h;
  return r2f32p(l->list.x0, y, l->list.x1, y + l->row_h);
}

// ListItem: border_1 + indent_level*indent_step_size + px(Base06), then the
// 16px icon slot
internal Rng2F32
op_icon_slot(OP_Layout *l, Rng2F32 row, U64 depth)
{
  F32 s = l->s;
  F32 x0 = row.x0 + Max(1.f, round_f32(s)) + round_f32(depth*OP_INDENT_SIZE_PX*s) + round_f32(6.f*s);
  F32 size = round_f32(16.f*s);
  F32 y0 = round_f32((row.y0 + row.y1 - size)/2);
  return r2f32p(x0, y0, x0 + size, y0 + size);
}

internal U64
op_row_at(OP_Layout *l, OP_Panel *panel, Vec2F32 pos)
{
  if(!contains_2f32(l->list, pos)) { return max_U64; }
  F32 y = pos.y - l->list.y0 + round_f32(panel->scroll_y);
  U64 row = (U64)(y / l->row_h);
  return (row < l->outline.entry_count) ? row : max_U64;
}

////////////////////////////////
//~ Panel

internal void
op_panel_init(OP_Panel *panel)
{
  MemoryZeroStruct(panel);
  panel->open = 1;
  panel->width = OP_DEFAULT_WIDTH_PX;
  panel->selected = max_U64;
}

internal void
op_select_row(OP_Panel *panel, MDP_View *view, WM_Window window, OP_Layout *l, U64 row)
{
  if(row >= l->outline.entry_count) { return; }
  panel->selected = l->outline.entries[row].heading;
  panel->autoscroll = 1;
  mdp_view_scroll_to_heading_index(view, window, panel->selected);
}

internal void
op_query_changed(OP_Panel *panel)
{
  panel->scroll_y = 0;
  panel->autoscroll = 1;
}

internal void
op_query_insert(OP_Panel *panel, String8 text)
{
  for EachIndex(i, text.size)
  {
    U8 c = text.str[i];
    if(c == '\n' || c == '\r') { break; }
    if(panel->query_size < ArrayCount(panel->query)) { panel->query[panel->query_size++] = c; }
  }
  op_query_changed(panel);
}

internal B32
op_panel_handle_event(OP_Panel *panel, MDP_View *view, WM_Window window, WM_Event *ev)
{
  if(!panel->open) { return 0; }
  Temp scratch = scratch_begin(0, 0);
  OP_Layout l = op_layout(scratch.arena, panel, view, window);
  OP_Outline *o = &l.outline;
  OP_Headings headings = op_headings(view);
  OP_Headings *el = &headings;
  B32 taken = 0;
  switch(ev->kind)
  {
    default:{}break;

    case WM_EventKind_MouseMove:
    {
      if(panel->resizing)
      {
        panel->width = ClampBot(OP_MIN_WIDTH_PX, (ev->pos.x - l.rect.x0)/l.s);
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

    case WM_EventKind_Text:
    {
      if(panel->focused)
      {
        U32 cp = ev->character;
        if(cp >= 32 && cp != 127)
        {
          U8 buf[8];
          U64 size = utf8_encode(buf, cp);
          op_query_insert(panel, str8(buf, size));
        }
        taken = 1;
      }
    }break;

    case WM_EventKind_Press:
    {
      //- mouse: the resize handle, then the panel; a press elsewhere blurs it
      if(ev->key == WM_Key_LeftMouseButton)
      {
        if(contains_2f32(l.handle, ev->pos))
        {
          panel->resizing = 1;
          taken = 1;
          break;
        }
        if(!contains_2f32(l.rect, ev->pos)) { panel->focused = 0; break; }
        panel->focused = 1;
        view->search_focused = 0;
        view->menu_open = 0;
        taken = 1;
        if(contains_2f32(l.clear, ev->pos)) { panel->query_size = 0; op_query_changed(panel); break; }
        U64 row = op_row_at(&l, panel, ev->pos);
        if(row != max_U64)
        {
          OP_Entry *e = &o->entries[row];
          Rng2F32 slot = op_icon_slot(&l, op_row_rect(&l, panel, row), e->depth);
          if(e->has_children && contains_2f32(slot, ev->pos)) { op_toggle_collapsed(panel, &el->v[e->heading]); }
          else { op_select_row(panel, view, window, &l, row); }
        }
        break;
      }
      if(ev->key == WM_Key_RightMouseButton && contains_2f32(l.rect, ev->pos)) { taken = 1; break; }
      if(!panel->focused) { break; }

      //- keys, while focused: the filter editor, and the list's menu::Select*
      WM_Modifiers m = ev->modifiers;
      U64 row = op_selected_row(panel, o, el);
      switch(ev->key)
      {
        default:{}break;
        case WM_Key_Esc:
        {
          if(panel->query_size) { panel->query_size = 0; op_query_changed(panel); }
          else { panel->focused = 0; }
          taken = 1;
        }break;
        case WM_Key_Backspace:
        {
          if(m & WM_Modifier_Ctrl) { panel->query_size = 0; }
          else
          {
            U64 n = panel->query_size;
            for(;n > 0;) { n -= 1; if((panel->query[n] & 0xC0) != 0x80) { break; } }
            panel->query_size = n;
          }
          op_query_changed(panel);
          taken = 1;
        }break;
        case WM_Key_V:
        {
          if(m == WM_Modifier_Ctrl) { op_query_insert(panel, wm_get_clipboard_text(scratch.arena)); taken = 1; }
        }break;
        case WM_Key_Down:
        {
          op_select_row(panel, view, window, &l, (row == max_U64) ? 0 : Min(row + 1, o->entry_count - 1));
          taken = 1;
        }break;
        case WM_Key_Up:
        {
          op_select_row(panel, view, window, &l, (row == max_U64 || row == 0) ? 0 : row - 1);
          taken = 1;
        }break;
        case WM_Key_Home: { op_select_row(panel, view, window, &l, 0); taken = 1; }break;
        case WM_Key_End:  { if(o->entry_count) { op_select_row(panel, view, window, &l, o->entry_count - 1); } taken = 1; }break;
        case WM_Key_Left:
        {
          // CollapseSelectedEntry, or up to the parent
          if(row != max_U64)
          {
            OP_Entry *e = &o->entries[row];
            if(e->has_children && !e->collapsed) { op_toggle_collapsed(panel, &el->v[e->heading]); }
            else
            {
              for(U64 r = row; r > 0; r -= 1) { if(o->entries[r-1].depth < e->depth) { op_select_row(panel, view, window, &l, r-1); break; } }
            }
          }
          taken = 1;
        }break;
        case WM_Key_Right:
        {
          // ExpandSelectedEntry, or down to the first child
          if(row != max_U64)
          {
            OP_Entry *e = &o->entries[row];
            if(e->has_children && e->collapsed) { op_toggle_collapsed(panel, &el->v[e->heading]); }
            else if(e->has_children) { op_select_row(panel, view, window, &l, row + 1); }
          }
          taken = 1;
        }break;
        case WM_Key_Return:
        {
          // Open: scroll to it and hand focus back to the preview
          if(row != max_U64) { op_select_row(panel, view, window, &l, row); }
          panel->focused = 0;
          taken = 1;
        }break;
        case WM_Key_F:
        {
          // the preview's search takes focus
          if(m == WM_Modifier_Ctrl) { panel->focused = 0; }
        }break;
      }
    }break;
  }
  scratch_end(scratch);
  return taken;
}

// auto_reveal_entries: while the panel is not focused, the selection follows
// the heading at the top of the preview; a new selection is scrolled to the
// list's center when it is out of view.
internal void
op_panel_update(OP_Panel *panel, MDP_View *view, WM_Window window)
{
  if(!panel->open) { return; }
  if(!panel->focused)
  {
    U64 h = mdp_view_heading_index_at_top(view, window);
    if(h != panel->selected) { panel->selected = h; panel->autoscroll = 1; }
  }
  if(panel->autoscroll)
  {
    panel->autoscroll = 0;
    Temp scratch = scratch_begin(0, 0);
    OP_Layout l = op_layout(scratch.arena, panel, view, window);
    OP_Headings headings = op_headings(view);
    U64 row = op_selected_row(panel, &l.outline, &headings);
    if(row != max_U64)
    {
      F32 y = row*l.row_h;
      F32 list_h = l.list.y1 - l.list.y0;
      if(y < panel->scroll_y || y + l.row_h > panel->scroll_y + list_h)
      {
        panel->scroll_y = Clamp(0, y - (list_h - l.row_h)/2, l.max_scroll);
      }
    }
    scratch_end(scratch);
  }
}

internal B32
op_panel_is_animating(OP_Panel *panel)
{
  return panel->open && panel->scroll_activity_us && now_time_us() < panel->scroll_activity_us + MDP_SCROLLBAR_HIDE_DELAY_US + 50000;
}

////////////////////////////////
//~ Drawing

internal void
op_panel_draw(OP_Panel *panel, MDP_View *view, WM_Window window)
{
  if(!panel->open) { return; }
  Temp scratch = scratch_begin(0, 0);
  OP_Layout l = op_layout(scratch.arena, panel, view, window);
  OP_Outline *o = &l.outline;
  OP_Headings headings = op_headings(view);
  OP_Headings *el = &headings;
  TH_Colors *colors = &view->theme.colors;
  MD_Fonts *fonts = &view->fonts;
  F32 s = l.s;
  F32 line = Max(1.f, round_f32(s));
  Vec2F32 mouse = wm_mouse_from_window(window);
  Vec4F32 bg = colors->panel_background;
  Vec4F32 hover_bg = th_over(bg, colors->ghost_element_hover);
  F32 ui_px = round_f32(OP_UI_TEXT_PX*s);

  //- the dock: panel_background, border_r_1
  dr_rect(l.rect, th_linear(bg), 0, 0, 0);
  dr_rect(r2f32p(l.rect.x1 - line, l.rect.y0, l.rect.x1, l.rect.y1), th_linear(colors->border), 0, 0, 0);

  //- filter row, border_b_1
  dr_rect(r2f32p(l.filter.x0, l.filter.y1 - line, l.filter.x1, l.filter.y1), th_linear(colors->border), 0, 0, 0);
  ic_draw(IC_Icon_MagnifyingGlass, l.search_icon, IC_SIZE_SMALL_PX*s, colors->text_muted);
  DR_ClipScope(l.editor)
  {
    String8 q = str8(panel->query, panel->query_size);
    F32 w = md_text_width(fonts->sans, ui_px, q);
    F32 x = l.editor.x0 - ClampBot(0, w + 2*s - (l.editor.x1 - l.editor.x0));
    if(q.size) { mdp_draw_text_centered_y(fonts->sans, ui_px, l.editor, x, q, colors->editor_foreground); }
    else { mdp_draw_text_centered_y(fonts->sans, ui_px, l.editor, l.editor.x0, str8_lit("Search\xe2\x80\xa6"), colors->text_placeholder); }
    if(panel->focused)
    {
      F32 cx = round_f32(x + w);
      F32 ch = round_f32(ui_px*1.2f);
      F32 cy = round_f32((l.editor.y0 + l.editor.y1)/2 - ch/2);
      dr_rect(r2f32p(cx, cy, cx + Max(1.f, round_f32(2.f*s)), cy + ch), th_linear(colors->text_accent), 0, 0, 0);
    }
  }
  if(op_query(panel).size)
  {
    if(contains_2f32(l.clear, mouse)) { dr_rect(l.clear, th_linear(hover_bg), 4.f*s, 0, 0.5f); }
    ic_draw(IC_Icon_Close, l.clear, IC_SIZE_MEDIUM_PX*s, colors->text);
  }

  //- rows
  U64 selected_row = op_selected_row(panel, o, el);
  B32 list_hovered = contains_2f32(l.list, mouse) && !panel->resizing && !contains_2f32(l.handle, mouse);
  U64 hovered_row = list_hovered ? op_row_at(&l, panel, mouse) : max_U64;
  F32 label_px = round_f32(OP_LABEL_PX*s);
  U64 first = (U64)(panel->scroll_y / l.row_h);
  U64 last = Min(o->entry_count, (U64)((panel->scroll_y + (l.list.y1 - l.list.y0)) / l.row_h) + 1);
  DR_ClipScope(l.list)
  {
    //- backgrounds: ghost hover over the whole row, unless active;
    // ghost_element_selected inside the borders, framed in
    // panel_focused_border (1px, 2px on the right) while focused
    for(U64 r = first; r < last; r += 1)
    {
      Rng2F32 row = op_row_rect(&l, panel, r);
      if(r == selected_row)
      {
        dr_rect(r2f32p(row.x0 + line, row.y0 + line, row.x1 - 2*line, row.y1 - line), th_linear(colors->ghost_element_selected), 0, 0, 0);
        if(panel->focused)
        {
          Vec4F32 c = th_linear(colors->panel_focused_border);
          dr_rect(r2f32p(row.x0, row.y0, row.x1, row.y0 + line), c, 0, 0, 0);
          dr_rect(r2f32p(row.x0, row.y1 - line, row.x1, row.y1), c, 0, 0, 0);
          dr_rect(r2f32p(row.x0, row.y0, row.x0 + line, row.y1), c, 0, 0, 0);
          dr_rect(r2f32p(row.x1 - 2*line, row.y0, row.x1, row.y1), c, 0, 0, 0);
        }
      }
      else if(r == hovered_row) { dr_rect(row, th_linear(hover_bg), 0, 0, 0); }
    }

    //- indent guides: at level k, through each run of rows deeper than k, at
    // the center of level k's icon slot. The active one is the selection's
    // children's, or its enclosing one for a leaf.
    {
      U64 active_level = max_U64, active_row = max_U64;
      if(selected_row != max_U64)
      {
        U64 t = selected_row;
        U64 d = o->entries[t].depth;
        if(t+1 < o->entry_count && o->entries[t+1].depth > d) { active_row = t+1; active_level = o->entries[t+1].depth - 1; }
        else if(d > 0) { active_row = t; active_level = d - 1; }
      }
      U64 max_depth = 0;
      for EachIndex(r, o->entry_count) { max_depth = Max(max_depth, o->entries[r].depth); }
      for(U64 k = 0; k < max_depth; k += 1)
      {
        for(U64 r = 0; r < o->entry_count;)
        {
          if(o->entries[r].depth <= k) { r += 1; continue; }
          U64 start = r;
          for(;r < o->entry_count && o->entries[r].depth > k; r += 1);
          if(r <= first || start >= last) { continue; }
          B32 active = (k == active_level && start <= active_row && active_row < r);
          F32 x = round_f32(l.list.x0 + (k*OP_INDENT_SIZE_PX + OP_GUIDE_LEFT_PX)*s);
          F32 y0 = op_row_rect(&l, panel, start).y0;
          Vec4F32 c = th_over(bg, active ? colors->panel_indent_guide_active : colors->panel_indent_guide);
          dr_rect(r2f32p(x, y0, x + line, y0 + (r - start)*l.row_h), th_linear(c), 0, 0, 0);
        }
      }
    }

    //- chevrons and labels
    for(U64 r = first; r < last; r += 1)
    {
      OP_Entry *e = &o->entries[r];
      Rng2F32 row = op_row_rect(&l, panel, r);
      Rng2F32 slot = op_icon_slot(&l, row, e->depth);
      B32 active = (r == selected_row);
      Vec4F32 row_bg = active ? colors->ghost_element_selected : (r == hovered_row) ? hover_bg : bg;
      if(e->has_children)
      {
        if(list_hovered && contains_2f32(slot, mouse)) { dr_rect(slot, th_linear(th_over(row_bg, colors->ghost_element_hover)), 4.f*s, 0, 0.5f); }
        ic_draw(e->collapsed ? IC_Icon_ChevronRight : IC_Icon_ChevronDown, slot, IC_SIZE_MEDIUM_PX*s, active ? colors->text : colors->text_muted);
      }
      F32 x = slot.x1 + round_f32(4.f*s);
      Rng2F32 label = r2f32p(x, row.y0 + line, row.x1 - 2*line - round_f32(6.f*s), row.y1 - line);
      // fuzzy matches: text_accent at 0.3 behind each matched character
      for EachIndex(m, e->match_count)
      {
        U64 at = e->match_positions[m];
        U64 end = at + 1;
        while(end < e->label.size && (e->label.str[end] & 0xC0) == 0x80) { end += 1; }
        F32 mx0 = x + md_text_width(fonts->mono, label_px, str8_prefix(e->label, at));
        F32 mx1 = x + md_text_width(fonts->mono, label_px, str8_prefix(e->label, end));
        Vec4F32 c = th_over(row_bg, th_alpha(colors->text_accent, 0.3f));
        dr_rect(r2f32p(round_f32(mx0), label.y0, round_f32(mx1), label.y1), th_linear(c), 0, 0, 0);
      }
      DR_ClipScope(intersect_2f32(label, l.list))
      {
        mdp_draw_text_centered_y(fonts->mono, label_px, label, x, e->label, colors->syntax[TH_Syntax_Title]);
      }
    }

    //- scrollbar: the thumb while the panel is hovered or just scrolled
    if(l.max_scroll > 0 && (contains_2f32(l.rect, mouse) || op_panel_is_animating(panel)))
    {
      F32 pad = MDP_SCROLLBAR_PADDING_PX*s;
      F32 width = MDP_SCROLLBAR_WIDTH_PX*s;
      F32 list_h = l.list.y1 - l.list.y0;
      F32 space = list_h - 2*pad;
      F32 total = o->entry_count*l.row_h;
      F32 thumb_h = Min(space, Max(MDP_SCROLLBAR_MIN_THUMB_PX*s, list_h*(list_h/total)));
      F32 thumb_y = l.list.y0 + pad + (panel->scroll_y/l.max_scroll)*(space - thumb_h);
      Rng2F32 thumb = r2f32p(l.list.x1 - pad - width, thumb_y, l.list.x1 - pad, thumb_y + thumb_h);
      Vec4F32 c = th_over(bg, contains_2f32(thumb, mouse) ? colors->scrollbar_thumb_hover_background : colors->scrollbar_thumb_background);
      dr_rect(thumb, th_linear(c), width/2, 0, 0.5f);
    }
  }

  //- empty state: the header, the query, and how to toggle the panel
  if(o->entry_count == 0)
  {
    String8 query = op_query(panel);
    String8 header = query.size ? str8_lit("No matches for query") : str8_lit("No outlines available");
    String8 hint = str8_lit("Toggle Panel With");
    String8 binding = str8_lit("Ctrl+B");
    F32 line_h = round_f32(ui_px*1.43f);
    F32 gap = round_f32(2.f*s);
    U64 lines = query.size ? 3 : 2;
    F32 y = round_f32((l.list.y0 + l.list.y1 - (lines*line_h + (lines-1)*gap))/2);
    F32 cx = (l.list.x0 + l.list.x1)/2;
    DR_ClipScope(l.list)
    {
      Rng2F32 r = r2f32p(l.list.x0, y, l.list.x1, y + line_h);
      mdp_draw_text_centered_y(fonts->sans, ui_px, r, cx - md_text_width(fonts->sans, ui_px, header)/2, header, colors->text);
      y += line_h + gap;
      if(query.size)
      {
        F32 w = md_text_width(fonts->sans, ui_px, query);
        F32 px_0p5 = round_f32(2.f*s);
        Rng2F32 chip = r2f32p(round_f32(cx - w/2 - px_0p5), y, round_f32(cx + w/2 + px_0p5), y + line_h);
        dr_rect(chip, th_linear(th_over(bg, th_alpha(colors->ghost_element_selected, 0.2f))), 0, 0, 0);
        mdp_draw_text_centered_y(fonts->sans, ui_px, chip, chip.x0 + px_0p5, query, colors->text);
        y += line_h + gap;
      }
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
  else if(contains_2f32(l.editor, mouse)) { wm_set_cursor(WM_Cursor_IBar); }
  else if(hovered_row != max_U64 || (op_query(panel).size && contains_2f32(l.clear, mouse))) { wm_set_cursor(WM_Cursor_HandPoint); }
  else if(contains_2f32(l.rect, mouse)) { wm_set_cursor(WM_Cursor_Pointer); }
  scratch_end(scratch);
}
