////////////////////////////////
//~ Style

internal F32
md_rems(MD_Style *style, F32 rems)
{
  return rems*style->rem;
}

internal F32
md_px(MD_Style *style, F32 px)
{
  return px*style->scale;
}

// window.pixel_snap
internal F32
md_snap(F32 v)
{
  return round_f32(v);
}

internal MD_Style
md_style_preview(TH_Theme *theme, MD_Fonts *fonts, F32 rem, F32 scale)
{
  MD_Style s = {0};
  s.theme = theme;
  s.fonts = fonts;
  s.rem = rem;
  s.scale = scale;
  s.paragraph_spacing      = 16.f*scale;
  s.paragraph_line_height  = 1.5f;
  s.list_spacing           = 12.f*scale;
  s.table_cell_padding     = v2f32(10.f*scale, 4.f*scale);
  s.code_block_padding     = 12.f*scale;
  s.code_block_margin      = 16.f*scale;
  s.code_block_radius      = 6.f*scale;
  s.code_block_font_size   = rem;
  s.code_block_line_height = 1.618f;
  s.heading_margin_top     = 24.f*scale;
  s.heading_margin_bottom  = 12.f*scale;
  s.heading_line_height    = 1.25f;
  s.heading_sizes[1] = 1.75f;
  s.heading_sizes[2] = 1.4f;
  s.heading_sizes[3] = 1.2f;
  s.heading_sizes[4] = 1.0f;
  s.heading_sizes[5] = 0.875f;
  s.heading_sizes[6] = 0.85f;
  s.heading_weight_semibold = 1;
  return s;
}

////////////////////////////////
//~ Fonts

internal FNT_Tag
md_font_tag(MD_Fonts *fonts, MD_FontFamily family, B32 bold, B32 italic)
{
  if(family == MD_FontFamily_Symbol) { return fonts->symbols; }
  if(family == MD_FontFamily_Mono)
  {
    if(bold && italic) { return fonts->mono_bold_italic; }
    if(bold)           { return fonts->mono_bold; }
    if(italic)         { return fonts->mono_italic; }
    return fonts->mono;
  }
  if(bold && italic) { return fonts->sans_semibold_italic; }
  if(bold)           { return fonts->sans_semibold; }
  if(italic)         { return fonts->sans_italic; }
  return fonts->sans;
}

// gpui sizes are pixels; the RAD font layer takes points (em = size*96/72)
internal F32
md_pt_from_px(F32 px)
{
  return px*0.75f;
}

internal F32
md_text_width(FNT_Tag font, F32 px, String8 s)
{
  if(s.size == 0) { return 0; }
  return fnt_dim_from_tag_size_string(font, md_pt_from_px(px), 0, px*4.f, s).x;
}

internal FNT_Tag
md_run_font(MD_Style *style, MD_TextRun *run)
{
  return md_font_tag(style->fonts, run->family, !!(run->flags & MD_RunFlag_Bold), !!(run->flags & MD_RunFlag_Italic));
}

////////////////////////////////
//~ Images

global MD_Image *md_first_image = 0;
global Arena *md_image_arena = 0;

internal void
md_images_reset(void)
{
  for(MD_Image *img = md_first_image; img != 0; img = img->next)
  {
    if(!r_handle_match(img->texture, r_handle_zero())) { r_tex2d_release(img->texture); }
  }
  md_first_image = 0;
  if(md_image_arena) { arena_clear(md_image_arena); }
}

internal String8
md_base64_decode(Arena *arena, String8 s)
{
  U8 *out = push_array_no_zero(arena, U8, s.size*3/4 + 4);
  U64 n = 0;
  U32 acc = 0;
  U32 bits = 0;
  for EachIndex(i, s.size)
  {
    U8 c = s.str[i];
    U32 v = 0;
    if(c >= 'A' && c <= 'Z') { v = c - 'A'; }
    else if(c >= 'a' && c <= 'z') { v = c - 'a' + 26; }
    else if(c >= '0' && c <= '9') { v = c - '0' + 52; }
    else if(c == '+' || c == '-') { v = 62; }
    else if(c == '/' || c == '_') { v = 63; }
    else { continue; }
    acc = (acc << 6) | v;
    bits += 6;
    if(bits >= 8) { bits -= 8; out[n++] = (U8)(acc >> bits); }
  }
  return str8(out, n);
}

internal String8
md_percent_decode(Arena *arena, String8 s)
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

//- Remote images. gpui's image cache fetches URIs on a background executor;
// here each URL gets one download thread, and the bytes are kept for the
// session so reloading the file doesn't fetch them again. A finished download
// wakes the frame loop, and the view rebuilds its element.

typedef struct MD_Download MD_Download;
struct MD_Download
{
  MD_Download *next;
  String8 url;
  Arena *arena;
  String8 bytes;
  B32 done;
  Thread thread;
  B32 detached;
};

global Mutex md_download_mutex = {0};
global MD_Download *md_first_download = 0;
global Arena *md_download_arena = 0;
global U64 md_download_finish_count = 0;
global U64 md_download_seen_count = 0;

#if OS_WINDOWS
#include <winhttp.h>
#pragma comment(lib, "winhttp")

internal String8
md_http_get(Arena *arena, String8 url)
{
  String8 result = {0};
  Temp scratch = scratch_begin(&arena, 1);
  U64 hash = str8_find_needle(url, 0, str8_lit("#"), 0);
  String16 url16 = str16_from_8(scratch.arena, str8_prefix(url, hash));
  URL_COMPONENTS parts = {0};
  parts.dwStructSize = sizeof(parts);
  parts.dwHostNameLength = (DWORD)-1;
  parts.dwUrlPathLength = (DWORD)-1;
  parts.dwExtraInfoLength = (DWORD)-1;
  HINTERNET session = 0, connect = 0, request = 0;
  if(url16.size && WinHttpCrackUrl((WCHAR *)url16.str, (DWORD)url16.size, 0, &parts) && parts.lpszHostName && parts.dwHostNameLength)
  {
    WCHAR *host = push_array(scratch.arena, WCHAR, parts.dwHostNameLength + 1);
    MemoryCopy(host, parts.lpszHostName, parts.dwHostNameLength*sizeof(WCHAR));
    // the path runs on through the query to the end of the (fragment-less) URL
    WCHAR *path = (parts.lpszUrlPath && parts.dwUrlPathLength) ? parts.lpszUrlPath : L"/";
    session = WinHttpOpen(L"mdview/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if(session == 0) { session = WinHttpOpen(L"mdview/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0); }
    if(session)
    {
      WinHttpSetTimeouts(session, 10000, 10000, 15000, 30000);
      connect = WinHttpConnect(session, host, parts.nPort, 0);
    }
    if(connect)
    {
      DWORD flags = (parts.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
      request = WinHttpOpenRequest(connect, L"GET", path, 0, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    }
    if(request)
    {
      DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_ALL;
      WinHttpSetOption(request, WINHTTP_OPTION_DECOMPRESSION, &decompression, sizeof(decompression));
      DWORD status = 0;
      DWORD status_size = sizeof(status);
      if(WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
         WinHttpReceiveResponse(request, 0) &&
         WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX) &&
         status >= 200 && status < 300)
      {
        String8List pieces = {0};
        U64 total = 0;
        for(;total < MB(32);)
        {
          DWORD available = 0;
          if(!WinHttpQueryDataAvailable(request, &available) || available == 0) { break; }
          U8 *buffer = push_array_no_zero(scratch.arena, U8, available);
          DWORD read = 0;
          if(!WinHttpReadData(request, buffer, available, &read) || read == 0) { break; }
          str8_list_push(scratch.arena, &pieces, str8(buffer, read));
          total += read;
        }
        result = str8_list_join(arena, &pieces, 0);
      }
    }
  }
  if(request) { WinHttpCloseHandle(request); }
  if(connect) { WinHttpCloseHandle(connect); }
  if(session) { WinHttpCloseHandle(session); }
  scratch_end(scratch);
  return result;
}
#else
internal String8
md_http_get(Arena *arena, String8 url)
{
  return str8_zero();
}
#endif

internal void
md_download_thread(void *p)
{
  MD_Download *d = (MD_Download *)p;
  set_thread_name(str8_lit("[mdview] image download"));
  Arena *arena = arena_alloc();
  String8 bytes = md_http_get(arena, d->url);
  MutexScope(md_download_mutex)
  {
    d->arena = arena;
    d->bytes = bytes;
    d->done = 1;
    md_download_finish_count += 1;
  }
  wm_send_wakeup_event();
}

// Returns the download of a URL, starting it on first request.
internal MD_Download *
md_download_from_url(String8 url)
{
  if(md_download_arena == 0)
  {
    md_download_arena = arena_alloc();
    md_download_mutex = mutex_alloc();
  }
  MD_Download *d = 0;
  for(MD_Download *n = md_first_download; n != 0; n = n->next)
  {
    if(str8_match(n->url, url, 0)) { d = n; break; }
  }
  if(d == 0)
  {
    d = push_array(md_download_arena, MD_Download, 1);
    d->url = push_str8_copy(md_download_arena, url);
    MutexScope(md_download_mutex) { SLLStackPush(md_first_download, d); }
    // detached only once done: the thread entity carries `d` to the new
    // thread, and a detached entity can be reused before the thread reads it
    d->thread = thread_launch(md_download_thread, d);
  }
  return d;
}

// True once per batch of finished downloads: the element must be rebuilt.
internal B32
md_images_poll(void)
{
  if(md_download_arena == 0) { return 0; }
  B32 changed = 0;
  MutexScope(md_download_mutex)
  {
    changed = (md_download_finish_count != md_download_seen_count);
    md_download_seen_count = md_download_finish_count;
    for(MD_Download *d = md_first_download; d != 0; d = d->next)
    {
      if(d->done && !d->detached) { thread_detach(d->thread); d->detached = 1; }
    }
  }
  return changed;
}

//- SVG. Zed rasterizes SVGs with resvg; nanosvg covers the shapes, and since
// it draws no text, <text> elements are collected here and drawn over the
// image with the UI font (that is what badges are made of).

internal String8 md_html_attr(String8 tag, String8 name);

internal B32
md_bytes_are_svg(String8 bytes)
{
  String8 head = str8_prefix(bytes, 4096);
  return (str8_find_needle(head, 0, str8_lit("<svg"), StringMatchFlag_CaseInsensitive) < head.size);
}

// An attribute, or the same property in the style attribute.
internal String8
md_svg_attr(String8 tag, String8 name)
{
  String8 style = md_html_attr(tag, str8_lit("style"));
  for(U64 i = 0; i < style.size;)
  {
    U64 semi = str8_find_needle(style, i, str8_lit(";"), 0);
    String8 decl = str8_substr(style, r1u64(i, semi));
    i = semi + 1;
    U64 colon = str8_find_needle(decl, 0, str8_lit(":"), 0);
    if(colon >= decl.size) { continue; }
    if(str8_match(str8_skip_chop_whitespace(str8_prefix(decl, colon)), name, StringMatchFlag_CaseInsensitive))
    {
      return str8_skip_chop_whitespace(str8_skip(decl, colon+1));
    }
  }
  return md_html_attr(tag, name);
}

// Reads up to `max` numbers separated by spaces or commas.
internal U64
md_svg_numbers(String8 s, F32 *out, U64 max)
{
  U64 count = 0;
  for(U64 i = 0; i < s.size && count < max;)
  {
    U8 c = s.str[i];
    if(char_is_space(c) || c == ',') { i += 1; continue; }
    U64 e = i;
    if(e < s.size && (s.str[e] == '-' || s.str[e] == '+')) { e += 1; }
    for(;e < s.size && (char_is_digit(s.str[e], 10) || s.str[e] == '.'); e += 1);
    if(e < s.size && (s.str[e] == 'e' || s.str[e] == 'E'))
    {
      e += 1;
      if(e < s.size && (s.str[e] == '-' || s.str[e] == '+')) { e += 1; }
      for(;e < s.size && char_is_digit(s.str[e], 10); e += 1);
    }
    if(e == i) { break; }
    out[count++] = (F32)f64_from_str8(str8_substr(s, r1u64(i, e)));
    i = e;
  }
  return count;
}

internal F32
md_svg_number(String8 s, F32 fallback)
{
  F32 v = 0;
  return md_svg_numbers(s, &v, 1) ? v : fallback;
}

// #rgb, #rrggbb, rgb(..), a few names; 0 when the value is not a color.
internal B32
md_svg_color(String8 s, Vec4F32 *out)
{
  s = str8_skip_chop_whitespace(s);
  if(s.size == 0) { return 0; }
  if(s.str[0] == '#')
  {
    String8 hex = str8_skip(s, 1);
    if(hex.size == 3)
    {
      U8 buf[6] = {hex.str[0], hex.str[0], hex.str[1], hex.str[1], hex.str[2], hex.str[2]};
      hex = str8(buf, 6);
      U32 v = (U32)u64_from_str8(hex, 16);
      *out = v4f32(((v >> 16) & 0xff)/255.f, ((v >> 8) & 0xff)/255.f, (v & 0xff)/255.f, 1);
      return 1;
    }
    if(hex.size >= 6)
    {
      U32 v = (U32)u64_from_str8(str8_prefix(hex, 6), 16);
      *out = v4f32(((v >> 16) & 0xff)/255.f, ((v >> 8) & 0xff)/255.f, (v & 0xff)/255.f, 1);
      return 1;
    }
    return 0;
  }
  if(str8_match(str8_prefix(s, 4), str8_lit("rgb("), StringMatchFlag_CaseInsensitive))
  {
    F32 v[3] = {0};
    md_svg_numbers(str8_skip(s, 4), v, 3);
    *out = v4f32(v[0]/255.f, v[1]/255.f, v[2]/255.f, 1);
    return 1;
  }
  local_persist struct { char *name; U32 rgb; } names[] =
  {
    {"black", 0x000000}, {"white", 0xffffff}, {"red", 0xff0000}, {"green", 0x008000},
    {"blue", 0x0000ff}, {"gray", 0x808080}, {"grey", 0x808080}, {"silver", 0xc0c0c0},
    {"yellow", 0xffff00}, {"orange", 0xffa500}, {"purple", 0x800080}, {"navy", 0x000080},
  };
  for EachElement(i, names)
  {
    if(str8_match(s, str8_cstring(names[i].name), StringMatchFlag_CaseInsensitive))
    {
      U32 v = names[i].rgb;
      *out = v4f32(((v >> 16) & 0xff)/255.f, ((v >> 8) & 0xff)/255.f, (v & 0xff)/255.f, 1);
      return 1;
    }
  }
  return 0;
}

typedef struct MD_SvgState MD_SvgState;
struct MD_SvgState
{
  F32 m[6];                      // affine: x' = a*x + c*y + e, y' = b*x + d*y + f
  Vec4F32 fill;
  B32 no_fill;
  F32 opacity;
  F32 font_size;
  MD_Align anchor;
  B32 bold;
};

internal void
md_svg_mul(F32 *m, F32 *t)
{
  F32 r[6];
  r[0] = m[0]*t[0] + m[2]*t[1];
  r[1] = m[1]*t[0] + m[3]*t[1];
  r[2] = m[0]*t[2] + m[2]*t[3];
  r[3] = m[1]*t[2] + m[3]*t[3];
  r[4] = m[0]*t[4] + m[2]*t[5] + m[4];
  r[5] = m[1]*t[4] + m[3]*t[5] + m[5];
  MemoryCopy(m, r, sizeof(r));
}

internal void
md_svg_apply_transform(F32 *m, String8 s)
{
  for(U64 i = 0; i < s.size;)
  {
    U64 open = str8_find_needle(s, i, str8_lit("("), 0);
    U64 close = str8_find_needle(s, open, str8_lit(")"), 0);
    if(open >= s.size || close >= s.size) { break; }
    String8 name = str8_skip_chop_whitespace(str8_substr(s, r1u64(i, open)));
    for(;name.size && (name.str[0] == ',' || char_is_space(name.str[0]));) { name = str8_skip(name, 1); }
    F32 v[6] = {0};
    U64 n = md_svg_numbers(str8_substr(s, r1u64(open+1, close)), v, 6);
    F32 t[6] = {1, 0, 0, 1, 0, 0};
    if(str8_match(name, str8_lit("translate"), 0)) { t[4] = v[0]; t[5] = (n > 1) ? v[1] : 0; }
    else if(str8_match(name, str8_lit("scale"), 0)) { t[0] = v[0]; t[3] = (n > 1) ? v[1] : v[0]; }
    else if(str8_match(name, str8_lit("matrix"), 0) && n == 6) { MemoryCopy(t, v, sizeof(t)); }
    md_svg_mul(m, t);
    i = close + 1;
  }
}

internal void
md_svg_state_apply(MD_SvgState *s, String8 tag)
{
  String8 v;
  v = md_svg_attr(tag, str8_lit("transform"));
  if(v.size) { md_svg_apply_transform(s->m, v); }
  v = md_svg_attr(tag, str8_lit("fill"));
  if(v.size)
  {
    if(str8_match(v, str8_lit("none"), StringMatchFlag_CaseInsensitive)) { s->no_fill = 1; }
    else if(md_svg_color(v, &s->fill)) { s->no_fill = 0; }
  }
  v = md_svg_attr(tag, str8_lit("fill-opacity"));
  if(v.size) { s->fill.w = Clamp(0, md_svg_number(v, 1), 1); }
  v = md_svg_attr(tag, str8_lit("opacity"));
  if(v.size) { s->opacity *= Clamp(0, md_svg_number(v, 1), 1); }
  v = md_svg_attr(tag, str8_lit("font-size"));
  if(v.size) { s->font_size = md_svg_number(v, s->font_size); }
  v = md_svg_attr(tag, str8_lit("text-anchor"));
  if(str8_match(v, str8_lit("middle"), 0)) { s->anchor = MD_Align_Center; }
  else if(str8_match(v, str8_lit("end"), 0)) { s->anchor = MD_Align_Right; }
  else if(str8_match(v, str8_lit("start"), 0)) { s->anchor = MD_Align_Left; }
  v = md_svg_attr(tag, str8_lit("font-weight"));
  if(v.size) { s->bold = str8_match(v, str8_lit("bold"), 0) || str8_match(v, str8_lit("bolder"), 0) || md_svg_number(v, 400) >= 600; }
}

