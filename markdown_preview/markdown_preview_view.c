////////////////////////////////
//~ Geometry (the Render impl's layout)

typedef struct MDP_Geometry MDP_Geometry;
struct MDP_Geometry
{
  Rng2F32 client;
  F32 scale;
  F32 rem;
  F32 padding;                   // p_4 = 1rem
  F32 content_x;
  F32 content_w;
  F32 total_h;
  F32 max_scroll;
  Vec2F32 origin;                // element origin in client coordinates
  Rng2F32 track;
  Rng2F32 thumb;
  B32 has_thumb;
};

internal void mdp_view_apply_pending_target(MDP_View *view, struct MDP_Geometry *g);

internal void
mdp_view_ensure_element(MDP_View *view, F32 width)
{
  MD_Style *style = &view->style;
  if(view->element == 0 || view->element_dirty || view->element_width != width ||
     view->element_rem != style->rem || view->element_theme != view->theme_kind)
  {
    arena_clear(view->element_arena);
    view->element = md_element_build(view->element_arena, &view->doc, style, view->base_dir);
    md_element_layout(view->element, style, width);
    view->element_dirty = 0;
    view->element_width = width;
    view->element_rem = style->rem;
    view->element_theme = view->theme_kind;
  }
}

internal MDP_Geometry
mdp_geometry(MDP_View *view, WM_Window window)
{
  MDP_Geometry g = {0};
  g.client = wm_client_rect_from_window(window);
  F32 dpi = wm_dpi_from_window(window);
  g.scale = (dpi > 0) ? dpi/96.f : 1.f;
  g.rem = round_f32(view->font_size*g.scale);
  view->theme = th_theme_from_kind(view->theme_kind);
  view->style = md_style_preview(&view->theme, &view->fonts, g.rem, g.scale);
  md_hit_style = &view->style;

  // .p_4() scroll container, then .w_full().max_w(800px).mx_auto()
  g.padding = g.rem;
  F32 avail = (g.client.x1 - g.client.x0) - 2*g.padding;
  g.content_w = floor_f32(ClampBot(1, Min(avail, MDP_MAX_WIDTH_PX*g.scale)));
  g.content_x = g.client.x0 + g.padding + floor_f32((avail - g.content_w)/2);
  mdp_view_ensure_element(view, g.content_w);

  F32 viewport_h = g.client.y1 - g.client.y0;
  g.total_h = view->element->height + 2*g.padding;
  g.max_scroll = ClampBot(0, g.total_h - viewport_h);
  view->scroll_y = Clamp(0, view->scroll_y, g.max_scroll);
  if(view->pending_target != MDP_LinkTargetKind_None) { mdp_view_apply_pending_target(view, &g); }
  g.origin = v2f32(g.content_x, g.client.y0 + g.padding - round_f32(view->scroll_y));

  // ScrollbarStyle::Regular: a 6px thumb in a track padded by 4px, along the right edge
  F32 pad = MDP_SCROLLBAR_PADDING_PX*g.scale;
  F32 width = MDP_SCROLLBAR_WIDTH_PX*g.scale;
  g.track = r2f32p(g.client.x1 - width - 2*pad, g.client.y0, g.client.x1, g.client.y1);
  if(g.max_scroll > 0)
  {
    Rng2F32 container = pad_2f32(g.track, -pad);
    F32 space = container.y1 - container.y0;
    F32 thumb_h = Max(MDP_SCROLLBAR_MIN_THUMB_PX*g.scale, viewport_h * (viewport_h / g.total_h));
    thumb_h = Min(thumb_h, space);
    F32 thumb_y = (view->scroll_y / g.max_scroll) * (space - thumb_h);
    g.thumb = r2f32p(container.x0, container.y0 + thumb_y, container.x1, container.y0 + thumb_y + thumb_h);
    g.has_thumb = 1;
  }
  return g;
}

////////////////////////////////
//~ Document

internal void
mdp_view_init(MDP_View *view)
{
  view->arena = arena_alloc();
  view->doc_arena = arena_alloc();
  view->element_arena = arena_alloc();
  view->interaction_arena = arena_alloc();
  view->font_size = MDP_DEFAULT_FONT_SIZE_PX;
  view->theme_kind = TH_Kind_OneDark;
  view->theme = th_theme_from_kind(view->theme_kind);
  view->element_dirty = 1;
}

internal void
mdp_view_load_text(MDP_View *view, String8 name, String8 path, String8 text)
{
  Temp scratch = scratch_begin(0, 0);
  String8 name_copy = push_str8_copy(scratch.arena, name);
  String8 path_copy = push_str8_copy(scratch.arena, path);
  String8 text_copy = push_str8_copy(scratch.arena, text);
  arena_clear(view->doc_arena);
  view->name = push_str8_copy(view->doc_arena, name_copy);
  view->path = push_str8_copy(view->doc_arena, path_copy);
  view->base_dir = push_str8_copy(view->doc_arena, str8_chop_last_slash(path_copy));
  view->text = push_str8_copy(view->doc_arena, text_copy);
  view->error = str8_zero();
  view->doc = md_parse(view->doc_arena, view->text);
  view->element_dirty = 1;
  view->ix.selection_start = view->ix.selection_end = 0;
  scratch_end(scratch);
}

