// mdview: a small Markdown viewer built on the RAD Debugger UI stack
// (base -> window_manager -> font_provider -> render -> font_cache -> draw -> ui).
//
// The parser handles the common subset of CommonMark/GFM: ATX and setext
// headings, paragraphs, emphasis, inline code, links, images (as links),
// block quotes, ordered/unordered/task lists, fenced and indented code,
// horizontal rules, and pipe tables.

////////////////////////////////
//~ Build Options

#define BUILD_TITLE "mdview"
#define OS_FEATURE_GRAPHICAL 1

////////////////////////////////
//~ Includes

//- [h]
#include "base/base_inc.h"
#include "window_manager/window_manager_inc.h"
#include "shell/shell_inc.h"
#include "font_provider/font_provider_inc.h"
#include "render/render_inc.h"
#include "font_cache/font_cache.h"
#include "draw/draw.h"
#include "ui/ui_inc.h"

//- [c]
#include "base/base_inc.c"
#include "window_manager/window_manager_inc.c"
#include "shell/shell_inc.c"
#include "font_provider/font_provider_inc.c"
#include "render/render_inc.c"
#include "font_cache/font_cache.c"
#include "draw/draw.c"
#include "ui/ui_inc.c"

////////////////////////////////
//~ Document Types

typedef U32 MV_SpanFlags;
enum
{
  MV_SpanFlag_Bold   = (1<<0),
  MV_SpanFlag_Italic = (1<<1),
  MV_SpanFlag_Code   = (1<<2),
  MV_SpanFlag_Link   = (1<<3),
  MV_SpanFlag_Strike = (1<<4),
  MV_SpanFlag_Break  = (1<<5), // hard line break
};

typedef struct MV_Span MV_Span;
struct MV_Span
{
  MV_Span *next;
  String8 text;
  String8 url;
  MV_SpanFlags flags;
};

typedef struct MV_SpanList MV_SpanList;
struct MV_SpanList
{
  MV_Span *first;
  MV_Span *last;
  U64 count;
};

typedef enum MV_BlockKind
{
  MV_BlockKind_Paragraph,
  MV_BlockKind_Heading,
  MV_BlockKind_ListItem,
  MV_BlockKind_Code,
  MV_BlockKind_Rule,
  MV_BlockKind_Table,
}
MV_BlockKind;

typedef enum MV_Align
{
  MV_Align_Left,
  MV_Align_Center,
  MV_Align_Right,
}
MV_Align;

typedef struct MV_Block MV_Block;
struct MV_Block
{
  MV_Block *next;
  MV_BlockKind kind;
  U64 quote_depth;

  // heading: 1..6; list item: nesting depth (0-based)
  U64 level;

  // list items
  B32 ordered;
  String8 marker;
  B32 is_task;
  B32 task_done;

  // paragraph/heading/list item content
  MV_SpanList spans;

  // code
  String8List code_lines;
  String8 code_lang;

  // tables (row 0 is the header)
  U64 table_cols;
  U64 table_rows;
  MV_SpanList *table_cells;
  MV_Align *table_aligns;
};

typedef struct MV_Doc MV_Doc;
struct MV_Doc
{
  MV_Block *first;
  MV_Block *last;
  U64 block_count;
  U64 word_count;
};

////////////////////////////////
//~ Layout Types

typedef enum MV_LineKind
{
  MV_LineKind_Spacer,
  MV_LineKind_Text,
  MV_LineKind_Code,
  MV_LineKind_Rule,
  MV_LineKind_TableRow,
}
MV_LineKind;

typedef struct MV_Piece MV_Piece;
struct MV_Piece
{
  String8 text;
  String8 url;
  MV_SpanFlags flags;
  F32 width;
};

typedef struct MV_PieceNode MV_PieceNode;
struct MV_PieceNode
{
  MV_PieceNode *next;
  MV_Piece v;
};

typedef struct MV_Line MV_Line;
struct MV_Line
{
  MV_LineKind kind;
  F32 y;
  F32 height;
  F32 indent;
  U64 quote_depth;
  U64 heading_level;
  B32 heading_rule;
  B32 code_bg;
  B32 code_first;
  B32 code_last;
  String8 bullet;
  B32 bullet_is_task;
  B32 bullet_task_done;
  MV_Piece *pieces;
  U64 piece_count;

  // table rows
  MV_Block *table;
  U64 table_row;
  F32 *table_col_widths;
};

typedef struct MV_LineChunk MV_LineChunk;
struct MV_LineChunk
{
  MV_LineChunk *next;
  MV_Line *v;
  U64 count;
  U64 cap;
};

typedef struct MV_OutlineItem MV_OutlineItem;
struct MV_OutlineItem
{
  U64 level;
  String8 text;
  F32 y;
};

typedef struct MV_Layout MV_Layout;
struct MV_Layout
{
  MV_Line *lines;
  U64 line_count;
  MV_OutlineItem *outline;
  U64 outline_count;
  F32 total_height;
};

////////////////////////////////
//~ App State

typedef struct MV_Fonts MV_Fonts;
struct MV_Fonts
{
  FNT_Tag regular;
  FNT_Tag bold;
  FNT_Tag italic;
  FNT_Tag bold_italic;
  FNT_Tag mono;
  FNT_Tag icons;
};

typedef struct MV_State MV_State;
struct MV_State
{
  Arena *arena;
  B32 quit;
  U64 frame_depth;
  U64 frame_index;
  U64 last_frame_us;

  // window
  WM_Window os;
  R_Handle r;
  UI_State *ui;
  DR_Bucket *draw_bucket;
  UI_EventList ui_events;

  // theme/fonts
  MV_Fonts fonts;
  B32 light_theme;
  UI_Theme theme_dark;
  UI_Theme theme_light;
  F32 zoom;
  B32 show_outline;

  // document
  Arena *doc_arena;
  String8 doc_path;
  String8 doc_name;
  String8 doc_text;
  String8 doc_error;
  MV_Doc doc;

  // layout cache
  Arena *layout_arena;
  MV_Layout layout;
  B32 layout_dirty;
  F32 layout_width;
  F32 layout_font_size;

  // file picker request (handled before the next build, since it's modal)
  B32 pick_file_requested;
  
  // pending scroll request (applied while building)
  B32 scroll_request;
  F32 scroll_request_y;

  // file watcher
  Mutex watch_mutex;
  U8 watch_path_buffer[4096];
  U64 watch_path_size;
  U64 watch_modified;
  U32 watch_changed;
};

global MV_State *mv = 0;

////////////////////////////////
//~ Welcome Document

read_only global String8 mv_welcome_text = str8_lit_comp(
"# mdview\n"
"\n"
"A small **Markdown** viewer built on the *RAD Debugger* UI libraries: "
"`base`, `window_manager`, `render`, `font_cache`, `draw`, and `ui`.\n"
"\n"
"## Opening files\n"
"\n"
"- Pass a path on the command line: `mdview README.md`\n"
"- Click **Open** (or press `Ctrl+O`) to pick a file\n"
"- Or **drag and drop** a `.md` file onto this window\n"
"- The file is reloaded automatically when it changes on disk\n"
"\n"
"## Keys\n"
"\n"
"| Key | Action |\n"
"|:----|:-------|\n"
"| `Ctrl` + `+` / `-` / `0` | Zoom in / out / reset |\n"
"| `Ctrl` + mouse wheel | Zoom |\n"
"| `PageUp` / `PageDown` / `Space` | Scroll a page |\n"
"| `Up` / `Down` | Scroll a line |\n"
"| `Home` / `End` | Jump to top / bottom |\n"
"| `Ctrl` + `O` | Open a file |\n"
"| `F5` | Reload |\n"
"| `Ctrl` + `T` | Toggle light / dark theme |\n"
"| `Ctrl` + `B` | Toggle outline |\n"
"\n"
"## What renders\n"
"\n"
"1. Headings, paragraphs, *emphasis*, **strong**, ***both***, ~~strike~~\n"
"2. `inline code`, [links](https://github.com/EpicGamesExt/raddebugger), and\n"
"   autolinks like <https://example.com>\n"
"3. Nested lists\n"
"   - like this one\n"
"     - and this\n"
"4. Task lists\n"
"\n"
"- [x] parse markdown\n"
"- [x] lay it out with `fnt_*` measurements\n"
"- [ ] world domination\n"
"\n"
"> Block quotes, which can be\n"
"> > nested as well.\n"
"\n"
"```c\n"
"internal B32\n"
"frame(void)\n"
"{\n"
"  ui_begin_build(window, &events, &icons, theme, &anim, dt, dt);\n"
"  // ... build boxes ...\n"
"  ui_end_build();\n"
"  return quit;\n"
"}\n"
"```\n"
"\n"
"---\n"
"\n"
"Click a heading in the outline on the left to jump to it.\n"
);

////////////////////////////////
//~ Small Helpers

internal B32
mv_char_is_punct(U8 c)
{
  return (c >= 0x21 && c <= 0x2f) || (c >= 0x3a && c <= 0x40) || (c >= 0x5b && c <= 0x60) || (c >= 0x7b && c <= 0x7e);
}

internal void
mv_set_next_corner_radius(F32 r)
{
  ui_set_next_corner_radius_00(r);
  ui_set_next_corner_radius_01(r);
  ui_set_next_corner_radius_10(r);
  ui_set_next_corner_radius_11(r);
}

internal U64
mv_count_leading(String8 s, U8 c)
{
  U64 n = 0;
  for(;n < s.size && s.str[n] == c; n += 1);
  return n;
}

internal U64
mv_indent_width(String8 s, U64 *bytes_out)
{
  U64 w = 0, i = 0;
  for(;i < s.size; i += 1)
  {
    if(s.str[i] == ' ') { w += 1; }
    else if(s.str[i] == '\t') { w += 4 - (w%4); }
    else break;
  }
  if(bytes_out) { *bytes_out = i; }
  return w;
}

internal B32
mv_is_blank(String8 s)
{
  return str8_skip_chop_whitespace(s).size == 0;
}

internal B32
mv_is_rule(String8 s)
{
  s = str8_skip_chop_whitespace(s);
  if(s.size < 3) { return 0; }
  U8 c = s.str[0];
  if(c != '-' && c != '*' && c != '_') { return 0; }
  U64 n = 0;
  for EachIndex(i, s.size)
  {
    if(s.str[i] == c) { n += 1; }
    else if(s.str[i] != ' ' && s.str[i] != '\t') { return 0; }
  }
  return n >= 3;
}

internal B32
mv_is_fence(String8 s, U8 *fence_char_out, U64 *fence_len_out)
{
  U64 indent_bytes = 0;
  if(mv_indent_width(s, &indent_bytes) > 3) { return 0; }
  s = str8_skip(s, indent_bytes);
  if(s.size < 3) { return 0; }
  U8 c = s.str[0];
  if(c != '`' && c != '~') { return 0; }
  U64 n = mv_count_leading(s, c);
  if(n < 3) { return 0; }
  if(fence_char_out) { *fence_char_out = c; }
  if(fence_len_out) { *fence_len_out = n; }
  return 1;
}

// Parses a list marker; returns the number of bytes consumed (marker plus one
// following space) or 0 if the line doesn't start with a list marker.
internal U64
mv_list_marker(String8 s, B32 *ordered_out, String8 *marker_out)
{
  if(s.size >= 2 && (s.str[0] == '-' || s.str[0] == '*' || s.str[0] == '+') && (s.str[1] == ' ' || s.str[1] == '\t'))
  {
    *ordered_out = 0;
    *marker_out = str8_prefix(s, 1);
    return 2;
  }
  if(s.size == 1 && (s.str[0] == '-' || s.str[0] == '*' || s.str[0] == '+'))
  {
    *ordered_out = 0;
    *marker_out = str8_prefix(s, 1);
    return 1;
  }
  U64 d = 0;
  for(;d < s.size && d < 9 && char_is_digit(s.str[d], 10); d += 1);
  if(d > 0 && d+1 < s.size && (s.str[d] == '.' || s.str[d] == ')') && (s.str[d+1] == ' ' || s.str[d+1] == '\t'))
  {
    *ordered_out = 1;
    *marker_out = str8_prefix(s, d+1);
    return d+2;
  }
  return 0;
}

internal B32
mv_is_table_delim_row(String8 s)
{
  s = str8_skip_chop_whitespace(s);
  if(s.size == 0) { return 0; }
  B32 has_dash = 0;
  B32 has_pipe = 0;
  for EachIndex(i, s.size)
  {
    U8 c = s.str[i];
    if(c == '-') { has_dash = 1; }
    else if(c == '|') { has_pipe = 1; }
    else if(c != ':' && c != ' ' && c != '\t') { return 0; }
  }
  return has_dash && has_pipe;
}