internal void
md_svg_collect_texts(Arena *arena, MD_Image *img, String8 src, F32 width, F32 height)
{
  Temp scratch = scratch_begin(&arena, 1);
  MD_SvgState stack[64];
  U64 depth = 0;
  MD_SvgState base = {{1, 0, 0, 1, 0, 0}};
  base.fill = v4f32(0, 0, 0, 1);
  base.opacity = 1;
  base.font_size = 16;
  base.anchor = MD_Align_Left;
  stack[depth++] = base;
  U64 skip_depth = 0;            // inside <title>, <defs>, ...: no text is drawn
  MD_SvgText *last = 0;
  for(U64 i = 0; i < src.size;)
  {
    U64 lt = str8_find_needle(src, i, str8_lit("<"), 0);
    if(lt >= src.size) { break; }
    if(str8_match(str8_prefix(str8_skip(src, lt), 4), str8_lit("<!--"), 0))
    {
      U64 e = str8_find_needle(src, lt+4, str8_lit("-->"), 0);
      i = e + 3;
      continue;
    }
    U64 gt = str8_find_needle(src, lt, str8_lit(">"), 0);
    if(gt >= src.size) { break; }
    String8 tag = str8_substr(src, r1u64(lt, gt+1));
    i = gt + 1;
    if(tag.size < 3 || tag.str[1] == '!' || tag.str[1] == '?') { continue; }
    B32 closing = (tag.str[1] == '/');
    B32 self_closing = (tag.str[tag.size-2] == '/');
    U64 ns = closing ? 2 : 1, ne = ns;
    for(;ne < tag.size && !char_is_space(tag.str[ne]) && tag.str[ne] != '>' && tag.str[ne] != '/'; ne += 1);
    String8 name = str8_substr(tag, r1u64(ns, ne));
    B32 hidden = (str8_match(name, str8_lit("title"), 0) || str8_match(name, str8_lit("desc"), 0) ||
                  str8_match(name, str8_lit("defs"), 0) || str8_match(name, str8_lit("style"), 0) ||
                  str8_match(name, str8_lit("script"), 0) || str8_match(name, str8_lit("clipPath"), 0) ||
                  str8_match(name, str8_lit("mask"), 0) || str8_match(name, str8_lit("symbol"), 0) ||
                  str8_match(name, str8_lit("metadata"), 0));
    if(closing)
    {
      if(depth > 1) { depth -= 1; }
      if(skip_depth && depth < skip_depth) { skip_depth = 0; }
      continue;
    }
    if(self_closing) { continue; }
    MD_SvgState s = stack[Min(depth, ArrayCount(stack))-1];
    md_svg_state_apply(&s, tag);
    if(depth < ArrayCount(stack)) { stack[depth] = s; }
    depth += 1;
    if(hidden && skip_depth == 0) { skip_depth = depth; }
    if(!str8_match(name, str8_lit("text"), 0) || skip_depth) { continue; }

    //- <text>: its content, with <tspan>s flattened; the first tspan's x/y
    // stand in when the text has none
    U64 end = str8_find_needle(src, i, str8_lit("</text"), 0);
    String8 body = str8_substr(src, r1u64(i, end));
    String8 x_attr = md_html_attr(tag, str8_lit("x"));
    String8 y_attr = md_html_attr(tag, str8_lit("y"));
    String8List parts = {0};
    for(U64 k = 0; k < body.size;)
    {
      if(body.str[k] == '<')
      {
        U64 e = str8_find_needle(body, k, str8_lit(">"), 0);
        String8 inner = str8_substr(body, r1u64(k, e+1));
        if(!x_attr.size) { x_attr = md_html_attr(inner, str8_lit("x")); }
        if(!y_attr.size) { y_attr = md_html_attr(inner, str8_lit("y")); }
        if(inner.size > 1 && inner.str[1] != '/') { md_svg_state_apply(&s, inner); }
        k = e + 1;
        continue;
      }
      U64 e = str8_find_needle(body, k, str8_lit("<"), 0);
      String8 raw = str8_substr(body, r1u64(k, e));
      k = e;
      U8 *out = push_array_no_zero(scratch.arena, U8, raw.size*4 + 1);
      U64 n = 0;
      for(U64 j = 0; j < raw.size; j += 1)
      {
        U8 c = raw.str[j];
        if(char_is_space(c)) { if(n == 0 || out[n-1] != ' ') { out[n++] = ' '; } continue; }
        if(c == '&')
        {
          U8 buf[8]; U64 sz = 0;
          U64 used = md_parse_entity(raw, j, buf, &sz);
          if(used) { MemoryCopy(out+n, buf, sz); n += sz; j += used-1; continue; }
        }
        out[n++] = c;
      }
      str8_list_push(scratch.arena, &parts, str8(out, n));
    }
    i = (end < src.size) ? end : src.size; // its </text> pops the state
    String8 string = str8_skip_chop_whitespace(str8_list_join(scratch.arena, &parts, 0));
    if(string.size == 0 || s.no_fill) { continue; }
    F32 x = md_svg_number(x_attr, 0);
    F32 y = md_svg_number(y_attr, 0);
    MD_SvgText *t = push_array(arena, MD_SvgText, 1);
    t->string = push_str8_copy(arena, string);
    t->pos = v2f32(s.m[0]*x + s.m[2]*y + s.m[4], s.m[1]*x + s.m[3]*y + s.m[5]);
    t->font_size = s.font_size*sqrt_f32(abs_f32(s.m[0]*s.m[3] - s.m[1]*s.m[2]));
    t->color = s.fill;
    t->color.w *= s.opacity;
    t->anchor = s.anchor;
    t->bold = s.bold;
    if(last) { last->next = t; } else { img->first_text = t; }
    last = t;
  }

  //- the root viewBox maps user units onto the image (xMidYMid meet)
  {
    U64 svg_at = str8_find_needle(src, 0, str8_lit("<svg"), StringMatchFlag_CaseInsensitive);
    U64 svg_end = str8_find_needle(src, svg_at, str8_lit(">"), 0);
    String8 svg_tag = str8_substr(src, r1u64(svg_at, svg_end+1));
    F32 vb[4] = {0};
    if(md_svg_numbers(md_html_attr(svg_tag, str8_lit("viewBox")), vb, 4) == 4 && vb[2] > 0 && vb[3] > 0)
    {
      F32 k = Min(width/vb[2], height/vb[3]);
      F32 tx = (width - vb[2]*k)/2 - vb[0]*k;
      F32 ty = (height - vb[3]*k)/2 - vb[1]*k;
      for(MD_SvgText *t = img->first_text; t != 0; t = t->next)
      {
        t->pos = v2f32(t->pos.x*k + tx, t->pos.y*k + ty);
        t->font_size *= k;
      }
    }
  }
  scratch_end(scratch);
}

internal void
md_image_decode(MD_Image *img, String8 bytes, F32 scale)
{
  Temp scratch = scratch_begin(0, 0);
  img->failed = 1;
  img->pending = 0;
  if(bytes.size && md_bytes_are_svg(bytes))
  {
    // nanosvg parses in place, so it gets a terminated copy
    char *src = (char *)push_array_no_zero(scratch.arena, U8, bytes.size + 1);
    MemoryCopy(src, bytes.str, bytes.size);
    src[bytes.size] = 0;
    NSVGimage *svg = nsvgParse(src, "px", 96.f);
    if(svg && svg->width >= 1 && svg->height >= 1)
    {
      // rasterize at the display scale (2x at least) so it stays crisp
      F32 k = Max(2.f, ceil_f32(scale));
      F32 big = Max(svg->width, svg->height)*k;
      if(big > 4096.f) { k *= 4096.f/big; }
      int w = (int)ceil_f32(svg->width*k);
      int h = (int)ceil_f32(svg->height*k);
      U8 *pixels = push_array(scratch.arena, U8, (U64)w*h*4);
      NSVGrasterizer *rast = nsvgCreateRasterizer();
      if(rast)
      {
        nsvgRasterize(rast, svg, 0, 0, k, pixels, w, h, w*4);
        nsvgDeleteRasterizer(rast);
        img->texture = r_tex2d_alloc(R_ResourceKind_Static, v2s32(w, h), R_Tex2DFormat_RGBA8, pixels);
        img->size = v2s32((S32)ceil_f32(svg->width), (S32)ceil_f32(svg->height));
        img->tex_size = v2s32(w, h);
        img->failed = 0;
        md_svg_collect_texts(md_image_arena, img, bytes, svg->width, svg->height);
      }
    }
    if(svg) { nsvgDelete(svg); }
  }
  else if(bytes.size)
  {
    int w = 0, h = 0, comp = 0;
    stbi_uc *pixels = stbi_load_from_memory(bytes.str, (int)bytes.size, &w, &h, &comp, 4);
    if(pixels && w > 0 && h > 0)
    {
      img->texture = r_tex2d_alloc(R_ResourceKind_Static, v2s32(w, h), R_Tex2DFormat_RGBA8, pixels);
      img->size = v2s32(w, h);
      img->tex_size = img->size;
      img->failed = 0;
    }
    if(pixels) { stbi_image_free(pixels); }
  }
  scratch_end(scratch);
}

// Zed's `resolve_preview_image`: http(s) URLs load as URIs, data: URLs are
// decoded, `/`-rooted paths resolve against the workspace root (here the
// file's folder) and other paths against the file's folder; a missing file
// yields no image, so the alt text renders as text. A remote image that is
// still downloading renders as nothing, as gpui's `img` does while loading.
typedef enum MD_ImageResolve
{
  MD_ImageResolve_None,
  MD_ImageResolve_Loaded,
  MD_ImageResolve_Failed,
  MD_ImageResolve_Pending,
}
MD_ImageResolve;

internal MD_ImageResolve
md_image_result(MD_Image *img)
{
  return img->pending ? MD_ImageResolve_Pending : img->failed ? MD_ImageResolve_Failed : MD_ImageResolve_Loaded;
}

internal MD_ImageResolve
md_image_resolve(String8 url, String8 base_dir, F32 scale, MD_Image **out)
{
  Temp scratch = scratch_begin(0, 0);
  if(md_image_arena == 0) { md_image_arena = arena_alloc(); }
  String8 key = url;
  B32 remote = (str8_match(str8_prefix(url, 7), str8_lit("http://"), StringMatchFlag_CaseInsensitive) ||
                str8_match(str8_prefix(url, 8), str8_lit("https://"), StringMatchFlag_CaseInsensitive));
  B32 data_url = str8_match(str8_prefix(url, 5), str8_lit("data:"), StringMatchFlag_CaseInsensitive);
  String8 path = {0};
  if(!remote && !data_url)
  {
    String8 rel = url;
    U64 q = str8_find_needle(rel, 0, str8_lit("?"), 0);
    rel = str8_prefix(rel, q);
    U64 h = str8_find_needle(rel, 0, str8_lit("#"), 0);
    rel = str8_prefix(rel, h);
    rel = md_percent_decode(scratch.arena, rel);
    for(;rel.size && (rel.str[0] == '/' || rel.str[0] == '\\');) { rel = str8_skip(rel, 1); }
    B32 absolute = (rel.size > 2 && rel.str[1] == ':');
    path = absolute ? rel : push_str8f(scratch.arena, "%S/%S", base_dir, rel);
    if(base_dir.size == 0 && !absolute) { path = rel; }
    if(!file_path_exists(path)) { scratch_end(scratch); return MD_ImageResolve_None; }
    key = path;
  }

  //- cached
  MD_Image *img = 0;
  for(MD_Image *n = md_first_image; n != 0; n = n->next)
  {
    if(str8_match(n->key, key, 0)) { img = n; break; }
  }
  if(img == 0)
  {
    img = push_array(md_image_arena, MD_Image, 1);
    img->key = push_str8_copy(md_image_arena, key);
    SLLStackPush(md_first_image, img);
    if(remote)
    {
      img->pending = 1;
    }
    else
    {
      String8 bytes = {0};
      if(data_url)
      {
        U64 comma = str8_find_needle(url, 0, str8_lit(","), 0);
        String8 header = str8_prefix(url, comma);
        String8 payload = str8_skip(url, comma+1);
        if(comma < url.size && str8_find_needle(header, 0, str8_lit(";base64"), StringMatchFlag_CaseInsensitive) < header.size)
        {
          bytes = md_base64_decode(scratch.arena, payload);
        }
        else if(comma < url.size)
        {
          bytes = md_percent_decode(scratch.arena, payload);
        }
      }
      else
      {
        bytes = data_from_file_path(scratch.arena, path);
      }
      md_image_decode(img, bytes, scale);
    }
  }

  //- a pending download finished
  if(img->pending)
  {
    MD_Download *d = md_download_from_url(url);
    B32 done = 0;
    String8 bytes = {0};
    MutexScope(md_download_mutex) { done = d->done; bytes = d->bytes; }
    if(done) { md_image_decode(img, bytes, scale); }
  }
  *out = img;
  scratch_end(scratch);
  return md_image_result(img);
}

////////////////////////////////
//~ Builder (MarkdownElementBuilder)

typedef struct MD_RunStyle MD_RunStyle;
struct MD_RunStyle
{
  MD_FontFamily family;
  B32 bold;
  B32 italic;
  Vec4F32 color;
  B32 underline;
  Vec4F32 underline_color;
  B32 strike;
  U64 link;
  MD_Align align;
};

typedef struct MD_PendingRun MD_PendingRun;
struct MD_PendingRun
{
  MD_PendingRun *next;
  MD_TextRun v;
};

typedef struct MD_HeadingNode MD_HeadingNode;
struct MD_HeadingNode
{
  MD_HeadingNode *next;
  MD_Heading v;
};

typedef struct MD_Builder MD_Builder;
struct MD_Builder
{
  Arena *arena;
  MD_Style *style;
  TH_Colors *colors;
  MD_Document *doc;
  MD_Element *el;
  String8 base_dir;
  MD_Box *current;

  // text_style_stack (applied over the base text style)
  MD_RunStyle run_stack[128];
  U64 run_depth;
  U64 link_depth;
  B32 in_code_block;
  B32 in_table;

  // pending_line
  String8List pending_parts;
  U64 pending_size;
  MD_PendingRun *first_run;
  MD_PendingRun *last_run;
  U64 run_count;
  Vec4F32 pending_chip_color;

  // links / texts
  String8List link_urls;
  MD_Text **texts;
  U64 text_count;
  U64 text_cap;

  // lists
  U64 list_depth;

  // tables
  B32 table_in_head;
  U64 table_row;
  U64 table_col;
  MD_Align *table_aligns;

  B32 rendered_footnote_separator;
  U64 root_count;
  
  // link targets: heading slugs and footnote definitions
  String8List anchor_names;
  MD_Box *anchor_boxes[4096];
  U64 anchor_count;

  // headings, for the outline
  MD_HeadingNode *first_heading;
  MD_HeadingNode *last_heading;
  U64 heading_count;
};

internal void
md_push_anchor(MD_Builder *b, String8 name, MD_Box *box)
{
  if(b->anchor_count >= ArrayCount(b->anchor_boxes)) { return; }
  str8_list_push(b->arena, &b->anchor_names, name);
  b->anchor_boxes[b->anchor_count++] = box;
}

// util::markdown::generate_heading_slug: lowercase, spaces to '-', drop punctuation
internal String8
md_heading_slug(Arena *arena, String8 text)
{
  U8 *out = push_array_no_zero(arena, U8, text.size + 1);
  U64 n = 0;
  for EachIndex(i, text.size)
  {
    U8 c = text.str[i];
    if(c >= 0x80 || char_is_alpha(c) || char_is_digit(c, 10) || c == '_' || c == '-') { out[n++] = lower_from_char(c); }
    else if(c == ' ') { out[n++] = '-'; }
  }
  return str8(out, n);
}

internal MD_RunStyle *
md_run_style(MD_Builder *b)
{
  return &b->run_stack[b->run_depth-1];
}

internal void
md_push_run_style(MD_Builder *b, MD_RunStyle s)
{
  if(b->run_depth < ArrayCount(b->run_stack)) { b->run_stack[b->run_depth++] = s; }
  else { b->run_depth += 1; }
}

internal void
md_pop_run_style(MD_Builder *b)
{
  if(b->run_depth > 1) { b->run_depth -= 1; }
}

internal F32
md_box_font_size(MD_Builder *b, MD_Box *box)
{
  for(MD_Box *x = box; x != 0; x = x->parent)
  {
    if(x->text_font_size > 0) { return x->text_font_size; }
  }
  return b->style->rem;
}

internal F32
md_box_line_height(MD_Builder *b, MD_Box *box, F32 font_size)
{
  for(MD_Box *x = box; x != 0; x = x->parent)
  {
    if(x->text_line_height_abs > 0) { return md_snap(x->text_line_height_abs); }
    if(x->text_line_height_rel > 0) { return md_snap(x->text_line_height_rel*font_size); }
  }
  return md_snap(1.618034f*font_size); // TextStyle::default line height (phi)
}

internal MD_Box *
md_box_alloc(MD_Builder *b, MD_BoxKind kind)
{
  MD_Box *box = push_array(b->arena, MD_Box, 1);
  box->kind = kind;
  return box;
}

internal void
md_box_append(MD_Box *parent, MD_Box *child)
{
  child->parent = parent;
  SLLQueuePush(parent->first, parent->last, child);
}

internal void md_flush_text(MD_Builder *b);

// push_div: flushes text, applies the first/last block margin rule
internal MD_Box *
md_push_div(MD_Builder *b, MD_Box *box, Rng1U64 range)
{
  md_flush_text(b);
  if(range.min == 0) { box->margin[MD_Side_Top] = 0; }
  if(range.max == b->doc->end) { box->margin[MD_Side_Bottom] = 0; }
  box->src = range;
  md_box_append(b->current, box);
  b->current = box;
  return box;
}

internal void
md_pop_div(MD_Builder *b)
{
  md_flush_text(b);
  if(b->current->parent) { b->current = b->current->parent; }
}

internal void
md_push_text(MD_Builder *b, String8 text)
{
  if(text.size == 0) { return; }
  MD_RunStyle *rs = md_run_style(b);
  U64 start = b->pending_size;
  str8_list_push(b->arena, &b->pending_parts, text);
  b->pending_size += text.size;
  MD_TextRun run = {0};
  run.start = start;
  run.end = b->pending_size;
  run.family = rs->family;
  run.flags = (rs->bold ? MD_RunFlag_Bold : 0) | (rs->italic ? MD_RunFlag_Italic : 0) |
    (rs->underline ? MD_RunFlag_Underline : 0) | (rs->strike ? MD_RunFlag_Strike : 0);
  run.color = rs->color;
  run.underline_color = rs->underline_color;
  run.link = rs->link;
  MD_PendingRun *last = b->last_run;
  if(last && last->v.end == start && last->v.family == run.family && last->v.flags == run.flags &&
     MemoryMatchStruct(&last->v.color, &run.color) && last->v.link == run.link &&
     MemoryMatchStruct(&last->v.underline_color, &run.underline_color))
  {
    last->v.end = run.end;
    return;
  }
  MD_PendingRun *n = push_array(b->arena, MD_PendingRun, 1);
  n->v = run;
  SLLQueuePush(b->first_run, b->last_run, n);
  b->run_count += 1;
}

internal void
md_push_code_chip_text(MD_Builder *b, String8 text, Vec4F32 chip)
{
  U64 before = b->run_count;
  md_push_text(b, text);
  if(b->last_run && b->run_count > before)
  {
    b->last_run->v.flags |= MD_RunFlag_Chip;
    b->last_run->v.chip_color = chip;
  }
  else if(b->last_run)
  {
    // merged into an identical code run: keep it a chip
    b->last_run->v.flags |= MD_RunFlag_Chip;
    b->last_run->v.chip_color = chip;
  }
}

internal void
md_push_highlighted_text(MD_Builder *b, String8 text, MD_HighlightArray *hl)
{
  if(hl == 0 || hl->count == 0) { md_push_text(b, text); return; }
  MD_RunStyle base = *md_run_style(b);
  U64 offset = 0;
  for EachIndex(i, hl->count)
  {
    MD_HighlightRun *h = &hl->v[i];
    if(h->start > offset) { md_push_text(b, str8_substr(text, r1u64(offset, h->start))); }
    MD_RunStyle s = base;
    s.color = b->colors->syntax[h->syntax];
    md_push_run_style(b, s);
    md_push_text(b, str8_substr(text, r1u64(h->start, h->end)));
    md_pop_run_style(b);
    offset = h->end;
  }
  if(offset < text.size) { md_push_text(b, str8_skip(text, offset)); }
}

