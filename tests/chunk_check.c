// Checks that a document parsed in chunks, as the preview does, parses as it
// does whole: chunk_check <file.md> [chunk_bytes] [read_bytes]
// With "scan" as the only option after the file, it times the scanner alone
// (for documents too large to parse whole).
#define BUILD_TITLE "chunk_check"
#define BUILD_CONSOLE_INTERFACE 1
#include "base/base_inc.h"
#include "theme/theme.h"
#include "markdown/markdown_parser.h"
#include "base/base_inc.c"
#include "markdown/markdown_parser.c"

internal void
dump(Arena *arena, String8List *out, MD_Node *n, U64 depth)
{
  for(MD_Node *c = n->first; c != 0; c = c->next)
  {
    str8_list_pushf(arena, out, "%*s%I64u [%I64u..%I64u] level=%I64u ordered=%d tight=%d start=%I64u task=%d/%d/%I64u alert=%d info=%S text=%S label=%S",
                    (int)depth*2, "", (U64)c->kind, c->range.min, c->range.max, c->level, c->ordered, c->tight, c->start_number,
                    c->is_task, c->task_checked, c->task_offset, c->alert, c->info, c->text, c->label);
    for EachIndex(i, c->inlines.count)
    {
      MD_Inline *in = &c->inlines.v[i];
      str8_list_pushf(arena, out, "%*s  %I64u \"%S\" %S", (int)depth*2, "", (U64)in->kind, in->string, in->url);
    }
    dump(arena, out, c, depth+1);
  }
}

internal void
collect_headings(Arena *arena, String8List *out, MD_Node *n)
{
  for(MD_Node *c = n->first; c != 0; c = c->next)
  {
    if(c->kind == MD_NodeKind_Heading)
    {
      str8_list_pushf(arena, out, "h%I64u @%I64u %S", c->level, c->range.min, md_plain_text_from_inlines(arena, c->inlines));
    }
    collect_headings(arena, out, c);
  }
}

internal U64
compare(char *what, String8List a, String8List b)
{
  String8Node *x = a.first, *y = b.first;
  U64 line = 0;
  for(;x != 0 && y != 0; x = x->next, y = y->next, line += 1)
  {
    if(!str8_match(x->string, y->string, 0))
    {
      printf("%s: differ at line %I64u\n  whole:   %.*s\n  chunked: %.*s\n", what, line, str8_varg(x->string), str8_varg(y->string));
      return 1;
    }
  }
  if(x || y)
  {
    printf("%s: lengths differ (whole %I64u, chunked %I64u)\n", what, a.node_count, b.node_count);
    return 1;
  }
  return 0;
}

internal void
scan(MD_Scanner *s, String8 text, U64 chunk_bytes, U64 read_bytes)
{
  md_scanner_init(s, chunk_bytes);
  U64 read = read_bytes;
  for(;!s->done;)
  {
    Rng1U64 r = r1u64(s->offset, Min(s->offset + read, text.size));
    U64 used = md_scanner_feed(s, str8_substr(text, r), r.max >= text.size);
    if(used == 0 && !s->done) { read *= 2; }
  }
}

internal void
entry_point(CmdLine *cmdline)
{
  Arena *arena = arena_alloc();
  String8List inputs = cmdline->inputs;
  String8 path = inputs.first ? inputs.first->string : str8_zero();
  String8 opt1 = (inputs.node_count > 1) ? inputs.first->next->string : str8_zero();
  String8 opt2 = (inputs.node_count > 2) ? inputs.first->next->next->string : str8_zero();
  String8 text = data_from_file_path(arena, path);

  //- scanner timing
  if(str8_match(opt1, str8_lit("scan"), 0))
  {
    U64 t0 = now_time_us();
    MD_Scanner s = {0};
    scan(&s, text, KB(64), MB(1));
    U64 t1 = now_time_us();
    printf("%I64u bytes: scanned in %I64u ms, %I64u chunks, %I64u lines, %I64u root blocks, %I64u headings; results %I64u KB, parser %I64u KB at the end\n",
           text.size, (t1-t0)/1000, s.chunk_count, s.line_count, s.root_count, s.heading_count,
           arena_pos(s.arena)/1024, arena_pos(s.work)/1024);
    U64 biggest = 0;
    for EachIndex(i, s.chunk_count) { biggest = Max(biggest, dim_1u64(s.chunks[i].range)); }
    printf("biggest chunk %I64u bytes\n", biggest);
    return;
  }

  U64 chunk_bytes = opt1.size ? u64_from_str8(opt1, 10) : KB(64);
  U64 read_bytes = opt2.size ? u64_from_str8(opt2, 10) : MB(1);

  //- whole
  MD_Document whole = md_parse(arena, text);
  String8List a = {0};
  dump(arena, &a, whole.root, 0);
  String8List ah = {0};
  collect_headings(arena, &ah, whole.root);

  //- chunked
  MD_Scanner s = {0};
  scan(&s, text, chunk_bytes, read_bytes);
  String8List b = {0};
  U64 roots = 0;
  U64 errors = 0;
  for EachIndex(i, s.chunk_count)
  {
    MD_ScanChunk *c = &s.chunks[i];
    if(i > 0 && c->range.min != s.chunks[i-1].range.max) { printf("chunk %I64u does not follow the last\n", i); errors += 1; }
    if(c->root_base != roots) { printf("chunk %I64u: root_base %I64u, expected %I64u\n", i, c->root_base, roots); errors += 1; }
    U64 lines = 0;
    for EachIndex(k, c->range.min) { lines += (text.str[k] == '\n'); }
    if(i < 64 && c->line_base != lines) { printf("chunk %I64u: line_base %I64u, expected %I64u\n", i, c->line_base, lines); errors += 1; }
    MD_Document part = md_parse_part(arena, str8_substr(text, c->range), c->range.min, &s.refs);
    dump(arena, &b, part.root, 0);
    roots += part.root->child_count;
  }
  if(s.chunk_count && s.chunks[s.chunk_count-1].range.max != text.size) { printf("chunks end at %I64u, not %I64u\n", s.chunks[s.chunk_count-1].range.max, text.size); errors += 1; }
  if(s.end != whole.end) { printf("end %I64u, expected %I64u\n", s.end, whole.end); errors += 1; }
  if(s.root_count != whole.root->child_count) { printf("root blocks %I64u, expected %I64u\n", s.root_count, whole.root->child_count); errors += 1; }
  String8List bh = {0};
  for EachIndex(i, s.heading_count)
  {
    str8_list_pushf(arena, &bh, "h%I64u @%I64u %S", s.headings[i].level, s.headings[i].offset, s.headings[i].text);
  }
  if(inputs.node_count > 3)
  {
    for EachIndex(i, s.chunk_count) { printf("chunk %I64u [%I64u..%I64u) root_base %I64u\n", i, s.chunks[i].range.min, s.chunks[i].range.max, s.chunks[i].root_base); }
  }
  errors += compare("blocks", a, b);
  errors += compare("headings", ah, bh);
  printf("%.*s: %I64u bytes, %I64u chunks of %I64u, reads of %I64u, %I64u root blocks, %I64u headings: %s\n",
         str8_varg(path), text.size, s.chunk_count, chunk_bytes, read_bytes, s.root_count, s.heading_count, errors ? "MISMATCH" : "ok");
}
