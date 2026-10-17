// markdown_parser: the counterpart of Zed's crates/markdown/src/parser.rs.
//
// Zed runs pulldown-cmark with tables, footnotes, strikethrough, task lists,
// smart punctuation, heading attributes, metadata blocks, GFM alerts and
// super/subscript, then adds linkify autolinks. This parser covers the same
// syntax and produces a block tree instead of a flat event list. Every block
// keeps its source byte range: the element builder needs it for the
// first/last block margin rules and the root-block gutter. Inline content of
// a text block (paragraph, heading, table cell) is a flat list of
// pulldown-style events: text, code, breaks and start/end tags.

#ifndef MARKDOWN_PARSER_H
#define MARKDOWN_PARSER_H

////////////////////////////////
//~ Inline Events

typedef enum MD_InlineKind
{
  MD_InlineKind_Text,
  MD_InlineKind_Code,
  MD_InlineKind_SoftBreak,
  MD_InlineKind_HardBreak,
  MD_InlineKind_FootnoteRef,     // string = label
  MD_InlineKind_EmphasisStart,
  MD_InlineKind_EmphasisEnd,
  MD_InlineKind_StrongStart,
  MD_InlineKind_StrongEnd,
  MD_InlineKind_StrikeStart,
  MD_InlineKind_StrikeEnd,
  MD_InlineKind_SuperscriptStart,
  MD_InlineKind_SuperscriptEnd,
  MD_InlineKind_SubscriptStart,
  MD_InlineKind_SubscriptEnd,
  MD_InlineKind_LinkStart,       // url
  MD_InlineKind_LinkEnd,
  MD_InlineKind_ImageStart,      // url; the events up to ImageEnd are the alt text
  MD_InlineKind_ImageEnd,
  MD_InlineKind_COUNT
}
MD_InlineKind;

typedef struct MD_Inline MD_Inline;
struct MD_Inline
{
  MD_InlineKind kind;
  String8 string;
  String8 url;
};

typedef struct MD_InlineArray MD_InlineArray;
struct MD_InlineArray
{
  MD_Inline *v;
  U64 count;
};

////////////////////////////////
//~ Blocks

typedef enum MD_NodeKind
{
  MD_NodeKind_Null,
  MD_NodeKind_Document,
  MD_NodeKind_Paragraph,
  MD_NodeKind_Heading,
  MD_NodeKind_BlockQuote,
  MD_NodeKind_CodeBlock,
  MD_NodeKind_HtmlBlock,
  MD_NodeKind_List,
  MD_NodeKind_Item,
  MD_NodeKind_Rule,
  MD_NodeKind_Table,
  MD_NodeKind_TableHead,         // holds the header cells directly, like pulldown-cmark
  MD_NodeKind_TableRow,
  MD_NodeKind_TableCell,
  MD_NodeKind_FootnoteDefinition,
  MD_NodeKind_MetadataBlock,
  MD_NodeKind_COUNT
}
MD_NodeKind;

typedef enum MD_Align
{
  MD_Align_None,
  MD_Align_Left,
  MD_Align_Center,
  MD_Align_Right,
}
MD_Align;

typedef enum MD_AlertKind
{
  MD_AlertKind_None,
  MD_AlertKind_Note,
  MD_AlertKind_Tip,
  MD_AlertKind_Important,
  MD_AlertKind_Warning,
  MD_AlertKind_Caution,
}
MD_AlertKind;

typedef struct MD_Node MD_Node;
struct MD_Node
{
  MD_Node *first;
  MD_Node *last;
  MD_Node *next;
  MD_Node *parent;
  U64 child_count;
  MD_NodeKind kind;
  Rng1U64 range;                 // source bytes, end exclusive

  // heading
  U64 level;

  // list / item
  B32 ordered;
  B32 tight;
  U64 start_number;
  B32 is_task;
  B32 task_checked;
  U64 task_offset;               // source offset of the character between the brackets

  // block quote
  MD_AlertKind alert;

  // table (on the Table node)
  MD_Align *aligns;
  U64 column_count;

  // code block: language (first word of the info string) and content
  String8 info;
  B32 fenced;

  // footnote definition
  String8 label;

  // code block / html block / metadata fallback text
  String8 text;

  // metadata block: simple `key: value` rows, or none (rendered as code)
  String8 *meta_keys;
  String8 *meta_values;
  U64 meta_count;

  // paragraph / heading / table cell content
  MD_InlineArray inlines;