internal B32
mdp_view_open_path(MDP_View *view, String8 path, B32 keep_scroll)
{
  Temp scratch = scratch_begin(0, 0);
  String8 full = full_path_from_path(scratch.arena, path);
  FileProperties props = properties_from_file_path(full);
  B32 ok = 0;
  if(props.modified != 0 && !(props.flags & FilePropertyFlag_IsFolder))
  {
    String8 data = data_from_file_path(scratch.arena, full);
    if(!keep_scroll || !str8_match(full, view->path, 0))
    {
      md_images_reset();
      view->scroll_y = 0;
      view->ix.active_root_index = 0;
      view->pending_target = MDP_LinkTargetKind_None;
      arena_clear(view->interaction_arena);
      view->ix.first_scroll = 0;
    }
    mdp_view_load_text(view, str8_skip_last_slash(full), full, data);
    view->file_modified = props.modified;
    ok = 1;
  }
  else
  {
    view->error = push_str8f(view->arena, "Could not open %S", full);
    view->error_until_us = now_time_us() + 4000000;
  }
  scratch_end(scratch);
  return ok;
}

// Re-reads the file when it changed on disk, after Zed's reparse debounce.
internal void
mdp_view_poll_file(MDP_View *view)
{
  if(view->path.size == 0) { return; }
  U64 now = now_time_us();
  if(view->reload_at_us == 0)
  {
    FileProperties props = properties_from_file_path(view->path);
    if(props.modified != 0 && props.modified != view->file_modified)
    {
      view->file_modified = props.modified;
      view->reload_at_us = now + MDP_REPARSE_DEBOUNCE_US;
    }
  }
  else if(now >= view->reload_at_us)
  {
    view->reload_at_us = 0;
    mdp_view_open_path(view, view->path, 1);
  }
}

////////////////////////////////
//~ Scrolling

internal void
mdp_view_scroll_by(MDP_View *view, F32 dy)
{
  view->scroll_y += dy;
  view->scroll_activity_us = now_time_us();
}

internal void
mdp_view_scroll_to_box(MDP_View *view, MDP_Geometry *g, MD_Box *box)
{
  view->scroll_y = Clamp(0, g->padding + box->rect.y0, g->max_scroll);
  view->scroll_activity_us = now_time_us();
}

// MarkdownElement::autoscroll (AutoscrollBehavior::Controlled): keep the
// target line 3 line heights inside the viewport, scrolling only as needed.
internal void
mdp_view_autoscroll_to_text(MDP_View *view, MDP_Geometry *g, MD_Text *t)
{
  if(t == 0 || t->box == 0) { return; }
  F32 line_h = t->line_height;
  F32 margin = line_h*3.f;
  F32 top_goal = g->client.y0 + margin;
  F32 bottom_goal = g->client.y1 - margin;
  F32 y = g->client.y0 + g->padding - view->scroll_y + t->box->rect.y0;
  if(y < top_goal) { view->scroll_y -= top_goal - y; }
  else if(y + line_h > bottom_goal) { view->scroll_y += (y + line_h) - bottom_goal; }
  view->scroll_y = Clamp(0, view->scroll_y, g->max_scroll);
  view->scroll_activity_us = now_time_us();
}

internal MD_Box *
mdp_view_anchor_box(MDP_View *view, String8 name)
{
  MD_Element *el = view->element;
  for EachIndex(i, el->anchor_count)
  {
    // heading_slugs is a HashMap: exact, case-sensitive lookup
    if(str8_match(el->anchor_names[i], name, 0)) { return el->anchor_boxes[i]; }
  }
  return 0;
}

// Markdown::scroll_to_heading, plus the editor-cursor move Zed does for links
// in the same file (that makes the heading's root block active).
internal B32
mdp_view_scroll_to_heading(MDP_View *view, MDP_Geometry *g, String8 slug, B32 activate)
{
  MD_Box *box = mdp_view_anchor_box(view, slug);
  if(box == 0) { return 0; }
  MD_Text *t = md_element_first_text_in(view->element, box);
  mdp_view_autoscroll_to_text(view, g, t);
  if(activate) { view->ix.active_root_index = md_box_root_index(box); }
  return 1;
}