internal B32
md_uses_flex_line_breaks(MD_Builder *b)
{
  return b->current->kind == MD_BoxKind_Wrap;
}

internal void
md_push_soft_break(MD_Builder *b)
{
  if(md_uses_flex_line_breaks(b) && b->pending_size == 0) { return; }
  md_push_text(b, str8_lit(" "));
}

internal void
md_push_line_break(MD_Builder *b)
{
  if(md_uses_flex_line_breaks(b))
  {
    md_flush_text(b);
    MD_Box *br = md_box_alloc(b, MD_BoxKind_Block);
    br->line_break = 1;
    md_box_append(b->current, br);
  }
  else
  {
    md_push_text(b, str8_lit("\n"));
  }
}

internal MD_Text *
md_text_from_pending(MD_Builder *b, MD_Box *parent)
{
  MD_Text *t = push_array(b->arena, MD_Text, 1);
  t->string = str8_list_join(b->arena, &b->pending_parts, 0);
  t->runs = push_array(b->arena, MD_TextRun, b->run_count);
  t->run_count = b->run_count;
  U64 i = 0;
  for(MD_PendingRun *r = b->first_run; r != 0; r = r->next, i += 1) { t->runs[i] = r->v; }
  t->font_size = md_box_font_size(b, parent);
  t->line_height = md_box_line_height(b, parent, t->font_size);
  t->align = md_run_style(b)->align;
  t->wrap = !b->in_code_block && !b->in_table;
  MemoryZeroStruct(&b->pending_parts);
  b->pending_size = 0;
  b->first_run = b->last_run = 0;
  b->run_count = 0;
  return t;
}

internal void
md_register_text(MD_Builder *b, MD_Text *t)
{
  if(b->text_count == b->text_cap)
  {
    U64 new_cap = b->text_cap*2 + 64;
    MD_Text **bigger = push_array(b->arena, MD_Text *, new_cap);
    MemoryCopy(bigger, b->texts, sizeof(MD_Text *)*b->text_count);
    b->texts = bigger;
    b->text_cap = new_cap;
  }
  b->texts[b->text_count++] = t;
}

internal void
md_flush_text(MD_Builder *b)
{
  if(b->pending_size == 0)
  {
    MemoryZeroStruct(&b->pending_parts);
    b->first_run = b->last_run = 0;
    b->run_count = 0;
    return;
  }
  MD_Box *box = md_box_alloc(b, MD_BoxKind_Text);
  box->text = md_text_from_pending(b, b->current);
  box->text->box = box;
  md_box_append(b->current, box);
  md_register_text(b, box->text);
}

internal MD_Box *
md_make_label(MD_Builder *b, MD_Box *parent, String8 string, MD_FontFamily family, B32 bold, Vec4F32 color, F32 font_size)
{
  MD_Box *box = md_box_alloc(b, MD_BoxKind_Text);
  MD_Text *t = push_array(b->arena, MD_Text, 1);
  t->string = string;
  t->run_count = 1;
  t->runs = push_array(b->arena, MD_TextRun, 1);
  t->runs[0].end = string.size;
  t->runs[0].family = family;
  t->runs[0].flags = bold ? MD_RunFlag_Bold : 0;
  t->runs[0].color = color;
  md_box_append(parent, box);
  t->font_size = font_size > 0 ? font_size : md_box_font_size(b, parent);
  t->line_height = md_box_line_height(b, parent, t->font_size);
  t->wrap = 0;
  t->box = box;
  box->text = t;
  return box;
}

internal U64
md_push_link(MD_Builder *b, String8 url)
{
  str8_list_push(b->arena, &b->link_urls, url);
  return b->link_urls.node_count;
}

////////////////////////////////
//~ Builder: Inline Content

internal void
md_push_image_child(MD_Builder *b, MD_Box *child)
{
  md_flush_text(b);
  b->current->kind = MD_BoxKind_Wrap;
  b->current->align = md_run_style(b)->align;
  md_box_append(b->current, child);
}

internal String8
md_alt_text(MD_Builder *b, MD_InlineArray inlines, U64 start, U64 *end_out)
{
  String8List parts = {0};
  S64 depth = 0;
  U64 i = start;
  for(; i < inlines.count; i += 1)
  {
    MD_Inline *in = &inlines.v[i];
    if(in->kind == MD_InlineKind_ImageStart) { depth += 1; continue; }
    if(in->kind == MD_InlineKind_ImageEnd) { depth -= 1; if(depth == 0) { break; } continue; }
    if(in->kind == MD_InlineKind_Text || in->kind == MD_InlineKind_Code) { str8_list_push(b->arena, &parts, in->string); }
  }
  *end_out = i;
  return str8_list_join(b->arena, &parts, 0);
}

internal void
md_push_image(MD_Builder *b, MD_Image *image, U64 link, F32 width, F32 height)
{
  MD_Box *box = md_box_alloc(b, MD_BoxKind_Image);
  box->image = image;
  box->link = link;
  box->radius = md_rems(b->style, 0.375f); // rounded_md
  box->margin[MD_Side_Right] = md_rems(b->style, 0.25f); // mr_1
  box->margin[MD_Side_Bottom] = md_rems(b->style, 0.25f); // mb_1
  box->fixed_width = width;
  box->fixed_height = height;
  md_push_image_child(b, box);
}

internal void
md_push_image_fallback(MD_Builder *b, String8 url, String8 alt)
{
  // image_fallback_element: "Failed to Load: {alt}", warning color, underlined
  String8 label = push_str8f(b->arena, "Failed to Load: %S", alt.size ? alt : url);
  MD_Box *wrap = md_box_alloc(b, MD_BoxKind_Block);
  wrap->link = md_push_link(b, url);
  md_push_image_child(b, wrap);
  MD_Box *text = md_make_label(b, wrap, label, MD_FontFamily_Sans, 0, b->colors->warning, md_rems(b->style, 0.875f));
  text->text->runs[0].flags |= MD_RunFlag_Underline;
  text->text->runs[0].underline_color = b->colors->warning;
  text->text->runs[0].link = wrap->link;
}

internal void
md_build_inlines(MD_Builder *b, MD_InlineArray inlines)
{
  MD_Style *style = b->style;
  TH_Colors *colors = b->colors;
  for(U64 i = 0; i < inlines.count; i += 1)
  {
    MD_Inline *in = &inlines.v[i];
    switch(in->kind)
    {
      default:{}break;
      case MD_InlineKind_Text: { md_push_text(b, in->string); }break;
      case MD_InlineKind_Code:
      {
        // inline_code: code font, buffer weight, `text` color; chip background
        // editor_foreground @ 8%. Inside links the code takes the link color.
        MD_RunStyle s = *md_run_style(b);
        s.family = MD_FontFamily_Mono;
        s.bold = 0;
        s.color = (b->link_depth > 0) ? colors->text_accent : colors->text;
        md_push_run_style(b, s);
        md_push_code_chip_text(b, in->string, th_opacity(colors->editor_foreground, 0.08f));
        md_pop_run_style(b);
      }break;
      case MD_InlineKind_SoftBreak: { md_push_soft_break(b); }break;
      case MD_InlineKind_HardBreak: { md_push_line_break(b); }break;
      case MD_InlineKind_FootnoteRef:
      {
        MD_RunStyle s = *md_run_style(b);
        s.color = colors->text_accent;
        s.underline = 1;
        s.underline_color = th_opacity(colors->text_accent, 0.5f);
        s.link = md_push_link(b, push_str8f(b->arena, "footnote:%S", in->string));
        md_push_run_style(b, s);
        md_push_text(b, push_str8f(b->arena, "[%S]", in->string));
        md_pop_run_style(b);
      }break;
      case MD_InlineKind_EmphasisStart:
      {
        MD_RunStyle s = *md_run_style(b);
        s.italic = 1;
        md_push_run_style(b, s);
      }break;
      case MD_InlineKind_StrongStart:
      {
        MD_RunStyle s = *md_run_style(b);
        s.bold = 1;
        s.color = colors->text;
        md_push_run_style(b, s);
      }break;
      case MD_InlineKind_StrikeStart:
      {
        MD_RunStyle s = *md_run_style(b);
        s.strike = 1;
        md_push_run_style(b, s);
      }break;
      case MD_InlineKind_EmphasisEnd:
      case MD_InlineKind_StrongEnd:
      case MD_InlineKind_StrikeEnd: { md_pop_run_style(b); }break;
      case MD_InlineKind_LinkStart:
      {
        b->link_depth += 1;
        MD_RunStyle s = *md_run_style(b);
        s.color = colors->text_accent;
        s.underline = 1;
        s.underline_color = th_opacity(colors->text_accent, 0.5f);
        s.link = md_push_link(b, in->url);
        md_push_run_style(b, s);
      }break;
      case MD_InlineKind_LinkEnd:
      {
        if(b->link_depth > 0) { b->link_depth -= 1; }
        md_pop_run_style(b);
      }break;
      case MD_InlineKind_ImageStart:
      {
        MD_Image *image = 0;
        MD_ImageResolve r = md_image_resolve(in->url, b->base_dir, b->style->scale, &image);
        if(r == MD_ImageResolve_None)
        {
          // no image source: the alt text flows as plain text
          break;
        }
        U64 end = i;
        String8 alt = md_alt_text(b, inlines, i, &end);
        if(r == MD_ImageResolve_Pending)
        {
          // still downloading: nothing yet
          i = end;
          break;
        }
        U64 link = (b->link_depth > 0) ? md_run_style(b)->link : 0;
        if(r == MD_ImageResolve_Loaded)
        {
          md_push_image(b, image, link, 0, 0);
        }
        else
        {
          md_push_image_fallback(b, in->url, alt);
        }
        i = end;
      }break;
      case MD_InlineKind_ImageEnd:{}break;
    }
  }
  (void)style;
}

////////////////////////////////
//~ Builder: HTML Blocks
//
// Zed parses HTML blocks with html5ever and renders the result into the same
// builder (html/html_parser.rs, html/html_rendering.rs): p, h1-h6, ul/ol
// (nested, with per-depth bullets), blockquote, table (caption, thead,
// tbody, th/td, align), img (width/height), b/strong, i/em, del, ins, a, and
// the style attribute's font-weight, font-style, text-decoration and
// text-align. Here a small tree builder with HTML's implied end tags stands
// in for html5ever. Beyond Zed: alignment inherited from containers
// (`<div align="center">`), `pre` as a code block, code/kbd in the code font,
// u/s/strike, br and hr, and id/name anchors for `#fragment` links.

internal String8
md_html_attr(String8 tag, String8 name)
{
  for(U64 i = 0; i < tag.size; i += 1)
  {
    if(i > 0 && !char_is_space(tag.str[i-1])) { continue; }
    if(i + name.size >= tag.size) { break; }
    if(!str8_match(str8_substr(tag, r1u64(i, i+name.size)), name, StringMatchFlag_CaseInsensitive)) { continue; }
    U64 k = i + name.size;
    for(;k < tag.size && char_is_space(tag.str[k]); k += 1);
    if(k >= tag.size || tag.str[k] != '=') { continue; }
    k += 1;
    for(;k < tag.size && char_is_space(tag.str[k]); k += 1);
    if(k >= tag.size) { break; }
    U8 q = tag.str[k];
    if(q == '"' || q == '\'')
    {
      U64 close = str8_find_needle(tag, k+1, str8(&q, 1), 0);
      return str8_substr(tag, r1u64(k+1, close));
    }
    U64 e = k;
    for(;e < tag.size && !char_is_space(tag.str[e]) && tag.str[e] != '>' && tag.str[e] != '/'; e += 1);
    return str8_substr(tag, r1u64(k, e));
  }
  return str8_zero();
}

// text_align_from_attributes: style's text-align, then the align attribute
internal MD_Align
md_html_align(String8 tag)
{
  String8 a = md_svg_attr(tag, str8_lit("text-align"));
  if(a.size == 0) { a = md_html_attr(tag, str8_lit("align")); }
  a = str8_skip_chop_whitespace(a);
  if(str8_match(a, str8_lit("center"), StringMatchFlag_CaseInsensitive)) { return MD_Align_Center; }
  if(str8_match(a, str8_lit("right"), StringMatchFlag_CaseInsensitive)) { return MD_Align_Right; }
  if(str8_match(a, str8_lit("left"), StringMatchFlag_CaseInsensitive)) { return MD_Align_Left; }
  return MD_Align_None;
}

internal void md_push_paragraph_box(MD_Builder *b, Rng1U64 range, MD_Align align);
internal void md_push_heading_box(MD_Builder *b, U64 level, Rng1U64 range, MD_Align align);
internal void md_pop_text_block(MD_Builder *b);
internal void md_build_code_block(MD_Builder *b, MD_Node *node, Rng1U64 range, String8 code, MD_Language lang);

//- tree

typedef struct MD_HtmlNode MD_HtmlNode;
struct MD_HtmlNode
{
  MD_HtmlNode *first;
  MD_HtmlNode *last;
  MD_HtmlNode *next;
  MD_HtmlNode *parent;
  String8 name;                  // lowercase; empty for text
  String8 tag;                   // the start tag, for attributes
  String8 text;                  // raw text, entities not yet decoded
  U64 offset;                    // position in the block's source
};

read_only global char *md_html_void_tags[] =
{
  "img", "br", "hr", "input", "meta", "link", "source", "wbr", "col", "area", "embed", "param", "track", "base",
};

read_only global char *md_html_render_block_tags[] =
{
  "p", "div", "center", "section", "article", "header", "footer", "main", "nav", "aside", "figure",
  "figcaption", "details", "summary", "dl", "dt", "dd", "picture", "ul", "ol", "li", "blockquote",
  "table", "caption", "thead", "tbody", "tfoot", "tr", "td", "th", "h1", "h2", "h3", "h4", "h5", "h6",
  "pre", "hr", "form", "fieldset", "address", "html", "body",
};

read_only global char *md_html_hidden_tags[] =
{
  "head", "title", "script", "style", "template", "noscript", "textarea", "select", "button", "iframe", "svg", "math",
};

internal B32
md_html_name_in(String8 name, char **names, U64 count)
{
  for EachIndex(i, count) { if(str8_match(name, str8_cstring(names[i]), 0)) { return 1; } }
  return 0;
}

#define md_html_is(node, lit) str8_match((node)->name, str8_lit(lit), 0)
#define md_html_is_block(name) md_html_name_in((name), md_html_render_block_tags, ArrayCount(md_html_render_block_tags))

internal MD_HtmlNode *
md_html_node_push(Arena *arena, MD_HtmlNode *parent, String8 name, U64 offset)
{
  MD_HtmlNode *n = push_array(arena, MD_HtmlNode, 1);
  n->name = name;
  n->offset = offset;
  n->parent = parent;
  SLLQueuePush(parent->first, parent->last, n);
  return n;
}

// The open element a start tag implicitly closes, if any (the tree
// builder's "generate implied end tags" for the elements READMEs use).
internal MD_HtmlNode *
md_html_implied_close(MD_HtmlNode *cur, MD_HtmlNode *root, String8 name)
{
  for(MD_HtmlNode *n = cur; n != root; n = n->parent)
  {
    if(str8_match(name, str8_lit("li"), 0))
    {
      if(md_html_is(n, "li")) { return n; }
      if(md_html_is(n, "ul") || md_html_is(n, "ol")) { break; }
    }
    else if(str8_match(name, str8_lit("td"), 0) || str8_match(name, str8_lit("th"), 0))
    {
      if(md_html_is(n, "td") || md_html_is(n, "th")) { return n; }
      if(md_html_is(n, "tr") || md_html_is(n, "table")) { break; }
    }
    else if(str8_match(name, str8_lit("tr"), 0))
    {
      if(md_html_is(n, "tr")) { return n; }
      if(md_html_is(n, "table") || md_html_is(n, "thead") || md_html_is(n, "tbody") || md_html_is(n, "tfoot")) { break; }
    }
    else if(str8_match(name, str8_lit("thead"), 0) || str8_match(name, str8_lit("tbody"), 0) || str8_match(name, str8_lit("tfoot"), 0))
    {
      if(md_html_is(n, "thead") || md_html_is(n, "tbody") || md_html_is(n, "tfoot")) { return n; }
      if(md_html_is(n, "table")) { break; }
    }
    else if(str8_match(name, str8_lit("dt"), 0) || str8_match(name, str8_lit("dd"), 0))
    {
      if(md_html_is(n, "dt") || md_html_is(n, "dd")) { return n; }
      if(md_html_is(n, "dl")) { break; }
    }
    if(md_html_is_block(name))
    {
      // a block start tag closes an open p
      if(md_html_is(n, "p")) { return n; }
      if(md_html_is_block(n->name)) { break; }
    }
    else
    {
      break;
    }
  }
  return 0;
}

internal MD_HtmlNode *
md_html_parse(Arena *arena, String8 s)
{
  MD_HtmlNode *root = push_array(arena, MD_HtmlNode, 1);
  MD_HtmlNode *cur = root;
  for(U64 i = 0; i < s.size;)
  {
    if(str8_match(str8_prefix(str8_skip(s, i), 4), str8_lit("<!--"), 0))
    {
      U64 e = str8_find_needle(s, i+4, str8_lit("-->"), 0);
      i = (e < s.size) ? e+3 : s.size;
      continue;
    }
    if(s.str[i] == '<')
    {
      U64 len = md_html_tag_length(str8_skip(s, i));
      if(len == 0 && i+1 < s.size && (s.str[i+1] == '!' || s.str[i+1] == '?'))
      {
        U64 e = str8_find_needle(s, i, str8_lit(">"), 0);
        i = (e < s.size) ? e+1 : s.size;
        continue;
      }
      if(len)
      {
        String8 tag = str8_substr(s, r1u64(i, i+len));
        U64 at = i;
        i += len;
        B32 closing = (tag.size > 1 && tag.str[1] == '/');
        U64 ns = closing ? 2 : 1, ne = ns;
        for(;ne < tag.size && (md_char_is_alnum(tag.str[ne]) || tag.str[ne] == '-'); ne += 1);
        String8 name = push_str8_copy(arena, str8_substr(tag, r1u64(ns, ne)));
        for EachIndex(k, name.size) { name.str[k] = lower_from_char(name.str[k]); }
        if(closing)
        {
          for(MD_HtmlNode *n = cur; n != root; n = n->parent)
          {
            if(str8_match(n->name, name, 0)) { cur = n->parent; break; }
          }
          continue;
        }
        for(MD_HtmlNode *closed = 0; (closed = md_html_implied_close(cur, root, name)) != 0;) { cur = closed->parent; }
        MD_HtmlNode *node = md_html_node_push(arena, cur, name, at);
        node->tag = tag;
        B32 self_closing = (tag.size >= 2 && tag.str[tag.size-2] == '/');
        if(md_html_name_in(name, md_html_void_tags, ArrayCount(md_html_void_tags)) || self_closing) { continue; }
        if(md_html_name_in(name, md_html_hidden_tags, ArrayCount(md_html_hidden_tags)))
        {
          // raw content up to the end tag; never rendered
          String8 end_tag = push_str8f(arena, "</%S", name);
          U64 e = str8_find_needle(s, i, end_tag, StringMatchFlag_CaseInsensitive);
          U64 gt = str8_find_needle(s, e, str8_lit(">"), 0);
          i = (gt < s.size) ? gt+1 : s.size;
          continue;
        }
        cur = node;
        continue;
      }
    }
    U64 e = i+1;
    for(;e < s.size && s.str[e] != '<'; e += 1);
    String8 text = str8_substr(s, r1u64(i, e));
    if(cur->last && cur->last->name.size == 0 && cur->last->text.str + cur->last->text.size == text.str)
    {
      cur->last->text.size += text.size;
    }
    else
    {
      MD_HtmlNode *t = md_html_node_push(arena, cur, str8_zero(), i);
      t->text = text;
    }
    i = e;
  }
  return root;
}