internal String8List
mv_split_table_row(Arena *arena, String8 s)
{
  String8List cells = {0};
  s = str8_skip_chop_whitespace(s);
  if(s.size > 0 && s.str[0] == '|') { s = str8_skip(s, 1); }
  if(s.size > 0 && s.str[s.size-1] == '|' && !(s.size >= 2 && s.str[s.size-2] == '\\')) { s = str8_chop(s, 1); }
  U64 start = 0;
  B32 in_code = 0;
  for(U64 i = 0; i <= s.size; i += 1)
  {
    if(i < s.size && s.str[i] == '\\') { i += 1; continue; }
    if(i < s.size && s.str[i] == '`') { in_code = !in_code; }
    if(i == s.size || (s.str[i] == '|' && !in_code))
    {
      str8_list_push(arena, &cells, str8_skip_chop_whitespace(str8_substr(s, r1u64(start, i))));
      start = i+1;
    }
  }
  return cells;
}

////////////////////////////////
//~ Inline Parsing

internal void
mv_span_push(Arena *arena, MV_SpanList *list, String8 text, MV_SpanFlags flags, String8 url)
{
  if(text.size == 0 && !(flags & MV_SpanFlag_Break)) { return; }
  MV_Span *span = push_array(arena, MV_Span, 1);
  span->text = text;
  span->flags = flags;
  span->url = url;
  SLLQueuePush(list->first, list->last, span);
  list->count += 1;
}

internal B32
mv_char_is_punct_or_space(U8 c)
{
  return c == 0 || char_is_space(c) || (mv_char_is_punct(c) && c != '_');
}

// Finds a closing delimiter run `delim` at or after `start`, which isn't
// preceded by whitespace. Returns s.size if none exists.
internal U64
mv_find_closer(String8 s, U64 start, String8 delim)
{
  for(U64 i = start; i + delim.size <= s.size; i += 1)
  {
    if(s.str[i] == '\\') { i += 1; continue; }
    if(s.str[i] == '`')
    {
      U64 n = mv_count_leading(str8_skip(s, i), '`');
      String8 ticks = str8_substr(s, r1u64(i, i+n));
      U64 close = str8_find_needle(s, i+n, ticks, 0);
      if(close < s.size) { i = close + n - 1; }
      continue;
    }
    if(MemoryMatch(s.str+i, delim.str, delim.size) && i > start && !char_is_space(s.str[i-1]))
    {
      return i;
    }
  }
  return s.size;
}

internal void
mv_parse_inlines(Arena *arena, MV_SpanList *out, String8 s, MV_SpanFlags flags, String8 url)
{
  String8List text_parts = {0};
  U64 text_start = 0;

  // flush pending plain text (with escapes resolved) as one span
#define MV_FlushText(end) do{ \
if((end) > text_start) { str8_list_push(arena, &text_parts, str8_substr(s, r1u64(text_start, (end)))); } \
if(text_parts.node_count) { mv_span_push(arena, out, str8_list_join(arena, &text_parts, 0), flags, url); MemoryZeroStruct(&text_parts); } \
}while(0)

  for(U64 i = 0; i < s.size;)
  {
    U8 c = s.str[i];

    //- backslash escapes & hard breaks
    if(c == '\\' && i+1 < s.size)
    {
      if(s.str[i+1] == '\n')
      {
        MV_FlushText(i);
        mv_span_push(arena, out, str8_zero(), MV_SpanFlag_Break, str8_zero());
        i += 2;
        text_start = i;
        continue;
      }
      if(mv_char_is_punct(s.str[i+1]))
      {
        if(i > text_start) { str8_list_push(arena, &text_parts, str8_substr(s, r1u64(text_start, i))); }
        text_start = i+1;
        i += 2;
        continue;
      }
    }

    //- newlines: soft break -> space, two trailing spaces -> hard break
    if(c == '\n')
    {
      U64 end = i;
      B32 hard = 0;
      if(end >= 2 && s.str[end-1] == ' ' && s.str[end-2] == ' ') { hard = 1; }
      for(;end > text_start && s.str[end-1] == ' '; end -= 1);
      if(hard)
      {
        MV_FlushText(end);
        mv_span_push(arena, out, str8_zero(), MV_SpanFlag_Break, str8_zero());
      }
      else
      {
        if(end > text_start) { str8_list_push(arena, &text_parts, str8_substr(s, r1u64(text_start, end))); }
        str8_list_push(arena, &text_parts, str8_lit(" "));
      }
      i += 1;
      text_start = i;
      continue;
    }

    //- code spans
    if(c == '`')
    {
      U64 n = mv_count_leading(str8_skip(s, i), '`');
      String8 ticks = str8_substr(s, r1u64(i, i+n));
      U64 close = str8_find_needle(s, i+n, ticks, 0);
      if(close < s.size)
      {
        MV_FlushText(i);
        String8 code = str8_substr(s, r1u64(i+n, close));
        String8 code_trimmed = code;
        if(code.size >= 2 && code.str[0] == ' ' && code.str[code.size-1] == ' ') { code_trimmed = str8_substr(code, r1u64(1, code.size-1)); }
        String8 code_flat = push_str8_copy(arena, code_trimmed);
        for EachIndex(k, code_flat.size) { if(code_flat.str[k] == '\n') { code_flat.str[k] = ' '; } }
        mv_span_push(arena, out, code_flat, flags|MV_SpanFlag_Code, url);
        i = close + n;
        text_start = i;
        continue;
      }
      i += n;
      continue;
    }

    //- links & images
    if(c == '[' || (c == '!' && i+1 < s.size && s.str[i+1] == '['))
    {
      B32 is_image = (c == '!');
      U64 open = i + (is_image ? 1 : 0);
      U64 depth = 0;
      U64 close = s.size;
      for(U64 k = open; k < s.size; k += 1)
      {
        if(s.str[k] == '\\') { k += 1; continue; }
        if(s.str[k] == '[') { depth += 1; }
        if(s.str[k] == ']') { depth -= 1; if(depth == 0) { close = k; break; } }
      }
      if(close+1 < s.size && s.str[close+1] == '(')
      {
        U64 url_end = s.size;
        U64 paren_depth = 0;
        for(U64 k = close+1; k < s.size; k += 1)
        {
          if(s.str[k] == '(') { paren_depth += 1; }
          if(s.str[k] == ')') { paren_depth -= 1; if(paren_depth == 0) { url_end = k; break; } }
        }
        if(url_end < s.size)
        {
          MV_FlushText(i);
          String8 label = str8_substr(s, r1u64(open+1, close));
          String8 target = str8_skip_chop_whitespace(str8_substr(s, r1u64(close+2, url_end)));
          U64 space = str8_find_needle(target, 0, str8_lit(" "), 0);
          target = str8_prefix(target, space); // drop optional "title"
          if(target.size >= 2 && target.str[0] == '<' && target.str[target.size-1] == '>') { target = str8_substr(target, r1u64(1, target.size-1)); }
          if(is_image)
          {
            String8 alt = label.size ? label : str8_lit("image");
            mv_span_push(arena, out, push_str8f(arena, "[image: %S]", alt), flags|MV_SpanFlag_Link|MV_SpanFlag_Italic, target);
          }
          else
          {
            mv_parse_inlines(arena, out, label, flags|MV_SpanFlag_Link, target);
          }
          i = url_end+1;
          text_start = i;
          continue;
        }
      }
    }

    //- autolinks: <scheme://...>
    if(c == '<')
    {
      U64 close = str8_find_needle(s, i+1, str8_lit(">"), 0);
      if(close < s.size)
      {
        String8 inner = str8_substr(s, r1u64(i+1, close));
        if(str8_find_needle(inner, 0, str8_lit("://"), 0) < inner.size && str8_find_needle(inner, 0, str8_lit(" "), 0) == inner.size)
        {
          MV_FlushText(i);
          mv_span_push(arena, out, inner, flags|MV_SpanFlag_Link, inner);
          i = close+1;
          text_start = i;
          continue;
        }
      }
    }

    //- bare urls
    if(!(flags & MV_SpanFlag_Link) && (c == 'h' || c == 'H') && (i == 0 || mv_char_is_punct_or_space(s.str[i-1])))
    {
      String8 rest = str8_skip(s, i);
      if(str8_match(str8_prefix(rest, 7), str8_lit("http://"), StringMatchFlag_CaseInsensitive) ||
         str8_match(str8_prefix(rest, 8), str8_lit("https://"), StringMatchFlag_CaseInsensitive))
      {
        U64 end = 0;
        for(;end < rest.size && !char_is_space(rest.str[end]) && rest.str[end] != '<' && rest.str[end] != ')'; end += 1);
        for(;end > 0 && (rest.str[end-1] == '.' || rest.str[end-1] == ',' || rest.str[end-1] == ';' || rest.str[end-1] == ':'); end -= 1);
        MV_FlushText(i);
        String8 link = str8_prefix(rest, end);
        mv_span_push(arena, out, link, flags|MV_SpanFlag_Link, link);
        i += end;
        text_start = i;
        continue;
      }
    }

    //- emphasis / strong / strike
    if(c == '*' || c == '_' || c == '~')
    {
      U64 n = mv_count_leading(str8_skip(s, i), c);
      U8 next = (i+n < s.size) ? s.str[i+n] : 0;
      U8 prev = (i > 0) ? s.str[i-1] : 0;
      B32 can_open = (next != 0 && !char_is_space(next));
      if(c == '_' && !mv_char_is_punct_or_space(prev)) { can_open = 0; } // no intraword _
      if(c == '~' && n != 2) { can_open = 0; }
      if(can_open)
      {
        U64 run = (c == '~') ? 2 : Min(n, 3);
        String8 delim = str8_substr(s, r1u64(i, i+run));
        U64 close = mv_find_closer(s, i+run, delim);
        if(close < s.size && close > i+run)
        {
          MV_SpanFlags add = 0;
          if(c == '~') { add = MV_SpanFlag_Strike; }
          else if(run == 1) { add = MV_SpanFlag_Italic; }
          else if(run == 2) { add = MV_SpanFlag_Bold; }
          else { add = MV_SpanFlag_Bold|MV_SpanFlag_Italic; }
          MV_FlushText(i);
          mv_parse_inlines(arena, out, str8_substr(s, r1u64(i+run, close)), flags|add, url);
          i = close + run;
          text_start = i;
          continue;
        }
      }
      i += n;
      continue;
    }

    i += 1;
  }
  MV_FlushText(s.size);
#undef MV_FlushText
}

////////////////////////////////
//~ Block Parsing

internal MV_Block *
mv_block_push(Arena *arena, MV_Doc *doc, MV_BlockKind kind, U64 quote_depth)
{
  MV_Block *b = push_array(arena, MV_Block, 1);
  b->kind = kind;
  b->quote_depth = quote_depth;
  SLLQueuePush(doc->first, doc->last, b);
  doc->block_count += 1;
  return b;
}