// navigate_to_link_target / sync_preview_to_source_index for a row and column
internal void
mdp_view_scroll_to_position(MDP_View *view, MDP_Geometry *g, U64 row, U64 column)
{
  String8 src = view->doc.source;
  U64 offset = 0;
  for(U64 r = 0; r < row && offset < src.size; r += 1)
  {
    U64 nl = str8_find_needle(src, offset, str8_lit("\n"), 0);
    offset = (nl < src.size) ? nl+1 : src.size;
  }
  U64 nl = str8_find_needle(src, offset, str8_lit("\n"), 0);
  offset = Min(offset + column, nl);
  MD_Text *t = md_element_text_for_source_index(view->element, offset);
  mdp_view_autoscroll_to_text(view, g, t);
  if(t) { view->ix.active_root_index = md_box_root_index(t->box); }
}

// Targets for a link into another file wait until that file is laid out
// (scroll_to_heading_when_parsed).
internal void
mdp_view_apply_pending_target(MDP_View *view, MDP_Geometry *g)
{
  MDP_LinkTargetKind kind = view->pending_target;
  view->pending_target = MDP_LinkTargetKind_None;
  switch(kind)
  {
    default:{}break;
    case MDP_LinkTargetKind_Heading:
    {
      mdp_view_scroll_to_heading(view, g, str8(view->pending_slug, view->pending_slug_size), 0);
    }break;
    case MDP_LinkTargetKind_Position:
    {
      mdp_view_scroll_to_position(view, g, view->pending_row, view->pending_column);
    }break;
  }
}

// util::markdown::has_uri_scheme
internal B32
mdp_has_uri_scheme(String8 url)
{
  if(url.size == 0 || !char_is_alpha(url.str[0])) { return 0; }
  for(U64 i = 1; i < url.size; i += 1)
  {
    U8 c = url.str[i];
    if(c == ':') { return 1; }
    if(!(char_is_alpha(c) || char_is_digit(c, 10) || c == '+' || c == '-' || c == '.')) { return 0; }
  }
  return 0;
}

// util::markdown::source_position_from_fragment: L12, 12, L12C3, 12:3, L12-L20
internal B32
mdp_position_from_fragment(String8 fragment, U64 *row, U64 *column)
{
  if(fragment.size && fragment.str[0] == 'L') { fragment = str8_skip(fragment, 1); }
  U64 dash = str8_find_needle(fragment, 0, str8_lit("-"), 0);
  fragment = str8_prefix(fragment, dash);
  U64 split = fragment.size;
  for EachIndex(i, fragment.size)
  {
    U8 c = fragment.str[i];
    if(c == ',' || c == ':' || c == 'C') { split = i; break; }
  }
  String8 line = str8_prefix(fragment, split);
  String8 col = (split < fragment.size) ? str8_skip(fragment, split+1) : str8_zero();
  if(line.size == 0 || !str8_is_integer(line, 10)) { return 0; }
  U64 l = u64_from_str8(line, 10);
  if(l == 0) { return 0; }
  *row = l-1;
  *column = 0;
  if(col.size && str8_is_integer(col, 10))
  {
    U64 c = u64_from_str8(col, 10);
    if(c > 0) { *column = c-1; }
  }
  return 1;
}

// PathWithPosition::parse_str: trailing `:row` or `:row:column`
internal String8
mdp_path_with_position(String8 path, U64 *row, U64 *column, B32 *has_row)
{
  *has_row = 0;
  String8 nums[2] = {0};
  U64 count = 0;
  for(;count < 2;)
  {
    U64 colon = str8_find_needle_reverse(path, 0, str8_lit(":"), 0);
    if(colon == 0) { break; }
    String8 tail = str8_skip(path, colon);
    if(tail.size == 0 || !str8_is_integer(tail, 10)) { break; }
    nums[count++] = tail;
    path = str8_prefix(path, colon-1);
  }
  if(count > 0)
  {
    *has_row = 1;
    String8 r = nums[count-1];
    String8 c = (count == 2) ? nums[0] : str8_zero();
    U64 rv = u64_from_str8(r, 10);
    *row = rv ? rv-1 : 0;
    U64 cv = c.size ? u64_from_str8(c, 10) : 0;
    *column = cv ? cv-1 : 0;
  }
  return path;
}

internal String8
mdp_percent_decode(Arena *arena, String8 s)
{
  U8 *out = push_array_no_zero(arena, U8, s.size + 1);
  U64 n = 0;
  for(U64 i = 0; i < s.size; i += 1)
  {
    if(s.str[i] == '%' && i+2 < s.size && char_is_digit(s.str[i+1], 16) && char_is_digit(s.str[i+2], 16))
    {
      out[n++] = (U8)u64_from_str8(str8_substr(s, r1u64(i+1, i+3)), 16);
      i += 2;
      continue;
    }
    out[n++] = s.str[i];
  }
  return str8(out, n);
}

