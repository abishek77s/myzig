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
  F32 ui;                        // the UI's rem (16px), outside WithRemSize
  Rng2F32 bar;                   // search bar, above the preview when open
};

internal void mdp_view_apply_pending_target(MDP_View *view, struct MDP_Geometry *g);
internal void mdp_search_refresh(MDP_View *view, struct MDP_Geometry *g);
internal void mdp_search_reset(MDP_View *view);

////////////////////////////////
//~ Source

// Source bytes: a slice of the text in memory, or read from the file. The
// file is opened per read and shared, so an editor can still save it.
internal String8
mdp_source_read(MDP_View *view, Arena *arena, Rng1U64 range)
{
  range.max = Min(range.max, view->source_size);
  range.min = Min(range.min, range.max);
  if(view->in_memory) { return str8_substr(view->text, range); }
  String8 result = {0};
  File file = file_open(AccessFlag_Read|AccessFlag_ShareRead|AccessFlag_ShareWrite, view->path);
  if(!file_match(file, file_zero()))
  {
    result = string_from_file_range(arena, file, range);
    file_close(file);
  }
  return result;
}

////////////////////////////////
//~ Chunks

internal void
mdp_chunk_evict(MDP_Chunk *c)
{
  if(c->arena) { arena_release(c->arena); }
  c->arena = 0;
  c->element = 0;
  MemoryZeroStruct(&c->doc);
  c->matches = 0;
  c->match_count = 0;
  c->matches_key = 0;
}

// Takes the chunks the scanner found since the last call.
internal void
mdp_view_sync_chunks(MDP_View *view)
{
  U64 n = view->scanner.chunk_count;
  if(n > view->chunk_cap)
  {
    U64 cap = Max(n, view->chunk_cap*2 + 64);
    MDP_Chunk *bigger = push_array(view->chunk_arena, MDP_Chunk, cap);
    if(view->chunk_count) { MemoryCopy(bigger, view->chunks, sizeof(MDP_Chunk)*view->chunk_count); }
    view->chunks = bigger;
    view->chunk_cap = cap;
  }
  view->chunk_count = n;
}

internal void mdp_view_place_chunks(MDP_View *view);

// Feeds the scanner the file, a piece at a time, until the budget runs out.
internal void
mdp_view_scan(MDP_View *view, U64 budget_us)
{
  MD_Scanner *s = &view->scanner;
  U64 start_us = now_time_us();
  for(;!s->done;)
  {
    Temp scratch = scratch_begin(0, 0);
    Rng1U64 range = r1u64(s->offset, Min(s->offset + view->scan_read_size, view->source_size));
    String8 data = mdp_source_read(view, scratch.arena, range);
    B32 eof = (range.max >= view->source_size || data.size < dim_1u64(range));
    U64 used = md_scanner_feed(s, data, eof);
    if(used == 0 && !s->done) { view->scan_read_size *= 2; } // a line longer than the read
    scratch_end(scratch);
    if(now_time_us() - start_us >= budget_us) { break; }
  }
  mdp_view_sync_chunks(view);
  mdp_view_place_chunks(view);
}

internal B32
mdp_chunk_is_built(MDP_View *view, MDP_Chunk *c)
{
  return c->element != 0 && c->build_key == view->build_key && c->measure_width == view->layout_width;
}

internal MD_Document
mdp_chunk_parse(MDP_View *view, Arena *arena, U64 i)
{
  MD_ScanChunk *sc = &view->scanner.chunks[i];
  String8 text = mdp_source_read(view, arena, sc->range);
  MD_Document doc = md_parse_part(arena, text, sc->range.min, &view->scanner.refs);
  // the last block loses its bottom margin only at the end of the document
  doc.end = view->scanner.done ? view->scanner.end : max_U64;
  doc.root_base = sc->root_base;
  doc.footnotes_before = sc->footnotes_before;
  return doc;
}

// Reads, parses, builds and lays out a chunk, unless it is already.
internal MDP_Chunk *
mdp_chunk_build(MDP_View *view, U64 i)
{
  MDP_Chunk *c = &view->chunks[i];
  if(mdp_chunk_is_built(view, c)) { return c; }
  if(c->arena == 0) { c->arena = arena_alloc(); }
  arena_clear(c->arena);
  c->doc = mdp_chunk_parse(view, c->arena, i);
  c->element = md_element_build(c->arena, &c->doc, &view->style, view->base_dir, mdp_text_base_from_chunk(i), 0);
  md_element_layout(c->element, &view->style, view->layout_width);
  c->build_key = view->build_key;
  c->height = c->element->height;
  c->lead = c->element->lead;
  c->trail = c->element->trail;
  c->measure_width = view->layout_width;
  c->measure_rem = view->style.rem;
  c->matches_key = 0;
  return c;
}

// A chunk's element for its texts alone: the built one, or one built in
// `arena` that loads no images and is not laid out.
internal MD_Element *
mdp_chunk_text_element(MDP_View *view, Arena *arena, U64 i, MD_Document **doc_out)
{
  MDP_Chunk *c = &view->chunks[i];
  if(c->element && c->build_key == view->build_key)
  {
    if(doc_out) { *doc_out = &c->doc; }
    return c->element;
  }
  MD_Document *doc = push_array(arena, MD_Document, 1);
  *doc = mdp_chunk_parse(view, arena, i);
  if(doc_out) { *doc_out = doc; }
  return md_element_build(arena, doc, &view->style, view->base_dir, mdp_text_base_from_chunk(i), 1);
}

// Stacks the chunks, collapsing the margins between their blocks as one
// element would. Chunks not laid out at this width take an estimate from
// the ones that are.
internal void
mdp_view_place_chunks(MDP_View *view)
{
  F64 measured_h = 0, measured_bytes = 0;
  for EachIndex(i, view->chunk_count)
  {
    MDP_Chunk *c = &view->chunks[i];
    if(c->measure_width == view->layout_width && c->measure_rem == view->style.rem)
    {
      measured_h += c->height;
      measured_bytes += dim_1u64(view->scanner.chunks[i].range);
    }
  }
  view->px_per_byte = (measured_bytes > 0) ? (F32)(measured_h/measured_bytes) : ClampBot(view->style.rem, 16.f)/48.f;
  F32 y = 0;
  F32 prev_trail = 0;
  for EachIndex(i, view->chunk_count)
  {
    MDP_Chunk *c = &view->chunks[i];
    if(c->measure_width != view->layout_width || c->measure_rem != view->style.rem)
    {
      c->height = dim_1u64(view->scanner.chunks[i].range)*view->px_per_byte;
      c->lead = c->trail = 0;
    }
    y -= Min(prev_trail, c->lead);
    c->y = y;
    y += c->height;
    prev_trail = c->trail;
  }
  view->tail_y = y;
  U64 scanned = view->chunk_count ? view->scanner.chunks[view->chunk_count-1].range.max : 0;
  view->content_height = y + (view->scanner.done ? 0 : (view->source_size - scanned)*view->px_per_byte);
}

// The last chunk that starts at or above y.
internal U64
mdp_chunk_at_y(MDP_View *view, F32 y)
{
  U64 lo = 0, hi = view->chunk_count;
  for(;lo + 1 < hi;)
  {
    U64 mid = (lo + hi)/2;
    if(view->chunks[mid].y <= y) { lo = mid; } else { hi = mid; }
  }
  return lo;
}

// The chunk holding a source offset.
internal U64
mdp_chunk_from_offset(MDP_View *view, U64 offset)
{
  U64 lo = 0, hi = view->chunk_count;
  for(;lo + 1 < hi;)
  {
    U64 mid = (lo + hi)/2;
    if(view->scanner.chunks[mid].range.min <= offset) { lo = mid; } else { hi = mid; }
  }
  return lo;
}

// The scroll position as a place in the chunk it falls in, which outlasts
// changes to the heights of the chunks above it.
typedef struct MDP_Anchor MDP_Anchor;
struct MDP_Anchor
{
  U64 chunk;
  F32 dy;
  F32 height;
};

internal MDP_Anchor
mdp_view_anchor(MDP_View *view)
{
  MDP_Anchor a = {0};
  if(view->chunk_count && view->scroll_y > 0)
  {
    a.chunk = mdp_chunk_at_y(view, view->scroll_y);
    a.dy = view->scroll_y - view->chunks[a.chunk].y;
    a.height = view->chunks[a.chunk].height;
  }
  return a;
}

// Puts the scroll position back at its anchor; when the anchor chunk itself
// changed height (it got laid out, or the font size changed), at the same
// fraction of it.
internal void
mdp_view_restore_anchor(MDP_View *view, MDP_Anchor a)
{
  if(a.chunk >= view->chunk_count) { return; }
  MDP_Chunk *c = &view->chunks[a.chunk];
  F32 dy = a.dy;
  if(c->height != a.height && a.height > 0 && dy > 0 && dy < a.height) { dy = dy/a.height*c->height; }
  view->scroll_y = c->y + dy;
}

