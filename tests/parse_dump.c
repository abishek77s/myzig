// Prints the block tree and inline events md_parse produces, for checking
// the parser without a window: parse_dump <file.md>
#define BUILD_TITLE "parse_dump"
#define BUILD_CONSOLE_INTERFACE 1
#include "base/base_inc.h"
#include "theme/theme.h"
#include "markdown/markdown_parser.h"
#include "base/base_inc.c"
#include "markdown/markdown_parser.c"

read_only global char *kind_names[] = {"Null","Document","Paragraph","Heading","BlockQuote","CodeBlock","HtmlBlock","List","Item","Rule","Table","TableHead","TableRow","TableCell","FootnoteDefinition","MetadataBlock"};
read_only global char *inline_names[] = {"Text","Code","SoftBreak","HardBreak","FootnoteRef","Em+","Em-","Strong+","Strong-","Strike+","Strike-","Sup+","Sup-","Sub+","Sub-","Link+","Link-","Image+","Image-"};

internal void
dump(MD_Node *n, U64 depth)
{
  for(MD_Node *c = n->first; c != 0; c = c->next)
  {
    printf("%*s%s [%llu..%llu]", (int)depth*2, "", kind_names[c->kind], c->range.min, c->range.max);
    if(c->kind == MD_NodeKind_List) { printf(" ordered=%d tight=%d start=%llu", c->ordered, c->tight, c->start_number); }
    if(c->kind == MD_NodeKind_Item && c->is_task) { printf(" task=%d offset=%llu", c->task_checked, c->task_offset); }
    if(c->kind == MD_NodeKind_Heading) { printf(" h%llu", c->level); }
    if(c->kind == MD_NodeKind_BlockQuote && c->alert) { printf(" alert=%d", c->alert); }
    if(c->kind == MD_NodeKind_CodeBlock) { printf(" info=\"%.*s\" text=\"%.*s\"", str8_varg(c->info), str8_varg(c->text)); }
    printf("\n");
    for EachIndex(i, c->inlines.count)
    {
      MD_Inline *in = &c->inlines.v[i];
      printf("%*s  %s \"%.*s\"%s%.*s\n", (int)depth*2, "", inline_names[in->kind], str8_varg(in->string), in->url.size ? " url=" : "", str8_varg(in->url));
    }
    dump(c, depth+1);
  }
}

internal void
entry_point(CmdLine *cmdline)
{
  Arena *arena = arena_alloc();
  String8 path = cmdline->inputs.first ? cmdline->inputs.first->string : str8_zero();
  String8 text = data_from_file_path(arena, path);
  MD_Document doc = md_parse(arena, text);
  printf("end=%llu words=%llu\n", doc.end, doc.word_count);
  dump(doc.root, 0);
}