internal MV_Doc
mv_doc_from_text(Arena *arena, String8 text)
{
  Temp scratch = scratch_begin(&arena, 1);
  MV_Doc doc = {0};

  //- split into lines
  String8List line_list = {0};
  {
    U64 start = 0;
    for(U64 i = 0; i <= text.size; i += 1)
    {
      if(i == text.size || text.str[i] == '\n')
      {
        U64 end = i;
        if(end > start && text.str[end-1] == '\r') { end -= 1; }
        if(i < text.size || end > start) { str8_list_push(scratch.arena, &line_list, str8_substr(text, r1u64(start, end))); }
        start = i+1;
      }
    }
  }
  String8Array lines = str8_array_from_list(scratch.arena, &line_list);

  //- open paragraph state
  String8List para = {0};
  MV_Block *para_target = 0; // list item collecting continuation text, if any
  U64 para_quote_depth = 0;
  U64 list_content_indent = 0;
  B32 in_list = 0;

#define MV_ClosePara() do{ \
if(para.node_count) { \
String8 joined = str8_list_join(scratch.arena, &para, &(StringJoin){.sep = str8_lit_comp("\n")}); \
MV_Block *pb = para_target; \
if(pb == 0) { pb = mv_block_push(arena, &doc, MV_BlockKind_Paragraph, para_quote_depth); } \
mv_parse_inlines(arena, &pb->spans, push_str8_copy(arena, joined), 0, str8_zero()); \
} \
MemoryZeroStruct(&para); para_target = 0; \
}while(0)

  for(U64 line_idx = 0; line_idx < lines.count; line_idx += 1)
  {
    String8 raw = lines.v[line_idx];

    //- strip block quote markers
    U64 quote_depth = 0;
    String8 line = raw;
    for(;;)
    {
      U64 ib = 0;
      if(mv_indent_width(line, &ib) > 3) { break; }
      String8 rest = str8_skip(line, ib);
      if(rest.size && rest.str[0] == '>')
      {
        quote_depth += 1;
        line = str8_skip(rest, 1);
        if(line.size && line.str[0] == ' ') { line = str8_skip(line, 1); }
      }
      else { break; }
    }

    //- blank line
    if(mv_is_blank(line))
    {
      MV_ClosePara();
      // a blank line keeps a list open only if the next line is indented or a marker
      continue;
    }

    U64 indent_bytes = 0;
    U64 indent = mv_indent_width(line, &indent_bytes);
    String8 content = str8_skip(line, indent_bytes);

    //- quote depth changes end paragraphs (except lazy continuation)
    if(para.node_count && quote_depth != para_quote_depth && quote_depth != 0)
    {
      MV_ClosePara();
    }

    //- fenced code
    U8 fence_char = 0;
    U64 fence_len = 0;
    if(mv_is_fence(line, &fence_char, &fence_len))
    {
      MV_ClosePara();
      MV_Block *b = mv_block_push(arena, &doc, MV_BlockKind_Code, quote_depth);
      b->level = in_list ? list_content_indent : 0;
      b->code_lang = push_str8_copy(arena, str8_skip_chop_whitespace(str8_skip(content, fence_len)));
      U64 fence_indent = indent;
      for(line_idx += 1; line_idx < lines.count; line_idx += 1)
      {
        String8 code_line = lines.v[line_idx];
        for EachIndex(q, quote_depth)
        {
          U64 ib = 0;
          mv_indent_width(code_line, &ib);
          String8 rest = str8_skip(code_line, ib);
          if(rest.size && rest.str[0] == '>') { code_line = str8_skip(rest, 1); if(code_line.size && code_line.str[0] == ' ') { code_line = str8_skip(code_line, 1); } }
        }
        U8 cf = 0; U64 cl = 0;
        if(mv_is_fence(code_line, &cf, &cl) && cf == fence_char && cl >= fence_len && mv_is_blank(str8_skip(str8_skip_chop_whitespace(code_line), cl)))
        {
          break;
        }
        U64 strip = 0;
        for(;strip < fence_indent && strip < code_line.size && code_line.str[strip] == ' '; strip += 1);
        str8_list_push(arena, &b->code_lines, push_str8_copy(arena, str8_skip(code_line, strip)));
      }
      continue;
    }

    //- indented code (not inside paragraphs or lists)
    if(indent >= 4 && para.node_count == 0 && !in_list)
    {
      MV_Block *b = mv_block_push(arena, &doc, MV_BlockKind_Code, quote_depth);
      for(; line_idx < lines.count; line_idx += 1)
      {
        String8 code_line = lines.v[line_idx];
        U64 ib = 0;
        U64 w = mv_indent_width(code_line, &ib);
        if(w < 4 && !mv_is_blank(code_line)) { line_idx -= 1; break; }
        U64 strip = 0, sw = 0;
        for(;strip < code_line.size && sw < 4; strip += 1) { sw += (code_line.str[strip] == '\t') ? 4 : 1; }
        str8_list_push(arena, &b->code_lines, push_str8_copy(arena, str8_skip(code_line, strip)));
      }
      // trim trailing blank lines
      for(;b->code_lines.last && mv_is_blank(b->code_lines.last->string);)
      {
        String8Node *prev = 0;
        for(String8Node *n = b->code_lines.first; n != b->code_lines.last; n = n->next) { prev = n; }
        b->code_lines.last = prev;
        if(prev) { prev->next = 0; } else { b->code_lines.first = 0; }
        b->code_lines.node_count -= 1;
      }
      continue;
    }

    //- ATX heading
    if(indent <= 3 && content.size && content.str[0] == '#')
    {
      U64 n = mv_count_leading(content, '#');
      if(n <= 6 && (n == content.size || content.str[n] == ' ' || content.str[n] == '\t'))
      {
        MV_ClosePara();
        in_list = 0;
        String8 title = str8_skip_chop_whitespace(str8_skip(content, n));
        // strip optional closing #s
        U64 end = title.size;
        for(;end > 0 && title.str[end-1] == '#'; end -= 1);
        if(end == 0 || title.str[end-1] == ' ') { title = str8_skip_chop_whitespace(str8_prefix(title, end)); }
        MV_Block *b = mv_block_push(arena, &doc, MV_BlockKind_Heading, quote_depth);
        b->level = n;
        mv_parse_inlines(arena, &b->spans, push_str8_copy(arena, title), 0, str8_zero());
        continue;
      }
    }

    //- setext heading underline
    if(para.node_count && para_target == 0 && indent <= 3 && content.size)
    {
      String8 t = str8_skip_chop_whitespace(content);
      U64 eq = mv_count_leading(t, '=');
      U64 dash = mv_count_leading(t, '-');
      if((eq > 0 && eq == t.size) || (dash > 0 && dash == t.size))
      {
        String8 joined = str8_list_join(scratch.arena, &para, &(StringJoin){.sep = str8_lit_comp("\n")});
        MemoryZeroStruct(&para);
        MV_Block *b = mv_block_push(arena, &doc, MV_BlockKind_Heading, quote_depth);
        b->level = (eq > 0) ? 1 : 2;
        mv_parse_inlines(arena, &b->spans, push_str8_copy(arena, joined), 0, str8_zero());
        continue;
      }
    }

    //- horizontal rule
    if(indent <= 3 && mv_is_rule(content))
    {
      MV_ClosePara();
      in_list = 0;
      mv_block_push(arena, &doc, MV_BlockKind_Rule, quote_depth);
      continue;
    }

    //- list item
    {
      B32 ordered = 0;
      String8 marker = {0};
      U64 marker_size = mv_list_marker(content, &ordered, &marker);
      if(marker_size != 0 && (para.node_count == 0 || in_list || para_target != 0 || !ordered || str8_match(marker, str8_lit("1."), 0)))
      {
        MV_ClosePara();
        in_list = 1;
        MV_Block *b = mv_block_push(arena, &doc, MV_BlockKind_ListItem, quote_depth);
        b->level = indent/2;
        if(b->level > 8) { b->level = 8; }
        b->ordered = ordered;
        b->marker = push_str8_copy(arena, marker);
        String8 item = str8_skip(content, marker_size);
        U64 extra = 0;
        for(;extra < item.size && item.str[extra] == ' '; extra += 1);
        item = str8_skip(item, extra);
        list_content_indent = indent + marker_size + extra;
        if(item.size >= 3 && item.str[0] == '[' && item.str[2] == ']' && (item.str[1] == ' ' || item.str[1] == 'x' || item.str[1] == 'X') && (item.size == 3 || item.str[3] == ' '))
        {
          b->is_task = 1;
          b->task_done = (item.str[1] != ' ');
          item = str8_skip_chop_whitespace(str8_skip(item, 3));
        }
        para_target = b;
        para_quote_depth = quote_depth;
        if(item.size) { str8_list_push(scratch.arena, &para, item); }
        continue;
      }
    }

    //- table
    if(para.node_count == 0 && str8_find_needle(content, 0, str8_lit("|"), 0) < content.size &&
       line_idx+1 < lines.count && mv_is_table_delim_row(lines.v[line_idx+1]))
    {
      in_list = 0;
      String8List header = mv_split_table_row(scratch.arena, content);
      String8List delims = mv_split_table_row(scratch.arena, lines.v[line_idx+1]);
      U64 cols = header.node_count;
      // gather rows
      typedef struct RowNode RowNode;
      struct RowNode { RowNode *next; String8List cells; };
      RowNode *first_row = 0, *last_row = 0;
      U64 row_count = 1;
      RowNode *hdr = push_array(scratch.arena, RowNode, 1);
      hdr->cells = header;
      SLLQueuePush(first_row, last_row, hdr);
      U64 k = line_idx + 2;
      for(; k < lines.count; k += 1)
      {
        String8 row_line = lines.v[k];
        if(mv_is_blank(row_line) || str8_find_needle(row_line, 0, str8_lit("|"), 0) == row_line.size) { break; }
        RowNode *rn = push_array(scratch.arena, RowNode, 1);
        rn->cells = mv_split_table_row(scratch.arena, row_line);
        SLLQueuePush(first_row, last_row, rn);
        row_count += 1;
      }
      line_idx = k-1;
      MV_Block *b = mv_block_push(arena, &doc, MV_BlockKind_Table, quote_depth);
      b->table_cols = cols;
      b->table_rows = row_count;
      b->table_cells = push_array(arena, MV_SpanList, cols*row_count);
      b->table_aligns = push_array(arena, MV_Align, cols);
      {
        U64 c = 0;
        for(String8Node *n = delims.first; n != 0 && c < cols; n = n->next, c += 1)
        {
          String8 d = n->string;
          B32 l = (d.size && d.str[0] == ':');
          B32 r = (d.size && d.str[d.size-1] == ':');
          b->table_aligns[c] = (l && r) ? MV_Align_Center : r ? MV_Align_Right : MV_Align_Left;
        }
      }
      U64 r = 0;
      for(RowNode *rn = first_row; rn != 0; rn = rn->next, r += 1)
      {
        U64 c = 0;
        for(String8Node *n = rn->cells.first; n != 0 && c < cols; n = n->next, c += 1)
        {
          mv_parse_inlines(arena, &b->table_cells[r*cols + c], push_str8_copy(arena, n->string), r == 0 ? MV_SpanFlag_Bold : 0, str8_zero());
        }
      }
      continue;
    }

    //- continuation / new paragraph
    if(para.node_count == 0)
    {
      if(in_list && indent >= 2 && doc.last && doc.last->kind == MV_BlockKind_ListItem)
      {
        // continuation paragraph inside a list item: render as a sub-paragraph
        MV_Block *b = mv_block_push(arena, &doc, MV_BlockKind_ListItem, quote_depth);
        b->level = doc.last ? (indent/2 > 0 ? (indent/2 - 1) : 0) : 0;
        b->marker = str8_zero();
        para_target = b;
      }
      else
      {
        in_list = 0;
      }
      para_quote_depth = quote_depth;
    }
    str8_list_push(scratch.arena, &para, content);
  }
  MV_ClosePara();
#undef MV_ClosePara

  //- word count
  for(MV_Block *b = doc.first; b != 0; b = b->next)
  {
    for(MV_Span *s = b->spans.first; s != 0; s = s->next)
    {
      B32 in_word = 0;
      for EachIndex(i, s->text.size)
      {
        B32 sp = char_is_space(s->text.str[i]);
        if(!sp && !in_word) { doc.word_count += 1; }
        in_word = !sp;
      }
    }
  }

  scratch_end(scratch);
  return doc;
}

////////////////////////////////
//~ Fonts

internal FNT_Tag
mv_font_from_candidates(String8 *candidates, U64 count, FNT_Tag fallback)
{
  for EachIndex(i, count)
  {
    if(candidates[i].size != 0 && file_path_exists(candidates[i]))
    {
      return fnt_tag_from_path(candidates[i]);
    }
  }
  return fallback;
}