//- rendering

typedef struct MD_HtmlCtx MD_HtmlCtx;
struct MD_HtmlCtx
{
  Rng1U64 range;                 // the HTML block's source range
  MD_Align align;
  U64 list_depth;
  B32 in_text;                   // an anonymous paragraph is open
  B32 space_pending;             // collapsed whitespace not yet emitted
  B32 pre;
};

internal B32
md_html_has_content(MD_HtmlNode *node)
{
  for(MD_HtmlNode *c = node->first; c != 0; c = c->next)
  {
    if(c->name.size == 0)
    {
      for EachIndex(i, c->text.size) { if(!char_is_space(c->text.str[i])) { return 1; } }
    }
    else if(md_html_is(c, "img") || md_html_is(c, "br") || md_html_is(c, "hr") || md_html_has_content(c)) { return 1; }
  }
  return 0;
}

internal void
md_html_text(MD_Builder *b, MD_HtmlCtx *ctx, String8 raw)
{
  Temp scratch = scratch_begin(&b->arena, 1);
  U8 *out = push_array_no_zero(scratch.arena, U8, raw.size*4 + 2);
  U64 n = 0;
  for(U64 k = 0; k < raw.size; k += 1)
  {
    U8 c = raw.str[k];
    if(!ctx->pre && char_is_space(c))
    {
      if(n > 0 || b->pending_size > 0) { ctx->space_pending = 1; }
      continue;
    }
    if(ctx->space_pending) { out[n++] = ' '; ctx->space_pending = 0; }
    if(c == '&')
    {
      U8 buf[8]; U64 sz = 0;
      U64 used = md_parse_entity(raw, k, buf, &sz);
      if(used) { MemoryCopy(out+n, buf, sz); n += sz; k += used-1; continue; }
    }
    if(c == '\r') { continue; }
    out[n++] = c;
  }
  if(n) { md_push_text(b, push_str8_copy(b->arena, str8(out, n))); }
  scratch_end(scratch);
}

// html_style_from_html_styles + the formatting elements
internal void
md_html_apply_inline_style(MD_Builder *b, MD_HtmlNode *node, MD_RunStyle *st)
{
  if(md_html_is(node, "b") || md_html_is(node, "strong")) { st->bold = 1; }
  else if(md_html_is(node, "i") || md_html_is(node, "em") || md_html_is(node, "cite") || md_html_is(node, "var")) { st->italic = 1; }
  else if(md_html_is(node, "del") || md_html_is(node, "s") || md_html_is(node, "strike")) { st->strike = 1; }
  else if(md_html_is(node, "ins") || md_html_is(node, "u")) { st->underline = 1; st->underline_color = st->color; }
  else if(md_html_is(node, "code") || md_html_is(node, "kbd") || md_html_is(node, "samp") || md_html_is(node, "tt")) { st->family = MD_FontFamily_Mono; st->bold = 0; }
  String8 v;
  v = md_svg_attr(node->tag, str8_lit("font-weight"));
  if(v.size) { st->bold = str8_match(v, str8_lit("bold"), StringMatchFlag_CaseInsensitive) || str8_match(v, str8_lit("bolder"), StringMatchFlag_CaseInsensitive) || md_svg_number(v, 400) >= 600; }
  v = md_svg_attr(node->tag, str8_lit("font-style"));
  if(str8_match(v, str8_lit("italic"), StringMatchFlag_CaseInsensitive) || str8_match(v, str8_lit("oblique"), StringMatchFlag_CaseInsensitive)) { st->italic = 1; }
  v = md_svg_attr(node->tag, str8_lit("text-decoration"));
  if(str8_find_needle(v, 0, str8_lit("underline"), StringMatchFlag_CaseInsensitive) < v.size) { st->underline = 1; st->underline_color = st->color; }
  if(str8_find_needle(v, 0, str8_lit("line-through"), StringMatchFlag_CaseInsensitive) < v.size) { st->strike = 1; }
}

internal void
md_html_anchor(MD_Builder *b, MD_HtmlNode *node, MD_Box *box)
{
  String8 id = md_html_attr(node->tag, str8_lit("id"));
  if(id.size == 0 && md_html_is(node, "a")) { id = md_html_attr(node->tag, str8_lit("name")); }
  if(id.size) { md_push_anchor(b, push_str8_copy(b->arena, id), box); }
}

internal void
md_html_image(MD_Builder *b, MD_HtmlNode *node)
{
  String8 src = md_unescape(b->arena, md_html_attr(node->tag, str8_lit("src")));
  String8 alt = md_unescape(b->arena, md_html_attr(node->tag, str8_lit("alt")));
  MD_Image *image = 0;
  MD_ImageResolve r = md_image_resolve(src, b->base_dir, b->style->scale, &image);
  if(r == MD_ImageResolve_None || r == MD_ImageResolve_Pending) { return; }
  U64 link = (b->link_depth > 0) ? md_run_style(b)->link : 0;
  if(r == MD_ImageResolve_Loaded)
  {
    // parse_html_element_dimension: px or bare numbers; the attribute, then style
    F32 dims[2] = {0};
    char *names[2] = {"width", "height"};
    for EachIndex(k, 2)
    {
      String8 v = md_html_attr(node->tag, str8_cstring(names[k]));
      if(v.size == 0) { v = md_svg_attr(node->tag, str8_cstring(names[k])); }
      v = str8_skip_chop_whitespace(v);
      if(v.size && char_is_digit(v.str[0], 10) && v.str[v.size-1] != '%') { dims[k] = md_svg_number(v, 0)*b->style->scale; }
    }
    // one dimension keeps the aspect ratio
    if(dims[0] > 0 && dims[1] == 0 && image->size.x > 0) { dims[1] = dims[0]*image->size.y/image->size.x; }
    if(dims[1] > 0 && dims[0] == 0 && image->size.y > 0) { dims[0] = dims[1]*image->size.x/image->size.y; }
    md_push_image(b, image, link, dims[0], dims[1]);
  }
  else
  {
    md_push_image_fallback(b, src, alt);
  }
}

internal void md_html_render_blocks(MD_Builder *b, MD_HtmlCtx *ctx, MD_HtmlNode *parent);

// Inline content: text and formatting into the open text block. Block
// elements nested in inline ones flow as their content.
internal void
md_html_render_inline(MD_Builder *b, MD_HtmlCtx *ctx, MD_HtmlNode *node)
{
  if(node->name.size == 0) { md_html_text(b, ctx, node->text); return; }
  if(md_html_is(node, "br"))
  {
    md_push_line_break(b);
    ctx->space_pending = 0;
    return;
  }
  if(md_html_is(node, "img")) { ctx->space_pending = 0; md_html_image(b, node); return; }
  if(md_html_name_in(node->name, md_html_hidden_tags, ArrayCount(md_html_hidden_tags))) { return; }
  // whitespace before an element takes the outer style, not the element's
  if(ctx->space_pending && b->pending_size > 0) { md_push_text(b, str8_lit(" ")); ctx->space_pending = 0; }
  MD_RunStyle st = *md_run_style(b);
  B32 is_link = 0;
  if(md_html_is(node, "a"))
  {
    md_html_anchor(b, node, b->current);
    String8 href = md_html_attr(node->tag, str8_lit("href"));
    if(href.size)
    {
      st.color = b->colors->text_accent;
      st.underline = 1;
      st.underline_color = th_opacity(b->colors->text_accent, 0.5f);
      st.link = md_push_link(b, md_unescape(b->arena, href));
      is_link = 1;
    }
  }
  md_html_apply_inline_style(b, node, &st);
  B32 chip = (md_html_is(node, "code") || md_html_is(node, "kbd")) && !ctx->pre;
  if(chip && !is_link) { st.color = (b->link_depth > 0) ? b->colors->text_accent : b->colors->text; }
  md_push_run_style(b, st);
  if(is_link) { b->link_depth += 1; }
  for(MD_HtmlNode *c = node->first; c != 0; c = c->next)
  {
    if(chip && c->name.size == 0)
    {
      // inline code: the same chip as `code` spans
      Temp scratch = scratch_begin(&b->arena, 1);
      String8 decoded = md_unescape(scratch.arena, c->text);
      if(ctx->space_pending) { md_push_text(b, str8_lit(" ")); ctx->space_pending = 0; }
      md_push_code_chip_text(b, push_str8_copy(b->arena, decoded), th_opacity(b->colors->editor_foreground, 0.08f));
      scratch_end(scratch);
      continue;
    }
    md_html_render_inline(b, ctx, c);
  }
  if(is_link) { b->link_depth -= 1; }
  md_pop_run_style(b);
}

internal void
md_html_end_text(MD_Builder *b, MD_HtmlCtx *ctx)
{
  if(ctx->in_text) { md_pop_text_block(b); ctx->in_text = 0; }
  ctx->space_pending = 0;
}

internal String8
md_html_raw_text(Arena *arena, MD_HtmlNode *node)
{
  String8List parts = {0};
  for(MD_HtmlNode *c = node->first; c != 0; c = c->next)
  {
    if(c->name.size == 0) { str8_list_push(arena, &parts, md_unescape(arena, c->text)); }
    else if(md_html_is(c, "br")) { str8_list_push(arena, &parts, str8_lit("\n")); }
    else { str8_list_push(arena, &parts, md_html_raw_text(arena, c)); }
  }
  return str8_list_join(arena, &parts, 0);
}

internal void
md_html_render_table(MD_Builder *b, MD_HtmlCtx *ctx, MD_HtmlNode *table)
{
  MD_Style *style = b->style;
  TH_Colors *colors = b->colors;
  Temp scratch = scratch_begin(&b->arena, 1);

  //- rows from thead, tbody, tfoot and direct children
  MD_HtmlNode *rows[1024];
  U64 row_count = 0;
  MD_HtmlNode *caption = 0;
  for(MD_HtmlNode *c = table->first; c != 0; c = c->next)
  {
    if(md_html_is(c, "caption")) { caption = c; }
    else if(md_html_is(c, "tr")) { if(row_count < ArrayCount(rows)) { rows[row_count++] = c; } }
    else if(md_html_is(c, "thead") || md_html_is(c, "tbody") || md_html_is(c, "tfoot"))
    {
      for(MD_HtmlNode *r = c->first; r != 0; r = r->next)
      {
        if(md_html_is(r, "tr") && row_count < ArrayCount(rows)) { rows[row_count++] = r; }
      }
    }
  }
  U64 cols = 0;
  for EachIndex(r, row_count)
  {
    U64 n = 0;
    for(MD_HtmlNode *cell = rows[r]->first; cell != 0; cell = cell->next)
    {
      if(!md_html_is(cell, "td") && !md_html_is(cell, "th")) { continue; }
      String8 span = md_html_attr(cell->tag, str8_lit("colspan"));
      n += Clamp(1, (U64)md_svg_number(span, 1), 64);
    }
    cols = Max(cols, n);
  }

  //- caption: div().mb_2().line_height(rems(1.3))
  if(caption)
  {
    MD_Box *cap = md_box_alloc(b, MD_BoxKind_Block);
    cap->margin[MD_Side_Bottom] = md_rems(style, 0.5f);
    cap->text_line_height_abs = md_rems(style, 1.3f);
    md_push_div(b, cap, ctx->range);
    for(MD_HtmlNode *c = caption->first; c != 0; c = c->next) { md_html_render_inline(b, ctx, c); }
    ctx->space_pending = 0;
    md_pop_div(b);
  }
  if(cols == 0) { scratch_end(scratch); return; }

  //- div().flex() > div().grid().grid_cols_max_content(n).mb_2().border(1.5).rounded_sm()
  MD_Box *wrapper = md_box_alloc(b, MD_BoxKind_Row);
  md_push_div(b, wrapper, ctx->range);
  MD_Box *grid = md_box_alloc(b, MD_BoxKind_Grid);
  grid->grid_columns = cols;
  grid->margin[MD_Side_Bottom] = md_rems(style, 0.5f);
  for EachIndex(k, 4) { grid->border[k] = md_px(style, 1.5f); }
  grid->border_color = colors->border;
  grid->radius = md_rems(style, 0.25f);
  grid->scroll_x = 1;
  grid->shrink_to_fit = 1;
  grid->key = ctx->range.min + table->offset + 1;
  md_push_div(b, grid, ctx->range);
  B32 was_in_table = b->in_table;
  b->in_table = 1;
  for EachIndex(r, row_count)
  {
    U64 col = 0;
    for(MD_HtmlNode *cell = rows[r]->first; cell != 0 && col < cols; cell = cell->next)
    {
      if(!md_html_is(cell, "td") && !md_html_is(cell, "th")) { continue; }
      B32 header = md_html_is(cell, "th");
      U64 span = Clamp(1, (U64)md_svg_number(md_html_attr(cell->tag, str8_lit("colspan")), 1), cols - col);
      MD_Align align = md_html_align(cell->tag);
      if(align == MD_Align_None) { align = md_html_align(rows[r]->tag); }
      for EachIndex(k, span)
      {
        MD_Box *c = md_box_alloc(b, MD_BoxKind_Block);
        c->bfc = 1;
        if(col > 0 && k == 0) { c->border[MD_Side_Left] = md_px(style, 1); }
        if(r > 0) { c->border[MD_Side_Top] = md_px(style, 1); }
        c->border_color = colors->border;
        c->padding[MD_Side_Left] = c->padding[MD_Side_Right] = md_rems(style, 0.5f);   // px_2
        c->padding[MD_Side_Top] = c->padding[MD_Side_Bottom] = md_rems(style, 0.25f);  // py_1
        if(header) { c->background = colors->title_bar_background; }
        else if(r % 2 == 1) { c->background = colors->panel_background; }
        c->align = align;
        MD_RunStyle s = *md_run_style(b);
        s.align = align;
        md_push_run_style(b, s);
        md_push_div(b, c, ctx->range);
        if(k == 0)
        {
          // colspan: the cell's content in its first column, the rest empty
          ctx->space_pending = 0;
          for(MD_HtmlNode *ch = cell->first; ch != 0; ch = ch->next) { md_html_render_inline(b, ctx, ch); }
          ctx->space_pending = 0;
        }
        md_pop_div(b);
        md_pop_run_style(b);
        col += 1;
      }
    }
    // missing cells: empty bordered divs
    for(;col < cols; col += 1)
    {
      MD_Box *c = md_box_alloc(b, MD_BoxKind_Block);
      if(col > 0) { c->border[MD_Side_Left] = md_px(style, 1); }
      if(r > 0) { c->border[MD_Side_Top] = md_px(style, 1); }
      c->border_color = colors->border;
      if(r % 2 == 1) { c->background = colors->panel_background; }
      md_push_div(b, c, ctx->range);
      md_pop_div(b);
    }
  }
  b->in_table = was_in_table;
  md_pop_div(b);
  md_pop_div(b);
  scratch_end(scratch);
}

internal void
md_html_render_block(MD_Builder *b, MD_HtmlCtx *ctx, MD_HtmlNode *node)
{
  MD_Style *style = b->style;
  TH_Colors *colors = b->colors;
  if(md_html_name_in(node->name, md_html_hidden_tags, ArrayCount(md_html_hidden_tags))) { return; }
  MD_HtmlCtx sub = *ctx;
  sub.in_text = 0;
  sub.space_pending = 0;
  MD_Align a = md_html_align(node->tag);
  if(md_html_is(node, "center")) { a = MD_Align_Center; }
  if(a != MD_Align_None) { sub.align = a; }
  B32 is_heading = (node->name.size == 2 && node->name.str[0] == 'h' && node->name.str[1] >= '1' && node->name.str[1] <= '6');

  if(md_html_is(node, "p") || md_html_is(node, "summary") || md_html_is(node, "dt") || is_heading)
  {
    if(!md_html_has_content(node)) { return; }
    if(is_heading) { md_push_heading_box(b, node->name.str[1] - '0', ctx->range, sub.align); }
    else { md_push_paragraph_box(b, ctx->range, sub.align); }
    md_html_anchor(b, node, b->current);
    MD_RunStyle st = *md_run_style(b);
    if(md_html_is(node, "dt")) { st.bold = 1; }
    md_html_apply_inline_style(b, node, &st);
    md_push_run_style(b, st);
    for(MD_HtmlNode *c = node->first; c != 0; c = c->next) { md_html_render_inline(b, &sub, c); }
    md_pop_run_style(b);
    md_pop_text_block(b);
  }
  else if(md_html_is(node, "ul") || md_html_is(node, "ol"))
  {
    // render_html_list: div().pl_2p5(), then push_markdown_list_item per item
    B32 ordered = md_html_is(node, "ol");
    MD_Box *l = md_box_alloc(b, MD_BoxKind_Block);
    l->padding[MD_Side_Left] = md_rems(style, 0.625f);
    md_push_div(b, l, ctx->range);
    md_html_anchor(b, node, l);
    U64 order = 1;
    String8 start = md_html_attr(node->tag, str8_lit("start"));
    if(start.size && str8_is_integer(start, 10)) { order = u64_from_str8(start, 10); }
    sub.list_depth = ctx->list_depth + 1;
    for(MD_HtmlNode *item = node->first; item != 0; item = item->next)
    {
      if(!md_html_is(item, "li")) { continue; }
      U64 this_order = order++;
      if(!md_html_has_content(item)) { continue; }
      // html_list_item_prefix
      String8 bullet;
      U64 depth = ctx->list_depth;
      if(ordered)
      {
        U64 index = this_order ? this_order-1 : 0;
        if(depth == 0) { bullet = push_str8f(b->arena, "%I64u. ", this_order); }
        else { bullet = push_str8f(b->arena, "%c. ", (char)((depth == 1 ? 'A' : 'a') + index % 26)); }
      }
      else
      {
        local_persist char *bullets[] = {"\xe2\x80\xa2 ", "\xe2\x97\xa6 ", "\xe2\x96\xaa ", "\xe2\x80\xa3 ", "\xe2\x81\x83 "};
        bullet = str8_cstring(bullets[Min(depth, ArrayCount(bullets)-1)]);
      }
      MD_Box *row = md_box_alloc(b, MD_BoxKind_Row);
      row->margin[MD_Side_Bottom] = md_rems(style, 0.25f);
      row->gap = md_rems(style, 0.25f);
      row->text_line_height_rel = style->paragraph_line_height;
      md_push_div(b, row, ctx->range);
      MD_Box *bullet_box = md_box_alloc(b, MD_BoxKind_Block);
      md_box_append(row, bullet_box);
      md_make_label(b, bullet_box, bullet, (depth == 0 || ordered) ? MD_FontFamily_Sans : MD_FontFamily_Symbol, 0, colors->text, 0);
      MD_Box *content = md_box_alloc(b, MD_BoxKind_Block);
      content->flex_1 = 1;
      content->bfc = 1;
      md_push_div(b, content, ctx->range);
      md_html_render_blocks(b, &sub, item);
      md_pop_div(b);
      md_pop_div(b);
    }
    md_pop_div(b);
  }
  else if(md_html_is(node, "blockquote"))
  {
    if(!md_html_has_content(node)) { return; }
    MD_Box *q = md_box_alloc(b, MD_BoxKind_Block);
    q->padding[MD_Side_Left] = md_rems(style, 1.f);
    q->margin[MD_Side_Bottom] = style->paragraph_spacing;
    q->border[MD_Side_Left] = md_px(style, 4);
    q->border_color = colors->border;
    MD_RunStyle s = *md_run_style(b);
    s.color = colors->text_muted;
    md_push_run_style(b, s);
    md_push_div(b, q, ctx->range);
    md_html_render_blocks(b, &sub, node);
    md_pop_div(b);
    md_pop_run_style(b);
  }
  else if(md_html_is(node, "table"))
  {
    md_html_render_table(b, &sub, node);
  }
  else if(md_html_is(node, "pre"))
  {
    Temp scratch = scratch_begin(&b->arena, 1);
    String8 code = md_html_raw_text(scratch.arena, node);
    if(code.size && code.str[0] == '\n') { code = str8_skip(code, 1); }
    for(;code.size && (code.str[code.size-1] == '\n' || code.str[code.size-1] == '\r');) { code.size -= 1; }
    String8 cls = md_html_attr(node->tag, str8_lit("class"));
    if(node->first && md_html_is(node->first, "code")) { String8 inner = md_html_attr(node->first->tag, str8_lit("class")); if(inner.size) { cls = inner; } }
    U64 dash = str8_find_needle(cls, 0, str8_lit("-"), 0);
    String8 lang = (dash < cls.size) ? str8_skip(cls, dash+1) : cls;
    U64 space = str8_find_needle(lang, 0, str8_lit(" "), 0);
    Rng1U64 range = r1u64(ctx->range.min + node->offset, ctx->range.max);
    md_build_code_block(b, 0, range, push_str8_copy(b->arena, code), md_language_from_info(str8_prefix(lang, space)));
    scratch_end(scratch);
  }
  else if(md_html_is(node, "hr"))
  {
    MD_Box *rule = md_box_alloc(b, MD_BoxKind_Block);
    rule->border[MD_Side_Bottom] = md_px(style, 1);
    rule->border_color = colors->border;
    rule->margin[MD_Side_Top] = rule->margin[MD_Side_Bottom] = style->paragraph_spacing;
    md_push_div(b, rule, ctx->range);
    md_pop_div(b);
  }
  else if(md_html_is(node, "dd"))
  {
    MD_Box *dd = md_box_alloc(b, MD_BoxKind_Block);
    dd->padding[MD_Side_Left] = md_rems(style, 1.f);
    md_push_div(b, dd, ctx->range);
    md_html_render_blocks(b, &sub, node);
    md_pop_div(b);
  }
  else
  {
    // div, center, section, details, ...: their children, with the alignment
    if(md_html_attr(node->tag, str8_lit("id")).size)
    {
      MD_Box *anchor = md_box_alloc(b, MD_BoxKind_Block);
      md_push_div(b, anchor, ctx->range);
      md_html_anchor(b, node, anchor);
      md_html_render_blocks(b, &sub, node);
      md_pop_div(b);
    }
    else
    {
      md_html_render_blocks(b, &sub, node);
    }
  }
}