  // parse-time state
  B32 open;
  String8List lines;
  U64 content_indent;
  U8 marker_char;
  U64 fence_len;
  U64 fence_indent;
  U32 html_kind;
  B32 blank_pending;
  B32 item_saw_content;
};

typedef struct MD_Document MD_Document;
struct MD_Document
{
  String8 source;
  U64 base;                      // document offset of source.str[0]: a part holds offsets into the whole
  MD_Node *root;
  U64 end;                       // end of the last root block (Zed's `markdown_end`)
  U64 word_count;
  U64 root_base;                 // root blocks before this part of the document
  B32 footnotes_before;          // a footnote definition comes before this part
};

////////////////////////////////
//~ References
//
// Link reference definitions and footnote labels apply to the whole document,
// wherever they are defined, so a parse of one part of it looks up those
// found in the rest (`shared`) as well as its own.

typedef struct MD_Ref MD_Ref;
struct MD_Ref
{
  MD_Ref *next;
  String8 label;                 // normalized; footnote labels lowercased
  String8 url;
};

#define MD_REF_SLOT_COUNT 256

typedef struct MD_RefTable MD_RefTable;
struct MD_RefTable
{
  MD_Ref *links[MD_REF_SLOT_COUNT];
  MD_Ref *footnotes[MD_REF_SLOT_COUNT];
  U64 count;                     // definitions added, to notice new ones
};

////////////////////////////////
//~ Scanner
//
// A pass over the whole document that keeps none of it: it runs the block
// parser line by line and, between root blocks, drops what it built. What it
// keeps is what a part of the document needs from the rest of it: where the
// root blocks fall (grouped into chunks of about `chunk_target` bytes, each
// parsed on its own later), the headings for the outline, the footnote
// definitions, and the reference table. The caller feeds it the source in
// pieces, so the document never has to be in memory at once.

typedef struct MD_ScanChunk MD_ScanChunk;
struct MD_ScanChunk
{
  Rng1U64 range;                 // whole root blocks and the blank lines after them
  U64 root_base;                 // root blocks before the chunk
  U64 line_base;                 // lines before the chunk
  B32 footnotes_before;
};

typedef struct MD_ScanHeading MD_ScanHeading;
struct MD_ScanHeading
{
  U64 level;
  String8 text;                  // plain text of the heading's inlines
  U64 offset;                    // source offset of the heading block
};

typedef struct MD_ScanFootnote MD_ScanFootnote;
struct MD_ScanFootnote
{
  String8 label;
  U64 offset;                    // source offset of the definition
};

typedef struct MD_Scanner MD_Scanner;
struct MD_Scanner
{
  Arena *arena;                  // results
  Arena *work;                   // the block parser, reset between root blocks
  U64 work_pos;
  struct MD_BlockParser *p;
  MD_Node *harvested;            // last root block already taken from the parser
  U64 chunk_target;

  // progress
  U64 offset;                    // start of the next line to feed
  U64 line_count;
  U64 root_count;                // root blocks finished
  U64 end;                       // end of the last root block
  B32 footnotes_seen;
  B32 done;

  // results
  MD_RefTable refs;
  MD_ScanChunk *chunks;
  U64 chunk_count;
  U64 chunk_cap;
  MD_ScanChunk open_chunk;       // the chunk being accumulated
  MD_ScanHeading *headings;
  U64 heading_count;
  U64 heading_cap;
  MD_ScanFootnote *footnotes;
  U64 footnote_count;
  U64 footnote_cap;
};

////////////////////////////////
//~ Parser API

internal MD_Document md_parse(Arena *arena, String8 source);
// Parses root blocks from `source`, which holds the document's bytes from
// offset `base` on. Node ranges are offsets into the whole document.
internal MD_Document md_parse_part(Arena *arena, String8 source, U64 base, MD_RefTable *shared);

internal void md_scanner_init(MD_Scanner *s, U64 chunk_target);
internal void md_scanner_release(MD_Scanner *s);
// Takes the source from s->offset on and returns how many bytes it used:
// whole lines, unless `eof`. 0 means a line is longer than `data`.
internal U64  md_scanner_feed(MD_Scanner *s, String8 data, B32 eof);

////////////////////////////////
//~ Helpers

internal B32 md_node_is_text_block(MD_Node *node);
internal String8 md_plain_text_from_inlines(Arena *arena, MD_InlineArray inlines);

#endif // MARKDOWN_PARSER_H