internal void
mv_load_fonts(void)
{
  Temp scratch = scratch_begin(0, 0);
  String8 exe_dir = get_process_info()->binary_path;
  // the raddbg data folder, relative to mdview/build/
  String8 rad_data = push_str8f(scratch.arena, "%S/../../raddebugger/data", exe_dir);
  String8 local_data = push_str8f(scratch.arena, "%S/data", exe_dir);

  String8 regular[] =
  {
#if OS_WINDOWS
    str8_lit("C:/Windows/Fonts/segoeui.ttf"),
#else
    str8_lit("/usr/share/fonts/noto/NotoSans-Regular.ttf"),
    str8_lit("/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf"),
    str8_lit("/usr/share/fonts/TTF/DejaVuSans.ttf"),
    str8_lit("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"),
#endif
    push_str8f(scratch.arena, "%S/segoeui.ttf", local_data),
    push_str8f(scratch.arena, "%S/segoeui.ttf", rad_data),
    push_str8f(scratch.arena, "%S/Roboto-Regular.ttf", rad_data),
  };
  String8 bold[] =
  {
#if OS_WINDOWS
    str8_lit("C:/Windows/Fonts/segoeuib.ttf"),
#else
    str8_lit("/usr/share/fonts/noto/NotoSans-Bold.ttf"),
    str8_lit("/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf"),
    str8_lit("/usr/share/fonts/TTF/DejaVuSans-Bold.ttf"),
    str8_lit("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"),
#endif
  };
  String8 italic[] =
  {
#if OS_WINDOWS
    str8_lit("C:/Windows/Fonts/segoeuii.ttf"),
#else
    str8_lit("/usr/share/fonts/noto/NotoSans-Italic.ttf"),
    str8_lit("/usr/share/fonts/truetype/noto/NotoSans-Italic.ttf"),
    str8_lit("/usr/share/fonts/TTF/DejaVuSans-Oblique.ttf"),
    str8_lit("/usr/share/fonts/truetype/dejavu/DejaVuSans-Oblique.ttf"),
#endif
  };
  String8 bold_italic[] =
  {
#if OS_WINDOWS
    str8_lit("C:/Windows/Fonts/segoeuiz.ttf"),
#else
    str8_lit("/usr/share/fonts/noto/NotoSans-BoldItalic.ttf"),
    str8_lit("/usr/share/fonts/truetype/noto/NotoSans-BoldItalic.ttf"),
    str8_lit("/usr/share/fonts/TTF/DejaVuSans-BoldOblique.ttf"),
    str8_lit("/usr/share/fonts/truetype/dejavu/DejaVuSans-BoldOblique.ttf"),
#endif
  };
  String8 mono[] =
  {
    push_str8f(scratch.arena, "%S/JetBrainsMono-Regular.ttf", local_data),
    push_str8f(scratch.arena, "%S/JetBrainsMono-Regular.ttf", rad_data),
#if OS_WINDOWS
    str8_lit("C:/Windows/Fonts/consola.ttf"),
#else
    str8_lit("/usr/share/fonts/TTF/DejaVuSansMono.ttf"),
    str8_lit("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"),
#endif
    push_str8f(scratch.arena, "%S/liberation-mono.ttf", rad_data),
  };
  String8 icons[] =
  {
    push_str8f(scratch.arena, "%S/icons.ttf", local_data),
    push_str8f(scratch.arena, "%S/icons.ttf", rad_data),
  };

  MV_Fonts *f = &mv->fonts;
  f->regular     = mv_font_from_candidates(regular, ArrayCount(regular), fnt_tag_zero());
  f->bold        = mv_font_from_candidates(bold, ArrayCount(bold), f->regular);
  f->italic      = mv_font_from_candidates(italic, ArrayCount(italic), f->regular);
  f->bold_italic = mv_font_from_candidates(bold_italic, ArrayCount(bold_italic), f->bold);
  f->mono        = mv_font_from_candidates(mono, ArrayCount(mono), f->regular);
  f->icons       = mv_font_from_candidates(icons, ArrayCount(icons), f->regular);
  scratch_end(scratch);
}

internal FNT_Tag
mv_font_from_flags(MV_SpanFlags flags)
{
  MV_Fonts *f = &mv->fonts;
  if(flags & MV_SpanFlag_Code) { return f->mono; }
  if((flags & MV_SpanFlag_Bold) && (flags & MV_SpanFlag_Italic)) { return f->bold_italic; }
  if(flags & MV_SpanFlag_Bold) { return f->bold; }
  if(flags & MV_SpanFlag_Italic) { return f->italic; }
  return f->regular;
}

////////////////////////////////
//~ Themes

typedef struct MV_ThemeEntry MV_ThemeEntry;
struct MV_ThemeEntry
{
  char *tags;
  U32 dark;
  U32 light;
};

read_only global MV_ThemeEntry mv_theme_table[] =
{
  {"background",               0x1f1f1fff, 0xfafafaff},
  {"alt background",           0x252526ff, 0xf0f0f0ff},
  {"border",                   0x404040ff, 0xd0d0d0ff},
  {"text",                     0xe5e5e5ff, 0x1f2328ff},
  {"weak text",                0xa4a4a4ff, 0x59636eff},
  {"hover",                    0xffffffff, 0x000000ff},
  {"focus overlay",            0x2292eb14, 0x2192eb30},
  {"focus border",             0x2392ebff, 0x2392ebff},
  {"drop_shadow",              0x0000007f, 0x183d8a33},
  {"bar background",           0x181818ff, 0xeeeeeeff},
  {"bar border",               0x333333ff, 0xd8d8d8ff},
  {"button background",        0x2d2d2dff, 0xffffffff},
  {"button border",            0x454545ff, 0xc8c8c8ff},
  {"outline background",       0x1b1b1bff, 0xf3f3f3ff},
  {"outline text",             0xb8b8b8ff, 0x3a3f45ff},
  {"current background",       0x2c3b4bff, 0xdbe9f7ff},
  {"current text",             0xffffffff, 0x0b2a4aff},
  {"heading text",             0xf2f2f2ff, 0x0f1419ff},
  {"link text",                0x58a6ffff, 0x0969daff},
  {"code background",          0x2b2b2bff, 0xeff1f3ff},
  {"code border",              0x3a3a3aff, 0xd8dee4ff},
  {"code text",                0xd7ba7dff, 0x953800ff},
  {"block text",               0xd4d4d4ff, 0x1f2328ff},
  {"quote border",             0x4a5f78ff, 0xd0d7deff},
  {"quote text",               0xb0b0b0ff, 0x59636eff},
  {"rule background",          0x3c3c3cff, 0xd8dee4ff},
  {"table border",             0x3c3c3cff, 0xd0d7deff},
  {"table header background",  0x262a30ff, 0xf3f5f7ff},
  {"table alt background",     0x232323ff, 0xf6f8faff},
  {"task background",          0x2392ebff, 0x0969daff},
  {"scroll_bar background",    0xffffff10, 0x00000010},
  {"thumb background",         0xffffff40, 0x00000040},
};

internal UI_Theme
mv_theme_make(Arena *arena, B32 light)
{
  UI_Theme theme = {0};
  theme.patterns_count = ArrayCount(mv_theme_table);
  theme.patterns = push_array(arena, UI_ThemePattern, theme.patterns_count);
  for EachIndex(i, theme.patterns_count)
  {
    MV_ThemeEntry *e = &mv_theme_table[i];
    U8 split = ' ';
    String8List tags = str8_split(arena, str8_cstring(e->tags), &split, 1, 0);
    theme.patterns[i].tags = str8_array_from_list(arena, &tags);
    theme.patterns[i].linear = linear_from_srgba(rgba_from_u32(light ? e->light : e->dark));
  }
  return theme;
}

////////////////////////////////
//~ Document Loading

internal void
mv_watch_set_path(String8 path, U64 modified)
{
  MutexScope(mv->watch_mutex)
  {
    mv->watch_path_size = Min(path.size, sizeof(mv->watch_path_buffer));
    MemoryCopy(mv->watch_path_buffer, path.str, mv->watch_path_size);
    mv->watch_modified = modified;
  }
}

internal void
mv_load_text(String8 name, String8 path, String8 text)
{
  arena_clear(mv->doc_arena);
  mv->doc_path = push_str8_copy(mv->doc_arena, path);
  mv->doc_name = push_str8_copy(mv->doc_arena, name);
  mv->doc_text = push_str8_copy(mv->doc_arena, text);
  mv->doc_error = str8_zero();
  mv->doc = mv_doc_from_text(mv->doc_arena, mv->doc_text);
  mv->layout_dirty = 1;
}

internal void
mv_open_path(String8 path, B32 keep_scroll)
{
  Temp scratch = scratch_begin(0, 0);
  String8 full = full_path_from_path(scratch.arena, path);
  FileProperties props = properties_from_file_path(full);
  String8 data = data_from_file_path(scratch.arena, full);
  if(props.modified == 0 && data.size == 0)
  {
    mv->doc_error = push_str8f(mv->arena, "Could not open %S", full);
  }
  else
  {
    mv_load_text(str8_skip_last_slash(full), full, data);
    mv_watch_set_path(full, props.modified);
    String8 title = push_str8f(scratch.arena, "%S - mdview", mv->doc_name);
    wm_window_set_title(mv->os, title);
    if(!keep_scroll)
    {
      mv->scroll_request = 1;
      mv->scroll_request_y = 0;
    }
  }
  scratch_end(scratch);
}

internal void
mv_watch_thread_entry_point(void *p)
{
  ThreadNameF("mdview_watcher");
  for(;!ins_atomic_u32_eval(&mv->quit);)
  {
    sleep_ms(250);
    Temp scratch = scratch_begin(0, 0);
    String8 path = {0};
    U64 known = 0;
    MutexScope(mv->watch_mutex)
    {
      path = push_str8_copy(scratch.arena, str8(mv->watch_path_buffer, mv->watch_path_size));
      known = mv->watch_modified;
    }
    if(path.size != 0)
    {
      FileProperties props = properties_from_file_path(path);
      if(props.modified != 0 && props.modified != known)
      {
        MutexScope(mv->watch_mutex) { mv->watch_modified = props.modified; }
        ins_atomic_u32_eval_assign(&mv->watch_changed, 1);
        wm_send_wakeup_event();
      }
    }
    scratch_end(scratch);
  }
}

internal void
mv_open_url(String8 url)
{
  sh_open_in_browser(url);
}

////////////////////////////////
//~ Layout

typedef struct MV_LayoutCtx MV_LayoutCtx;
struct MV_LayoutCtx
{
  Arena *arena;
  MV_LineChunk *first_chunk;
  MV_LineChunk *last_chunk;
  U64 line_count;
  F32 y;
  F32 font_size;
  F32 width;
  F32 tab_size;
};

internal MV_Line *
mv_line_push(MV_LayoutCtx *ctx, MV_LineKind kind, F32 height)
{
  MV_LineChunk *chunk = ctx->last_chunk;
  if(chunk == 0 || chunk->count == chunk->cap)
  {
    chunk = push_array(ctx->arena, MV_LineChunk, 1);
    chunk->cap = 256;
    chunk->v = push_array(ctx->arena, MV_Line, chunk->cap);
    SLLQueuePush(ctx->first_chunk, ctx->last_chunk, chunk);
  }
  MV_Line *line = &chunk->v[chunk->count];
  chunk->count += 1;
  ctx->line_count += 1;
  line->kind = kind;
  line->y = ctx->y;
  line->height = height;
  ctx->y += height;
  return line;
}

internal void
mv_space(MV_LayoutCtx *ctx, F32 h)
{
  mv_line_push(ctx, MV_LineKind_Spacer, h);
}

internal F32
mv_heading_scale(U64 level)
{
  local_persist F32 scales[] = {1.f, 2.0f, 1.55f, 1.3f, 1.12f, 1.0f, 0.92f};
  return scales[Clamp(0, level, 6)];
}

internal F32
mv_code_pad(F32 size)
{
  return floor_f32(size*0.3f);
}

internal F32
mv_piece_width(MV_SpanFlags flags, F32 size, F32 tab_size, String8 text)
{
  F32 piece_size = (flags & MV_SpanFlag_Code) ? floor_f32(size*0.9f) : size;
  F32 w = fnt_dim_from_tag_size_string(mv_font_from_flags(flags), piece_size, 0, tab_size, text).x;
  if(flags & MV_SpanFlag_Code) { w += 2*mv_code_pad(size); }
  return w;
}