// Block content: block elements in order; runs of text and inline elements
// between them become paragraphs.
internal void
md_html_render_blocks(MD_Builder *b, MD_HtmlCtx *ctx, MD_HtmlNode *parent)
{
  for(MD_HtmlNode *c = parent->first; c != 0; c = c->next)
  {
    B32 block = (c->name.size && md_html_is_block(c->name));
    if(block)
    {
      md_html_end_text(b, ctx);
      md_html_render_block(b, ctx, c);
      continue;
    }
    if(!ctx->in_text)
    {
      B32 content = 0;
      if(c->name.size == 0) { for EachIndex(i, c->text.size) { if(!char_is_space(c->text.str[i])) { content = 1; break; } } }
      else if(md_html_is(c, "img") || md_html_is(c, "br") || md_html_has_content(c)) { content = 1; }
      else if(md_html_is(c, "a")) { md_html_anchor(b, c, b->current); }
      if(!content) { continue; }
      md_push_paragraph_box(b, ctx->range, ctx->align);
      ctx->in_text = 1;
      ctx->space_pending = 0;
    }
    md_html_render_inline(b, ctx, c);
  }
  md_html_end_text(b, ctx);
}

internal void
md_build_html_block(MD_Builder *b, MD_Node *node)
{
  Temp scratch = scratch_begin(&b->arena, 1);
  MD_Box *html_div = b->current;
  MD_HtmlNode *root = md_html_parse(scratch.arena, node->text);
  MD_HtmlCtx ctx = {0};
  ctx.range = r1u64(node->range.min + 1, node->range.max + 1);
  md_html_render_blocks(b, &ctx, root);
  // the last box of the block gets the end-of-document margin rule
  if(html_div->last && node->range.max == b->doc->end) { html_div->last->margin[MD_Side_Bottom] = 0; }
  scratch_end(scratch);
}

////////////////////////////////
//~ Builder: Blocks

internal void
md_push_paragraph_box(MD_Builder *b, Rng1U64 range, MD_Align align)
{
  // push_markdown_paragraph: div().mb(paragraph_spacing).line_height(paragraph_line_height)
  MD_Box *p = md_box_alloc(b, MD_BoxKind_Block);
  p->margin[MD_Side_Bottom] = b->style->paragraph_spacing;
  p->text_line_height_rel = b->style->paragraph_line_height;
  p->align = align;
  MD_RunStyle s = *md_run_style(b);
  s.align = align;
  md_push_run_style(b, s);
  md_push_div(b, p, range);
}

internal void
md_push_heading_box(MD_Builder *b, U64 level, Rng1U64 range, MD_Align align)
{
  MD_Style *style = b->style;
  level = Clamp(1, level, 6);
  // div().mt_4().mb_2(), apply_heading_style, then style.heading's margins win
  MD_Box *h = md_box_alloc(b, MD_BoxKind_Block);
  h->margin[MD_Side_Top] = md_rems(style, 1.f);
  h->margin[MD_Side_Bottom] = md_rems(style, 0.5f);
  if(level > 1) { h->margin[MD_Side_Top] = md_rems(style, 1.5f); }
  if(level == 1) { h->padding[MD_Side_Bottom] = md_rems(style, 0.5f); h->border[MD_Side_Bottom] = md_px(style, 1); h->border_color = b->colors->border_variant; }
  if(level == 2) { h->padding[MD_Side_Bottom] = md_rems(style, 0.25f); h->border[MD_Side_Bottom] = md_px(style, 1); h->border_color = b->colors->border_variant; }
  h->margin[MD_Side_Top] = style->heading_margin_top;
  h->margin[MD_Side_Bottom] = style->heading_margin_bottom;
  h->text_font_size = md_rems(style, style->heading_sizes[level]);
  h->text_line_height_rel = style->heading_line_height;
  h->align = align;
  MD_RunStyle s = *md_run_style(b);
  s.bold = style->heading_weight_semibold;
  s.color = (level == 6) ? b->colors->text_muted : b->colors->text;
  s.align = align;
  md_push_run_style(b, s);
  md_push_div(b, h, range);
}

internal void
md_pop_text_block(MD_Builder *b)
{
  md_pop_div(b);
  md_pop_run_style(b);
}

internal void md_build_children(MD_Builder *b, MD_Node *node, U64 depth);

internal void
md_build_code_block(MD_Builder *b, MD_Node *node, Rng1U64 range, String8 code, MD_Language lang)
{
  MD_Style *style = b->style;
  TH_Colors *colors = b->colors;
  // parent container: group("code_block").relative().w_full() refined by style.code_block
  MD_Box *outer = md_box_alloc(b, MD_BoxKind_Block);
  outer->code_group = 1;
  outer->code_text = code;
  outer->key = range.min + 1;
  for EachIndex(k, 4) { outer->padding[k] = style->code_block_padding; outer->border[k] = md_px(style, 1); }
  outer->margin[MD_Side_Top] = style->code_block_margin;
  outer->margin[MD_Side_Bottom] = style->code_block_margin;
  outer->border_color = colors->border_variant;
  outer->background = colors->editor_background;
  outer->radius = style->code_block_radius;
  outer->text_font_size = style->code_block_font_size;
  outer->text_line_height_rel = style->code_block_line_height;
  md_push_div(b, outer, range);

  // code_block: overflow_x_scroll
  MD_Box *inner = md_box_alloc(b, MD_BoxKind_Block);
  inner->scroll_x = 1;
  inner->key = range.min + 1;
  inner->bfc = 1;
  MD_RunStyle s = *md_run_style(b);
  s.family = MD_FontFamily_Mono;
  s.bold = 0;
  s.italic = 0;
  s.underline = 0;
  s.strike = 0;
  s.link = 0;
  s.align = MD_Align_None;
  md_push_run_style(b, s);
  b->in_code_block = 1;
  md_push_div(b, inner, range);
  Temp scratch = scratch_begin(&b->arena, 1);
  MD_HighlightArray hl = md_highlights_from_code(scratch.arena, lang, code);
  md_push_highlighted_text(b, code, &hl);
  scratch_end(scratch);
  md_pop_div(b);
  b->in_code_block = 0;
  md_pop_run_style(b);

  // copy button: absolute top_1 right_1, visible on hover
  MD_Box *copy = md_box_alloc(b, MD_BoxKind_CopyButton);
  copy->absolute = 1;
  copy->fixed_width = md_rems(style, 22.f/16.f);
  copy->fixed_height = md_rems(style, 22.f/16.f);
  copy->margin[MD_Side_Top] = md_rems(style, 0.25f);
  copy->margin[MD_Side_Right] = md_rems(style, 0.25f);
  copy->key = outer->key;
  md_box_append(outer, copy);
  md_pop_div(b);
}

internal void
md_build_node(MD_Builder *b, MD_Node *node, U64 depth)
{
  MD_Style *style = b->style;
  TH_Colors *colors = b->colors;
  Rng1U64 range = node->range;

  //- root blocks: push_root_block
  MD_Box *root_wrapper = 0;
  if(depth == 0)
  {
    root_wrapper = md_box_alloc(b, MD_BoxKind_Block);
    root_wrapper->root_index = ++b->root_count;
    md_push_div(b, root_wrapper, range);
    MD_Box *pad = md_box_alloc(b, MD_BoxKind_Block);
    pad->padding[MD_Side_Left] = md_rems(style, 1.f);
    md_push_div(b, pad, range);
  }

  switch(node->kind)
  {
    default:{}break;

    case MD_NodeKind_Paragraph:
    {
      MD_Align align = MD_Align_None;
      md_push_paragraph_box(b, range, align);
      md_build_inlines(b, node->inlines);
      md_pop_text_block(b);
    }break;

    case MD_NodeKind_Heading:
    {
      md_push_heading_box(b, node->level, range, MD_Align_None);
      {
        String8 text = md_plain_text_from_inlines(b->arena, node->inlines);
        MD_HeadingNode *h = push_array(b->arena, MD_HeadingNode, 1);
        h->v.level = node->level;
        h->v.text = text;
        h->v.box = b->current;
        SLLQueuePush(b->first_heading, b->last_heading, h);
        b->heading_count += 1;
        String8 slug = md_heading_slug(b->arena, text);
        String8 unique = slug;
        U64 dup = 0;
        for(String8Node *n = b->anchor_names.first; n != 0; n = n->next) { if(str8_match(n->string, slug, 0)) { dup += 1; } }
        if(dup) { unique = push_str8f(b->arena, "%S-%I64u", slug, dup); }
        md_push_anchor(b, slug, b->current);
        if(dup) { md_push_anchor(b, unique, b->current); }
      }
      md_build_inlines(b, node->inlines);
      md_pop_text_block(b);
    }break;

    case MD_NodeKind_BlockQuote:
    {
      Vec4F32 border_color = colors->border;
      String8 icon = {0};
      String8 label = {0};
      switch(node->alert)
      {
        default:{}break;
        case MD_AlertKind_Note:      { border_color = colors->info;    icon = str8_lit("i"); label = str8_lit("Note"); }break;
        case MD_AlertKind_Tip:       { border_color = colors->success; icon = str8_lit("V"); label = str8_lit("Tip"); }break;
        case MD_AlertKind_Important: { border_color = colors->info;    icon = str8_lit("W"); label = str8_lit("Important"); }break;
        case MD_AlertKind_Warning:   { border_color = colors->warning; icon = str8_lit("W"); label = str8_lit("Warning"); }break;
        case MD_AlertKind_Caution:   { border_color = colors->error;   icon = str8_lit("s"); label = str8_lit("Caution"); }break;
      }
      MD_Box *q = md_box_alloc(b, MD_BoxKind_Block);
      q->padding[MD_Side_Left] = md_rems(style, 1.f);
      q->margin[MD_Side_Bottom] = style->paragraph_spacing;
      q->border[MD_Side_Left] = md_px(style, 4);
      q->border_color = border_color;
      MD_RunStyle s = *md_run_style(b);
      s.color = colors->text_muted;
      md_push_run_style(b, s);
      md_push_div(b, q, range);
      if(label.size)
      {
        // h_flex().gap_1().items_center().mb_1(): icon + bold label
        MD_Box *header = md_box_alloc(b, MD_BoxKind_Row);
        header->gap = md_rems(style, 0.25f);
        header->items_center = 1;
        header->margin[MD_Side_Bottom] = md_rems(style, 0.25f);
        md_box_append(q, header);
        MD_Box *ic = md_box_alloc(b, MD_BoxKind_Icon);
        ic->icon = icon;
        ic->icon_size = md_rems(style, 14.f/16.f);
        ic->icon_color = border_color;
        ic->fixed_width = ic->fixed_height = md_rems(style, 14.f/16.f);
        md_box_append(header, ic);
        md_make_label(b, header, label, MD_FontFamily_Sans, 1, border_color, md_rems(style, 0.875f));
      }
      md_build_children(b, node, depth+1);
      md_pop_div(b);
      md_pop_run_style(b);
    }break;

    case MD_NodeKind_CodeBlock:
    {
      MD_Language lang = node->fenced ? md_language_from_info(node->info) : MD_Language_None;
      md_build_code_block(b, node, range, node->text, lang);
    }break;

    case MD_NodeKind_HtmlBlock:
    {
      MD_Box *div = md_box_alloc(b, MD_BoxKind_Block);
      md_push_div(b, div, range);
      md_build_html_block(b, node);
      md_pop_div(b);
    }break;

    case MD_NodeKind_List:
    {
      b->list_depth += 1;
      MD_Box *l = md_box_alloc(b, MD_BoxKind_Block);
      l->padding[MD_Side_Left] = md_rems(style, 0.625f);
      if(b->list_depth == 1) { l->margin[MD_Side_Bottom] = style->list_spacing; }
      md_push_div(b, l, range);
      U64 number = node->start_number;
      for(MD_Node *item = node->first; item != 0; item = item->next)
      {
        Rng1U64 item_range = item->range;
        // push_markdown_list_item: div().mb_1().gap_1().line_height(..).h_flex().items_start()
        MD_Box *row = md_box_alloc(b, MD_BoxKind_Row);
        row->margin[MD_Side_Bottom] = md_rems(style, 0.25f);
        row->gap = md_rems(style, 0.25f);
        row->text_line_height_rel = style->paragraph_line_height;
        md_push_div(b, row, item_range);
        if(item->is_task)
        {
          F32 font_size = md_box_font_size(b, row);
          MD_Box *slot = md_box_alloc(b, MD_BoxKind_Row);
          slot->items_center = 1;
          slot->fixed_height = md_snap(style->paragraph_line_height*style->rem);
          (void)font_size;
          md_box_append(row, slot);
          MD_Box *cb = md_box_alloc(b, MD_BoxKind_Checkbox);
          cb->checked = item->task_checked;
          cb->key = item->task_offset + 1;
          cb->fixed_width = cb->fixed_height = md_px(style, 20);
          md_box_append(slot, cb);
        }
        else
        {
          MD_Box *bullet = md_box_alloc(b, MD_BoxKind_Block);
          md_box_append(row, bullet);
          String8 marker = node->ordered ? push_str8f(b->arena, "%I64u.", number) : str8_lit("\xe2\x80\xa2");
          md_make_label(b, bullet, marker, MD_FontFamily_Sans, 0, colors->text, 0);
        }
        number += 1;
        MD_Box *content = md_box_alloc(b, MD_BoxKind_Block);
        content->flex_1 = 1;
        content->bfc = 1;
        md_push_div(b, content, item_range);
        if(node->tight)
        {
          // tight lists: paragraphs are not wrapped in a paragraph div
          for(MD_Node *c = item->first; c != 0; c = c->next)
          {
            if(c->kind == MD_NodeKind_Paragraph)
            {
              md_build_inlines(b, c->inlines);
              md_flush_text(b);
            }
            else
            {
              md_build_node(b, c, depth+1);
            }
          }
        }
        else
        {
          md_build_children(b, item, depth+1);
        }
        md_pop_div(b);
        md_pop_div(b);
      }
      md_pop_div(b);
      b->list_depth -= 1;
    }break;

    case MD_NodeKind_Rule:
    {
      MD_Box *rule = md_box_alloc(b, MD_BoxKind_Block);
      rule->border[MD_Side_Bottom] = md_px(style, 1);
      rule->border_color = colors->border;
      rule->margin[MD_Side_Top] = style->paragraph_spacing;
      rule->margin[MD_Side_Bottom] = style->paragraph_spacing;
      md_push_div(b, rule, range);
      md_pop_div(b);
    }break;

    case MD_NodeKind_Table:
    {
      MD_Box *wrapper = md_box_alloc(b, MD_BoxKind_Row);
      md_push_div(b, wrapper, range);
      MD_Box *grid = md_box_alloc(b, MD_BoxKind_Grid);
      grid->grid_columns = node->column_count;
      grid->margin[MD_Side_Bottom] = md_rems(style, 0.5f);
      for EachIndex(k, 4) { grid->border[k] = md_px(style, 1.5f); }
      grid->border_color = colors->border;
      grid->radius = md_rems(style, 0.25f);
      grid->scroll_x = 1;
      grid->shrink_to_fit = 1;
      grid->key = range.min + 1;
      md_push_div(b, grid, range);
      b->table_aligns = node->aligns;
      b->table_row = 0;
      b->in_table = 1;
      for(MD_Node *row = node->first; row != 0; row = row->next)
      {
        B32 head = (row->kind == MD_NodeKind_TableHead);
        if(head)
        {
          MD_RunStyle s = *md_run_style(b);
          s.bold = 1;
          md_push_run_style(b, s);
        }
        U64 col = 0;
        for(MD_Node *cell = row->first; cell != 0; cell = cell->next, col += 1)
        {
          MD_Align align = (col < node->column_count) ? node->aligns[col] : MD_Align_None;
          MD_Box *c = md_box_alloc(b, MD_BoxKind_Block);
          c->bfc = 1;
          if(col > 0) { c->border[MD_Side_Left] = md_px(style, 1); }
          if(b->table_row > 0) { c->border[MD_Side_Top] = md_px(style, 1); }
          c->border_color = colors->border;
          c->padding[MD_Side_Left] = c->padding[MD_Side_Right] = style->table_cell_padding.x;
          c->padding[MD_Side_Top] = c->padding[MD_Side_Bottom] = style->table_cell_padding.y;
          if(head) { c->background = colors->title_bar_background; }
          else if(b->table_row % 2 == 1) { c->background = colors->panel_background; }
          c->align = align;
          MD_RunStyle s = *md_run_style(b);
          s.align = align;
          md_push_run_style(b, s);
          md_push_div(b, c, cell->range);
          md_build_inlines(b, cell->inlines);
          md_pop_div(b);
          md_pop_run_style(b);
        }
        if(head) { md_pop_run_style(b); }
        else { b->table_row += 1; } // TableHead has no row end, like pulldown-cmark
      }
      b->in_table = 0;
      md_pop_div(b);
      md_pop_div(b);
    }break;

    case MD_NodeKind_FootnoteDefinition:
    {
      if(!b->rendered_footnote_separator)
      {
        b->rendered_footnote_separator = 1;
        MD_Box *sep = md_box_alloc(b, MD_BoxKind_Block);
        sep->border[MD_Side_Top] = md_px(style, 1);
        sep->margin[MD_Side_Top] = md_rems(style, 0.5f);
        sep->border_color = colors->border;
        md_push_div(b, sep, range);
        md_pop_div(b);
      }
      MD_Box *row = md_box_alloc(b, MD_BoxKind_Row);
      row->padding[MD_Side_Top] = md_rems(style, 0.25f);
      row->margin[MD_Side_Bottom] = md_rems(style, 0.25f);
      row->text_line_height_abs = md_rems(style, 1.3f);
      row->text_font_size = md_rems(style, 0.85f);
      row->gap = md_rems(style, 0.5f);
      row->key = range.min + 1;
      md_push_div(b, row, range);
      md_push_anchor(b, push_str8f(b->arena, "footnote:%S", node->label), row);
      MD_Box *label = md_box_alloc(b, MD_BoxKind_Block);
      md_box_append(row, label);
      md_make_label(b, label, push_str8f(b->arena, "%S.", node->label), MD_FontFamily_Sans, 0, colors->text, md_rems(style, 0.85f));
      MD_Box *content = md_box_alloc(b, MD_BoxKind_Block);
      content->flex_1 = 1;
      content->bfc = 1;
      content->key = range.min + 1;
      md_push_div(b, content, range);
      md_build_children(b, node, depth+1);
      md_pop_div(b);
      md_pop_div(b);
    }break;

    case MD_NodeKind_MetadataBlock:
    {
      if(node->meta_count)
      {
        MD_Box *grid = md_box_alloc(b, MD_BoxKind_Grid);
        grid->grid_columns = 2;
        grid->grid_equal_columns = 1;
        grid->margin[MD_Side_Bottom] = md_rems(style, 0.5f);
        for EachIndex(k, 4) { grid->border[k] = md_px(style, 1); }
        grid->border_color = colors->border;
        grid->radius = md_rems(style, 0.25f);
        md_push_div(b, grid, range);
        for EachIndex(r, node->meta_count)
        {
          for EachIndex(k, 2)
          {
            B32 is_key = (k == 0);
            MD_Box *c = md_box_alloc(b, MD_BoxKind_Block);
            c->bfc = 1;
            c->padding[MD_Side_Left] = c->padding[MD_Side_Right] = md_rems(style, 0.5f);
            c->padding[MD_Side_Top] = c->padding[MD_Side_Bottom] = md_rems(style, 0.25f);
            c->border_color = colors->border;
            if(r > 0) { c->border[MD_Side_Top] = md_px(style, 1); }
            if(!is_key) { c->border[MD_Side_Left] = md_px(style, 1); }
            if(is_key) { c->background = colors->panel_background; }
            md_push_div(b, c, range);
            MD_RunStyle s = *md_run_style(b);
            if(is_key) { s.color = colors->text_muted; s.bold = 1; }
            md_push_run_style(b, s);
            md_push_text(b, is_key ? node->meta_keys[r] : node->meta_values[r]);
            md_pop_run_style(b);
            md_pop_div(b);
          }
        }
        md_pop_div(b);
      }
      else
      {
        md_build_code_block(b, node, range, node->text, MD_Language_None);
      }
    }break;
  }

  //- pop_root_block: the gutter bar
  if(root_wrapper)
  {
    md_pop_div(b);
    MD_Box *bar = md_box_alloc(b, MD_BoxKind_Gutter);
    bar->absolute = 1;
    bar->fixed_width = md_px(style, 4);
    bar->radius = md_rems(style, 0.125f);
    bar->root_index = root_wrapper->root_index;
    md_box_append(root_wrapper, bar);
    md_pop_div(b);
  }
}