// Builds the chunks around the viewport and drops those far from it. As
// chunks get laid out, those above the viewport change height, so the
// scroll position follows the chunk it is in.
internal void
mdp_view_update_chunks(MDP_View *view, MDP_Geometry *g, B32 stick_to_bottom)
{
  if(view->build_rem != view->style.rem || view->build_theme != view->theme_kind || view->build_scan_done != view->scanner.done)
  {
    view->build_key += 1;
    view->build_rem = view->style.rem;
    view->build_theme = view->theme_kind;
    view->build_scan_done = view->scanner.done;
  }
  // the anchor, from the positions the scroll position was set against
  MDP_Anchor anchor = mdp_view_anchor(view);
  view->layout_width = g->content_w;
  mdp_view_place_chunks(view);
  if(!stick_to_bottom) { mdp_view_restore_anchor(view, anchor); }
  F32 viewport_h = g->client.y1 - g->client.y0;
  view->scan_urgent = (!view->scanner.done && view->tail_y < view->scroll_y - g->padding + 1.5f*viewport_h);
  if(view->chunk_count == 0) { return; }
  for(U64 pass = 0; pass < 8; pass += 1)
  {
    if(stick_to_bottom) { view->scroll_y = ClampBot(0, view->content_height + 2*g->padding - viewport_h); }
    F32 top = view->scroll_y - g->padding - 0.5f*viewport_h;
    F32 bottom = view->scroll_y - g->padding + 1.5f*viewport_h;
    B32 built = 0;
    for(U64 i = mdp_chunk_at_y(view, top); i < view->chunk_count && view->chunks[i].y < bottom; i += 1)
    {
      if(!mdp_chunk_is_built(view, &view->chunks[i])) { mdp_chunk_build(view, i); built = 1; }
    }
    if(!built) { break; }
    mdp_view_place_chunks(view);
    if(!stick_to_bottom) { mdp_view_restore_anchor(view, anchor); }
  }

  //- drop what is far away, and the images only it showed
  F32 keep_top = view->scroll_y - g->padding - 2*viewport_h;
  F32 keep_bottom = view->scroll_y - g->padding + 3*viewport_h;
  B32 evicted = 0;
  for EachIndex(i, view->chunk_count)
  {
    MDP_Chunk *c = &view->chunks[i];
    if(c->arena && (c->y + c->height < keep_top || c->y > keep_bottom)) { mdp_chunk_evict(c); evicted = 1; }
  }
  if(evicted)
  {
    Temp scratch = scratch_begin(0, 0);
    MD_Element **keep = push_array(scratch.arena, MD_Element *, view->chunk_count);
    U64 keep_count = 0;
    for EachIndex(i, view->chunk_count) { if(view->chunks[i].element) { keep[keep_count++] = view->chunks[i].element; } }
    md_images_trim(keep, keep_count);
    scratch_end(scratch);
  }
}

// Builds a chunk away from the viewport (a link or search target), keeping
// the viewport's content where it is.
internal MDP_Chunk *
mdp_view_build_chunk_at(MDP_View *view, MDP_Geometry *g, U64 i)
{
  MDP_Anchor anchor = mdp_view_anchor(view);
  MDP_Chunk *c = mdp_chunk_build(view, i);
  mdp_view_place_chunks(view);
  mdp_view_restore_anchor(view, anchor);
  F32 viewport_h = g->client.y1 - g->client.y0;
  g->total_h = view->content_height + 2*g->padding;
  g->max_scroll = ClampBot(0, g->total_h - viewport_h);
  return c;
}