// Wraps spans into lines of width `max_w`, emitting lines via ctx.
internal void
mv_layout_spans(MV_LayoutCtx *ctx, MV_SpanList *spans, MV_SpanFlags extra_flags, F32 size, F32 line_h, F32 indent, F32 max_w, MV_Line *proto)
{
  Temp scratch = scratch_begin(&ctx->arena, 1);
  MV_PieceNode *first = 0, *last = 0;
  U64 count = 0;
  F32 x = 0;
  B32 first_line = 1;

#define MV_EmitLine() do{ \
MV_Line *l = mv_line_push(ctx, MV_LineKind_Text, line_h); \
*l = (MV_Line){.kind = MV_LineKind_Text, .y = l->y, .height = line_h, .indent = indent, \
.quote_depth = proto->quote_depth, .heading_level = proto->heading_level}; \
if(first_line) { l->bullet = proto->bullet; l->bullet_is_task = proto->bullet_is_task; l->bullet_task_done = proto->bullet_task_done; } \
l->pieces = push_array(ctx->arena, MV_Piece, count); \
l->piece_count = count; \
U64 _i = 0; for(MV_PieceNode *_n = first; _n != 0; _n = _n->next, _i += 1) { l->pieces[_i] = _n->v; } \
first = last = 0; count = 0; x = 0; first_line = 0; \
}while(0)

  for(MV_Span *span = spans->first; span != 0; span = span->next)
  {
    MV_SpanFlags flags = span->flags | extra_flags;
    if(flags & MV_SpanFlag_Break)
    {
      MV_EmitLine();
      continue;
    }
    String8 text = span->text;

    // code spans are kept whole when they fit; otherwise split by words
    U64 pos = 0;
    for(;pos < text.size;)
    {
      // next token: word + trailing spaces
      U64 word_end = pos;
      for(;word_end < text.size && text.str[word_end] != ' '; word_end += 1);
      U64 tok_end = word_end;
      for(;tok_end < text.size && text.str[tok_end] == ' '; tok_end += 1);
      if(flags & MV_SpanFlag_Code)
      {
        // try to keep the whole code span together
        F32 whole = mv_piece_width(flags, size, ctx->tab_size, str8_skip(text, pos));
        if(x + whole <= max_w || x == 0)
        {
          word_end = tok_end = text.size;
        }
      }
      String8 word = str8_substr(text, r1u64(pos, word_end));
      String8 tok = str8_substr(text, r1u64(pos, tok_end));
      F32 word_w = mv_piece_width(flags, size, ctx->tab_size, word);
      if(x > 0 && x + word_w > max_w)
      {
        // trim trailing spaces of the last piece before wrapping
        if(last != 0)
        {
          String8 t = last->v.text;
          U64 e = t.size;
          for(;e > 0 && t.str[e-1] == ' '; e -= 1);
          if(e != t.size)
          {
            last->v.text = str8_prefix(t, e);
            last->v.width = mv_piece_width(last->v.flags, size, ctx->tab_size, last->v.text);
          }
        }
        MV_EmitLine();
      }
      if(x == 0)
      {
        // skip leading spaces on a fresh line
        U64 k = 0;
        for(;k < tok.size && tok.str[k] == ' '; k += 1);
        tok = str8_skip(tok, k);
        if(tok.size == 0) { pos = tok_end; continue; }
      }
      // merge into the last piece if styles match (and it's not a code span)
      if(last != 0 && last->v.flags == flags && str8_match(last->v.url, span->url, 0) && !(flags & MV_SpanFlag_Code) &&
         last->v.text.str + last->v.text.size == tok.str)
      {
        last->v.text.size += tok.size;
        last->v.width = mv_piece_width(flags, size, ctx->tab_size, last->v.text);
      }
      else
      {
        MV_PieceNode *n = push_array(scratch.arena, MV_PieceNode, 1);
        n->v.text = tok;
        n->v.url = span->url;
        n->v.flags = flags;
        n->v.width = mv_piece_width(flags, size, ctx->tab_size, tok);
        SLLQueuePush(first, last, n);
        count += 1;
      }
      x += mv_piece_width(flags, size, ctx->tab_size, tok);
      pos = tok_end;
    }
  }
  if(count > 0 || first_line)
  {
    MV_EmitLine();
  }
#undef MV_EmitLine
  scratch_end(scratch);
}

internal MV_Layout
mv_layout_from_doc(Arena *arena, MV_Doc *doc, F32 width, F32 font_size)
{
  MV_LayoutCtx ctx_ = {arena};
  MV_LayoutCtx *ctx = &ctx_;
  ctx->font_size = font_size;
  ctx->width = width;
  ctx->tab_size = font_size*4.f;
  Temp scratch = scratch_begin(&arena, 1);

  typedef struct OutlineNode OutlineNode;
  struct OutlineNode { OutlineNode *next; MV_OutlineItem v; };
  OutlineNode *first_outline = 0, *last_outline = 0;
  U64 outline_count = 0;

  F32 body_line_h = floor_f32(font_size*1.6f);
  F32 para_gap = floor_f32(font_size*0.75f);
  F32 quote_indent = floor_f32(font_size*1.25f);
  F32 list_indent = floor_f32(font_size*1.5f);

  mv_space(ctx, floor_f32(font_size*1.5f));
  MV_BlockKind prev_kind = MV_BlockKind_Paragraph;
  B32 first_block = 1;
  for(MV_Block *b = doc->first; b != 0; b = b->next)
  {
    F32 base_indent = b->quote_depth*quote_indent;
    F32 avail = width - base_indent;
    MV_Line proto = {0};
    proto.quote_depth = b->quote_depth;

    //- vertical spacing between blocks
    if(!first_block)
    {
      B32 tight_list = (b->kind == MV_BlockKind_ListItem && prev_kind == MV_BlockKind_ListItem);
      if(b->kind == MV_BlockKind_Heading)
      {
        mv_space(ctx, floor_f32(font_size*(b->level <= 2 ? 1.4f : 1.0f)));
      }
      else if(tight_list)
      {
        mv_space(ctx, floor_f32(font_size*0.2f));
      }
      else
      {
        mv_space(ctx, para_gap);
      }
    }
    first_block = 0;
    prev_kind = b->kind;

    switch(b->kind)
    {
      case MV_BlockKind_Paragraph:
      {
        mv_layout_spans(ctx, &b->spans, 0, font_size, body_line_h, base_indent, avail, &proto);
      }break;

      case MV_BlockKind_Heading:
      {
        F32 size = floor_f32(font_size*mv_heading_scale(b->level));
        F32 line_h = floor_f32(size*1.35f);
        proto.heading_level = b->level;
        OutlineNode *on = push_array(scratch.arena, OutlineNode, 1);
        on->v.level = b->level;
        on->v.y = ctx->y;
        {
          String8List parts = {0};
          for(MV_Span *s = b->spans.first; s != 0; s = s->next) { str8_list_push(scratch.arena, &parts, s->text); }
          on->v.text = str8_list_join(arena, &parts, 0);
        }
        SLLQueuePush(first_outline, last_outline, on);
        outline_count += 1;
        mv_layout_spans(ctx, &b->spans, MV_SpanFlag_Bold, size, line_h, base_indent, avail, &proto);
        if(b->level <= 2)
        {
          mv_space(ctx, floor_f32(font_size*0.3f));
          MV_Line *rule = mv_line_push(ctx, MV_LineKind_Rule, 1.f);
          rule->indent = base_indent;
          rule->quote_depth = b->quote_depth;
          rule->heading_rule = 1;
        }
      }break;

      case MV_BlockKind_ListItem:
      {
        F32 indent = base_indent + list_indent*(b->level+1);
        proto.bullet = b->marker;
        if(b->marker.size && !b->ordered)
        {
          local_persist String8 bullets[] = {str8_lit_comp("\xe2\x80\xa2"), str8_lit_comp("\xe2\x97\xa6"), str8_lit_comp("\xe2\x96\xaa")};
          proto.bullet = bullets[b->level%ArrayCount(bullets)];
        }
        proto.bullet_is_task = b->is_task;
        proto.bullet_task_done = b->task_done;
        if(b->is_task) { indent += floor_f32(font_size*0.4f); }
        F32 line_avail = width - indent;
        mv_layout_spans(ctx, &b->spans, 0, font_size, body_line_h, indent, line_avail, &proto);
      }break;

      case MV_BlockKind_Code:
      {
        F32 code_size = floor_f32(font_size*0.9f);
        F32 line_h = floor_f32(code_size*1.5f);
        F32 indent = base_indent + (b->level ? list_indent : 0);
        F32 pad = floor_f32(font_size*0.6f);
        MV_Line *top = mv_line_push(ctx, MV_LineKind_Code, pad);
        top->indent = indent; top->quote_depth = b->quote_depth; top->code_bg = 1; top->code_first = 1;
        for(String8Node *n = b->code_lines.first; n != 0; n = n->next)
        {
          MV_Line *l = mv_line_push(ctx, MV_LineKind_Code, line_h);
          l->indent = indent;
          l->quote_depth = b->quote_depth;
          l->code_bg = 1;
          l->pieces = push_array(arena, MV_Piece, 1);
          l->piece_count = 1;
          l->pieces[0].text = n->string;
          l->pieces[0].flags = MV_SpanFlag_Code;
        }
        MV_Line *bot = mv_line_push(ctx, MV_LineKind_Code, pad);
        bot->indent = indent; bot->quote_depth = b->quote_depth; bot->code_bg = 1; bot->code_last = 1;
      }break;

      case MV_BlockKind_Rule:
      {
        mv_space(ctx, floor_f32(font_size*0.5f));
        MV_Line *l = mv_line_push(ctx, MV_LineKind_Rule, 2.f);
        l->indent = base_indent;
        l->quote_depth = b->quote_depth;
        mv_space(ctx, floor_f32(font_size*0.5f));
      }break;

      case MV_BlockKind_Table:
      {
        F32 cell_pad = floor_f32(font_size*0.6f);
        F32 *col_w = push_array(arena, F32, b->table_cols);
        for EachIndex(r, b->table_rows)
        {
          for EachIndex(c, b->table_cols)
          {
            F32 w = 0;
            for(MV_Span *s = b->table_cells[r*b->table_cols + c].first; s != 0; s = s->next)
            {
              w += mv_piece_width(s->flags, font_size, ctx->tab_size, s->text);
            }
            col_w[c] = Max(col_w[c], w + 2*cell_pad);
          }
        }
        F32 total = 0;
        for EachIndex(c, b->table_cols) { total += col_w[c]; }
        if(total > avail && total > 0)
        {
          F32 scale = avail/total;
          for EachIndex(c, b->table_cols) { col_w[c] = floor_f32(col_w[c]*scale); }
        }
        F32 row_h = floor_f32(font_size*2.0f);
        for EachIndex(r, b->table_rows)
        {
          MV_Line *l = mv_line_push(ctx, MV_LineKind_TableRow, row_h);
          l->indent = base_indent;
          l->quote_depth = b->quote_depth;
          l->table = b;
          l->table_row = r;
          l->table_col_widths = col_w;
        }
      }break;
    }
  }
  mv_space(ctx, floor_f32(font_size*4.f));

  //- flatten
  MV_Layout layout = {0};
  layout.line_count = ctx->line_count;
  layout.lines = push_array(arena, MV_Line, layout.line_count);
  {
    U64 i = 0;
    for(MV_LineChunk *c = ctx->first_chunk; c != 0; c = c->next)
    {
      MemoryCopy(layout.lines + i, c->v, sizeof(MV_Line)*c->count);
      i += c->count;
    }
  }
  layout.outline_count = outline_count;
  layout.outline = push_array(arena, MV_OutlineItem, outline_count);
  {
    U64 i = 0;
    for(OutlineNode *n = first_outline; n != 0; n = n->next, i += 1) { layout.outline[i] = n->v; }
  }
  layout.total_height = ctx->y;
  scratch_end(scratch);
  return layout;
}

////////////////////////////////
//~ UI Building

internal F32
mv_font_size(void)
{
  F32 dpi = wm_dpi_from_window(mv->os);
  if(dpi <= 0) { dpi = 96.f; }
  return floor_f32(15.f * mv->zoom * dpi/96.f);
}

internal void
mv_build_piece(MV_Piece *piece, F32 size, F32 line_h, U64 line_idx, U64 piece_idx, B32 heading)
{
  MV_SpanFlags flags = piece->flags;
  B32 is_code = !!(flags & MV_SpanFlag_Code);
  B32 is_link = !!(flags & MV_SpanFlag_Link) && piece->url.size;
  F32 piece_size = is_code ? floor_f32(size*0.9f) : size;
  char *tag = heading ? "heading" : "block";
  if(is_code) { tag = "code"; }
  if(is_link) { tag = "link"; }
  UI_TagF(tag)
  {
    UI_BoxFlags box_flags = UI_BoxFlag_DrawText|UI_BoxFlag_DisableTextTrunc;
    UI_Key key = ui_key_zero();
    if(is_link)
    {
      box_flags |= UI_BoxFlag_Clickable;
      key = ui_key_from_stringf(ui_key_zero(), "piece_%I64u_%I64u", line_idx, piece_idx);
    }
    if(is_code)
    {
      box_flags |= UI_BoxFlag_DrawBackground|UI_BoxFlag_DrawBorder;
    }
    ui_set_next_font(mv_font_from_flags(flags));
    ui_set_next_font_size(piece_size);
    ui_set_next_text_padding(is_code ? mv_code_pad(size) : 0);
    ui_set_next_pref_width(ui_px(piece->width, 1.f));
    ui_set_next_pref_height(ui_px(is_code ? floor_f32(line_h*0.8f) : line_h, 1.f));
    if(is_code) { mv_set_next_corner_radius(floor_f32(size*0.25f)); }
    if(is_link) { ui_set_next_hover_cursor(WM_Cursor_HandPoint); }
    UI_Box *box = ui_build_box_from_key(box_flags, key);
    Vec4F32 color = ui_color_from_name(str8_lit("text"));
    DR_FStrNode node = {0, {piece->text, {box->font, box->text_raster_flags, color, piece_size, 0, 0}}};
    if(is_link) { node.v.params.underline_thickness = ClampBot(1.f, floor_f32(size/14.f)); }
    if(flags & MV_SpanFlag_Strike) { node.v.params.strikethrough_thickness = ClampBot(1.f, floor_f32(size/14.f)); }
    DR_FStrList fstrs = {&node, &node, 1, piece->text.size};
    ui_box_equip_display_fstrs(box, &fstrs);
    if(is_link)
    {
      UI_Signal sig = ui_signal_from_box(box);
      if(ui_clicked(sig)) { mv_open_url(piece->url); }
      if(ui_hovering(sig)) UI_Tooltip
      {
        UI_TagF("weak") ui_label(piece->url);
      }
    }
  }
}