internal void
md_build_children(MD_Builder *b, MD_Node *node, U64 depth)
{
  for(MD_Node *c = node->first; c != 0; c = c->next)
  {
    md_build_node(b, c, depth);
  }
}

internal MD_Element *
md_element_build(Arena *arena, MD_Document *doc, MD_Style *style, String8 base_dir)
{
  MD_Element *el = push_array(arena, MD_Element, 1);
  el->arena = arena;
  MD_Builder b_ = {0};
  MD_Builder *b = &b_;
  b->arena = arena;
  b->style = style;
  b->colors = &style->theme->colors;
  b->doc = doc;
  b->el = el;
  b->base_dir = base_dir;

  // base_text_style: preview font, 1rem, `text` color
  MD_RunStyle base = {0};
  base.family = MD_FontFamily_Sans;
  base.color = b->colors->text;
  md_push_run_style(b, base);

  // container_style: text.font_size = 1rem
  MD_Box *root = md_box_alloc(b, MD_BoxKind_Block);
  root->text_font_size = style->rem;
  root->bfc = 1;
  b->current = root;
  el->root = root;
  md_build_children(b, doc->root, 0);
  md_flush_text(b);

  //- links
  el->link_count = b->link_urls.node_count;
  el->links = push_array(arena, MD_Link, el->link_count + 1);
  {
    U64 i = 0;
    for(String8Node *n = b->link_urls.first; n != 0; n = n->next, i += 1) { el->links[i].url = n->string; }
  }

  //- selection space: texts in document order, one separator between each
  el->texts = b->texts;
  el->text_count = b->text_count;
  U64 offset = 0;
  for EachIndex(i, el->text_count)
  {
    el->texts[i]->global_start = offset;
    offset += el->texts[i]->string.size + 1;
  }
  el->text_total = offset;
  el->root_block_count = b->root_count;
  el->anchor_count = b->anchor_count;
  el->anchor_names = push_array(arena, String8, b->anchor_count + 1);
  el->anchor_boxes = push_array(arena, MD_Box *, b->anchor_count + 1);
  {
    U64 i = 0;
    for(String8Node *n = b->anchor_names.first; n != 0; n = n->next, i += 1)
    {
      el->anchor_names[i] = n->string;
      el->anchor_boxes[i] = b->anchor_boxes[i];
    }
  }
  el->heading_count = b->heading_count;
  el->headings = push_array(arena, MD_Heading, b->heading_count + 1);
  {
    U64 i = 0;
    for(MD_HeadingNode *n = b->first_heading; n != 0; n = n->next, i += 1) { el->headings[i] = n->v; }
  }
  return el;
}

////////////////////////////////
//~ Text Layout (gpui LineWrapper)

typedef struct MD_TextFragNode MD_TextFragNode;
struct MD_TextFragNode
{
  MD_TextFragNode *next;
  MD_TextFrag v;
};

typedef struct MD_TextLineNode MD_TextLineNode;
struct MD_TextLineNode
{
  MD_TextLineNode *next;
  MD_TextLine v;
};

internal U64
md_run_index_at(MD_Text *t, U64 pos)
{
  for EachIndex(i, t->run_count)
  {
    if(pos >= t->runs[i].start && pos < t->runs[i].end) { return i; }
  }
  return t->run_count ? t->run_count-1 : 0;
}

internal F32
md_text_range_width(MD_Style *style, MD_Text *t, U64 start, U64 end)
{
  F32 w = 0;
  for(U64 i = start; i < end;)
  {
    U64 r = md_run_index_at(t, i);
    U64 e = Min(end, t->runs[r].end);
    if(e <= i) { e = end; }
    w += md_text_width(md_run_font(style, &t->runs[r]), t->font_size, str8_substr(t->string, r1u64(i, e)));
    i = e;
  }
  return w;
}

internal void
md_text_layout(Arena *arena, MD_Style *style, MD_Text *t, F32 max_width)
{
  if(t->lines && t->layout_width == max_width) { return; }
  t->layout_width = max_width;
  Temp scratch = scratch_begin(&arena, 1);
  String8 s = t->string;
  B32 wrap = t->wrap;

  //- metrics: the max ascent/descent over the runs' fonts
  t->ascent = 0;
  t->descent = 0;
  for EachIndex(i, t->run_count)
  {
    FP_Metrics m = fnt_fp_metrics_from_tag(md_run_font(style, &t->runs[i]));
    if(m.design_units_per_em <= 0) { continue; }
    F32 a = m.ascent / m.design_units_per_em * t->font_size;
    F32 d = m.descent / m.design_units_per_em * t->font_size;
    t->ascent = Max(t->ascent, a);
    t->descent = Max(t->descent, d);
  }

  MD_TextLineNode *first_line = 0, *last_line = 0;
  U64 line_count = 0;
  MD_TextFragNode *first_frag = 0, *last_frag = 0;
  U64 frag_count = 0;

  MD_TextLine line = {0};
  F32 x = 0;
  F32 content_x = 0;             // x after the last non-space
  U64 content_end = 0;
#define MD_EmitFrag(run_, start_, end_, x_, w_) do { \
MD_TextFragNode *_n = push_array(scratch.arena, MD_TextFragNode, 1); \
_n->v = (MD_TextFrag){(run_), (start_), (end_), (x_), (w_)}; \
SLLQueuePush(first_frag, last_frag, _n); frag_count += 1; line.frag_count += 1; \
} while(0)
#define MD_EndLine(next_start, keep_trailing_spaces) do { \
line.end = content_end > line.start ? content_end : line.start; \
line.width = (keep_trailing_spaces) ? x : content_x; \
MD_TextLineNode *_l = push_array(scratch.arena, MD_TextLineNode, 1); \
_l->v = line; SLLQueuePush(first_line, last_line, _l); line_count += 1; \
MemoryZeroStruct(&line); line.start = (next_start); line.first_frag = frag_count; \
x = 0; content_x = 0; content_end = (next_start); \
} while(0)

  line.start = 0;
  line.first_frag = 0;
  for(U64 i = 0; i < s.size;)
  {
    U8 c = s.str[i];
    if(c == '\n')
    {
      if(content_end < i && x > content_x) { /* trailing spaces */ }
      MD_EndLine(i+1, 1);
      i += 1;
      continue;
    }
    B32 space = (c == ' ');
    U64 e = i;
    for(;e < s.size && s.str[e] != '\n' && ((s.str[e] == ' ') == space); e += 1);

    // the token as pieces per run
    F32 token_w = md_text_range_width(style, t, i, e);
    if(!space && wrap && x > 0 && content_x > 0 && x + token_w > max_width)
    {
      MD_EndLine(i, 0);
    }
    if(!space && wrap && x == 0 && token_w > max_width)
    {
      // a word wider than the line breaks between characters
      U64 k = i;
      for(;k < e;)
      {
        U64 fit_end = k;
        F32 fit_w = 0;
        for(U64 m = k; m < e;)
        {
          UnicodeDecode d = utf8_decode(s.str+m, e-m);
          U64 next = m + Max(1, d.inc);
          F32 w = md_text_range_width(style, t, k, next);
          if(w > max_width - x && fit_end > k) { break; }
          fit_end = next;
          fit_w = w;
          m = next;
        }
        for(U64 p = k; p < fit_end;)
        {
          U64 r = md_run_index_at(t, p);
          U64 pe = Min(fit_end, t->runs[r].end);
          if(pe <= p) { pe = fit_end; }
          F32 pw = md_text_range_width(style, t, p, pe);
          MD_EmitFrag(r, p, pe, x, pw);
          x += pw;
          p = pe;
        }
        content_x = x;
        content_end = fit_end;
        k = fit_end;
        if(k < e) { MD_EndLine(k, 0); }
      }
      i = e;
      continue;
    }
    for(U64 p = i; p < e;)
    {
      U64 r = md_run_index_at(t, p);
      U64 pe = Min(e, t->runs[r].end);
      if(pe <= p) { pe = e; }
      F32 pw = md_text_range_width(style, t, p, pe);
      // merge into the previous fragment of the same run
      if(last_frag && line.frag_count > 0 && last_frag->v.run == r && last_frag->v.end == p)
      {
        last_frag->v.end = pe;
        last_frag->v.width += pw;
      }
      else
      {
        MD_EmitFrag(r, p, pe, x, pw);
      }
      x += pw;
      p = pe;
    }
    if(!space) { content_x = x; content_end = e; }
    i = e;
  }
  MD_EndLine(s.size, 1);
#undef MD_EmitFrag
#undef MD_EndLine

  t->line_count = line_count;
  t->lines = push_array(arena, MD_TextLine, line_count);
  t->frag_count = frag_count;
  t->frags = push_array(arena, MD_TextFrag, frag_count);
  t->width = 0;
  {
    U64 i = 0;
    for(MD_TextLineNode *n = first_line; n != 0; n = n->next, i += 1) { t->lines[i] = n->v; t->width = Max(t->width, n->v.width); }
    i = 0;
    for(MD_TextFragNode *n = first_frag; n != 0; n = n->next, i += 1) { t->frags[i] = n->v; }
  }
  scratch_end(scratch);
}

internal F32
md_text_max_content_width(Arena *arena, MD_Style *style, MD_Text *t)
{
  B32 wrap = t->wrap;
  t->wrap = 0;
  t->lines = 0;
  md_text_layout(arena, style, t, (1e30f));
  t->wrap = wrap;
  F32 w = t->width;
  t->lines = 0;
  return w;
}

////////////////////////////////
//~ Box Layout (taffy stand-in)

typedef struct MD_LayoutCtx MD_LayoutCtx;
struct MD_LayoutCtx
{
  Arena *arena;
  MD_Style *style;
};

internal F32 md_layout_box(MD_LayoutCtx *c, MD_Box *box, F32 x, F32 y, F32 w);

internal B32
md_box_in_flow(MD_Box *box)
{
  return !box->absolute;
}

internal void
md_compute_margins(MD_Box *box)
{
  for(MD_Box *c = box->first; c != 0; c = c->next) { md_compute_margins(c); }
  box->eff_margin_top = box->margin[MD_Side_Top];
  box->eff_margin_bottom = box->margin[MD_Side_Bottom];
  box->collapse_top = 0;
  box->collapse_bottom = 0;
  if(box->kind != MD_BoxKind_Block || box->bfc || box->scroll_x) { return; }
  MD_Box *first = 0, *last = 0;
  for(MD_Box *c = box->first; c != 0; c = c->next)
  {
    if(!md_box_in_flow(c)) { continue; }
    if(first == 0) { first = c; }
    last = c;
  }
  if(first && box->border[MD_Side_Top] == 0 && box->padding[MD_Side_Top] == 0)
  {
    box->collapse_top = 1;
    box->eff_margin_top = Max(box->eff_margin_top, first->eff_margin_top);
  }
  if(last && box->border[MD_Side_Bottom] == 0 && box->padding[MD_Side_Bottom] == 0 && box->fixed_height == 0)
  {
    box->collapse_bottom = 1;
    box->eff_margin_bottom = Max(box->eff_margin_bottom, last->eff_margin_bottom);
  }
}

internal F32
md_box_horizontal_extras(MD_Box *box)
{
  return box->padding[MD_Side_Left] + box->padding[MD_Side_Right] + box->border[MD_Side_Left] + box->border[MD_Side_Right];
}

internal Vec2F32
md_image_size(MD_LayoutCtx *c, MD_Box *box, F32 max_w)
{
  F32 w = box->fixed_width;
  F32 h = box->fixed_height;
  F32 iw = box->image ? (F32)box->image->size.x*c->style->scale : 0;
  F32 ih = box->image ? (F32)box->image->size.y*c->style->scale : 0;
  if(w == 0 && h == 0) { w = iw; h = ih; }
  else if(w == 0 && ih > 0) { w = iw * h / ih; }
  else if(h == 0 && iw > 0) { h = ih * w / iw; }
  if(w > max_w && w > 0) { h = h * max_w / w; w = max_w; }
  return v2f32(w, h);
}

// max-content width of a box's border box (no wrapping)
internal F32
md_max_content_width(MD_LayoutCtx *c, MD_Box *box)
{
  F32 extras = md_box_horizontal_extras(box);
  if(box->fixed_width > 0 && box->kind != MD_BoxKind_Image) { return box->fixed_width; }
  switch(box->kind)
  {
    case MD_BoxKind_Text: return md_text_max_content_width(c->arena, c->style, box->text);
    case MD_BoxKind_Image: return md_image_size(c, box, (1e30f)).x;
    case MD_BoxKind_Checkbox:
    case MD_BoxKind_Icon:
    case MD_BoxKind_CopyButton:
    case MD_BoxKind_Gutter: return box->fixed_width;
    case MD_BoxKind_Row:
    case MD_BoxKind_Wrap:
    {
      F32 w = 0;
      U64 n = 0;
      for(MD_Box *ch = box->first; ch != 0; ch = ch->next)
      {
        if(!md_box_in_flow(ch)) { continue; }
        w += md_max_content_width(c, ch) + ch->margin[MD_Side_Left] + ch->margin[MD_Side_Right];
        n += 1;
      }
      if(n > 1) { w += box->gap*(n-1); }
      return w + extras;
    }
    case MD_BoxKind_Grid:
    {
      Temp scratch = scratch_begin(&c->arena, 1);
      U64 cols = Max(1, box->grid_columns);
      F32 *col_w = push_array(scratch.arena, F32, cols);
      for(MD_Box *row = box->first; row != 0;)
      {
        U64 k = 0;
        for(MD_Box *cell = row; cell != 0 && k < cols; cell = cell->next, k += 1)
        {
          col_w[k] = Max(col_w[k], md_max_content_width(c, cell));
          row = cell->next;
        }
      }
      F32 w = 0;
      if(box->grid_equal_columns)
      {
        F32 m = 0;
        for EachIndex(k, cols) { m = Max(m, col_w[k]); }
        w = m*cols;
      }
      else
      {
        for EachIndex(k, cols) { w += col_w[k]; }
      }
      scratch_end(scratch);
      return w + extras;
    }
    default:
    case MD_BoxKind_Block:
    {
      F32 w = 0;
      for(MD_Box *ch = box->first; ch != 0; ch = ch->next)
      {
        if(!md_box_in_flow(ch)) { continue; }
        w = Max(w, md_max_content_width(c, ch) + ch->margin[MD_Side_Left] + ch->margin[MD_Side_Right]);
      }
      return w + extras;
    }
  }
}