// the Markdown language's path suffixes
internal B32
mdp_is_markdown_path(String8 path)
{
  String8 name = str8_skip_last_slash(path);
  U64 dot = str8_find_needle_reverse(name, 0, str8_lit("."), 0);
  if(dot == 0) { return 0; }
  String8 ext = str8_skip(name, dot);
  local_persist char *suffixes[] = {"md", "mdx", "mdwn", "mdc", "markdown", "MD"};
  for EachElement(i, suffixes)
  {
    if(str8_match(ext, str8_cstring(suffixes[i]), 0)) { return 1; }
  }
  return 0;
}

internal String8
mdp_resolve_path(Arena *arena, MDP_View *view, String8 path)
{
  B32 absolute = (path.size > 2 && path.str[1] == ':') || (path.size > 1 && path.str[0] == '\\' && path.str[1] == '\\');
  if(absolute) { return path; }
  // a leading slash resolves against the workspace root: here, the file's folder
  for(;path.size && (path.str[0] == '/' || path.str[0] == '\\');) { path = str8_skip(path, 1); }
  if(view->base_dir.size == 0) { return path; }
  return push_str8f(arena, "%S/%S", view->base_dir, path);
}

// workspace.open_url_or_file: a URL goes to the system handler, a file to
// the system's default app (there is no editor here)
internal void
mdp_view_open_url_or_file(MDP_View *view, String8 url)
{
  Temp scratch = scratch_begin(0, 0);
  String8 path = mdp_resolve_path(scratch.arena, view, url);
  if(str8_find_needle(url, 0, str8_lit("://"), 0) >= url.size && file_path_exists(path))
  {
    sh_open_in_browser(path);
  }
  else if(mdp_has_uri_scheme(url))
  {
    sh_open_in_browser(url);
  }
  else
  {
    view->error = push_str8f(view->arena, "Could not open %S", path);
    view->error_until_us = now_time_us() + 4000000;
  }
  scratch_end(scratch);
}

// handle_url_click + open_preview_url (markdown_preview_view.rs)
internal void
mdp_view_open_link(MDP_View *view, MDP_Geometry *g, String8 url, B32 alt)
{
  //- footnote references scroll to the definition's content
  if(str8_match(str8_prefix(url, 9), str8_lit("footnote:"), 0))
  {
    MD_Box *box = mdp_view_anchor_box(view, url);
    if(box) { mdp_view_autoscroll_to_text(view, g, md_element_first_text_in(view->element, box)); }
    return;
  }
  
  //- split_local_url_fragment
  String8 path_part = url;
  String8 fragment = {0};
  if(!mdp_has_uri_scheme(url))
  {
    U64 hash = str8_find_needle(url, 0, str8_lit("#"), 0);
    if(hash < url.size)
    {
      path_part = str8_prefix(url, hash);
      fragment = str8_skip(url, hash+1);
    }
  }
  
  //- `#slug`: a heading in this document
  if(path_part.size == 0)
  {
    if(fragment.size) { mdp_view_scroll_to_heading(view, g, fragment, 1); }
    return;
  }
  
  Temp scratch = scratch_begin(0, 0);
  String8 decoded = mdp_percent_decode(scratch.arena, path_part);
  
  //- only `://` URLs skip local handling
  if(str8_find_needle(decoded, 0, str8_lit("://"), 0) >= decoded.size)
  {
    U64 row = 0, column = 0;
    B32 has_row = 0;
    String8 file = mdp_path_with_position(decoded, &row, &column, &has_row);
    U64 frow = 0, fcolumn = 0;
    B32 has_fragment_position = fragment.size && fragment.str[0] == 'L' && mdp_position_from_fragment(fragment, &frow, &fcolumn);
    if(!alt && mdp_is_markdown_path(file))
    {
      String8 path = mdp_resolve_path(scratch.arena, view, file);
      if(file_path_exists(path) && mdp_view_open_path(view, path, 0))
      {
        if(has_row) { view->pending_target = MDP_LinkTargetKind_Position; view->pending_row = row; view->pending_column = column; }
        else if(has_fragment_position) { view->pending_target = MDP_LinkTargetKind_Position; view->pending_row = frow; view->pending_column = fcolumn; }
        else if(fragment.size)
        {
          view->pending_target = MDP_LinkTargetKind_Heading;
          view->pending_slug_size = Min(fragment.size, sizeof(view->pending_slug));
          MemoryCopy(view->pending_slug, fragment.str, view->pending_slug_size);
        }
      }
      else
      {
        mdp_view_open_url_or_file(view, decoded);
      }
      scratch_end(scratch);
      return;
    }
    // other files (with or without a position) open outside the preview
  }
  mdp_view_open_url_or_file(view, decoded);
  scratch_end(scratch);
}

////////////////////////////////
//~ Events