internal void
mv_build_line(MV_Line *line, U64 line_idx, F32 font_size, F32 content_w)
{
  F32 quote_indent = floor_f32(font_size*1.25f);
  F32 bar_w = ClampBot(3.f, floor_f32(font_size*0.22f));
  F32 heading_size = floor_f32(font_size*mv_heading_scale(line->heading_level));
  F32 size = line->heading_level ? heading_size : font_size;

  ui_set_next_child_layout_axis(Axis2_X);
  ui_set_next_pref_width(ui_px(content_w, 1.f));
  ui_set_next_pref_height(ui_px(line->height, 1.f));
  UI_Box *row = ui_build_box_from_key(0, ui_key_zero());
  UI_Parent(row) UI_PrefHeight(ui_pct(1, 0))
  {
    //- quote bars
    F32 x = 0;
    for EachIndex(q, line->quote_depth)
    {
      UI_TagF("quote")
      {
        ui_set_next_pref_width(ui_px(bar_w, 1.f));
        ui_build_box_from_key(UI_BoxFlag_DrawSideLeft, ui_key_zero());
      }
      ui_spacer(ui_px(quote_indent - bar_w, 1.f));
      x += quote_indent;
    }

    switch(line->kind)
    {
      default:{}break;

      case MV_LineKind_Rule: UI_TagF("rule")
      {
        ui_spacer(ui_px(line->indent - x, 1.f));
        ui_set_next_pref_width(ui_px(content_w - line->indent, 1.f));
        ui_build_box_from_key(UI_BoxFlag_DrawBackground, ui_key_zero());
      }break;

      case MV_LineKind_Code: UI_TagF("code")
      {
        ui_spacer(ui_px(line->indent - x, 1.f));
        ui_set_next_pref_width(ui_px(content_w - line->indent, 1.f));
        ui_set_next_child_layout_axis(Axis2_X);
        F32 radius = floor_f32(font_size*0.35f);
        ui_set_next_corner_radius_00(line->code_first ? radius : 0);
        ui_set_next_corner_radius_10(line->code_first ? radius : 0);
        ui_set_next_corner_radius_01(line->code_last ? radius : 0);
        ui_set_next_corner_radius_11(line->code_last ? radius : 0);
        UI_Box *bg = ui_build_box_from_key(UI_BoxFlag_DrawBackground|UI_BoxFlag_Clip, ui_key_zero());
        if(line->piece_count) UI_Parent(bg)
        {
          ui_spacer(ui_px(floor_f32(font_size*0.9f), 1.f));
          ui_set_next_font(mv->fonts.mono);
          ui_set_next_font_size(floor_f32(font_size*0.9f));
          ui_set_next_text_padding(0);
          ui_set_next_pref_width(ui_text_dim(0, 1.f));
          UI_TagF("block")
          {
            UI_Box *text = ui_build_box_from_key(UI_BoxFlag_DrawText|UI_BoxFlag_DisableTextTrunc, ui_key_zero());
            ui_box_equip_display_string(text, line->pieces[0].text);
          }
        }
      }break;

      case MV_LineKind_Text:
      {
        F32 bullet_w = floor_f32(font_size*1.5f);
        if(line->bullet.size || line->bullet_is_task)
        {
          F32 bullet_x = line->indent - bullet_w - (line->bullet_is_task ? floor_f32(font_size*0.4f) : 0);
          ui_spacer(ui_px(ClampBot(0, bullet_x - x), 1.f));
          if(line->bullet_is_task)
          {
            // checkbox
            F32 box_size = floor_f32(font_size*0.95f);
            ui_set_next_child_layout_axis(Axis2_Y);
            ui_set_next_pref_width(ui_px(bullet_w, 1.f));
            UI_Box *cell = ui_build_box_from_key(0, ui_key_zero());
            UI_Parent(cell)
            {
              ui_spacer(ui_px(floor_f32((line->height - box_size)/2), 1.f));
              UI_TagF(line->bullet_task_done ? "task" : "code")
              {
                ui_set_next_pref_width(ui_px(box_size, 1.f));
                ui_set_next_pref_height(ui_px(box_size, 1.f));
                mv_set_next_corner_radius(floor_f32(box_size*0.25f));
                ui_set_next_text_alignment(UI_TextAlign_Center);
                ui_set_next_font(mv->fonts.bold);
                ui_set_next_font_size(floor_f32(font_size*0.8f));
                ui_set_next_text_color(v4f32(1, 1, 1, 1));
                UI_Box *check = ui_build_box_from_key(UI_BoxFlag_DrawBackground|UI_BoxFlag_DrawBorder|UI_BoxFlag_DrawText, ui_key_zero());
                ui_box_equip_display_string(check, line->bullet_task_done ? str8_lit("\xe2\x9c\x93") : str8_lit(" "));
              }
            }
            ui_spacer(ui_px(floor_f32(font_size*0.4f), 1.f));
          }
          else UI_TagF("weak")
          {
            ui_set_next_pref_width(ui_px(bullet_w, 1.f));
            ui_set_next_text_alignment(UI_TextAlign_Right);
            ui_set_next_text_padding(floor_f32(font_size*0.5f));
            UI_Box *bullet = ui_build_box_from_key(UI_BoxFlag_DrawText|UI_BoxFlag_DisableTextTrunc, ui_key_zero());
            ui_box_equip_display_string(bullet, line->bullet);
          }
        }
        else
        {
          ui_spacer(ui_px(ClampBot(0, line->indent - x), 1.f));
        }
        UI_TagF(line->quote_depth ? "quote" : "")
          for EachIndex(i, line->piece_count)
        {
          mv_build_piece(&line->pieces[i], size, line->height, line_idx, i, line->heading_level != 0);
        }
      }break;

      case MV_LineKind_TableRow: UI_TagF("table")
      {
        ui_spacer(ui_px(line->indent - x, 1.f));
        MV_Block *t = line->table;
        B32 header = (line->table_row == 0);
        B32 alt = (line->table_row % 2 == 0) && !header;
        UI_TagF(header ? "header" : alt ? "alt" : "")
          for EachIndex(c, t->table_cols)
        {
          ui_set_next_pref_width(ui_px(line->table_col_widths[c], 1.f));
          ui_set_next_text_padding(floor_f32(font_size*0.6f));
          UI_TextAlign align = UI_TextAlign_Left;
          if(t->table_aligns[c] == MV_Align_Center) { align = UI_TextAlign_Center; }
          if(t->table_aligns[c] == MV_Align_Right) { align = UI_TextAlign_Right; }
          ui_set_next_text_alignment(align);
          UI_BoxFlags flags = UI_BoxFlag_DrawText|UI_BoxFlag_DrawSideRight|UI_BoxFlag_DrawSideBottom;
          if(c == 0) { flags |= UI_BoxFlag_DrawSideLeft; }
          if(header) { flags |= UI_BoxFlag_DrawSideTop; }
          if(header || alt) { flags |= UI_BoxFlag_DrawBackground; }
          UI_Box *cell = ui_build_box_from_key(flags, ui_key_zero());
          DR_FStrList fstrs = {0};
          for(MV_Span *s = t->table_cells[line->table_row*t->table_cols + c].first; s != 0; s = s->next)
          {
            FNT_Tag font = mv_font_from_flags(s->flags);
            Vec4F32 color = cell->text_color;
            if(s->flags & MV_SpanFlag_Code) UI_TagF("code") { color = ui_color_from_name(str8_lit("text")); }
            if(s->flags & MV_SpanFlag_Link) UI_TagF("link") { color = ui_color_from_name(str8_lit("text")); }
            F32 piece_size = (s->flags & MV_SpanFlag_Code) ? floor_f32(font_size*0.9f) : font_size;
            DR_FStrParams params = {font, cell->text_raster_flags, color, piece_size, 0, 0};
            if(s->flags & MV_SpanFlag_Strike) { params.strikethrough_thickness = 1.f; }
            if(s->flags & MV_SpanFlag_Link) { params.underline_thickness = 1.f; }
            dr_fstrs_push_new(ui_build_arena(), &fstrs, &params, s->text);
          }
          ui_box_equip_display_fstrs(cell, &fstrs);
        }
      }break;
    }
  }
}

internal void
mv_scroll_to(F32 y)
{
  mv->scroll_request = 1;
  mv->scroll_request_y = y;
}