internal void
md_layout_absolute_children(MD_LayoutCtx *c, MD_Box *box)
{
  for(MD_Box *ch = box->first; ch != 0; ch = ch->next)
  {
    if(!ch->absolute) { continue; }
    if(ch->kind == MD_BoxKind_Gutter)
    {
      // .h_full().w(px(4)).absolute().left_0().top_0()
      ch->rect = r2f32p(box->rect.x0, box->rect.y0, box->rect.x0 + ch->fixed_width, box->rect.y1);
    }
    else
    {
      // top_N right_N
      F32 x1 = box->rect.x1 - ch->margin[MD_Side_Right];
      F32 y0 = box->rect.y0 + ch->margin[MD_Side_Top];
      ch->rect = r2f32p(x1 - ch->fixed_width, y0, x1, y0 + ch->fixed_height);
    }
  }
}

internal F32
md_layout_block(MD_LayoutCtx *c, MD_Box *box, F32 x, F32 y, F32 w)
{
  F32 inner_x = x + box->border[MD_Side_Left] + box->padding[MD_Side_Left];
  F32 inner_w = w - md_box_horizontal_extras(box);
  F32 cy = y + box->border[MD_Side_Top] + box->padding[MD_Side_Top];
  F32 prev_mb = 0;
  B32 first = 1;
  F32 child_avail = box->scroll_x ? (1e30f) : inner_w;
  F32 content_w = 0;
  for(MD_Box *ch = box->first; ch != 0; ch = ch->next)
  {
    if(!md_box_in_flow(ch)) { continue; }
    F32 top = ch->eff_margin_top;
    F32 gap = first ? (box->collapse_top ? 0 : top) : Max(prev_mb, top);
    cy += gap;
    F32 cw = ch->fixed_width > 0 ? ch->fixed_width : (box->scroll_x ? Max(inner_w, md_max_content_width(c, ch)) : inner_w - ch->margin[MD_Side_Left] - ch->margin[MD_Side_Right]);
    (void)child_avail;
    F32 h = md_layout_box(c, ch, inner_x + ch->margin[MD_Side_Left], cy, cw);
    content_w = Max(content_w, cw);
    cy += h;
    prev_mb = ch->eff_margin_bottom;
    first = 0;
  }
  if(!first && !box->collapse_bottom) { cy += prev_mb; }
  F32 h = cy - y + box->padding[MD_Side_Bottom] + box->border[MD_Side_Bottom];
  if(box->fixed_height > 0) { h = box->fixed_height; }
  box->content_width = content_w;
  return h;
}

internal F32
md_layout_row(MD_LayoutCtx *c, MD_Box *box, F32 x, F32 y, F32 w)
{
  F32 inner_x = x + box->border[MD_Side_Left] + box->padding[MD_Side_Left];
  F32 inner_y = y + box->border[MD_Side_Top] + box->padding[MD_Side_Top];
  F32 inner_w = w - md_box_horizontal_extras(box);
  // fixed children first, then flex_1 children share the rest
  F32 used = 0;
  U64 n = 0, flex_n = 0;
  for(MD_Box *ch = box->first; ch != 0; ch = ch->next)
  {
    if(!md_box_in_flow(ch)) { continue; }
    n += 1;
    if(ch->flex_1) { flex_n += 1; continue; }
    F32 mw = md_max_content_width(c, ch);
    if(ch->shrink_to_fit) { mw = Min(mw, inner_w - used); }
    ch->rect.x1 = mw; // stash
    used += mw + ch->margin[MD_Side_Left] + ch->margin[MD_Side_Right];
  }
  if(n > 1) { used += box->gap*(n-1); }
  F32 flex_w = flex_n ? Max(0, (inner_w - used)/flex_n) : 0;
  F32 cx = inner_x;
  F32 row_h = 0;
  for(MD_Box *ch = box->first; ch != 0; ch = ch->next)
  {
    if(!md_box_in_flow(ch)) { continue; }
    F32 cw = ch->flex_1 ? flex_w : ch->rect.x1;
    cx += ch->margin[MD_Side_Left];
    F32 h = md_layout_box(c, ch, cx, inner_y + ch->margin[MD_Side_Top], cw);
    row_h = Max(row_h, h + ch->margin[MD_Side_Top] + ch->margin[MD_Side_Bottom]);
    cx += cw + ch->margin[MD_Side_Right] + box->gap;
  }
  if(box->fixed_height > 0) { row_h = box->fixed_height - (box->padding[MD_Side_Top] + box->padding[MD_Side_Bottom] + box->border[MD_Side_Top] + box->border[MD_Side_Bottom]); }
  if(box->items_center)
  {
    for(MD_Box *ch = box->first; ch != 0; ch = ch->next)
    {
      if(!md_box_in_flow(ch)) { continue; }
      F32 h = ch->rect.y1 - ch->rect.y0;
      F32 dy = floor_f32((row_h - h)/2) - (ch->rect.y0 - inner_y);
      // shift the subtree
      MD_Box *stack[256]; U64 top = 0;
      stack[top++] = ch;
      for(;top > 0;)
      {
        MD_Box *bx = stack[--top];
        bx->rect.y0 += dy; bx->rect.y1 += dy;
        for(MD_Box *k = bx->first; k != 0 && top < ArrayCount(stack); k = k->next) { stack[top++] = k; }
      }
    }
  }
  return row_h + box->padding[MD_Side_Top] + box->padding[MD_Side_Bottom] + box->border[MD_Side_Top] + box->border[MD_Side_Bottom];
}

internal void
md_shift_subtree(MD_Box *box, F32 dx, F32 dy)
{
  box->rect.x0 += dx; box->rect.x1 += dx;
  box->rect.y0 += dy; box->rect.y1 += dy;
  for(MD_Box *k = box->first; k != 0; k = k->next) { md_shift_subtree(k, dx, dy); }
}

internal F32
md_layout_wrap(MD_LayoutCtx *c, MD_Box *box, F32 x, F32 y, F32 w)
{
  // flex_row().flex_wrap().items_start(), justified by text alignment
  F32 inner_x = x + box->border[MD_Side_Left] + box->padding[MD_Side_Left];
  F32 inner_y = y + box->border[MD_Side_Top] + box->padding[MD_Side_Top];
  F32 inner_w = w - md_box_horizontal_extras(box);
  F32 cx = 0, cy = 0, line_h = 0;
  MD_Box *line_first = box->first;
#define MD_FinishLine(end_child) do { \
F32 slack = inner_w - cx; \
F32 dx = (box->align == MD_Align_Center) ? floor_f32(slack/2) : (box->align == MD_Align_Right) ? slack : 0; \
if(dx > 0) { for(MD_Box *_k = line_first; _k != (end_child); _k = _k->next) { if(md_box_in_flow(_k)) { md_shift_subtree(_k, dx, 0); } } } \
} while(0)
  for(MD_Box *ch = box->first; ch != 0; ch = ch->next)
  {
    if(!md_box_in_flow(ch)) { continue; }
    if(ch->line_break)
    {
      MD_FinishLine(ch);
      cy += line_h; cx = 0; line_h = 0;
      ch->rect = r2f32p(inner_x, inner_y + cy, inner_x + inner_w, inner_y + cy);
      line_first = ch->next;
      continue;
    }
    F32 mw = md_max_content_width(c, ch);
    F32 outer = mw + ch->margin[MD_Side_Left] + ch->margin[MD_Side_Right];
    if(cx > 0 && cx + outer > inner_w)
    {
      MD_FinishLine(ch);
      cy += line_h; cx = 0; line_h = 0;
      line_first = ch;
    }
    F32 cw = Min(mw, inner_w - cx - ch->margin[MD_Side_Left] - ch->margin[MD_Side_Right]);
    cw = Max(cw, 0);
    F32 h = md_layout_box(c, ch, inner_x + cx + ch->margin[MD_Side_Left], inner_y + cy + ch->margin[MD_Side_Top], cw);
    line_h = Max(line_h, h + ch->margin[MD_Side_Top] + ch->margin[MD_Side_Bottom]);
    cx += cw + ch->margin[MD_Side_Left] + ch->margin[MD_Side_Right];
  }
  MD_FinishLine((MD_Box *)0);
#undef MD_FinishLine
  cy += line_h;
  return cy + box->padding[MD_Side_Top] + box->padding[MD_Side_Bottom] + box->border[MD_Side_Top] + box->border[MD_Side_Bottom];
}

internal F32
md_layout_grid(MD_LayoutCtx *c, MD_Box *box, F32 x, F32 y, F32 w)
{
  Temp scratch = scratch_begin(&c->arena, 1);
  U64 cols = Max(1, box->grid_columns);
  F32 inner_x = x + box->border[MD_Side_Left] + box->padding[MD_Side_Left];
  F32 inner_y = y + box->border[MD_Side_Top] + box->padding[MD_Side_Top];
  F32 inner_w = w - md_box_horizontal_extras(box);
  F32 *col_w = push_array(scratch.arena, F32, cols);
  if(box->grid_equal_columns)
  {
    for EachIndex(k, cols) { col_w[k] = floor_f32(inner_w/cols); }
  }
  else
  {
    U64 k = 0;
    for(MD_Box *cell = box->first; cell != 0; cell = cell->next, k = (k+1) % cols)
    {
      col_w[k] = Max(col_w[k], md_max_content_width(c, cell));
    }
  }
  F32 total_w = 0;
  for EachIndex(k, cols) { total_w += col_w[k]; }
  F32 cy = inner_y;
  for(MD_Box *row_first = box->first; row_first != 0;)
  {
    F32 cx = inner_x;
    F32 row_h = 0;
    MD_Box *cell = row_first;
    for(U64 k = 0; k < cols && cell != 0; k += 1, cell = cell->next)
    {
      F32 h = md_layout_box(c, cell, cx, cy, col_w[k]);
      row_h = Max(row_h, h);
      cx += col_w[k];
    }
    // cells stretch to the row (h_full) and center their content vertically
    cell = row_first;
    for(U64 k = 0; k < cols && cell != 0; k += 1, cell = cell->next)
    {
      F32 h = cell->rect.y1 - cell->rect.y0;
      if(h < row_h)
      {
        F32 dy = floor_f32((row_h - h)/2);
        for(MD_Box *ch = cell->first; ch != 0; ch = ch->next) { md_shift_subtree(ch, 0, dy); }
        cell->rect.y1 = cell->rect.y0 + row_h;
      }
    }
    row_first = cell;
    cy += row_h;
  }
  box->content_width = total_w + md_box_horizontal_extras(box);
  scratch_end(scratch);
  return cy - y + box->padding[MD_Side_Bottom] + box->border[MD_Side_Bottom];
}

internal F32
md_layout_box(MD_LayoutCtx *c, MD_Box *box, F32 x, F32 y, F32 w)
{
  F32 h = 0;
  switch(box->kind)
  {
    case MD_BoxKind_Text:
    {
      MD_Text *t = box->text;
      t->lines = 0;
      md_text_layout(c->arena, c->style, t, w);
      h = t->line_count * t->line_height;
    }break;
    case MD_BoxKind_Image:
    {
      Vec2F32 sz = md_image_size(c, box, w);
      w = sz.x;
      h = sz.y;
    }break;
    case MD_BoxKind_Checkbox:
    case MD_BoxKind_Icon:
    case MD_BoxKind_CopyButton:
    {
      w = box->fixed_width;
      h = box->fixed_height;
    }break;
    case MD_BoxKind_Row:  { h = md_layout_row(c, box, x, y, w); }break;
    case MD_BoxKind_Wrap: { h = md_layout_wrap(c, box, x, y, w); }break;
    case MD_BoxKind_Grid:
    {
      F32 content = md_max_content_width(c, box);
      if(box->shrink_to_fit) { /* w already min(content, avail) */ }
      (void)content;
      h = md_layout_grid(c, box, x, y, w);
    }break;
    default:
    case MD_BoxKind_Block: { h = md_layout_block(c, box, x, y, w); }break;
  }
  box->rect = r2f32p(x, y, x + w, y + h);
  md_layout_absolute_children(c, box);
  return h;
}

internal void
md_element_layout(MD_Element *el, MD_Style *style, F32 width)
{
  MD_LayoutCtx c = {el->arena, style};
  md_compute_margins(el->root);
  el->height = md_layout_box(&c, el->root, 0, 0, width);
  el->width = width;
}

////////////////////////////////
//~ Interaction State

internal F32 *
md_scroll_offset_ptr(MD_Interaction *ix, Arena *arena, U64 key)
{
  for(MD_ScrollState *s = ix->first_scroll; s != 0; s = s->next)
  {
    if(s->key == key) { return &s->offset; }
  }
  if(arena == 0) { return 0; }
  MD_ScrollState *s = push_array(arena, MD_ScrollState, 1);
  s->key = key;
  SLLStackPush(ix->first_scroll, s);
  return &s->offset;
}

internal F32
md_scroll_offset(MD_Interaction *ix, U64 key)
{
  F32 *p = md_scroll_offset_ptr(ix, 0, key);
  return p ? *p : 0;
}

////////////////////////////////
//~ Paint

global MD_Style *md_hit_style = 0;

typedef struct MD_PaintCtx MD_PaintCtx;
struct MD_PaintCtx
{
  MD_Element *el;
  MD_Style *style;
  MD_Interaction *ix;
  TH_Colors *colors;
  Rng2F32 clip;
  MD_Hit hover;
};

internal void
md_fill(Rng2F32 r, Vec4F32 srgb, F32 radius)
{
  if(srgb.w <= 0) { return; }
  dr_rect(r, th_linear(srgb), radius, 0, radius > 0 ? 0.5f : 0);
}

internal F32
md_text_x_for_offset(MD_Style *style, MD_Text *t, MD_TextLine *line, U64 offset)
{
  if(offset <= line->start) { return 0; }
  for EachIndex(i, line->frag_count)
  {
    MD_TextFrag *f = &t->frags[line->first_frag + i];
    if(offset <= f->start) { return f->x; }
    if(offset < f->end)
    {
      return f->x + md_text_width(md_run_font(style, &t->runs[f->run]), t->font_size, str8_substr(t->string, r1u64(f->start, offset)));
    }
  }
  return line->width;
}

internal F32
md_text_line_x0(MD_Text *t, MD_TextLine *line, F32 box_w)
{
  if(t->align == MD_Align_Center) { return floor_f32((box_w - line->width)/2); }
  if(t->align == MD_Align_Right) { return box_w - line->width; }
  return 0;
}

internal void
md_paint_text(MD_PaintCtx *p, MD_Box *box, Vec2F32 origin, Vec4F32 bg)
{
  MD_Style *style = p->style;
  MD_Text *t = box->text;
  if(t == 0 || t->lines == 0) { return; }
  Rng2F32 r = shift_2f32(box->rect, origin);
  F32 lh = t->line_height;
  F32 padding_top = (lh - t->ascent - t->descent)/2;
  F32 baseline_offset = padding_top + t->ascent;
  F32 underline_offset = padding_top + t->ascent + t->descent*0.618f;
  F32 strike_offset = ((t->ascent*0.5f) + baseline_offset)*0.5f;
  F32 thickness = Max(1.f, round_f32(style->scale));
  F32 box_w = r.x1 - r.x0;

  // selection overlap
  U64 sel0 = p->ix->selection_start, sel1 = p->ix->selection_end;
  B32 has_sel = (sel1 > sel0 && sel1 > t->global_start && sel0 < t->global_start + t->string.size + 1);

  for EachIndex(li, t->line_count)
  {
    MD_TextLine *line = &t->lines[li];
    F32 top = r.y0 + li*lh;
    if(top > p->clip.y1 || top + lh < p->clip.y0) { continue; }
    F32 x0 = r.x0 + md_text_line_x0(t, line, box_w);
    F32 baseline = round_f32(top + baseline_offset);

    //- code chips
    for EachIndex(fi, line->frag_count)
    {
      MD_TextFrag *f = &t->frags[line->first_frag + fi];
      MD_TextRun *run = &t->runs[f->run];
      if(!(run->flags & MD_RunFlag_Chip)) { continue; }
      // merge with following fragments of chip runs that touch
      F32 fx0 = x0 + f->x;
      F32 fx1 = fx0 + f->width;
      F32 outset = md_px(style, 1);
      F32 inset = lh*0.1f;
      Rng2F32 chip = r2f32p(fx0 - outset, top + inset, fx1 + outset, top + lh - inset);
      md_fill(chip, th_over(bg, run->chip_color), md_px(style, 4));
    }

    //- search highlights (MarkdownHighlights::highlights_for_line)
    if(p->ix->search_match_count)
    {
      Rng1U64 *m = p->ix->search_matches;
      U64 count = p->ix->search_match_count;
      U64 ls = t->global_start + line->start;
      U64 le = t->global_start + line->end;
      U64 lo = 0, hi = count;
      for(;lo < hi;) { U64 mid = (lo + hi)/2; if(m[mid].max <= ls) { lo = mid+1; } else { hi = mid; } }
      for(U64 mi = lo; mi < count && m[mi].min < le; mi += 1)
      {
        U64 a = Max(m[mi].min, ls), z = Min(m[mi].max, le);
        if(a >= z) { continue; }
        F32 sx0 = md_text_x_for_offset(style, t, line, a - t->global_start);
        F32 sx1 = md_text_x_for_offset(style, t, line, z - t->global_start);
        Vec4F32 color = (mi == p->ix->search_active) ? p->colors->search_active_match_background : p->colors->search_match_background;
        md_fill(r2f32p(x0 + sx0, top, x0 + sx1, top + lh), th_over(bg, color), 0);
      }
    }

    //- selection
    if(has_sel)
    {
      U64 ls = t->global_start + line->start;
      U64 le = t->global_start + line->end;
      U64 a = Max(sel0, ls), z = Min(sel1, le);
      if(a < z || (a == z && sel0 <= ls && sel1 > le))
      {
        F32 sx0 = md_text_x_for_offset(style, t, line, a - t->global_start);
        F32 sx1 = md_text_x_for_offset(style, t, line, z - t->global_start);
        md_fill(r2f32p(x0 + sx0, top, x0 + sx1, top + lh), th_over(bg, p->colors->element_selection_background), 0);
      }
    }

    //- glyphs and decorations
    for EachIndex(fi, line->frag_count)
    {
      MD_TextFrag *f = &t->frags[line->first_frag + fi];
      MD_TextRun *run = &t->runs[f->run];
      String8 str = str8_substr(t->string, r1u64(f->start, f->end));
      F32 fx = x0 + f->x;
      if(fx > p->clip.x1 || fx + f->width < p->clip.x0) { continue; }
      FNT_Run fr = fnt_run_from_string(md_run_font(style, run), md_pt_from_px(t->font_size), 0, t->font_size*4.f, FNT_RasterFlag_Smooth, str);
      dr_text_run(v2f32(fx, baseline), th_linear(run->color), fr);
      if(run->flags & MD_RunFlag_Underline)
      {
        F32 uy = round_f32(top + underline_offset);
        md_fill(r2f32p(fx, uy, fx + f->width, uy + thickness), th_over(bg, run->underline_color), 0);
      }
      if(run->flags & MD_RunFlag_Strike)
      {
        F32 sy = round_f32(top + strike_offset);
        md_fill(r2f32p(fx, sy, fx + f->width, sy + thickness), run->color, 0);
      }
    }
  }
}