internal B32
mdp_view_handle_event(MDP_View *view, WM_Window window, WM_Event *ev)
{
  MDP_Geometry g = mdp_geometry(view, window);
  MD_Element *el = view->element;
  F32 viewport_h = g.client.y1 - g.client.y0;
  B32 taken = 0;
  switch(ev->kind)
  {
    default:{}break;

    case WM_EventKind_Scroll:
    {
      if(ev->modifiers & WM_Modifier_Ctrl) { break; }
      Vec2F32 p = sub_2f32(ev->pos, g.origin);
      MD_Hit hit = md_element_hit_test(el, &view->ix, p);
      F32 notches_y = ev->delta.y/120.f;
      F32 notches_x = ev->delta.x/120.f;
      if(ev->modifiers & WM_Modifier_Shift) { notches_x = notches_y; notches_y = 0; }
      F32 line = round_f32(1.618034f*g.rem); // the window's default line height
      if(notches_x != 0 && hit.scroll_box)
      {
        MD_Box *box = hit.scroll_box;
        F32 inner = (box->rect.x1 - box->rect.x0) - box->border[MD_Side_Left] - box->border[MD_Side_Right];
        F32 max = ClampBot(0, box->content_width - inner);
        F32 *off = md_scroll_offset_ptr(&view->ix, view->interaction_arena, box->key);
        *off = Clamp(0, *off + notches_x*MDP_LINES_PER_WHEEL_NOTCH*line, max);
      }
      if(notches_y != 0)
      {
        mdp_view_scroll_by(view, notches_y*MDP_LINES_PER_WHEEL_NOTCH*line);
      }
      taken = 1;
    }break;

    case WM_EventKind_Press:
    {
      //- markdown::Scroll* actions (MarkdownPreview key context)
      WM_Modifiers m = ev->modifiers;
      if(m == 0 && ev->key == WM_Key_PageDown) { mdp_view_scroll_by(view, viewport_h); taken = 1; }
      else if(m == 0 && ev->key == WM_Key_PageUp) { mdp_view_scroll_by(view, -viewport_h); taken = 1; }
      else if(m == 0 && ev->key == WM_Key_Down) { mdp_view_scroll_by(view, 2*g.rem); taken = 1; }
      else if(m == 0 && ev->key == WM_Key_Up) { mdp_view_scroll_by(view, -2*g.rem); taken = 1; }
      else if(m == WM_Modifier_Ctrl && ev->key == WM_Key_Home) { view->scroll_y = 0; view->scroll_activity_us = now_time_us(); taken = 1; }
      else if(m == WM_Modifier_Ctrl && ev->key == WM_Key_End) { view->scroll_y = g.max_scroll; view->scroll_activity_us = now_time_us(); taken = 1; }
      else if(m == WM_Modifier_Alt && (ev->key == WM_Key_Down || ev->key == WM_Key_Up))
      {
        // ScrollDownByItem / ScrollUpByItem: to the next / previous root block
        F32 top = view->scroll_y - g.padding;
        MD_Box *target = 0;
        for(MD_Box *b = el->root->first; b != 0; b = b->next)
        {
          if(ev->key == WM_Key_Down && b->rect.y0 > top + 1) { target = b; break; }
          if(ev->key == WM_Key_Up && b->rect.y0 < top - 1) { target = b; }
        }
        if(target) { mdp_view_scroll_to_box(view, &g, target); }
        else if(ev->key == WM_Key_Up) { view->scroll_y = 0; }
        taken = 1;
      }

      //- markdown::Copy / SelectAll
      else if(m == WM_Modifier_Ctrl && (ev->key == WM_Key_C || ev->key == WM_Key_Insert))
      {
        if(view->ix.selection_end > view->ix.selection_start)
        {
          Temp scratch = scratch_begin(0, 0);
          wm_set_clipboard_text(md_element_text_in_range(scratch.arena, el, view->ix.selection_start, view->ix.selection_end));
          scratch_end(scratch);
        }
        taken = 1;
      }
      else if(m == WM_Modifier_Ctrl && ev->key == WM_Key_A)
      {
        view->ix.selection_start = 0;
        view->ix.selection_end = el->text_total;
        taken = 1;
      }

      //- zoom: buffer font size actions change the preview size by 1px
      else if(m == WM_Modifier_Ctrl && (ev->key == WM_Key_Equal || ev->key == WM_Key_NumPlus))
      {
        view->font_size = Clamp(MDP_MIN_FONT_SIZE_PX, view->font_size + 1, MDP_MAX_FONT_SIZE_PX);
        taken = 1;
      }
      else if(m == WM_Modifier_Ctrl && (ev->key == WM_Key_Minus || ev->key == WM_Key_NumMinus))
      {
        view->font_size = Clamp(MDP_MIN_FONT_SIZE_PX, view->font_size - 1, MDP_MAX_FONT_SIZE_PX);
        taken = 1;
      }
      else if(m == WM_Modifier_Ctrl && (ev->key == WM_Key_0 || ev->key == WM_Key_Num0))
      {
        view->font_size = MDP_DEFAULT_FONT_SIZE_PX;
        taken = 1;
      }

      //- mouse
      else if(ev->key == WM_Key_LeftMouseButton)
      {
        Vec2F32 p = sub_2f32(ev->pos, g.origin);
        if(g.has_thumb && contains_2f32(g.track, ev->pos))
        {
          if(contains_2f32(g.thumb, ev->pos))
          {
            view->thumb_drag_offset = ev->pos.y - g.thumb.y0;
          }
          else
          {
            // TrackClick: center the thumb on the click
            view->thumb_drag_offset = (g.thumb.y1 - g.thumb.y0)/2;
          }
          view->thumb_dragging = 1;
          Rng2F32 container = pad_2f32(g.track, -MDP_SCROLLBAR_PADDING_PX*g.scale);
          F32 thumb_h = g.thumb.y1 - g.thumb.y0;
          F32 space = (container.y1 - container.y0) - thumb_h;
          F32 start = Clamp(0, ev->pos.y - container.y0 - view->thumb_drag_offset, space);
          view->scroll_y = (space > 0) ? start/space*g.max_scroll : 0;
          view->scroll_activity_us = now_time_us();
          taken = 1;
          break;
        }
        MD_Hit hit = md_element_hit_test(el, &view->ix, p);
        if(hit.checkbox)
        {
          // on_checkbox_toggle: rewrite [ ] / [x] in the source
          U64 offset = hit.checkbox - 1;
          if(offset < view->text.size)
          {
            Temp scratch = scratch_begin(0, 0);
            String8 text = push_str8_copy(scratch.arena, view->text);
            text.str[offset] = (text.str[offset] == ' ') ? 'x' : ' ';
            F32 scroll = view->scroll_y;
            if(view->path.size && write_data_to_file_path(view->path, text))
            {
              mdp_view_open_path(view, view->path, 1);
            }
            else
            {
              mdp_view_load_text(view, view->name, view->path, text);
            }
            view->scroll_y = scroll;
            scratch_end(scratch);
          }
          taken = 1;
          break;
        }
        if(hit.copy_button && hit.code_block)
        {
          wm_set_clipboard_text(hit.code_block->code_text);
          view->ix.copied_code_key = hit.code_block->key;
          view->ix.copied_code_until_us = now_time_us() + 2000000;
          taken = 1;
          break;
        }
        U64 now = now_time_us();
        B32 double_click = (now - view->last_click_us < 500000 && length_2f32(sub_2f32(ev->pos, view->last_click_pos)) < 4*g.scale);
        view->last_click_us = now;
        view->last_click_pos = ev->pos;
        view->press_on_link = (hit.link != 0);
        view->press_link = hit.link;
        view->press_pos = ev->pos;
        U64 offset = md_element_offset_from_point(el, &view->style, &view->ix, p);
        if(double_click)
        {
          Rng1U64 word = md_element_word_at(el, offset);
          view->ix.selection_start = word.min;
          view->ix.selection_end = word.max;
          view->selecting = 0;
        }
        else if(ev->modifiers & WM_Modifier_Shift)
        {
          view->ix.selection_end = offset;
          if(view->ix.selection_end < view->ix.selection_start) { Swap(U64, view->ix.selection_start, view->ix.selection_end); }
          view->selecting = 1;
        }
        else
        {
          view->ix.selection_start = view->ix.selection_end = offset;
          view->selecting = 1;
        }
        // anchor of a drag selection
        view->press_pos = ev->pos;
        view->select_anchor = (ev->modifiers & WM_Modifier_Shift) ? view->ix.selection_start : offset;
        taken = 1;
      }
    }break;

    case WM_EventKind_Release:
    {
      if(ev->key == WM_Key_LeftMouseButton)
      {
        if(view->thumb_dragging) { view->thumb_dragging = 0; taken = 1; break; }
        B32 moved = length_2f32(sub_2f32(ev->pos, view->press_pos)) > 3*g.scale;
        if(view->press_on_link && !moved && view->ix.selection_start == view->ix.selection_end)
        {
          Vec2F32 p = sub_2f32(ev->pos, g.origin);
          MD_Hit hit = md_element_hit_test(el, &view->ix, p);
          if(hit.link == view->press_link && hit.link > 0 && hit.link <= el->link_count)
          {
            mdp_view_open_link(view, &g, el->links[hit.link-1].url, !!(ev->modifiers & WM_Modifier_Alt));
          }
        }
        view->selecting = 0;
        view->press_on_link = 0;
        taken = 1;
      }
    }break;

    case WM_EventKind_MouseMove:
    {
      if(view->thumb_dragging)
      {
        Rng2F32 container = pad_2f32(g.track, -MDP_SCROLLBAR_PADDING_PX*g.scale);
        F32 thumb_h = g.thumb.y1 - g.thumb.y0;
        F32 space = (container.y1 - container.y0) - thumb_h;
        F32 start = Clamp(0, ev->pos.y - container.y0 - view->thumb_drag_offset, space);
        view->scroll_y = (space > 0) ? start/space*g.max_scroll : 0;
        view->scroll_activity_us = now_time_us();
      }
      else if(view->selecting)
      {
        Vec2F32 p = sub_2f32(ev->pos, g.origin);
        U64 anchor = view->select_anchor;
        U64 head = md_element_offset_from_point(el, &view->style, &view->ix, p);
        view->ix.selection_start = Min(anchor, head);
        view->ix.selection_end = Max(anchor, head);
      }
      taken = 1;
    }break;
  }
  return taken;
}