internal void
mv_build_ui(F32 font_size)
{
  F32 bar_h = floor_f32(font_size*2.4f);
  Rng2F32 client = wm_client_rect_from_window(mv->os);
  Vec2F32 client_dim = dim_2f32(client);
  F32 outline_w = mv->show_outline && mv->layout.outline_count ? floor_f32(Min(font_size*17.f, client_dim.x*0.3f)) : 0;
  F32 content_area_w = client_dim.x - outline_w;
  F32 content_area_h = client_dim.y - bar_h;
  F32 scroll_bar_w = floor_f32(font_size*0.7f);

  //- relayout if needed
  F32 max_col_w = floor_f32(font_size*48.f);
  F32 margin = floor_f32(font_size*2.f);
  F32 col_w = Min(max_col_w, content_area_w - 2*margin - scroll_bar_w);
  col_w = ClampBot(font_size*10.f, col_w);
  if(mv->layout_dirty || col_w != mv->layout_width || font_size != mv->layout_font_size)
  {
    arena_clear(mv->layout_arena);
    mv->layout = mv_layout_from_doc(mv->layout_arena, &mv->doc, col_w, font_size);
    mv->layout_width = col_w;
    mv->layout_font_size = font_size;
    mv->layout_dirty = 0;
  }
  MV_Layout *layout = &mv->layout;

  //- previous-frame content box state (for culling & outline highlight)
  UI_Key content_key = ui_key_from_string(ui_key_zero(), str8_lit("###content"));
  UI_Box *prev_content = ui_box_from_key(content_key);
  F32 view_y = ui_box_is_nil(prev_content) ? 0 : prev_content->view_off.y;
  F32 max_scroll = ClampBot(0, layout->total_height - content_area_h);

  //- column: top bar + body
  ui_set_next_child_layout_axis(Axis2_Y);
  ui_set_next_pref_width(ui_px(client_dim.x, 1.f));
  ui_set_next_pref_height(ui_px(client_dim.y, 1.f));
  UI_Box *main_col = ui_build_box_from_string(0, str8_lit("###main"));
  UI_Parent(main_col)
  {
    //- top bar
    UI_TagF("bar")
    {
      ui_set_next_child_layout_axis(Axis2_X);
      ui_set_next_pref_width(ui_pct(1, 0));
      ui_set_next_pref_height(ui_px(bar_h, 1.f));
      UI_Box *bar = ui_build_box_from_string(UI_BoxFlag_DrawBackground|UI_BoxFlag_DrawSideBottom, str8_lit("###bar"));
      UI_Parent(bar) UI_PrefHeight(ui_pct(1, 0))
      {
        ui_spacer(ui_px(floor_f32(font_size*0.6f), 1.f));
        F32 btn_h = floor_f32(bar_h*0.7f);
        UI_Box *bv = 0;

#define MV_Button(label, tip, out_clicked) do{ \
ui_set_next_child_layout_axis(Axis2_Y); \
ui_set_next_pref_width(ui_children_sum(1.f)); \
bv = ui_build_box_from_key(0, ui_key_zero()); \
UI_Parent(bv) { \
UI_PrefWidth(ui_px(0, 0)) ui_spacer(ui_px(floor_f32((bar_h - btn_h)/2), 1.f)); \
UI_TagF("button") UI_PrefHeight(ui_px(btn_h, 1.f)) UI_PrefWidth(ui_text_dim(floor_f32(font_size*0.7f), 1.f)) UI_CornerRadius(floor_f32(font_size*0.3f)) UI_TextAlignment(UI_TextAlign_Center) { \
UI_Signal _s = ui_button(str8_lit(label)); \
(out_clicked) = ui_clicked(_s); \
if(ui_hovering(_s)) UI_Tooltip { ui_label(str8_lit(tip)); } \
} \
} \
ui_spacer(ui_px(floor_f32(font_size*0.35f), 1.f)); \
}while(0)

        B32 clicked = 0;
        MV_Button("Open###open_btn", "Open a Markdown file (Ctrl+O)", clicked);
        if(clicked) { mv->pick_file_requested = 1; }
        MV_Button("Outline###outline_btn", "Toggle outline (Ctrl+B)", clicked);
        if(clicked) { mv->show_outline = !mv->show_outline; }
        MV_Button("Reload###reload_btn", "Reload file (F5)", clicked);
        if(clicked && mv->doc_path.size) { mv_open_path(mv->doc_path, 1); }
        MV_Button("A-###zoom_out_btn", "Zoom out (Ctrl+-)", clicked);
        if(clicked) { mv->zoom = Max(0.5f, mv->zoom - 0.1f); }
        MV_Button("A+###zoom_in_btn", "Zoom in (Ctrl++)", clicked);
        if(clicked) { mv->zoom = Min(3.0f, mv->zoom + 0.1f); }
        if(mv->light_theme)
        {
          MV_Button("Dark###theme_btn", "Switch theme (Ctrl+T)", clicked);
        }
        else
        {
          MV_Button("Light###theme_btn", "Switch theme (Ctrl+T)", clicked);
        }
        if(clicked) { mv->light_theme = !mv->light_theme; }
#undef MV_Button

        ui_spacer(ui_px(floor_f32(font_size*0.6f), 1.f));
        UI_Font(mv->fonts.bold) UI_PrefWidth(ui_text_dim(0, 1.f))
        {
          ui_label(mv->doc_name.size ? mv->doc_name : str8_lit("Welcome"));
        }
        ui_spacer(ui_px(floor_f32(font_size*0.6f), 1.f));
        UI_TagF("weak") UI_PrefWidth(ui_pct(1, 0))
        {
          if(mv->doc_error.size)
          {
            UI_TagF("link") ui_label(mv->doc_error);
          }
          else
          {
            ui_label(mv->doc_path.size ? mv->doc_path : str8_lit("drop a .md file here"));
          }
        }
        UI_TagF("weak") UI_PrefWidth(ui_text_dim(floor_f32(font_size*0.8f), 1.f))
        {
          F32 pct = max_scroll > 0 ? 100.f*Clamp(0, view_y, max_scroll)/max_scroll : 100.f;
          ui_labelf("%I64u words  \xc2\xb7  %.0f%%  \xc2\xb7  %.0f%%", mv->doc.word_count, pct, mv->zoom*100.f);
        }
      }
    }

    //- body: outline + content
    ui_set_next_child_layout_axis(Axis2_X);
    ui_set_next_pref_width(ui_pct(1, 0));
    ui_set_next_pref_height(ui_px(content_area_h, 1.f));
    UI_Box *body = ui_build_box_from_string(0, str8_lit("###body"));
    UI_Parent(body)
    {
      //- outline
      if(outline_w > 0) UI_TagF("outline")
      {
        // current section = last heading at or above the top of the view
        U64 current = 0;
        for EachIndex(i, layout->outline_count)
        {
          if(layout->outline[i].y <= view_y + font_size*2.f) { current = i; }
        }
        ui_set_next_child_layout_axis(Axis2_Y);
        ui_set_next_pref_width(ui_px(outline_w, 1.f));
        ui_set_next_pref_height(ui_pct(1, 0));
        UI_Box *outline = ui_build_box_from_string(UI_BoxFlag_DrawBackground|UI_BoxFlag_DrawSideRight|UI_BoxFlag_Clip|
                                                   UI_BoxFlag_ViewScrollY|UI_BoxFlag_ViewClampY|UI_BoxFlag_AllowOverflowY,
                                                   str8_lit("###outline"));
        UI_Parent(outline) UI_PrefWidth(ui_pct(1, 0)) UI_PrefHeight(ui_px(floor_f32(font_size*1.9f), 1.f))
        {
          ui_spacer(ui_px(floor_f32(font_size*0.8f), 1.f));
          UI_TagF("weak") UI_FontSize(floor_f32(font_size*0.8f)) UI_TextPadding(floor_f32(font_size*1.f))
          {
            ui_label(str8_lit("CONTENTS"));
          }
          for EachIndex(i, layout->outline_count)
          {
            MV_OutlineItem *item = &layout->outline[i];
            if(item->level > 4) { continue; }
            B32 is_current = (i == current);
            UI_TagF(is_current ? "current" : "")
              UI_TextPadding(floor_f32(font_size*(0.8f + 0.9f*(item->level-1))))
              UI_Font(item->level == 1 ? mv->fonts.bold : mv->fonts.regular)
              UI_CornerRadius(floor_f32(font_size*0.3f))
            {
              UI_BoxFlags flags = UI_BoxFlag_Clickable|UI_BoxFlag_DrawText|UI_BoxFlag_DrawHotEffects|UI_BoxFlag_DrawActiveEffects;
              if(is_current) { flags |= UI_BoxFlag_DrawBackground; }
              ui_set_next_hover_cursor(WM_Cursor_HandPoint);
              UI_Box *entry = ui_build_box_from_stringf(flags, "%S###outline_%I64u", item->text, i);
              UI_Signal sig = ui_signal_from_box(entry);
              if(ui_clicked(sig))
              {
                mv_scroll_to(item->y - font_size*0.5f);
              }
            }
          }
          ui_spacer(ui_px(floor_f32(font_size*2.f), 1.f));
        }
      }

      //- content
      ui_set_next_child_layout_axis(Axis2_Y);
      ui_set_next_pref_width(ui_px(content_area_w, 1.f));
      ui_set_next_pref_height(ui_pct(1, 0));
      UI_Box *content = ui_build_box_from_key(UI_BoxFlag_Clip|UI_BoxFlag_ViewScrollY|UI_BoxFlag_ViewClampY|
                                              UI_BoxFlag_AllowOverflowY|UI_BoxFlag_Scroll|UI_BoxFlag_MouseClickable,
                                              content_key);

      //- apply keyboard scrolling & scroll requests
      {
        F32 page = content_area_h*0.9f;
        F32 step = font_size*3.f;
        F32 target = content->view_off_target.y;
        B32 changed = 0;
        if(ui_key_press(0, WM_Key_PageDown) || ui_key_press(0, WM_Key_Space)) { target += page; changed = 1; }
        if(ui_key_press(0, WM_Key_PageUp) || ui_key_press(WM_Modifier_Shift, WM_Key_Space)) { target -= page; changed = 1; }
        if(ui_key_press(0, WM_Key_Down)) { target += step; changed = 1; }
        if(ui_key_press(0, WM_Key_Up))   { target -= step; changed = 1; }
        if(ui_key_press(0, WM_Key_Home) || ui_key_press(WM_Modifier_Ctrl, WM_Key_Home)) { target = 0; changed = 1; }
        if(ui_key_press(0, WM_Key_End) || ui_key_press(WM_Modifier_Ctrl, WM_Key_End)) { target = max_scroll; changed = 1; }
        if(mv->scroll_request)
        {
          target = mv->scroll_request_y;
          changed = 1;
          mv->scroll_request = 0;
        }
        if(changed)
        {
          content->view_off_target.y = Clamp(0, target, max_scroll);
        }
        // keep the view in range when the layout shrinks (resize, reload)
        if(content->view_off_target.y > max_scroll) { content->view_off_target.y = max_scroll; }
        if(content->view_off.y > max_scroll) { content->view_off.y = max_scroll; }
      }

      UI_Parent(content)
      {
        // cull to the visible range; spacers keep the total height for scrolling
        F32 vis_y0 = view_y - content_area_h;
        F32 vis_y1 = view_y + content_area_h*2.f;
        F32 x_off = floor_f32(ClampBot(margin, (content_area_w - scroll_bar_w - col_w)/2));
        U64 first_vis = layout->line_count;
        U64 last_vis = 0;
        for EachIndex(i, layout->line_count)
        {
          MV_Line *l = &layout->lines[i];
          if(l->y + l->height >= vis_y0 && l->y <= vis_y1)
          {
            first_vis = Min(first_vis, i);
            last_vis = i+1;
          }
        }
        if(first_vis < last_vis)
        {
          ui_spacer(ui_px(layout->lines[first_vis].y, 1.f));
          for(U64 i = first_vis; i < last_vis; i += 1)
          {
            MV_Line *l = &layout->lines[i];
            ui_set_next_child_layout_axis(Axis2_X);
            ui_set_next_pref_width(ui_pct(1, 0));
            ui_set_next_pref_height(ui_px(l->height, 1.f));
            UI_Box *row = ui_build_box_from_key(0, ui_key_zero());
            UI_Parent(row)
            {
              ui_spacer(ui_px(x_off, 1.f));
              mv_build_line(l, i, font_size, col_w);
            }
          }
          MV_Line *last = &layout->lines[last_vis-1];
          ui_spacer(ui_px(layout->total_height - (last->y + last->height), 1.f));
        }
        else
        {
          ui_spacer(ui_px(layout->total_height, 1.f));
        }
      }

      //- scroll bar (floating over the content's right edge)
      if(max_scroll > 0) UI_TagF("scroll_bar")
      {
        Rng2F32 content_rect = r2f32p(outline_w, bar_h, client_dim.x, client_dim.y);
        F32 track_h = content_area_h;
        F32 thumb_h = ClampBot(font_size*2.f, track_h * content_area_h / layout->total_height);
        F32 thumb_y = (track_h - thumb_h) * Clamp(0, view_y, max_scroll) / max_scroll;
        F32 pad = floor_f32(scroll_bar_w*0.25f);
        ui_set_next_fixed_x(content_rect.x1 - scroll_bar_w);
        ui_set_next_fixed_y(content_rect.y0);
        ui_set_next_fixed_width(scroll_bar_w);
        ui_set_next_fixed_height(track_h);
        ui_set_next_child_layout_axis(Axis2_Y);
        UI_Box *track = ui_build_box_from_string(UI_BoxFlag_Floating|UI_BoxFlag_MouseClickable, str8_lit("###scroll_track"));
        UI_Signal track_sig = ui_signal_from_box(track);
        if(ui_pressed(track_sig))
        {
          F32 click_y = ui_mouse().y - content_rect.y0;
          F32 target = content->view_off_target.y + (click_y < thumb_y ? -content_area_h : content_area_h);
          content->view_off_target.y = Clamp(0, target, max_scroll);
        }
        UI_Parent(track) UI_TagF("thumb")
        {
          ui_spacer(ui_px(thumb_y, 1.f));
          ui_set_next_child_layout_axis(Axis2_X);
          ui_set_next_pref_width(ui_pct(1, 0));
          ui_set_next_pref_height(ui_px(thumb_h, 1.f));
          UI_Box *thumb_row = ui_build_box_from_key(0, ui_key_zero());
          UI_Parent(thumb_row)
          {
            ui_spacer(ui_px(pad, 1.f));
            ui_set_next_pref_width(ui_px(scroll_bar_w - 2*pad, 1.f));
            ui_set_next_pref_height(ui_pct(1, 0));
            mv_set_next_corner_radius((scroll_bar_w - 2*pad)/2);
            UI_Box *thumb = ui_build_box_from_string(UI_BoxFlag_MouseClickable|UI_BoxFlag_DrawBackground|UI_BoxFlag_DrawHotEffects|UI_BoxFlag_DrawActiveEffects,
                                                     str8_lit("###scroll_thumb"));
            UI_Signal sig = ui_signal_from_box(thumb);
            if(ui_pressed(sig))
            {
              F32 start = content->view_off_target.y;
              ui_store_drag_struct(&start);
            }
            if(ui_dragging(sig))
            {
              F32 start = *ui_get_drag_struct(F32);
              F32 scale = max_scroll / ClampBot(1.f, track_h - thumb_h);
              F32 target = Clamp(0, start + ui_drag_delta().y*scale, max_scroll);
              content->view_off_target.y = target;
              content->view_off.y = target;
            }
          }
        }
      }
    }
  }
}

////////////////////////////////
//~ Box Rendering (adapted from raddbg's window frame)