internal void
md_paint_icon(MD_PaintCtx *p, String8 glyph, Rng2F32 r, F32 size, Vec4F32 color)
{
  FNT_Tag icons = p->style->fonts->icons;
  F32 pt = md_pt_from_px(size);
  FNT_Run run = fnt_run_from_string(icons, pt, 0, 0, FNT_RasterFlag_Smooth, glyph);
  F32 x = floor_f32((r.x0 + r.x1)/2 - run.dim.x/2);
  F32 y = floor_f32((r.y0 + r.y1)/2 + (run.ascent - run.descent)/2);
  dr_text_run(v2f32(x, y), th_linear(color), run);
}

internal void
md_paint_box(MD_PaintCtx *p, MD_Box *box, Vec2F32 origin, Vec4F32 bg)
{
  MD_Style *style = p->style;
  TH_Colors *colors = p->colors;
  Rng2F32 r = shift_2f32(box->rect, origin);
  if(r.y0 > p->clip.y1 || r.y1 < p->clip.y0) { return; }
  if(box->line_break) { return; }

  //- background
  if(box->background.w > 0)
  {
    md_fill(r, box->background, box->radius);
    bg = th_over(bg, box->background);
  }

  //- borders
  {
    F32 *bw = box->border;
    B32 uniform = (bw[0] > 0 && bw[0] == bw[1] && bw[1] == bw[2] && bw[2] == bw[3]);
    if(uniform && box->radius > 0)
    {
      dr_rect(r, th_linear(th_over(bg, box->border_color)), box->radius, bw[0], 0.5f);
    }
    else
    {
      Vec4F32 bc = th_over(bg, box->border_color);
      if(bw[MD_Side_Top] > 0)    { md_fill(r2f32p(r.x0, r.y0, r.x1, r.y0 + bw[MD_Side_Top]), bc, 0); }
      if(bw[MD_Side_Bottom] > 0) { md_fill(r2f32p(r.x0, r.y1 - bw[MD_Side_Bottom], r.x1, r.y1), bc, 0); }
      if(bw[MD_Side_Left] > 0)   { md_fill(r2f32p(r.x0, r.y0, r.x0 + bw[MD_Side_Left], r.y1), bc, 0); }
      if(bw[MD_Side_Right] > 0)  { md_fill(r2f32p(r.x1 - bw[MD_Side_Right], r.y0, r.x1, r.y1), bc, 0); }
    }
  }

  //- content
  switch(box->kind)
  {
    default:{}break;
    case MD_BoxKind_Text: { md_paint_text(p, box, origin, bg); }break;
    case MD_BoxKind_Image:
    {
      if(box->image && !box->image->failed)
      {
        MD_Image *img = box->image;
        Vec2S32 sz = img->tex_size;
        dr_img(r, r2f32p(0, 0, (F32)sz.x, (F32)sz.y), img->texture, v4f32(1, 1, 1, 1), box->radius, 0, 0.5f);
        // SVG <text>, which the rasterizer leaves out
        if(img->first_text && img->size.x > 0)
        {
          F32 k = (r.x1 - r.x0)/(F32)img->size.x;
          DR_ClipScope(intersect_2f32(r, p->clip))
            for(MD_SvgText *t = img->first_text; t != 0; t = t->next)
          {
            F32 px = t->font_size*k;
            if(px < 1.f) { continue; }
            FNT_Tag font = t->bold ? style->fonts->sans_semibold : style->fonts->sans;
            FNT_Run run = fnt_run_from_string(font, md_pt_from_px(px), 0, px*4.f, FNT_RasterFlag_Smooth, t->string);
            F32 x = r.x0 + t->pos.x*k;
            if(t->anchor == MD_Align_Center) { x -= run.dim.x/2; }
            else if(t->anchor == MD_Align_Right) { x -= run.dim.x; }
            dr_text_run(v2f32(round_f32(x), round_f32(r.y0 + t->pos.y*k)), th_linear(t->color), run);
          }
        }
      }
    }break;
    case MD_BoxKind_Checkbox:
    {
      // Checkbox::fill(): size_4 rounded_xs, element_background, 1px border
      F32 inner = md_rems(style, 1.f);
      Vec2F32 c = v2f32((r.x0 + r.x1)/2, (r.y0 + r.y1)/2);
      Rng2F32 sq = r2f32p(floor_f32(c.x - inner/2), floor_f32(c.y - inner/2), floor_f32(c.x - inner/2) + inner, floor_f32(c.y - inner/2) + inner);
      F32 rad = md_rems(style, 0.125f);
      md_fill(sq, colors->element_background, rad);
      // group_hover: border at 70% alpha
      Vec4F32 border = colors->border;
      if(p->hover.checkbox == box->key) { border = th_over(bg, th_alpha(border, 0.7f)); }
      dr_rect(sq, th_linear(border), rad, md_px(style, 1), 0.5f);
      if(box->checked)
      {
        md_paint_icon(p, str8_lit("V"), sq, md_rems(style, 14.f/16.f), colors->text_accent);
      }
    }break;
    case MD_BoxKind_Icon:
    {
      md_paint_icon(p, box->icon, r, box->icon_size, box->icon_color);
    }break;
    case MD_BoxKind_Gutter:
    {
      // the active root block (cursor / link target) uses `border`; others
      // show `border_variant` while hovered
      if(p->ix->active_root_index == box->root_index)
      {
        md_fill(r, colors->border, box->radius);
      }
      else if(p->hover.root_index == box->root_index)
      {
        md_fill(r, colors->border_variant, box->radius);
      }
    }break;
    case MD_BoxKind_CopyButton:
    {
      if(p->hover.code_block && p->hover.code_block->key == box->key)
      {
        B32 copied = (p->ix->copied_code_key == box->key && now_time_us() < p->ix->copied_code_until_us);
        md_fill(r, colors->editor_background, 0);
        if(p->hover.copy_button) { md_fill(r, colors->border_variant, md_rems(style, 0.25f)); }
        md_paint_icon(p, copied ? str8_lit("V") : str8_lit("a"), r, md_rems(style, 14.f/16.f), copied ? colors->success : colors->text_muted);
      }
    }break;
  }

  //- children (absolute ones after the flow, as gpui appends them last)
  if(box->first)
  {
    Vec2F32 child_origin = origin;
    B32 clipped = 0;
    Rng2F32 saved_clip = p->clip;
    if(box->scroll_x)
    {
      F32 off = md_scroll_offset(p->ix, box->key);
      child_origin.x -= off;
      Rng2F32 inner = r2f32p(r.x0 + box->border[MD_Side_Left], r.y0 + box->border[MD_Side_Top],
                             r.x1 - box->border[MD_Side_Right], r.y1 - box->border[MD_Side_Bottom]);
      Rng2F32 c = intersect_2f32(inner, dr_top_clip().x1 != 0 ? dr_top_clip() : inner);
      dr_push_clip(c);
      p->clip = intersect_2f32(p->clip, c);
      clipped = 1;
    }
    for(MD_Box *ch = box->first; ch != 0; ch = ch->next)
    {
      if(ch->absolute) { continue; }
      md_paint_box(p, ch, child_origin, bg);
    }
    if(clipped)
    {
      dr_pop_clip();
      p->clip = saved_clip;
      // horizontal scrollbar while hovered and overflowing
      F32 inner_w = (r.x1 - r.x0) - box->border[MD_Side_Left] - box->border[MD_Side_Right];
      F32 content_w = box->content_width;
      if(box->kind != MD_BoxKind_Grid) { content_w = box->content_width; }
      if(content_w > inner_w + 0.5f && p->hover.scroll_box == box)
      {
        F32 off = md_scroll_offset(p->ix, box->key);
        F32 track_w = inner_w;
        F32 thumb_w = Max(md_px(style, 25), track_w*inner_w/content_w);
        F32 thumb_x = (track_w - thumb_w) * off / Max(1.f, content_w - inner_w);
        F32 tx0 = r.x0 + box->border[MD_Side_Left] + thumb_x;
        F32 ty1 = r.y1 - box->border[MD_Side_Bottom] - md_px(style, 4);
        Rng2F32 thumb = r2f32p(tx0, ty1 - md_px(style, 6), tx0 + thumb_w, ty1);
        md_fill(thumb, th_over(bg, th_over(th_alpha(colors->surface_background, 0.7f), colors->scrollbar_thumb_background)), md_px(style, 3));
      }
    }
    for(MD_Box *ch = box->first; ch != 0; ch = ch->next)
    {
      if(!ch->absolute) { continue; }
      md_paint_box(p, ch, origin, bg);
    }
  }
}

internal void
md_element_paint(MD_Element *el, MD_Style *style, MD_Interaction *ix, Vec2F32 origin, Rng2F32 clip)
{
  MD_PaintCtx p = {0};
  p.el = el;
  p.style = style;
  p.ix = ix;
  p.colors = &style->theme->colors;
  p.clip = clip;
  md_hit_style = style;
  if(ix->mouse_inside)
  {
    p.hover = md_element_hit_test(el, ix, ix->mouse);
  }
  md_paint_box(&p, el->root, origin, p.colors->editor_background);
}

////////////////////////////////
//~ Hit Testing

internal U64
md_text_offset_in_line(MD_Style *style, MD_Text *t, MD_TextLine *line, F32 x)
{
  if(x <= 0) { return line->start; }
  for EachIndex(i, line->frag_count)
  {
    MD_TextFrag *f = &t->frags[line->first_frag + i];
    if(x >= f->x + f->width) { continue; }
    if(x < f->x) { return f->start; }
    FNT_Tag font = md_run_font(style, &t->runs[f->run]);
    F32 prev_w = 0;
    for(U64 k = f->start; k < f->end;)
    {
      UnicodeDecode d = utf8_decode(t->string.str + k, f->end - k);
      U64 next = k + Max(1, d.inc);
      F32 w = md_text_width(font, t->font_size, str8_substr(t->string, r1u64(f->start, next)));
      if(f->x + w > x)
      {
        return (x - (f->x + prev_w) < (w - prev_w)/2) ? k : next;
      }
      prev_w = w;
      k = next;
    }
    return f->end;
  }
  return line->end;
}

internal void
md_hit_test_rec(MD_Element *el, MD_Interaction *ix, MD_Box *box, Vec2F32 p, F32 dx, MD_Hit *hit, MD_Style *style)
{
  Rng2F32 r = shift_2f32(box->rect, v2f32(dx, 0));
  if(!contains_2f32(r, p)) { return; }
  if(box->root_index && box->kind == MD_BoxKind_Block) { hit->root_index = box->root_index; }
  if(box->code_group) { hit->code_block = box; }
  if(box->scroll_x) { hit->scroll_box = box; }
  if(box->link) { hit->link = box->link; }
  if(box->kind == MD_BoxKind_CopyButton && hit->code_block) { hit->copy_button = 1; }
  if(box->kind == MD_BoxKind_Checkbox) { hit->checkbox = box->key; }
  if(box->kind == MD_BoxKind_Text && box->text && box->text->lines)
  {
    MD_Text *t = box->text;
    U64 li = (U64)ClampBot(0, (p.y - r.y0)/t->line_height);
    if(li < t->line_count)
    {
      MD_TextLine *line = &t->lines[li];
      F32 x0 = r.x0 + md_text_line_x0(t, line, r.x1 - r.x0);
      if(p.x >= x0 && p.x <= x0 + line->width)
      {
        hit->over_text = 1;
        if(style)
        {
          U64 off = md_text_offset_in_line(style, t, line, p.x - x0);
          // use the character under the cursor, not the nearest boundary
          for EachIndex(i, line->frag_count)
          {
            MD_TextFrag *f = &t->frags[line->first_frag + i];
            if(p.x >= x0 + f->x && p.x < x0 + f->x + f->width)
            {
              if(t->runs[f->run].link) { hit->link = t->runs[f->run].link; }
              break;
            }
          }
          (void)off;
        }
      }
    }
  }
  F32 child_dx = dx;
  if(box->scroll_x) { child_dx -= md_scroll_offset(ix, box->key); }
  for(MD_Box *ch = box->first; ch != 0; ch = ch->next)
  {
    md_hit_test_rec(el, ix, ch, p, ch->absolute ? dx : child_dx, hit, style);
  }
}


internal MD_Hit
md_element_hit_test(MD_Element *el, MD_Interaction *ix, Vec2F32 p)
{
  MD_Hit hit = {0};
  md_hit_test_rec(el, ix, el->root, p, 0, &hit, md_hit_style);
  return hit;
}

internal F32
md_box_scroll_dx(MD_Interaction *ix, MD_Box *box)
{
  F32 dx = 0;
  for(MD_Box *b = box->parent; b != 0; b = b->parent)
  {
    if(b->scroll_x) { dx -= md_scroll_offset(ix, b->key); }
  }
  return dx;
}

internal U64
md_element_offset_from_point(MD_Element *el, MD_Style *style, MD_Interaction *ix, Vec2F32 p)
{
  if(el->text_count == 0) { return 0; }
  // the first text whose box reaches below the point
  MD_Text *best = 0;
  for EachIndex(i, el->text_count)
  {
    MD_Text *t = el->texts[i];
    Rng2F32 r = t->box->rect;
    if(t->lines == 0) { continue; }
    if(p.y < r.y0)
    {
      if(best == 0) { return t->global_start; }
      // between blocks: snap to whichever is closer vertically
      Rng2F32 br = best->box->rect;
      if(p.y - br.y1 < r.y0 - p.y) { break; }
      return t->global_start;
    }
    best = t;
    if(p.y < r.y1) { break; }
  }
  if(best == 0) { return 0; }
  MD_Text *t = best;
  Rng2F32 r = shift_2f32(t->box->rect, v2f32(md_box_scroll_dx(ix, t->box), 0));
  if(p.y >= r.y1) { return t->global_start + t->string.size; }
  U64 li = (U64)ClampBot(0, (p.y - r.y0)/t->line_height);
  li = Min(li, t->line_count-1);
  MD_TextLine *line = &t->lines[li];
  F32 x0 = r.x0 + md_text_line_x0(t, line, r.x1 - r.x0);
  return t->global_start + md_text_offset_in_line(style, t, line, p.x - x0);
}

internal Rng1U64
md_element_word_at(MD_Element *el, U64 offset)
{
  for EachIndex(i, el->text_count)
  {
    MD_Text *t = el->texts[i];
    if(offset < t->global_start || offset > t->global_start + t->string.size) { continue; }
    U64 o = offset - t->global_start;
    String8 s = t->string;
    U64 a = o, z = o;
    for(;a > 0 && (md_char_is_alnum(s.str[a-1]) || s.str[a-1] == '_' || s.str[a-1] >= 0x80); a -= 1);
    for(;z < s.size && (md_char_is_alnum(s.str[z]) || s.str[z] == '_' || s.str[z] >= 0x80); z += 1);
    if(a == z && z < s.size) { z += 1; }
    return r1u64(t->global_start + a, t->global_start + z);
  }
  return r1u64(offset, offset);
}

internal String8
md_element_text_in_range(Arena *arena, MD_Element *el, U64 start, U64 end)
{
  String8List parts = {0};
  for EachIndex(i, el->text_count)
  {
    MD_Text *t = el->texts[i];
    U64 ts = t->global_start, te = t->global_start + t->string.size;
    if(te < start || ts >= end) { continue; }
    U64 a = Max(start, ts) - ts;
    U64 z = Min(end, te) - ts;
    if(parts.node_count) { str8_list_push(arena, &parts, str8_lit("\n")); }
    str8_list_push(arena, &parts, str8_substr(t->string, r1u64(a, z)));
  }
  return str8_list_join(arena, &parts, 0);
}

internal MD_Text *
md_element_text_at(MD_Element *el, U64 offset)
{
  for EachIndex(i, el->text_count)
  {
    MD_Text *t = el->texts[i];
    if(offset >= t->global_start && offset <= t->global_start + t->string.size) { return t; }
  }
  return 0;
}

internal Rng1U64 md_box_source_range(MD_Box *box);

internal B32
md_char_is_marker(U8 c)
{
  return (c == '*' || c == '_' || c == '`' || c == '~');
}

// The Markdown source behind a selection (Copy as Markdown). Selections live
// in rendered text, so this maps through the blocks' source ranges: a
// selection inside one block that occurs verbatim in its source copies that
// source with the emphasis and code markers around it; anything else copies
// the blocks it touches.
internal String8
md_element_source_in_range(MD_Element *el, String8 source, U64 start, U64 end)
{
  MD_Text *a = md_element_text_at(el, start);
  MD_Text *z = md_element_text_at(el, end);
  if(a == 0 || z == 0 || end <= start) { return str8_zero(); }
  Rng1U64 ra = md_box_source_range(a->box);
  Rng1U64 rz = md_box_source_range(z->box);
  ra.min = Min(ra.min, source.size); ra.max = Min(ra.max, source.size);
  rz.min = Min(rz.min, source.size); rz.max = Min(rz.max, source.size);
  if(a == z)
  {
    String8 rendered = str8_substr(a->string, r1u64(start - a->global_start, end - a->global_start));
    String8 block = str8_substr(source, ra);
    U64 at = str8_find_needle(block, 0, rendered, 0);
    if(rendered.size && at < block.size && str8_find_needle(block, at+1, rendered, 0) >= block.size)
    {
      U64 s0 = ra.min + at, s1 = s0 + rendered.size;
      for(;s0 > ra.min && md_char_is_marker(source.str[s0-1]);) { s0 -= 1; }
      for(;s1 < ra.max && md_char_is_marker(source.str[s1]);) { s1 += 1; }
      return str8_substr(source, r1u64(s0, s1));
    }
  }
  return str8_skip_chop_whitespace(str8_substr(source, r1u64(Min(ra.min, rz.min), Max(ra.max, rz.max))));
}

////////////////////////////////
//~ Source Lookups (RenderedText::position_for_source_index)

internal B32
md_box_is_within(MD_Box *box, MD_Box *ancestor)
{
  for(MD_Box *b = box; b != 0; b = b->parent) { if(b == ancestor) { return 1; } }
  return 0;
}

internal MD_Text *
md_element_first_text_in(MD_Element *el, MD_Box *box)
{
  for EachIndex(i, el->text_count)
  {
    if(md_box_is_within(el->texts[i]->box, box)) { return el->texts[i]; }
  }
  return 0;
}

internal Rng1U64
md_box_source_range(MD_Box *box)
{
  for(MD_Box *b = box; b != 0; b = b->parent)
  {
    if(b->src.max > b->src.min) { return b->src; }
  }
  return r1u64(0, 0);
}

// The text of the innermost block whose source range holds the index, or
// failing that the first text after it.
internal MD_Text *
md_element_text_for_source_index(MD_Element *el, U64 source_index)
{
  MD_Text *best = 0;
  U64 best_span = max_U64;
  MD_Text *after = 0;
  for EachIndex(i, el->text_count)
  {
    MD_Text *t = el->texts[i];
    Rng1U64 r = md_box_source_range(t->box);
    if(source_index >= r.min && source_index < r.max && dim_1u64(r) < best_span)
    {
      best = t;
      best_span = dim_1u64(r);
    }
    if(after == 0 && r.min >= source_index) { after = t; }
  }
  return best ? best : after;
}

internal U64
md_box_root_index(MD_Box *box)
{
  for(MD_Box *b = box; b != 0; b = b->parent)
  {
    if(b->root_index && b->kind == MD_BoxKind_Block) { return b->root_index; }
  }
  return 0;
}