////////////////////////////////
//~ Drawing

internal B32
mdp_view_is_animating(MDP_View *view)
{
  U64 now = now_time_us();
  if(view->scroll_activity_us && now < view->scroll_activity_us + MDP_SCROLLBAR_HIDE_DELAY_US + MDP_SCROLLBAR_HIDE_DURATION_US) { return 1; }
  if(view->ix.copied_code_until_us && now < view->ix.copied_code_until_us) { return 1; }
  if(view->reload_at_us) { return 1; }
  if(view->error.size && now < view->error_until_us + 100000) { return 1; }
  return 0;
}

internal F32
mdp_ease_in_out(F32 t)
{
  return (t < 0.5f) ? 2*t*t : 1 - (-2*t + 2)*(-2*t + 2)/2;
}

internal void
mdp_view_draw(MDP_View *view, WM_Window window)
{
  Temp scratch = scratch_begin(0, 0);
  MDP_Geometry g = mdp_geometry(view, window);
  MD_Element *el = view->element;
  TH_Colors *colors = &view->theme.colors;
  Vec2F32 mouse = wm_mouse_from_window(window);
  B32 mouse_inside = contains_2f32(g.client, mouse);

  //- hover state
  view->ix.mouse = sub_2f32(mouse, g.origin);
  view->ix.mouse_inside = mouse_inside && !view->thumb_dragging && !(g.has_thumb && contains_2f32(g.track, mouse));
  MD_Hit hit = {0};
  if(view->ix.mouse_inside) { hit = md_element_hit_test(el, &view->ix, view->ix.mouse); }
  view->hovered_url = (hit.link && hit.link <= el->link_count) ? el->links[hit.link-1].url : str8_zero();
  view->thumb_hovered = g.has_thumb && mouse_inside && contains_2f32(g.thumb, mouse);
  if(view->selecting) { wm_set_cursor(WM_Cursor_IBar); }
  else if(hit.link || hit.copy_button || hit.checkbox) { wm_set_cursor(WM_Cursor_HandPoint); }
  else if(hit.over_text) { wm_set_cursor(WM_Cursor_IBar); }
  else { wm_set_cursor(WM_Cursor_Pointer); }

  //- background: .bg(editor_background)
  dr_rect(g.client, th_linear(colors->editor_background), 0, 0, 0);

  //- markdown element
  md_element_paint(el, &view->style, &view->ix, g.origin, g.client);

  //- vertical scrollbar
  if(g.has_thumb)
  {
    U64 now = now_time_us();
    F32 fade = 1.f;
    U64 since = (view->scroll_activity_us && now > view->scroll_activity_us) ? now - view->scroll_activity_us : max_U64;
    if(view->scroll_activity_us == 0) { since = max_U64; }
    if(since < MDP_SCROLLBAR_HIDE_DELAY_US) { fade = 0; }
    else if(since < MDP_SCROLLBAR_HIDE_DELAY_US + MDP_SCROLLBAR_HIDE_DURATION_US)
    {
      fade = mdp_ease_in_out((F32)(since - MDP_SCROLLBAR_HIDE_DELAY_US)/MDP_SCROLLBAR_HIDE_DURATION_US);
    }
    B32 hovered = view->thumb_hovered;
    B32 track_hovered = mouse_inside && contains_2f32(g.track, mouse);
    if(view->thumb_dragging || track_hovered) { fade = 0; }
    Vec4F32 base = view->thumb_dragging ? colors->scrollbar_thumb_active_background :
      hovered ? colors->scrollbar_thumb_hover_background : colors->scrollbar_thumb_background;
    Vec4F32 blending = hovered ? colors->surface_background : th_alpha(colors->surface_background, Min(colors->surface_background.w, 0.7f));
    Vec4F32 thumb = th_over(blending, base);
    if(!hovered) { thumb.w *= (1.f - fade); }
    if(thumb.w > 0.001f)
    {
      F32 radius = (g.thumb.x1 - g.thumb.x0)/2;
      dr_rect(g.thumb, th_linear(th_over(colors->editor_background, thumb)), radius, 0, 0.5f);
    }
  }

  //- LinkPreview at the bottom left (UI rem, outside WithRemSize)
  if(view->hovered_url.size && !view->selecting)
  {
    F32 ui_rem = 16.f*g.scale;
    F32 font_px = round_f32(10.f*g.scale);          // LabelSize::XSmall
    F32 line_h = round_f32(font_px*1.618034f);
    String8 url = view->hovered_url;
    // wrap every 100 characters, cut at 500
    String8List lines = {0};
    U64 chars = 0;
    U64 seg = 0;
    for(U64 i = 0; i < url.size;)
    {
      if(chars == 500) { str8_list_push(scratch.arena, &lines, push_str8f(scratch.arena, "%S\xe2\x80\xa6", str8_substr(url, r1u64(seg, i)))); seg = url.size; break; }
      if(chars % 100 == 0 && chars != 0) { str8_list_push(scratch.arena, &lines, str8_substr(url, r1u64(seg, i))); seg = i; }
      UnicodeDecode d = utf8_decode(url.str + i, url.size - i);
      i += Max(1, d.inc);
      chars += 1;
    }
    if(seg < url.size) { str8_list_push(scratch.arena, &lines, str8_skip(url, seg)); }
    F32 text_w = 0;
    for(String8Node *n = lines.first; n != 0; n = n->next)
    {
      text_w = Max(text_w, md_text_width(view->fonts.sans, font_px, n->string));
    }
    F32 border = 1.f*g.scale;
    F32 px_2 = 0.5f*ui_rem, py_1 = 0.25f*ui_rem;
    F32 box_h = lines.node_count*line_h + 2*py_1 + 2*border;
    F32 box_w = text_w + 2*px_2 + 2*border;
    F32 x0 = g.client.x0 + 0.5f*ui_rem;               // left_0 + pl_2
    F32 y1 = g.client.y1 - 0.5f*ui_rem;               // bottom_2
    Rng2F32 box = r2f32p(x0, y1 - box_h, Min(x0 + box_w, g.client.x1 - 0.5f*ui_rem), y1);
    F32 radius = 0.5f*ui_rem;                          // rounded_lg
    // elevation_2 shadow
    dr_rect(shift_2f32(pad_2f32(box, 1.f*g.scale), v2f32(0, 1.f*g.scale)), v4f32(0, 0, 0, 0.12f), radius + 1, 0, 3.f*g.scale);
    dr_rect(box, th_linear(colors->elevated_surface_background), radius, 0, 0.5f);
    dr_rect(box, th_linear(colors->border_variant), radius, border, 0.5f);
    FNT_Metrics m = fnt_metrics_from_tag_size(view->fonts.sans, md_pt_from_px(font_px));
    F32 y = box.y0 + border + py_1;
    DR_ClipScope(intersect_2f32(box, g.client))
      for(String8Node *n = lines.first; n != 0; n = n->next)
    {
      F32 baseline = round_f32(y + (line_h - m.ascent - m.descent)/2 + m.ascent);
      dr_text(view->fonts.sans, md_pt_from_px(font_px), 0, 0, FNT_RasterFlag_Smooth, v2f32(box.x0 + border + px_2, baseline), th_linear(colors->text_muted), n->string);
      y += line_h;
    }
  }

  //- load errors
  // load errors: a toast at the bottom left that goes away after a few seconds
  if(view->error.size && now_time_us() < view->error_until_us)
  {
    F32 ui_rem = 16.f*g.scale;
    F32 font_px = round_f32(14.f*g.scale);
    F32 line_h = round_f32(font_px*1.618034f);
    F32 text_w = md_text_width(view->fonts.sans, font_px, view->error);
    Rng2F32 box = r2f32p(g.client.x0 + 0.5f*ui_rem, g.client.y1 - 0.5f*ui_rem - line_h - 0.5f*ui_rem,
                         Min(g.client.x0 + 0.5f*ui_rem + text_w + ui_rem, g.client.x1 - 0.5f*ui_rem), g.client.y1 - 0.5f*ui_rem);
    dr_rect(box, th_linear(colors->elevated_surface_background), 0.5f*ui_rem, 0, 0.5f);
    dr_rect(box, th_linear(colors->border_variant), 0.5f*ui_rem, 1.f*g.scale, 0.5f);
    FNT_Metrics m = fnt_metrics_from_tag_size(view->fonts.sans, md_pt_from_px(font_px));
    F32 baseline = round_f32(box.y0 + 0.25f*ui_rem + (line_h - m.ascent - m.descent)/2 + m.ascent);
    DR_ClipScope(box)
      dr_text(view->fonts.sans, md_pt_from_px(font_px), 0, 0, FNT_RasterFlag_Smooth,
              v2f32(box.x0 + 0.5f*ui_rem, baseline), th_linear(colors->error), view->error);
  }
  scratch_end(scratch);
}