internal void
mv_draw_ui(void)
{
  Vec4F32 base_background_color = ui_color_from_name(str8_lit("background"));
  Rng2F32 window_rect = wm_client_rect_from_window(mv->os);
  dr_rect(window_rect, base_background_color, 0, 0, 0);
  F32 border_softness = 1.f;

  for(UI_Box *box = ui_root_from_state(mv->ui); !ui_box_is_nil(box);)
  {
    UI_BoxRec rec = ui_box_rec_df_post(box, &ui_nil_box);

    if(box->transparency != 0) { dr_push_transparency(box->transparency); }

    //- background
    if(box->flags & UI_BoxFlag_DrawBackground)
    {
      Rng2F32 bg_rect = box->rect;
      R_Rect2DInst *inst = dr_rect(bg_rect, box->background_color, 0, 0, border_softness*0.5f);
      MemoryCopyArray(inst->corner_radii, box->corner_radii);

      // hover highlight
      if(box->flags & UI_BoxFlag_DrawHotEffects)
      {
        Vec4F32 hover_color = ui_color_from_tags_key_name(box->tags_key, str8_lit("hover"));
        hover_color.w *= 0.08f*box->hot_t;
        R_Rect2DInst *h = dr_rect(bg_rect, hover_color, 0, 0, border_softness*0.5f);
        MemoryCopyArray(h->corner_radii, box->corner_radii);
      }

      // pressed darkening
      if(box->flags & UI_BoxFlag_DrawActiveEffects && box->active_t > 0.01f)
      {
        R_Rect2DInst *a = dr_rect(bg_rect, v4f32(0, 0, 0, 0.15f*box->active_t), 0, 0, border_softness*0.5f);
        MemoryCopyArray(a->corner_radii, box->corner_radii);
      }
    }
    else if(box->flags & UI_BoxFlag_DrawHotEffects && box->hot_t > 0.01f)
    {
      // hover effect for background-less clickables (outline entries)
      Vec4F32 hover_color = ui_color_from_tags_key_name(box->tags_key, str8_lit("hover"));
      hover_color.w *= 0.06f*box->hot_t;
      R_Rect2DInst *h = dr_rect(box->rect, hover_color, 0, 0, border_softness*0.5f);
      MemoryCopyArray(h->corner_radii, box->corner_radii);
    }

    //- text
    if(box->flags & UI_BoxFlag_DrawText)
    {
      Vec2F32 text_position = ui_box_text_position(box);
      F32 max_x = 100000.f;
      FNT_Run ellipses_run = {0};
      if(!(box->flags & UI_BoxFlag_DisableTextTrunc))
      {
        max_x = (box->rect.x1 - text_position.x);
        ellipses_run = fnt_run_from_string(box->font, box->font_size, 0, box->tab_size, box->text_raster_flags, str8_lit("..."));
      }
      dr_truncated_fancy_run_list(text_position, &box->display_fruns, max_x, ellipses_run);
    }

    //- clip
    if(box->flags & UI_BoxFlag_Clip)
    {
      Rng2F32 top_clip = dr_top_clip();
      Rng2F32 new_clip = box->rect;
      if(top_clip.x1 != 0 || top_clip.y1 != 0)
      {
        new_clip = intersect_2f32(new_clip, top_clip);
      }
      dr_push_clip(new_clip);
    }

    //- custom draw
    if(box->custom_draw != 0)
    {
      box->custom_draw(box, box->custom_draw_user_data);
    }

    //- pops & post-children decorations
    {
      S32 pop_idx = 0;
      for(UI_Box *b = box; !ui_box_is_nil(b) && pop_idx <= rec.pop_count; b = b->parent)
      {
        pop_idx += 1;
        if(b == box && rec.push_count != 0)
        {
          continue;
        }
        if(b->flags & UI_BoxFlag_Clip)
        {
          dr_pop_clip();
        }
        if(b->flags & UI_BoxFlag_DrawBorder)
        {
          R_Rect2DInst *inst = dr_rect(b->rect, b->border_color, 0, 1.f, border_softness*0.5f);
          MemoryCopyArray(inst->corner_radii, b->corner_radii);
        }
        if(b->flags & (UI_BoxFlag_DrawSideTop|UI_BoxFlag_DrawSideBottom|UI_BoxFlag_DrawSideLeft|UI_BoxFlag_DrawSideRight))
        {
          Rng2F32 r = b->rect;
          Vec4F32 c = b->border_color;
          if(b->flags & UI_BoxFlag_DrawSideTop)    { dr_rect(r2f32p(r.x0, r.y0, r.x1, r.y0+1), c, 0, 0, 0); }
          if(b->flags & UI_BoxFlag_DrawSideBottom) { dr_rect(r2f32p(r.x0, r.y1-1, r.x1, r.y1), c, 0, 0, 0); }
          if(b->flags & UI_BoxFlag_DrawSideRight)  { dr_rect(r2f32p(r.x1-1, r.y0, r.x1, r.y1), c, 0, 0, 0); }
          if(b->flags & UI_BoxFlag_DrawSideLeft)
          {
            // left sides double as quote bars, so they fill the box width when narrow
            F32 w = (r.x1 - r.x0) <= 8.f ? (r.x1 - r.x0) : 1.f;
            dr_rect(r2f32p(r.x0, r.y0, r.x0+w, r.y1), c, 0, 0, 0);
          }
        }
        if(b->transparency != 0)
        {
          dr_pop_transparency();
        }
      }
    }

    box = rec.next;
  }
}

////////////////////////////////
//~ Per-Frame Entry Point

internal B32
frame(void)
{
  Temp scratch = scratch_begin(0, 0);
  mv->frame_depth += 1;

  //- timing
  U64 now_us = now_time_us();
  F32 dt = (mv->last_frame_us != 0) ? (now_us - mv->last_frame_us)/1000000.f : 1.f/60.f;
  dt = Clamp(1.f/240.f, dt, 1.f/20.f);
  mv->last_frame_us = now_us;

  //- gather events
  ui_select_state(mv->ui);
  B32 wait = !ui_animating_from_state(mv->ui) && mv->frame_index > 2;
  {
    UI_Box *content = ui_box_from_key(ui_key_from_string(ui_key_zero(), str8_lit("###content")));
    if(!ui_box_is_nil(content) && abs_f32(content->view_off.y - content->view_off_target.y) > 0.5f) { wait = 0; }
  }
  WM_EventList events = {0};
  if(mv->frame_depth == 1)
  {
    events = wm_get_events(scratch.arena, wait);
  }

  //- file watcher reload
  if(ins_atomic_u32_eval_assign(&mv->watch_changed, 0) && mv->doc_path.size)
  {
    mv_open_path(mv->doc_path, 1);
  }

  //- process events: app-level shortcuts, then translate the rest for the UI
  MemoryZeroStruct(&mv->ui_events);
  for(WM_Event *ev = events.first; ev != 0; ev = ev->next)
  {
    if(!wm_window_match(ev->window, mv->os) && ev->kind != WM_EventKind_Wakeup) { continue; }
    B32 taken = 0;
    switch(ev->kind)
    {
      default:{}break;
      case WM_EventKind_WindowClose:
      {
        mv->quit = 1;
        taken = 1;
      }break;
      case WM_EventKind_FileDrop:
      {
        if(ev->strings.first) { mv_open_path(ev->strings.first->string, 0); }
        taken = 1;
      }break;
      case WM_EventKind_Scroll:
      if(ev->modifiers & WM_Modifier_Ctrl)
      {
        mv->zoom = Clamp(0.5f, mv->zoom + (ev->delta.y < 0 ? 0.1f : -0.1f), 3.0f);
        taken = 1;
      }break;
      case WM_EventKind_Press:
      {
        if(ev->modifiers == WM_Modifier_Ctrl)
        {
          switch(ev->key)
          {
            default:{}break;
            case WM_Key_Equal: case WM_Key_NumPlus:      { mv->zoom = Min(3.0f, mv->zoom + 0.1f); taken = 1; }break;
            case WM_Key_Minus: case WM_Key_NumMinus: { mv->zoom = Max(0.5f, mv->zoom - 0.1f); taken = 1; }break;
            case WM_Key_0:                               { mv->zoom = 1.f; taken = 1; }break;
            case WM_Key_O:                               { mv->pick_file_requested = 1; taken = 1; }break;
            case WM_Key_T:                               { mv->light_theme = !mv->light_theme; taken = 1; }break;
            case WM_Key_B:                               { mv->show_outline = !mv->show_outline; taken = 1; }break;
            case WM_Key_W: case WM_Key_Q:                { mv->quit = 1; taken = 1; }break;
          }
        }
        else if(ev->modifiers == 0 && ev->key == WM_Key_F5 && mv->doc_path.size)
        {
          mv_open_path(mv->doc_path, 1);
          taken = 1;
        }
        else if(ev->modifiers == 0 && ev->key == WM_Key_Esc)
        {
          ui_ctx_menu_close();
        }
      }break;
    }
    if(taken) { continue; }
    UI_EventKind kind = UI_EventKind_Null;
    switch(ev->kind)
    {
      default:{}break;
      case WM_EventKind_Press:     {kind = UI_EventKind_Press;}break;
      case WM_EventKind_Release:   {kind = UI_EventKind_Release;}break;
      case WM_EventKind_MouseMove: {kind = UI_EventKind_MouseMove;}break;
      case WM_EventKind_Text:      {kind = UI_EventKind_Text;}break;
      case WM_EventKind_Scroll:    {kind = UI_EventKind_Scroll;}break;
    }
    if(kind == UI_EventKind_Null) { continue; }
    UI_Event ui_event = zero_struct;
    ui_event.kind         = kind;
    ui_event.key          = ev->key;
    ui_event.modifiers    = ev->modifiers;
    ui_event.string       = ev->character ? str8_from_32(scratch.arena, str32(&ev->character, 1)) : str8_zero();
    ui_event.pos          = ev->pos;
    ui_event.delta_2f32   = ev->delta;
    ui_event.timestamp_us = ev->timestamp_us;
    ui_event_list_push(scratch.arena, &mv->ui_events, &ui_event);
  }

  //- native file picker
  if(mv->pick_file_requested && mv->frame_depth == 1)
  {
    mv->pick_file_requested = 0;
    String8 initial = mv->doc_path.size ? str8_chop_last_slash(mv->doc_path) : get_process_info()->initial_path;
    String8 picked = sh_pick_file(scratch.arena, str8_lit("Open Markdown File"), initial);
    if(picked.size) { mv_open_path(picked, 0); }
  }
  
  //- build UI (the draw layer must be begun first: ui measures text through it)
  dr_begin_frame(mv->fonts.icons);
  F32 font_size = mv_font_size();
  {
    UI_IconInfo icon_info = {0};
    icon_info.icon_font = mv->fonts.icons;
    F32 rate = 1 - pow_f32(2, (-60.f * dt));
    UI_AnimationInfo animation_info = {0};
    animation_info.hot_animation_rate     = rate;
    animation_info.active_animation_rate  = rate;
    animation_info.focus_animation_rate   = 1.f;
    animation_info.tooltip_animation_rate = rate;
    animation_info.menu_animation_rate    = rate;
    animation_info.scroll_animation_rate  = 1 - pow_f32(2, (-40.f * dt));
    UI_Theme *theme = mv->light_theme ? &mv->theme_light : &mv->theme_dark;

    ui_select_state(mv->ui);
    ui_begin_build(mv->os, &mv->ui_events, &icon_info, theme, &animation_info, dt, dt);
    ui_push_font(mv->fonts.regular);
    ui_push_font_size(font_size);
    ui_push_text_raster_flags(FNT_RasterFlag_Smooth);
    ui_push_text_padding(floor_f32(font_size*0.3f));
    ui_push_pref_width(ui_px(floor_f32(font_size*20.f), 1.f));
    ui_push_pref_height(ui_px(floor_f32(font_size*2.f), 1.f));
    mv_build_ui(font_size);
    ui_end_build();
  }

  //- draw
  mv->draw_bucket = dr_bucket_make();
  DR_BucketScope(mv->draw_bucket)
  {
    mv_draw_ui();
  }

  //- submit
  r_begin_frame();
  r_window_begin_frame(mv->os, mv->r);
  dr_submit_bucket(mv->os, mv->r, mv->draw_bucket);
  r_window_end_frame(mv->os, mv->r);
  r_end_frame();

  //- show the window after the first frame has been rendered
  if(mv->frame_index == 0)
  {
    wm_window_first_paint(mv->os);
  }
  mv->frame_index += 1;

  mv->frame_depth -= 1;
  scratch_end(scratch);
  return mv->quit;
}

////////////////////////////////
//~ Entry Point

internal void
entry_point(CmdLine *cmdline)
{
  //- state
  Arena *arena = arena_alloc();
  mv = push_array(arena, MV_State, 1);
  mv->arena = arena;
  mv->doc_arena = arena_alloc();
  mv->layout_arena = arena_alloc();
  mv->zoom = 1.f;
  mv->show_outline = 1;
  mv->watch_mutex = mutex_alloc();
  mv->theme_dark = mv_theme_make(arena, 0);
  mv->theme_light = mv_theme_make(arena, 1);

  //- window
  mv->os = wm_window_open(r2f32p(0, 0, 1280, 860), WM_WindowFlag_UseDefaultPosition, str8_lit("mdview"));
  mv->r = r_window_equip(mv->os);
  mv->ui = ui_state_alloc();
  mv_load_fonts();

  //- document
  String8 path = cmdline->inputs.first ? cmdline->inputs.first->string : str8_zero();
  if(path.size)
  {
    mv_open_path(path, 0);
  }
  if(mv->doc_text.size == 0 && mv->doc_path.size == 0)
  {
    mv_load_text(str8_zero(), str8_zero(), mv_welcome_text);
  }

  //- file watcher
  Thread watcher = thread_launch(mv_watch_thread_entry_point, 0);

  //- main loop
  for(;!update(););

  ins_atomic_u32_eval_assign(&mv->quit, 1);
  thread_join(watcher, max_U64);
  r_window_unequip(mv->os, mv->r);
  wm_window_close(mv->os);
}