internal MDP_Geometry
mdp_geometry(MDP_View *view, WM_Window window)
{
  MDP_Geometry g = {0};
  g.client = wm_client_rect_from_window(window);
  g.client.y0 = Min(g.client.y0 + view->top_inset, g.client.y1);
  g.client.x0 = Min(g.client.x0 + view->left_inset, g.client.x1);
  F32 dpi = wm_dpi_from_window(window);
  g.scale = (dpi > 0) ? dpi/96.f : 1.f;
  g.rem = round_f32(view->font_size*g.scale);
  g.ui = 16.f*g.scale;
  if(view->search_open)
  {
    g.bar = r2f32p(g.client.x0, g.client.y0, g.client.x1, g.client.y0 + round_f32(2.5f*g.ui));
    g.client.y0 = g.bar.y1;
  }
  view->theme = th_theme_from_kind(view->theme_kind);
  view->style = md_style_preview(&view->theme, &view->fonts, g.rem, g.scale);
  md_hit_style = &view->style;

  // .p_4() scroll container, then .w_full().max_w(800px).mx_auto()
  g.padding = g.rem;
  F32 avail = (g.client.x1 - g.client.x0) - 2*g.padding;
  g.content_w = floor_f32(ClampBot(1, Min(avail, MDP_MAX_WIDTH_PX*g.scale)));
  g.content_x = g.client.x0 + g.padding + floor_f32((avail - g.content_w)/2);

  F32 viewport_h = g.client.y1 - g.client.y0;
  B32 at_bottom = (view->scroll_y > 0 && view->scroll_y >= view->content_height + 2*g.padding - viewport_h - 0.5f);
  mdp_view_update_chunks(view, &g, at_bottom);
  g.total_h = view->content_height + 2*g.padding;
  g.max_scroll = ClampBot(0, g.total_h - viewport_h);
  view->scroll_y = Clamp(0, view->scroll_y, g.max_scroll);
  if(view->pending_target != MDP_LinkTargetKind_None || view->pending_offset_valid) { mdp_view_apply_pending_target(view, &g); }
  mdp_search_refresh(view, &g);
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
//~ Selection Space Across Chunks

internal MD_Hit
mdp_view_hit_test(MDP_View *view, Vec2F32 p, MD_Element **el_out)
{
  MD_Hit hit = {0};
  *el_out = 0;
  if(view->chunk_count == 0) { return hit; }
  MDP_Chunk *c = &view->chunks[mdp_chunk_at_y(view, p.y)];
  if(c->element == 0) { return hit; }
  *el_out = c->element;
  return md_element_hit_test(c->element, &view->ix, v2f32(p.x, p.y - c->y));
}

internal String8
mdp_view_link_url(MD_Element *el, U64 link)
{
  return (el && link && link <= el->link_count) ? el->links[link-1].url : str8_zero();
}

internal U64
mdp_view_offset_from_point(MDP_View *view, Vec2F32 p)
{
  if(view->chunk_count == 0) { return 0; }
  U64 i = mdp_chunk_at_y(view, p.y);
  // a point past the chunks that are built (a drag far out of view): the nearest one that is
  if(view->chunks[i].element == 0)
  {
    U64 j = i;
    for(;j > 0 && view->chunks[j].element == 0; j -= 1);
    if(view->chunks[j].element == 0)
    {
      for(j = i; j+1 < view->chunk_count && view->chunks[j].element == 0; j += 1);
    }
    if(view->chunks[j].element == 0) { return mdp_text_base_from_chunk(i); }
    i = j;
  }
  MDP_Chunk *c = &view->chunks[i];
  return md_element_offset_from_point(c->element, &view->style, &view->ix, v2f32(p.x, p.y - c->y));
}

internal MD_Element *
mdp_view_element_at_offset(MDP_View *view, U64 offset)
{
  U64 i = mdp_chunk_from_text_offset(offset);
  return (i < view->chunk_count) ? view->chunks[i].element : 0;
}

internal Rng1U64
mdp_view_word_at(MDP_View *view, U64 offset)
{
  MD_Element *el = mdp_view_element_at_offset(view, offset);
  return el ? md_element_word_at(el, offset) : r1u64(offset, offset);
}

// The rendered text in a selection, through every chunk it covers.
internal String8
mdp_view_text_in_range(MDP_View *view, Arena *arena, U64 start, U64 end)
{
  if(end <= start || view->chunk_count == 0) { return str8_zero(); }
  U64 a = mdp_chunk_from_text_offset(start);
  U64 z = Min(mdp_chunk_from_text_offset(end), view->chunk_count-1);
  String8List parts = {0};
  for(U64 i = a; i <= z; i += 1)
  {
    Temp scratch = scratch_begin(&arena, 1);
    MD_Element *el = mdp_chunk_text_element(view, scratch.arena, i, 0);
    U64 s = Max(start, el->text_base), e = Min(end, el->text_end);
    if(s < e)
    {
      String8 text = md_element_text_in_range(arena, el, s, e);
      if(text.size) { str8_list_push(arena, &parts, text); }
    }
    scratch_end(scratch);
  }
  return str8_list_join(arena, &parts, &(StringJoin){.sep = str8_lit_comp("\n")});
}

// The Markdown source behind a selection; across chunks, the source of the
// blocks it touches.
internal String8
mdp_view_source_in_range(MDP_View *view, Arena *arena, U64 start, U64 end)
{
  if(end <= start || view->chunk_count == 0) { return str8_zero(); }
  U64 a = mdp_chunk_from_text_offset(start);
  U64 z = Min(mdp_chunk_from_text_offset(end), view->chunk_count-1);
  Temp scratch = scratch_begin(&arena, 1);
  String8 result = {0};
  MD_Document *doc_a = 0;
  MD_Element *el_a = mdp_chunk_text_element(view, scratch.arena, a, &doc_a);
  if(a == z)
  {
    if(el_a->text_end > el_a->text_base) { end = Min(end, el_a->text_end - 1); }
    result = push_str8_copy(arena, md_element_source_in_range(el_a, doc_a, start, end));
  }
  else
  {
    MD_Element *el_z = mdp_chunk_text_element(view, scratch.arena, z, 0);
    MD_Text *ta = md_element_text_at(el_a, start);
    MD_Text *tz = md_element_text_at(el_z, end);
    Rng1U64 range = r1u64(view->scanner.chunks[a].range.min, view->scanner.chunks[z].range.max);
    if(ta) { range.min = md_box_source_range(ta->box).min; }
    if(tz) { range.max = md_box_source_range(tz->box).max; }
    result = push_str8_copy(arena, str8_skip_chop_whitespace(mdp_source_read(view, scratch.arena, range)));
  }
  scratch_end(scratch);
  return result;
}

// Puts a selection on the clipboard, as rendered text or as its Markdown
// source. A selection can span the whole document, so it is gathered in an
// arena of its own and given back after.
internal void
mdp_view_copy(MDP_View *view, U64 start, U64 end, B32 as_markdown)
{
  Arena *arena = arena_alloc();
  wm_set_clipboard_text(as_markdown ? mdp_view_source_in_range(view, arena, start, end) : mdp_view_text_in_range(view, arena, start, end));
  arena_release(arena);
}

////////////////////////////////
//~ Document

internal void
mdp_view_init(MDP_View *view)
{
  view->arena = arena_alloc();
  view->doc_arena = arena_alloc();
  view->chunk_arena = arena_alloc();
  view->interaction_arena = arena_alloc();
  view->menu_selected = max_U64;
  view->font_size = MDP_DEFAULT_FONT_SIZE_PX;
  view->theme_kind = TH_Kind_OneDark;
  view->theme = th_theme_from_kind(view->theme_kind);
  view->search_key = 1;
}

// Forgets the document and starts scanning the source again.
internal void
mdp_view_reset_document(MDP_View *view)
{
  for EachIndex(i, view->chunk_count) { mdp_chunk_evict(&view->chunks[i]); }
  arena_clear(view->chunk_arena);
  view->chunks = 0;
  view->chunk_count = 0;
  view->chunk_cap = 0;
  md_scanner_release(&view->scanner);
  md_scanner_init(&view->scanner, MDP_CHUNK_BYTES);
  view->scan_read_size = MDP_SCAN_READ_BYTES;
  view->build_key += 1;
  view->content_height = 0;
  view->ix.selection_start = view->ix.selection_end = 0;
  mdp_search_reset(view);

  // what is on screen first is scanned now, the rest a little each frame
  mdp_view_scan(view, 20000);
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
  view->in_memory = 1;
  view->source_size = view->text.size;
  view->error = str8_zero();
  mdp_view_reset_document(view);
  scratch_end(scratch);
}

internal B32
mdp_view_open_path(MDP_View *view, String8 path, B32 keep_scroll)
{
  Temp scratch = scratch_begin(0, 0);
  String8 full = full_path_from_path(scratch.arena, path);
  FileProperties props = properties_from_file_path(full);
  File file = file_open(AccessFlag_Read|AccessFlag_ShareRead|AccessFlag_ShareWrite, full);
  B32 ok = 0;
  if(props.modified != 0 && !(props.flags & FilePropertyFlag_IsFolder) && !file_match(file, file_zero()))
  {
    B32 same = str8_match(full, view->path, 0);
    if(!keep_scroll || !same)
    {
      md_images_reset();
      view->scroll_y = 0;
      view->ix.active_root_index = 0;
      view->pending_target = MDP_LinkTargetKind_None;
      view->pending_offset_valid = 0;
      arena_clear(view->interaction_arena);
      view->ix.first_scroll = 0;
    }
    else if(view->chunk_count)
    {
      // a reload keeps the top of the viewport where it was in the source
      MDP_Chunk *c = &view->chunks[mdp_chunk_at_y(view, view->scroll_y)];
      view->pending_offset_valid = 1;
      view->pending_offset = view->scanner.chunks[c - view->chunks].range.min;
      view->pending_offset_dy = view->scroll_y - c->y;
    }
    arena_clear(view->doc_arena);
    view->name = push_str8_copy(view->doc_arena, str8_skip_last_slash(full));
    view->path = push_str8_copy(view->doc_arena, full);
    view->base_dir = push_str8_copy(view->doc_arena, str8_chop_last_slash(full));
    view->text = str8_zero();
    view->in_memory = 0;
    view->source_size = props.size;
    view->error = str8_zero();
    view->file_modified = props.modified;
    mdp_view_reset_document(view);
    ok = 1;
  }
  else
  {
    view->error = push_str8f(view->arena, "Could not open %S", full);
    view->error_until_us = now_time_us() + 4000000;
  }
  file_close(file);
  scratch_end(scratch);
  return ok;
}

internal void mdp_search_count(MDP_View *view, U64 budget_us);

// Once a frame: re-reads the file when it changed on disk (after Zed's
// reparse debounce), scans more of it, and counts more search matches.
internal void
mdp_view_poll_file(MDP_View *view)
{
  // a remote image finished downloading: build the chunks again
  if(md_images_poll()) { view->build_key += 1; }
  if(!view->scanner.done)
  {
    // the viewport waits on the scan: scan harder
    mdp_view_scan(view, view->scan_urgent ? 4*MDP_SCAN_BUDGET_US : MDP_SCAN_BUDGET_US);
  }
  mdp_search_count(view, MDP_SCAN_BUDGET_US);
  if(view->path.size == 0 || view->in_memory) { return; }
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

// on_checkbox_toggle: [ ] <-> [x]. One byte changes, so the scan still holds
// and only the chunk with the checkbox is built again.
internal void
mdp_view_toggle_task(MDP_View *view, U64 offset)
{
  Temp scratch = scratch_begin(0, 0);
  String8 current = mdp_source_read(view, scratch.arena, r1u64(offset, offset+1));
  if(current.size == 1 && view->chunk_count)
  {
    U8 c = (current.str[0] == ' ') ? 'x' : ' ';
    B32 ok = 1;
    if(view->in_memory)
    {
      view->text.str[offset] = c;
    }
    else
    {
      File file = file_open(AccessFlag_Write|AccessFlag_Append|AccessFlag_ShareRead|AccessFlag_ShareWrite, view->path);
      ok = !file_match(file, file_zero()) && file_write(file, r1u64(offset, offset+1), &c) == 1;
      file_close(file);
      view->file_modified = properties_from_file_path(view->path).modified;
    }
    if(ok) { view->chunks[mdp_chunk_from_offset(view, offset)].build_key = 0; }
    else
    {
      view->error = push_str8f(view->arena, "Could not write %S", view->path);
      view->error_until_us = now_time_us() + 4000000;
    }
  }
  scratch_end(scratch);
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
mdp_view_scroll_to_box(MDP_View *view, MDP_Geometry *g, MDP_Chunk *c, MD_Box *box)
{
  view->scroll_y = Clamp(0, g->padding + c->y + box->rect.y0, g->max_scroll);
  view->scroll_activity_us = now_time_us();
}

internal F32
mdp_text_chunk_y(MDP_View *view, MD_Text *t)
{
  U64 i = mdp_chunk_from_text_offset(t->global_start);
  return (i < view->chunk_count) ? view->chunks[i].y : 0;
}

// MarkdownElement::autoscroll (AutoscrollBehavior::Controlled): keep the
// target line 3 line heights inside the viewport, scrolling only as needed.
internal void
mdp_view_autoscroll_to_line(MDP_View *view, MDP_Geometry *g, MD_Text *t, U64 line_index)
{
  if(t == 0 || t->box == 0) { return; }
  F32 line_h = t->line_height;
  F32 margin = line_h*3.f;
  F32 top_goal = g->client.y0 + margin;
  F32 bottom_goal = g->client.y1 - margin;
  F32 y = g->client.y0 + g->padding - view->scroll_y + mdp_text_chunk_y(view, t) + t->box->rect.y0 + line_index*line_h;
  if(y < top_goal) { view->scroll_y -= top_goal - y; }
  else if(y + line_h > bottom_goal) { view->scroll_y += (y + line_h) - bottom_goal; }
  view->scroll_y = Clamp(0, view->scroll_y, g->max_scroll);
  view->scroll_activity_us = now_time_us();
}

internal void
mdp_view_autoscroll_to_text(MDP_View *view, MDP_Geometry *g, MD_Text *t)
{
  mdp_view_autoscroll_to_line(view, g, t, 0);
}

// Scrolls a selection-space offset into view, horizontally too when it sits
// in a code block or table that scrolls sideways.
internal void
mdp_view_autoscroll_to_offset(MDP_View *view, MDP_Geometry *g, U64 offset)
{
  U64 i = mdp_chunk_from_text_offset(offset);
  if(i >= view->chunk_count) { return; }
  MDP_Chunk *c = mdp_view_build_chunk_at(view, g, i);
  MD_Text *t = md_element_text_at(c->element, offset);
  if(t == 0 || t->lines == 0) { return; }
  U64 local = offset - t->global_start;
  U64 li = 0;
  for(;li+1 < t->line_count && local >= t->lines[li+1].start; li += 1);
  mdp_view_autoscroll_to_line(view, g, t, li);
  for(MD_Box *b = t->box->parent; b != 0; b = b->parent)
  {
    if(!b->scroll_x) { continue; }
    F32 inner = (b->rect.x1 - b->rect.x0) - b->border[MD_Side_Left] - b->border[MD_Side_Right];
    F32 max = ClampBot(0, b->content_width - inner);
    F32 x = (t->box->rect.x0 - b->rect.x0) + md_text_x_for_offset(&view->style, t, &t->lines[li], local);
    F32 *off = md_scroll_offset_ptr(&view->ix, view->interaction_arena, b->key);
    if(x < *off + g->rem) { *off = x - g->rem; }
    else if(x > *off + inner - 2*g->rem) { *off = x - inner + 2*g->rem; }
    *off = Clamp(0, *off, max);
    break;
  }
}

//- headings

internal MD_ScanHeading *
mdp_view_headings(MDP_View *view, U64 *count_out)
{
  *count_out = view->scanner.heading_count;
  return view->scanner.headings;
}

// The last heading that starts at or before a source offset, or max_U64.
internal U64
mdp_heading_at_or_before(MDP_View *view, U64 offset)
{
  MD_ScanHeading *h = view->scanner.headings;
  U64 lo = 0, hi = view->scanner.heading_count;
  for(;lo < hi;)
  {
    U64 mid = (lo + hi)/2;
    if(h[mid].offset <= offset) { lo = mid + 1; } else { hi = mid; }
  }
  return lo ? lo-1 : max_U64;
}

internal MD_Box *
mdp_chunk_heading_box(MDP_Chunk *c, U64 offset)
{
  for EachIndex(i, c->element->heading_count)
  {
    if(c->element->headings[i].box->src.min == offset) { return c->element->headings[i].box; }
  }
  return 0;
}

// The heading a `#slug` names: heading_slugs maps each heading's slug, and
// a repeated slug also as slug-N (N = earlier anchors with that slug).
internal U64
mdp_heading_from_slug(MDP_View *view, String8 name)
{
  Temp scratch = scratch_begin(0, 0);
  typedef struct MDP_SlugCount MDP_SlugCount;
  struct MDP_SlugCount { MDP_SlugCount *next; String8 slug; U64 count; };
  U64 slot_count = 4096;
  MDP_SlugCount **slots = push_array(scratch.arena, MDP_SlugCount *, slot_count);
  U64 result = max_U64;
  for EachIndex(i, view->scanner.heading_count)
  {
    String8 names[2] = {md_heading_slug(scratch.arena, view->scanner.headings[i].text)};
    U64 name_count = 1;
    for EachIndex(k, name_count)
    {
      if(str8_match(names[k], name, 0)) { result = i; goto done; }
      MDP_SlugCount **slot = &slots[u64_hash_from_str8(names[k]) % slot_count];
      MDP_SlugCount *n = *slot;
      for(;n != 0 && !str8_match(n->slug, names[k], 0); n = n->next);
      if(n == 0) { n = push_array(scratch.arena, MDP_SlugCount, 1); n->slug = names[k]; SLLStackPush(*slot, n); }
      if(k == 0 && n->count) { names[name_count++] = push_str8f(scratch.arena, "%S-%I64u", names[0], n->count); }
      n->count += 1;
    }
  }
  done:;
  scratch_end(scratch);
  return result;
}

// Markdown::scroll_to_heading, plus the editor-cursor move Zed does for links
// in the same file (that makes the heading's root block active).
internal B32
mdp_view_scroll_to_heading(MDP_View *view, MDP_Geometry *g, String8 slug, B32 activate)
{
  U64 index = mdp_heading_from_slug(view, slug);
  if(index == max_U64 || view->chunk_count == 0) { return 0; }
  U64 offset = view->scanner.headings[index].offset;
  MDP_Chunk *c = mdp_view_build_chunk_at(view, g, mdp_chunk_from_offset(view, offset));
  MD_Box *box = mdp_chunk_heading_box(c, offset);
  if(box == 0) { return 0; }
  mdp_view_autoscroll_to_text(view, g, md_element_first_text_in(c->element, box));
  if(activate) { view->ix.active_root_index = md_box_root_index(box); }
  return 1;
}

// A footnote reference's target: the definition's content.
internal void
mdp_view_scroll_to_footnote(MDP_View *view, MDP_Geometry *g, String8 anchor)
{
  String8 label = str8_skip(anchor, 9);
  for EachIndex(i, view->scanner.footnote_count)
  {
    MD_ScanFootnote *f = &view->scanner.footnotes[i];
    if(!str8_match(f->label, label, 0)) { continue; }
    MDP_Chunk *c = mdp_view_build_chunk_at(view, g, mdp_chunk_from_offset(view, f->offset));
    MD_Element *el = c->element;
    for EachIndex(k, el->anchor_count)
    {
      if(str8_match(el->anchor_names[k], anchor, 0))
      {
        mdp_view_autoscroll_to_text(view, g, md_element_first_text_in(el, el->anchor_boxes[k]));
        break;
      }
    }
    break;
  }
}

// navigate_to_link_target / sync_preview_to_source_index for a row and
// column. False while the scan has not reached the row.
internal B32
mdp_view_scroll_to_position(MDP_View *view, MDP_Geometry *g, U64 row, U64 column)
{
  if(view->chunk_count == 0) { return view->scanner.done; }
  MD_ScanChunk *last = &view->scanner.chunks[view->chunk_count-1];
  if(!view->scanner.done && row >= last->line_base) { return 0; }
  U64 lo = 0, hi = view->chunk_count;
  for(;lo + 1 < hi;)
  {
    U64 mid = (lo + hi)/2;
    if(view->scanner.chunks[mid].line_base <= row) { lo = mid; } else { hi = mid; }
  }
  MD_ScanChunk *sc = &view->scanner.chunks[lo];
  MDP_Chunk *c = mdp_view_build_chunk_at(view, g, lo);
  String8 src = c->doc.source;
  U64 offset = 0;
  for(U64 r = sc->line_base; r < row && offset < src.size; r += 1)
  {
    U64 nl = str8_find_needle(src, offset, str8_lit("\n"), 0);
    offset = (nl < src.size) ? nl+1 : src.size;
  }
  U64 nl = str8_find_needle(src, offset, str8_lit("\n"), 0);
  offset = sc->range.min + Min(offset + column, nl);
  MD_Text *t = md_element_text_for_source_index(c->element, offset);
  mdp_view_autoscroll_to_text(view, g, t);
  if(t) { view->ix.active_root_index = md_box_root_index(t->box); }
  return 1;
}

// The outline panel's jump: the heading's box to the top of the viewport,
// below the container's p_4.
internal void
mdp_view_scroll_to_heading_index(MDP_View *view, WM_Window window, U64 index)
{
  MDP_Geometry g = mdp_geometry(view, window);
  if(index >= view->scanner.heading_count || view->chunk_count == 0) { return; }
  U64 offset = view->scanner.headings[index].offset;
  MDP_Chunk *c = mdp_view_build_chunk_at(view, &g, mdp_chunk_from_offset(view, offset));
  MD_Box *box = mdp_chunk_heading_box(c, offset);
  view->scroll_y = Clamp(0, c->y + (box ? box->rect.y0 : 0), g.max_scroll);
  view->scroll_activity_us = now_time_us();
  if(box) { view->ix.active_root_index = md_box_root_index(box); }
}

// The heading being read: the last one that starts in the top fifth of the
// viewport or above it, or max_U64 above the first.
internal U64
mdp_view_heading_index_at_top(MDP_View *view, WM_Window window)
{
  MDP_Geometry g = mdp_geometry(view, window);
  if(view->chunk_count == 0) { return max_U64; }
  F32 top = view->scroll_y + 0.2f*(g.client.y1 - g.client.y0);
  U64 i = mdp_chunk_at_y(view, top);
  MDP_Chunk *c = &view->chunks[i];
  U64 offset = max_U64;
  if(c->element)
  {
    for EachIndex(k, c->element->heading_count)
    {
      MD_Box *box = c->element->headings[k].box;
      if(c->y + box->rect.y0 > top) { break; }
      offset = box->src.min;
    }
  }
  if(offset != max_U64) { return mdp_heading_at_or_before(view, offset); }
  U64 start = view->scanner.chunks[i].range.min;
  return start ? mdp_heading_at_or_before(view, start-1) : max_U64;
}

// Targets for a link into another file wait until that file is scanned that
// far, and laid out (scroll_to_heading_when_parsed); so does the position a
// reload keeps.
internal void
mdp_view_apply_pending_target(MDP_View *view, MDP_Geometry *g)
{
  B32 done = view->scanner.done;
  if(view->pending_offset_valid && view->chunk_count &&
     (done || view->pending_offset < view->scanner.chunks[view->chunk_count-1].range.max))
  {
    view->pending_offset_valid = 0;
    MDP_Chunk *c = mdp_view_build_chunk_at(view, g, mdp_chunk_from_offset(view, view->pending_offset));
    view->scroll_y = Clamp(0, c->y + view->pending_offset_dy, g->max_scroll);
  }
  else if(view->pending_offset_valid && done)
  {
    view->pending_offset_valid = 0;
  }
  switch(view->pending_target)
  {
    default:{}break;
    case MDP_LinkTargetKind_Heading:
    {
      if(mdp_view_scroll_to_heading(view, g, str8(view->pending_slug, view->pending_slug_size), 0) || done)
      {
        view->pending_target = MDP_LinkTargetKind_None;
      }
    }break;
    case MDP_LinkTargetKind_Position:
    {
      if(mdp_view_scroll_to_position(view, g, view->pending_row, view->pending_column) || done)
      {
        view->pending_target = MDP_LinkTargetKind_None;
      }
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
    mdp_view_scroll_to_footnote(view, g, url);
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
//~ Search (BufferSearchBar + SearchableItem)
//
// Zed searches the source and drops matches in markup that isn't rendered;
// mdview searches the rendered text, which is what that leaves. Matches
// highlight with `search_match_background`, the active one with
// `search_active_match_background`, and activating one autoscrolls to it.
//
// The whole document is searched a few chunks a frame, starting at the
// anchor's chunk; a chunk keeps only its match count, and its matches while
// it is built.

internal B32
mdp_char_is_word(U8 c)
{
  return md_char_is_alnum(c) || c == '_' || c >= 0x80;
}

internal B32
mdp_search_active(MDP_View *view)
{
  return view->search_open && view->search_query_size > 0;
}

internal Rng1U64 *
mdp_search_element(MDP_View *view, Arena *arena, MD_Element *el, U64 *count_out)
{
  String8 q = str8(view->search_query, view->search_query_size);
  U64 cap = 64, n = 0;
  Rng1U64 *m = push_array_no_zero(arena, Rng1U64, cap);
  for EachIndex(ti, el->text_count)
  {
    MD_Text *t = el->texts[ti];
    String8 s = t->string;
    for(U64 i = 0; i + q.size <= s.size; i += 1)
    {
      B32 eq = 1;
      for EachIndex(k, q.size)
      {
        U8 a = s.str[i+k], c = q.str[k];
        if(!view->search_case) { a = lower_from_char(a); c = lower_from_char(c); }
        if(a != c) { eq = 0; break; }
      }
      if(!eq) { continue; }
      if(view->search_word)
      {
        if(i > 0 && mdp_char_is_word(s.str[i-1]) && mdp_char_is_word(q.str[0])) { continue; }
        if(i + q.size < s.size && mdp_char_is_word(s.str[i+q.size]) && mdp_char_is_word(q.str[q.size-1])) { continue; }
      }
      if(n == cap)
      {
        Rng1U64 *bigger = push_array_no_zero(arena, Rng1U64, cap*2);
        MemoryCopy(bigger, m, sizeof(*m)*n);
        m = bigger;
        cap *= 2;
      }
      m[n++] = r1u64(t->global_start + i, t->global_start + i + q.size);
      i += q.size - 1;
    }
  }
  *count_out = n;
  return m;
}

// False when a chunk's source can't hold a match: rendered text keeps the
// source's runs of letters and digits, so the query's longest run must be
// in it.
internal B32
mdp_search_source_may_match(MDP_View *view, String8 source)
{
  String8 q = str8(view->search_query, view->search_query_size);
  String8 run = {0};
  for(U64 i = 0; i < q.size;)
  {
    U64 k = i;
    for(;k < q.size && (md_char_is_alnum(q.str[k]) || q.str[k] >= 0x80); k += 1);
    if(k - i > run.size) { run = str8_substr(q, r1u64(i, k)); }
    i = k+1;
  }
  if(run.size == 0) { return 1; }
  return str8_find_needle(source, 0, run, view->search_case ? 0 : StringMatchFlag_CaseInsensitive) < source.size;
}

// A chunk's matches: kept while it is built, else found in `arena`.
internal Rng1U64 *
mdp_search_chunk_matches(MDP_View *view, Arena *arena, U64 i, U64 *count_out)
{
  MDP_Chunk *c = &view->chunks[i];
  if(c->element && c->build_key == view->build_key)
  {
    if(c->matches_key != view->search_key)
    {
      c->matches = mdp_search_element(view, c->arena, c->element, &c->match_count);
      c->matches_key = view->search_key;
    }
    *count_out = c->match_count;
    return c->matches;
  }
  Temp scratch = scratch_begin(&arena, 1);
  *count_out = 0;
  Rng1U64 *result = 0;
  String8 source = mdp_source_read(view, scratch.arena, view->scanner.chunks[i].range);
  if(mdp_search_source_may_match(view, source))
  {
    U64 n = 0;
    Rng1U64 *m = mdp_search_element(view, scratch.arena, mdp_chunk_text_element(view, scratch.arena, i, 0), &n);
    result = push_array_no_zero(arena, Rng1U64, n + 1);
    MemoryCopy(result, m, sizeof(*m)*n);
    *count_out = n;
  }
  scratch_end(scratch);
  return result;
}

internal U64
mdp_search_count_chunk(MDP_View *view, U64 i)
{
  MDP_Chunk *c = &view->chunks[i];
  if(c->search_key != view->search_key)
  {
    Temp scratch = scratch_begin(0, 0);
    mdp_search_chunk_matches(view, scratch.arena, i, &c->search_count);
    scratch_end(scratch);
    c->search_key = view->search_key;
    view->search_total += c->search_count;
  }
  return c->search_count;
}

internal void
mdp_search_activate(MDP_View *view, U64 i, U64 index)
{
  Temp scratch = scratch_begin(0, 0);
  U64 n = 0;
  Rng1U64 *m = mdp_search_chunk_matches(view, scratch.arena, i, &n);
  if(index < n)
  {
    view->search_has_active = 1;
    view->search_active_range = m[index];
  }
  scratch_end(scratch);
}

internal U64
mdp_search_first_chunk(MDP_View *view)
{
  return Min(mdp_chunk_from_text_offset(view->search_anchor), ClampBot(view->chunk_count, 1) - 1);
}

internal B32
mdp_search_counting(MDP_View *view)
{
  if(!mdp_search_active(view)) { return 0; }
  return !view->search_wrapped || view->search_next <= mdp_search_first_chunk(view);
}

// Counts matches a chunk at a time until the budget runs out: from the
// anchor's chunk to the end, then from the start. The first match at or
// after the anchor becomes active (active_match_index, Direction::Next),
// wrapping around to the first one.
internal void
mdp_search_count(MDP_View *view, U64 budget_us)
{
  U64 start_us = now_time_us();
  for(;mdp_search_counting(view);)
  {
    if(!view->search_wrapped && view->search_next >= view->chunk_count)
    {
      if(!view->scanner.done) { break; }
      view->search_wrapped = 1;
      view->search_next = 0;
      continue;
    }
    if(view->search_next >= view->chunk_count) { view->search_next = mdp_search_first_chunk(view) + 1; break; } // nothing to search
    U64 i = view->search_next++;
    U64 count = mdp_search_count_chunk(view, i);
    if(!view->search_has_active && count > 0)
    {
      if(view->search_wrapped) { mdp_search_activate(view, i, 0); }
      else
      {
        Temp scratch = scratch_begin(0, 0);
        U64 n = 0;
        Rng1U64 *m = mdp_search_chunk_matches(view, scratch.arena, i, &n);
        for EachIndex(k, n) { if(m[k].min >= view->search_anchor) { mdp_search_activate(view, i, k); break; } }
        scratch_end(scratch);
      }
    }
    if(now_time_us() - start_us >= budget_us) { break; }
  }
}

// Starts counting over: the query, its options or the document changed.
internal void
mdp_search_reset(MDP_View *view)
{
  view->search_key += 1;
  view->search_total = 0;
  view->search_wrapped = 0;
  view->search_has_active = 0;
  view->search_next = mdp_search_first_chunk(view);
}

// The active match's place among all of them (1-based), or 0 while chunks
// before it are still to be counted.
internal U64
mdp_search_active_ordinal(MDP_View *view)
{
  if(!view->search_has_active) { return 0; }
  U64 i = mdp_chunk_from_text_offset(view->search_active_range.min);
  U64 before = 0;
  for EachIndex(k, i)
  {
    if(view->chunks[k].search_key != view->search_key) { return 0; }
    before += view->chunks[k].search_count;
  }
  Temp scratch = scratch_begin(0, 0);
  U64 n = 0;
  Rng1U64 *m = mdp_search_chunk_matches(view, scratch.arena, i, &n);
  U64 local = 0;
  for(;local < n && m[local].min < view->search_active_range.min; local += 1);
  scratch_end(scratch);
  return before + local + 1;
}

internal void
mdp_search_refresh(MDP_View *view, MDP_Geometry *g)
{
  if(view->search_dirty)
  {
    view->search_dirty = 0;
    mdp_search_reset(view);
    mdp_search_count(view, MDP_SCAN_BUDGET_US);
  }
  if(view->search_scroll_pending && view->search_has_active)
  {
    view->search_scroll_pending = 0;
    mdp_view_autoscroll_to_offset(view, g, view->search_active_range.min);
  }
}

internal void
mdp_search_changed(MDP_View *view)
{
  view->search_dirty = 1;
  view->search_scroll_pending = 1;
}

internal void
mdp_search_open(MDP_View *view, MDP_Geometry *g)
{
  view->search_open = 1;
  view->search_focused = 1;
  // query_suggestion: the selected text, when it is a single line (and so
  // fits the query: offsets in one chunk count bytes)
  U64 sel_start = view->ix.selection_start, sel_end = view->ix.selection_end;
  if(sel_end > sel_start)
  {
    if(sel_end - sel_start <= sizeof(view->search_query))
    {
      Temp scratch = scratch_begin(0, 0);
      String8 sel = mdp_view_text_in_range(view, scratch.arena, sel_start, sel_end);
      if(sel.size && sel.size <= sizeof(view->search_query) && str8_find_needle(sel, 0, str8_lit("\n"), 0) == sel.size)
      {
        MemoryCopy(view->search_query, sel.str, sel.size);
        view->search_query_size = sel.size;
      }
      scratch_end(scratch);
    }
    view->search_anchor = sel_start;
  }
  else
  {
    // from the top of the viewport
    view->search_anchor = mdp_view_offset_from_point(view, v2f32(0, view->scroll_y - g->padding));
  }
  mdp_search_changed(view);
}

internal void
mdp_search_close(MDP_View *view)
{
  view->search_open = 0;
  view->search_focused = 0;
  view->search_dirty = 1;
}

// The next or previous match, counting the chunks on the way that are not
// counted yet.
internal void
mdp_search_step(MDP_View *view, B32 backwards)
{
  if(!view->search_has_active || view->chunk_count == 0) { return; }
  U64 n = view->chunk_count;
  U64 i = mdp_chunk_from_text_offset(view->search_active_range.min);
  Temp scratch = scratch_begin(0, 0);
  U64 count = 0;
  Rng1U64 *m = mdp_search_chunk_matches(view, scratch.arena, i, &count);
  U64 local = 0;
  for(;local < count && m[local].min < view->search_active_range.min; local += 1);
  scratch_end(scratch);
  if(!backwards && local+1 < count) { mdp_search_activate(view, i, local+1); }
  else if(backwards && local > 0 && local <= count) { mdp_search_activate(view, i, local-1); }
  else
  {
    for(U64 step = 1; step <= n; step += 1)
    {
      if(!view->scanner.done && (backwards ? step > i : i + step >= n)) { break; } // no wrapping before the end is known
      U64 j = backwards ? (i + n - step) % n : (i + step) % n;
      U64 c = mdp_search_count_chunk(view, j);
      if(c > 0) { mdp_search_activate(view, j, backwards ? c-1 : 0); break; }
    }
  }
  view->search_anchor = view->search_active_range.min;
  view->search_scroll_pending = 1;
}

internal void
mdp_search_insert(MDP_View *view, String8 text)
{
  for EachIndex(i, text.size)
  {
    U8 c = text.str[i];
    if(c == '\n' || c == '\r') { break; }
    if(view->search_query_size < sizeof(view->search_query)) { view->search_query[view->search_query_size++] = (c == '\t') ? ' ' : c; }
  }
  mdp_search_changed(view);
}

typedef struct MDP_SearchBar MDP_SearchBar;
struct MDP_SearchBar
{
  Rng2F32 input;
  Rng2F32 toggle_case;
  Rng2F32 toggle_word;
  Rng2F32 count;
  Rng2F32 prev;
  Rng2F32 next;
  Rng2F32 close;
};

internal MDP_SearchBar
mdp_search_bar_layout(MDP_Geometry *g)
{
  MDP_SearchBar l = {0};
  F32 u = g->ui;
  Rng2F32 bar = g->bar;
  F32 bar_h = bar.y1 - bar.y0;
  F32 h = round_f32(1.75f*u);
  F32 btn = round_f32(1.5f*u);
  F32 gap = round_f32(0.25f*u);
  F32 count_w = round_f32(4.f*u);
  F32 y0 = bar.y0 + round_f32((bar_h - h)/2);
  F32 by0 = bar.y0 + round_f32((bar_h - btn)/2);
  F32 x = bar.x0 + round_f32(0.5f*u);
  F32 fixed = 5*btn + 6*gap + count_w + u;
  F32 input_w = Clamp(8*u, (bar.x1 - bar.x0) - fixed - u, 25*u);
  l.input = r2f32p(x, y0, x + input_w, y0 + h);                   x += input_w + gap;
  l.toggle_case = r2f32p(x, by0, x + btn, by0 + btn);             x += btn + gap;
  l.toggle_word = r2f32p(x, by0, x + btn, by0 + btn);             x += btn + 2*gap;
  l.count = r2f32p(x, y0, x + count_w, y0 + h);                   x += count_w + gap;
  l.prev = r2f32p(x, by0, x + btn, by0 + btn);                    x += btn + gap;
  l.next = r2f32p(x, by0, x + btn, by0 + btn);
  F32 cx = bar.x1 - round_f32(0.5f*u) - btn;
  l.close = r2f32p(cx, by0, cx + btn, by0 + btn);
  return l;
}

////////////////////////////////
//~ Context Menu (right_click_menu + ContextMenu)

typedef enum MDP_MenuItem
{
  MDP_MenuItem_Copy,
  MDP_MenuItem_CopyAsMarkdown,
  MDP_MenuItem_CopyLink,
}
MDP_MenuItem;

typedef struct MDP_MenuLayout MDP_MenuLayout;
struct MDP_MenuLayout
{
  Rng2F32 box;
  Rng2F32 items[3];
  F32 font_px;
  F32 item_pad;
};

internal String8
mdp_menu_label(U32 item)
{
  switch(item)
  {
    case MDP_MenuItem_Copy:           return str8_lit("Copy");
    case MDP_MenuItem_CopyAsMarkdown: return str8_lit("Copy as Markdown");
    case MDP_MenuItem_CopyLink:       return str8_lit("Copy Link");
  }
  return str8_zero();
}

internal String8
mdp_menu_binding(U32 item)
{
  return (item == MDP_MenuItem_Copy) ? str8_lit("Ctrl+C") : str8_zero();
}

internal MDP_MenuLayout
mdp_menu_layout(MDP_View *view, MDP_Geometry *g)
{
  MDP_MenuLayout l = {0};
  F32 u = g->ui;
  l.font_px = round_f32(14.f*g->scale);           // LabelSize::Default
  l.item_pad = round_f32(0.5f*u);
  F32 item_h = round_f32(1.625f*u);
  F32 pad = round_f32(0.25f*u);
  F32 border = 1.f*g->scale;
  F32 w = 10.f*u;
  for EachIndex(i, view->menu_item_count)
  {
    U32 item = view->menu_items[i];
    F32 iw = md_text_width(view->fonts.sans, l.font_px, mdp_menu_label(item));
    String8 binding = mdp_menu_binding(item);
    if(binding.size) { iw += 2*u + md_text_width(view->fonts.sans, l.font_px, binding); }
    w = Max(w, iw + 2*l.item_pad + 2*pad + 2*border);
  }
  F32 h = view->menu_item_count*item_h + 2*pad + 2*border;
  Rng2F32 bounds = r2f32p(g->client.x0, g->bar.y1 > 0 ? g->bar.y0 : g->client.y0, g->client.x1, g->client.y1);
  F32 x0 = view->menu_pos.x, y0 = view->menu_pos.y;
  if(x0 + w > bounds.x1) { x0 = Max(bounds.x0, x0 - w); }
  if(y0 + h > bounds.y1) { y0 = Max(bounds.y0, y0 - h); }
  l.box = r2f32p(round_f32(x0), round_f32(y0), round_f32(x0 + w), round_f32(y0 + h));
  for EachIndex(i, view->menu_item_count)
  {
    F32 iy = l.box.y0 + border + pad + i*item_h;
    l.items[i] = r2f32p(l.box.x0 + border + pad, iy, l.box.x1 - border - pad, iy + item_h);
  }
  return l;
}

internal void
mdp_menu_activate(MDP_View *view, U32 item)
{
  Temp scratch = scratch_begin(0, 0);
  U64 a = view->ix.selection_start, z = view->ix.selection_end;
  switch(item)
  {
    case MDP_MenuItem_Copy:           { mdp_view_copy(view, a, z, 0); }break;
    case MDP_MenuItem_CopyAsMarkdown: { mdp_view_copy(view, a, z, 1); }break;
    case MDP_MenuItem_CopyLink:       { wm_set_clipboard_text(str8(view->menu_link, view->menu_link_size)); }break;
  }
  view->menu_open = 0;
  scratch_end(scratch);
}

// The menu shows only when it has something to offer: a selection to copy
// or a link under the pointer.
internal void
mdp_menu_open(MDP_View *view, MDP_Geometry *g, Vec2F32 pos)
{
  MD_Element *el = 0;
  MD_Hit hit = mdp_view_hit_test(view, sub_2f32(pos, g->origin), &el);
  view->menu_item_count = 0;
  if(view->ix.selection_end > view->ix.selection_start)
  {
    view->menu_items[view->menu_item_count++] = MDP_MenuItem_Copy;
    view->menu_items[view->menu_item_count++] = MDP_MenuItem_CopyAsMarkdown;
  }
  String8 url = mdp_view_link_url(el, hit.link);
  if(url.size)
  {
    view->menu_link_size = Min(url.size, sizeof(view->menu_link));
    MemoryCopy(view->menu_link, url.str, view->menu_link_size);
    view->menu_items[view->menu_item_count++] = MDP_MenuItem_CopyLink;
  }
  view->menu_open = (view->menu_item_count > 0);
  view->menu_pos = pos;
  view->menu_selected = max_U64;
}

////////////////////////////////
//~ Events

internal B32
mdp_view_handle_event(MDP_View *view, WM_Window window, WM_Event *ev)
{
  MDP_Geometry g = mdp_geometry(view, window);
  F32 viewport_h = g.client.y1 - g.client.y0;
  B32 taken = 0;

  //- context menu
  if(view->menu_open)
  {
    MDP_MenuLayout ml = mdp_menu_layout(view, &g);
    if(ev->kind == WM_EventKind_Press)
    {
      if(ev->key == WM_Key_LeftMouseButton || ev->key == WM_Key_RightMouseButton)
      {
        if(contains_2f32(ml.box, ev->pos))
        {
          for EachIndex(i, view->menu_item_count)
          {
            if(contains_2f32(ml.items[i], ev->pos) && ev->key == WM_Key_LeftMouseButton) { mdp_menu_activate(view, view->menu_items[i]); }
          }
          return 1;
        }
        // a click elsewhere dismisses the menu and goes on to the preview
        view->menu_open = 0;
      }
      else if(ev->key == WM_Key_Esc) { view->menu_open = 0; return 1; }
      else if(ev->key == WM_Key_Down || ev->key == WM_Key_Up)
      {
        U64 n = view->menu_item_count;
        U64 cur = view->menu_selected;
        view->menu_selected = (cur >= n) ? (ev->key == WM_Key_Down ? 0 : n-1) : (ev->key == WM_Key_Down ? (cur+1) % n : (cur+n-1) % n);
        return 1;
      }
      else if(ev->key == WM_Key_Return && view->menu_selected < view->menu_item_count)
      {
        mdp_menu_activate(view, view->menu_items[view->menu_selected]);
        return 1;
      }
    }
    else if(ev->kind == WM_EventKind_Release && contains_2f32(ml.box, ev->pos)) { return 1; }
    else if(ev->kind == WM_EventKind_MouseMove) { view->menu_selected = max_U64; }
    else if(ev->kind == WM_EventKind_Scroll) { view->menu_open = 0; }
  }

  //- search bar
  if(ev->kind == WM_EventKind_Press)
  {
    WM_Modifiers m = ev->modifiers;
    if(m == WM_Modifier_Ctrl && ev->key == WM_Key_F) { mdp_search_open(view, &g); return 1; }
    if(view->search_open)
    {
      if(ev->key == WM_Key_F3 && (m & ~WM_Modifier_Shift) == 0) { mdp_search_step(view, !!(m & WM_Modifier_Shift)); return 1; }
      if(m == WM_Modifier_Alt && ev->key == WM_Key_C) { view->search_case = !view->search_case; mdp_search_changed(view); return 1; }
      if(m == WM_Modifier_Alt && ev->key == WM_Key_W) { view->search_word = !view->search_word; mdp_search_changed(view); return 1; }
      if(ev->key == WM_Key_Esc && (view->search_focused || m == 0)) { mdp_search_close(view); return 1; }
      if(view->search_focused)
      {
        if(ev->key == WM_Key_Return) { mdp_search_step(view, !!(m & WM_Modifier_Shift)); return 1; }
        if(ev->key == WM_Key_Backspace)
        {
          if(m & WM_Modifier_Ctrl) { view->search_query_size = 0; }
          else
          {
            // drop the last code point
            U64 n = view->search_query_size;
            for(;n > 0;) { n -= 1; if((view->search_query[n] & 0xC0) != 0x80) { break; } }
            view->search_query_size = n;
          }
          mdp_search_changed(view);
          return 1;
        }
        if(m == WM_Modifier_Ctrl && ev->key == WM_Key_V)
        {
          Temp scratch = scratch_begin(0, 0);
          mdp_search_insert(view, wm_get_clipboard_text(scratch.arena));
          scratch_end(scratch);
          return 1;
        }
      }
      if(ev->key == WM_Key_LeftMouseButton && contains_2f32(g.bar, ev->pos))
      {
        MDP_SearchBar l = mdp_search_bar_layout(&g);
        if(contains_2f32(l.toggle_case, ev->pos)) { view->search_case = !view->search_case; mdp_search_changed(view); }
        else if(contains_2f32(l.toggle_word, ev->pos)) { view->search_word = !view->search_word; mdp_search_changed(view); }
        else if(contains_2f32(l.prev, ev->pos)) { mdp_search_step(view, 1); }
        else if(contains_2f32(l.next, ev->pos)) { mdp_search_step(view, 0); }
        else if(contains_2f32(l.close, ev->pos)) { mdp_search_close(view); }
        else { view->search_focused = 1; }
        return 1;
      }
    }
  }
  if(ev->kind == WM_EventKind_Text && view->search_focused)
  {
    U32 cp = ev->character;
    if(cp >= 32 && cp != 127)
    {
      U8 buf[8];
      U64 size = utf8_encode(buf, cp);
      mdp_search_insert(view, str8(buf, size));
    }
    return 1;
  }
  if(ev->kind == WM_EventKind_Release && view->search_open && contains_2f32(g.bar, ev->pos) && !view->selecting && !view->thumb_dragging)
  {
    return 1;
  }

  switch(ev->kind)
  {
    default:{}break;

    case WM_EventKind_Scroll:
    {
      if(ev->modifiers & WM_Modifier_Ctrl) { break; }
      Vec2F32 p = sub_2f32(ev->pos, g.origin);
      MD_Element *el = 0;
      MD_Hit hit = mdp_view_hit_test(view, p, &el);
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
      else if(m == WM_Modifier_Alt && (ev->key == WM_Key_Down || ev->key == WM_Key_Up) && view->chunk_count)
      {
        // ScrollDownByItem / ScrollUpByItem: to the next / previous root
        // block, in the chunk at the top or the ones next to it
        F32 top = view->scroll_y - g.padding;
        B32 down = (ev->key == WM_Key_Down);
        U64 i = mdp_chunk_at_y(view, top);
        MDP_Chunk *target_chunk = 0;
        MD_Box *target = 0;
        for(U64 step = 0; step < 3 && target == 0 && i < view->chunk_count; step += 1)
        {
          MDP_Chunk *c = mdp_view_build_chunk_at(view, &g, i);
          top = view->scroll_y - g.padding;
          for(MD_Box *b = c->element->root->first; b != 0; b = b->next)
          {
            if(down && c->y + b->rect.y0 > top + 1) { target = b; break; }
            if(!down && c->y + b->rect.y0 < top - 1) { target = b; }
          }
          target_chunk = c;
          if(down) { i += 1; } else if(i == 0) { break; } else { i -= 1; }
        }
        if(target) { mdp_view_scroll_to_box(view, &g, target_chunk, target); }
        else if(!down) { view->scroll_y = 0; }
        taken = 1;
      }

      //- markdown::Copy / SelectAll
      else if(m == WM_Modifier_Ctrl && (ev->key == WM_Key_C || ev->key == WM_Key_Insert))
      {
        if(view->ix.selection_end > view->ix.selection_start)
        {
          mdp_view_copy(view, view->ix.selection_start, view->ix.selection_end, 0);
        }
        taken = 1;
      }
      else if(m == WM_Modifier_Ctrl && ev->key == WM_Key_A)
      {
        view->ix.selection_start = 0;
        view->ix.selection_end = max_U64;
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
      else if(ev->key == WM_Key_RightMouseButton && contains_2f32(g.client, ev->pos))
      {
        mdp_menu_open(view, &g, ev->pos);
        taken = 1;
      }
      else if(ev->key == WM_Key_LeftMouseButton)
      {
        Vec2F32 p = sub_2f32(ev->pos, g.origin);
        view->search_focused = 0;
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
        MD_Element *el = 0;
        MD_Hit hit = mdp_view_hit_test(view, p, &el);
        if(hit.checkbox)
        {
          mdp_view_toggle_task(view, hit.checkbox - 1);
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
        // on_source_click moves the editor cursor here, and the cursor's
        // root block gets the gutter marker
        if(hit.root_index) { view->ix.active_root_index = hit.root_index; }
        U64 now = now_time_us();
        B32 double_click = (now - view->last_click_us < 500000 && length_2f32(sub_2f32(ev->pos, view->last_click_pos)) < 4*g.scale);
        view->last_click_us = now;
        view->last_click_pos = ev->pos;
        view->press_on_link = (hit.link != 0);
        view->press_link = hit.link;
        view->press_element = el;
        view->press_pos = ev->pos;
        U64 offset = mdp_view_offset_from_point(view, p);
        if(double_click)
        {
          Rng1U64 word = mdp_view_word_at(view, offset);
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
          MD_Element *el = 0;
          MD_Hit hit = mdp_view_hit_test(view, p, &el);
          String8 url = mdp_view_link_url(el, hit.link);
          if(el == view->press_element && hit.link == view->press_link && url.size)
          {
            Temp scratch = scratch_begin(0, 0);
            mdp_view_open_link(view, &g, push_str8_copy(scratch.arena, url), !!(ev->modifiers & WM_Modifier_Alt));
            scratch_end(scratch);
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
        U64 head = mdp_view_offset_from_point(view, p);
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
  if(!view->scanner.done || mdp_search_counting(view)) { return 1; }
  if(view->pending_target != MDP_LinkTargetKind_None || view->pending_offset_valid) { return 1; }
  return 0;
}

internal F32
mdp_ease_in_out(F32 t)
{
  return (t < 0.5f) ? 2*t*t : 1 - (-2*t + 2)*(-2*t + 2)/2;
}

internal void
mdp_draw_text_centered_y(FNT_Tag font, F32 px, Rng2F32 r, F32 x, String8 string, Vec4F32 color)
{
  FNT_Metrics m = fnt_metrics_from_tag_size(font, md_pt_from_px(px));
  F32 baseline = round_f32((r.y0 + r.y1)/2 + (m.ascent - m.descent)/2);
  dr_text(font, md_pt_from_px(px), 0, 0, FNT_RasterFlag_Smooth, v2f32(round_f32(x), baseline), th_linear(color), string);
}

// IconButton: ghost background on hover, a selected style when toggled on
internal void
mdp_draw_icon_button(MDP_View *view, MDP_Geometry *g, Rng2F32 r, String8 label, FNT_Tag font, B32 on, Vec2F32 mouse, B32 underline)
{
  TH_Colors *colors = &view->theme.colors;
  B32 hovered = contains_2f32(r, mouse);
  if(on) { dr_rect(r, th_linear(th_over(colors->editor_background, colors->element_selection_background)), 0.25f*g->ui, 0, 0.5f); }
  else if(hovered) { dr_rect(r, th_linear(colors->ghost_element_hover), 0.25f*g->ui, 0, 0.5f); }
  F32 px = round_f32(14.f*g->scale);
  F32 w = md_text_width(font, px, label);
  F32 x = (r.x0 + r.x1)/2 - w/2;
  Vec4F32 color = on ? colors->text : colors->text_muted;
  mdp_draw_text_centered_y(font, px, r, x, label, color);
  if(underline)
  {
    F32 y = round_f32(r.y1 - 0.3f*g->ui);
    dr_rect(r2f32p(round_f32(x), y, round_f32(x + w), y + Max(1.f, round_f32(g->scale))), th_linear(color), 0, 0, 0);
  }
}

internal void
mdp_search_bar_draw(MDP_View *view, MDP_Geometry *g, Vec2F32 mouse)
{
  TH_Colors *colors = &view->theme.colors;
  MDP_SearchBar l = mdp_search_bar_layout(g);
  F32 u = g->ui;
  F32 border = 1.f*g->scale;
  dr_rect(g->bar, th_linear(colors->editor_background), 0, 0, 0);
  dr_rect(r2f32p(g->bar.x0, g->bar.y1 - border, g->bar.x1, g->bar.y1), th_linear(colors->border_variant), 0, 0, 0);

  //- query editor
  B32 no_match = (view->search_query_size > 0 && view->search_total == 0 && !mdp_search_counting(view));
  Vec4F32 frame = no_match ? colors->error : view->search_focused ? colors->border_focused : colors->border;
  dr_rect(l.input, th_linear(colors->editor_background), 0.25f*u, 0, 0.5f);
  dr_rect(l.input, th_linear(frame), 0.25f*u, border, 0.5f);
  {
    F32 px = round_f32(14.f*g->scale);
    Rng2F32 inner = r2f32p(l.input.x0 + 0.5f*u, l.input.y0, l.input.x1 - 0.5f*u, l.input.y1);
    String8 q = str8(view->search_query, view->search_query_size);
    FNT_Tag font = view->fonts.mono;
    F32 w = md_text_width(font, px, q);
    F32 x = inner.x0 - ClampBot(0, w + 2*g->scale - (inner.x1 - inner.x0));
    DR_ClipScope(inner)
    {
      if(q.size) { mdp_draw_text_centered_y(font, px, inner, x, q, colors->text); }
      else { mdp_draw_text_centered_y(view->fonts.sans, px, inner, inner.x0, str8_lit("Search\xe2\x80\xa6"), colors->text_placeholder); }
      if(view->search_focused)
      {
        F32 cx = round_f32(x + w);
        F32 ch = round_f32(px*1.2f);
        F32 cy = round_f32((inner.y0 + inner.y1)/2 - ch/2);
        dr_rect(r2f32p(cx, cy, cx + Max(1.f, round_f32(1.5f*g->scale)), cy + ch), th_linear(colors->text_accent), 0, 0, 0);
      }
    }
  }

  //- options, match count, navigation
  mdp_draw_icon_button(view, g, l.toggle_case, str8_lit("Aa"), view->fonts.sans, view->search_case, mouse, 0);
  mdp_draw_icon_button(view, g, l.toggle_word, str8_lit("ab"), view->fonts.sans, view->search_word, mouse, 1);
  if(view->search_query_size)
  {
    // while chunks are still to be counted: a "+" on the total, and "?" for
    // a place not known yet
    Temp scratch = scratch_begin(0, 0);
    U64 n = view->search_total;
    U64 a = mdp_search_active_ordinal(view);
    B32 counting = mdp_search_counting(view);
    String8 label = (view->search_has_active && a == 0) ? push_str8f(scratch.arena, "?/%I64u%s", n, counting ? "+" : "") :
      push_str8f(scratch.arena, "%I64u/%I64u%s", a, n, counting ? "+" : "");
    F32 px = round_f32(14.f*g->scale);
    F32 w = md_text_width(view->fonts.sans, px, label);
    mdp_draw_text_centered_y(view->fonts.sans, px, l.count, (l.count.x0 + l.count.x1)/2 - w/2, label, colors->text_muted);
    scratch_end(scratch);
  }
  mdp_draw_icon_button(view, g, l.prev, str8_lit("\xe2\x80\xb9"), view->fonts.sans, 0, mouse, 0);
  mdp_draw_icon_button(view, g, l.next, str8_lit("\xe2\x80\xba"), view->fonts.sans, 0, mouse, 0);
  mdp_draw_icon_button(view, g, l.close, str8_lit("\xc3\x97"), view->fonts.sans, 0, mouse, 0);
}

internal void
mdp_menu_draw(MDP_View *view, MDP_Geometry *g, Vec2F32 mouse)
{
  TH_Colors *colors = &view->theme.colors;
  MDP_MenuLayout l = mdp_menu_layout(view, g);
  F32 u = g->ui;
  F32 radius = 0.5f*u;                             // rounded_lg
  // elevation_3 shadow, surface, border
  dr_rect(shift_2f32(pad_2f32(l.box, 2.f*g->scale), v2f32(0, 2.f*g->scale)), v4f32(0, 0, 0, 0.18f), radius + 2, 0, 6.f*g->scale);
  dr_rect(l.box, th_linear(colors->elevated_surface_background), radius, 0, 0.5f);
  dr_rect(l.box, th_linear(colors->border_variant), radius, 1.f*g->scale, 0.5f);
  for EachIndex(i, view->menu_item_count)
  {
    Rng2F32 r = l.items[i];
    U32 item = view->menu_items[i];
    B32 hot = (view->menu_selected == i) || (view->menu_selected == max_U64 && contains_2f32(r, mouse));
    if(hot) { dr_rect(r, th_linear(th_over(colors->elevated_surface_background, colors->ghost_element_hover)), 0.25f*u, 0, 0.5f); }
    mdp_draw_text_centered_y(view->fonts.sans, l.font_px, r, r.x0 + l.item_pad, mdp_menu_label(item), colors->text);
    String8 binding = mdp_menu_binding(item);
    if(binding.size)
    {
      F32 w = md_text_width(view->fonts.sans, l.font_px, binding);
      mdp_draw_text_centered_y(view->fonts.sans, l.font_px, r, r.x1 - l.item_pad - w, binding, colors->text_muted);
    }
  }
}

internal void
mdp_view_draw(MDP_View *view, WM_Window window)
{
  Temp scratch = scratch_begin(0, 0);
  MDP_Geometry g = mdp_geometry(view, window);
  TH_Colors *colors = &view->theme.colors;
  Vec2F32 mouse = wm_mouse_from_window(window);
  B32 mouse_inside = contains_2f32(g.client, mouse);

  //- hover state
  view->ix.mouse = sub_2f32(mouse, g.origin);
  view->ix.mouse_inside = mouse_inside && !view->thumb_dragging && !(g.has_thumb && contains_2f32(g.track, mouse)) &&
    !(view->menu_open && contains_2f32(mdp_menu_layout(view, &g).box, mouse));
  MD_Hit hit = {0};
  MD_Element *hit_el = 0;
  if(view->ix.mouse_inside) { hit = mdp_view_hit_test(view, view->ix.mouse, &hit_el); }
  view->hovered_url = mdp_view_link_url(hit_el, hit.link);
  view->thumb_hovered = g.has_thumb && mouse_inside && contains_2f32(g.thumb, mouse);
  if(view->selecting) { wm_set_cursor(WM_Cursor_IBar); }
  else if(hit.link || hit.copy_button || hit.checkbox) { wm_set_cursor(WM_Cursor_HandPoint); }
  else if(hit.over_text) { wm_set_cursor(WM_Cursor_IBar); }
  else if(view->search_open && contains_2f32(mdp_search_bar_layout(&g).input, mouse)) { wm_set_cursor(WM_Cursor_IBar); }
  else { wm_set_cursor(WM_Cursor_Pointer); }

  //- background: .bg(editor_background)
  dr_rect(g.client, th_linear(colors->editor_background), 0, 0, 0);

  //- the chunks in view, each an element of its own; the selection space
  // is shared, the search matches are the chunk's own
  F32 view_top = view->scroll_y - g.padding;
  F32 view_bottom = view_top + (g.client.y1 - g.client.y0);
  B32 show_matches = mdp_search_active(view);
  for(U64 i = view->chunk_count ? mdp_chunk_at_y(view, view_top) : 0; i < view->chunk_count && view->chunks[i].y < view_bottom; i += 1)
  {
    MDP_Chunk *c = &view->chunks[i];
    if(c->element == 0) { continue; }
    MD_Interaction ix = view->ix;
    ix.mouse.y -= c->y;
    ix.search_matches = 0;
    ix.search_match_count = 0;
    ix.search_active = max_U64;
    if(show_matches)
    {
      ix.search_matches = mdp_search_chunk_matches(view, scratch.arena, i, &ix.search_match_count);
      for EachIndex(k, ix.search_match_count)
      {
        if(view->search_has_active && ix.search_matches[k].min == view->search_active_range.min) { ix.search_active = k; break; }
      }
    }
    md_element_paint(c->element, &view->style, &ix, v2f32(g.origin.x, g.origin.y + c->y), g.client);
  }

  //- the rest of the document, until the scan gets there
  if(!view->scanner.done && view->tail_y < view_bottom)
  {
    String8 label = str8_lit("Loading\xe2\x80\xa6");
    F32 px = round_f32(14.f*g.scale);
    F32 y = g.origin.y + view->tail_y + g.rem;
    mdp_draw_text_centered_y(view->fonts.sans, px, r2f32p(g.content_x, y, g.content_x + g.content_w, y + 2*px), g.content_x, label, colors->text_muted);
  }

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

  //- search bar
  if(view->search_open) { mdp_search_bar_draw(view, &g, mouse); }

  //- LinkPreview at the bottom left (UI rem, outside WithRemSize)
  if(view->hovered_url.size && !view->selecting && !view->menu_open)
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

  //- context menu, above everything
  if(view->menu_open) { mdp_menu_draw(view, &g, mouse); }
  scratch_end(scratch);
}
