////////////////////////////////
//~ Parse State

typedef struct MD_LinkRef MD_LinkRef;
struct MD_LinkRef
{
  MD_LinkRef *next;
  String8 label;                 // normalized
  String8 url;
};

typedef struct MD_BlockParser MD_BlockParser;
struct MD_BlockParser
{
  Arena *arena;
  String8 source;
  MD_Node *root;
  MD_Node *tip;
  MD_Node *old_tip;
  MD_Node *last_matched;
  B32 all_closed;

  // current line (tabs expanded)
  String8 line;
  U64 line_start;
  U64 line_end;
  U64 pos;
  U64 next_nonspace;
  U64 indent;
  B32 indented;
  B32 blank;
  B32 line_consumed;             // the block start used the whole line (fence opener)

  // link reference definitions, footnote definition labels
  MD_LinkRef *first_ref;
  String8List footnote_labels;
};

////////////////////////////////
//~ Character Helpers

internal B32
md_char_is_space_or_tab(U8 c)
{
  return c == ' ' || c == '\t';
}

internal B32
md_char_is_ascii_punct(U8 c)
{
  return ((c >= 0x21 && c <= 0x2f) || (c >= 0x3a && c <= 0x40) ||
          (c >= 0x5b && c <= 0x60) || (c >= 0x7b && c <= 0x7e));
}

internal B32
md_char_is_alnum(U8 c)
{
  return char_is_alpha(c) || char_is_digit(c, 10);
}

// Unicode whitespace / punctuation at byte `i` (and before `i`), used by the
// emphasis flanking rules. Non-ASCII code points count as neither, except the
// common punctuation blocks.
internal U32
md_codepoint_at(String8 s, U64 i)
{
  if(i >= s.size) { return 0; }
  UnicodeDecode d = utf8_decode(s.str + i, s.size - i);
  return d.codepoint;
}

internal U32
md_codepoint_before(String8 s, U64 i)
{
  if(i == 0) { return 0; }
  U64 k = i - 1;
  for(;k > 0 && (s.str[k] & 0xc0) == 0x80; k -= 1);
  return md_codepoint_at(s, k);
}

internal B32
md_codepoint_is_whitespace(U32 c)
{
  return (c == 0 || c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == 0x0b ||
          c == 0xa0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200a) || c == 0x202f || c == 0x205f || c == 0x3000);
}

internal B32
md_codepoint_is_punct(U32 c)
{
  if(c < 128) { return md_char_is_ascii_punct((U8)c); }
  return ((c >= 0x2010 && c <= 0x2027) || (c >= 0x2030 && c <= 0x205e) || (c >= 0x3001 && c <= 0x3003) ||
          (c >= 0x3008 && c <= 0x3011) || c == 0xa1 || c == 0xa7 || c == 0xab || c == 0xb6 || c == 0xb7 ||
          c == 0xbb || c == 0xbf);
}

internal String8
md_trim_spaces(String8 s)
{
  U64 a = 0, b = s.size;
  for(;a < b && md_char_is_space_or_tab(s.str[a]); a += 1);
  for(;b > a && (md_char_is_space_or_tab(s.str[b-1]) || s.str[b-1] == '\r'); b -= 1);
  return str8_substr(s, r1u64(a, b));
}

internal B32
md_str8_is_blank(String8 s)
{
  for EachIndex(i, s.size)
  {
    if(!md_char_is_space_or_tab(s.str[i]) && s.str[i] != '\r') { return 0; }
  }
  return 1;
}

// Case-folds and collapses whitespace in a link label, for reference matching.
internal String8
md_normalize_label(Arena *arena, String8 label)
{
  String8 trimmed = str8_skip_chop_whitespace(label);
  U8 *out = push_array_no_zero(arena, U8, trimmed.size + 1);
  U64 n = 0;
  B32 in_space = 0;
  for EachIndex(i, trimmed.size)
  {
    U8 c = trimmed.str[i];
    if(char_is_space(c))
    {
      if(!in_space) { out[n++] = ' '; }
      in_space = 1;
    }
    else
    {
      out[n++] = lower_from_char(c);
      in_space = 0;
    }
  }
  return str8(out, n);
}

////////////////////////////////
//~ Entities

typedef struct MD_Entity MD_Entity;
struct MD_Entity
{
  char *name;
  U32 codepoint;
};

read_only global MD_Entity md_entity_table[] =
{
  {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"nbsp", 0xa0},
  {"copy", 0xa9}, {"reg", 0xae}, {"trade", 0x2122}, {"hellip", 0x2026}, {"mdash", 0x2014},
  {"ndash", 0x2013}, {"lsquo", 0x2018}, {"rsquo", 0x2019}, {"ldquo", 0x201c}, {"rdquo", 0x201d},
  {"laquo", 0xab}, {"raquo", 0xbb}, {"middot", 0xb7}, {"bull", 0x2022}, {"times", 0xd7},
  {"divide", 0xf7}, {"deg", 0xb0}, {"plusmn", 0xb1}, {"para", 0xb6}, {"sect", 0xa7},
  {"larr", 0x2190}, {"rarr", 0x2192}, {"uarr", 0x2191}, {"darr", 0x2193}, {"harr", 0x2194},
  {"le", 0x2264}, {"ge", 0x2265}, {"ne", 0x2260}, {"infin", 0x221e}, {"check", 0x2713},
  {"euro", 0x20ac}, {"pound", 0xa3}, {"yen", 0xa5}, {"cent", 0xa2}, {"ensp", 0x2002},
  {"emsp", 0x2003}, {"thinsp", 0x2009}, {"zwj", 0x200d}, {"zwnj", 0x200c}, {"shy", 0xad},
};

// Parses an entity or numeric character reference at s[i] == '&'. Returns the
// number of bytes consumed (0 if none) and writes the UTF-8 into out.
internal U64
md_parse_entity(String8 s, U64 i, U8 out[8], U64 *out_size)
{
  U64 semi = 0;
  for(U64 k = i+1; k < s.size && k < i+34; k += 1)
  {
    if(s.str[k] == ';') { semi = k; break; }
    if(!md_char_is_alnum(s.str[k]) && s.str[k] != '#') { break; }
  }
  if(semi == 0) { return 0; }
  String8 name = str8_substr(s, r1u64(i+1, semi));
  U32 cp = 0;
  if(name.size >= 2 && name.str[0] == '#')
  {
    B32 hex = (name.str[1] == 'x' || name.str[1] == 'X');
    String8 digits = str8_skip(name, hex ? 2 : 1);
    if(digits.size == 0 || digits.size > (hex ? 6 : 7)) { return 0; }
    for EachIndex(k, digits.size)
    {
      if(!char_is_digit(digits.str[k], hex ? 16 : 10)) { return 0; }
    }
    cp = (U32)u64_from_str8(digits, hex ? 16 : 10);
    if(cp == 0 || cp > 0x10ffff) { cp = 0xfffd; }
  }
  else
  {
    for EachElement(k, md_entity_table)
    {
      if(str8_match(name, str8_cstring(md_entity_table[k].name), 0)) { cp = md_entity_table[k].codepoint; break; }
    }
    if(cp == 0) { return 0; }
  }
  *out_size = utf8_encode(out, cp);
  return semi + 1 - i;
}

////////////////////////////////
//~ Node Helpers

internal B32
md_node_is_text_block(MD_Node *node)
{
  return node->kind == MD_NodeKind_Paragraph || node->kind == MD_NodeKind_Heading || node->kind == MD_NodeKind_TableCell;
}

internal MD_Node *
md_node_alloc(Arena *arena, MD_NodeKind kind)
{
  MD_Node *n = push_array(arena, MD_Node, 1);
  n->kind = kind;
  n->open = 1;
  return n;
}

internal void
md_node_append(MD_Node *parent, MD_Node *child)
{
  child->parent = parent;
  SLLQueuePush(parent->first, parent->last, child);
  parent->child_count += 1;
}

internal void
md_node_remove(MD_Node *node)
{
  MD_Node *parent = node->parent;
  MD_Node *prev = 0;
  for(MD_Node *c = parent->first; c != 0 && c != node; c = c->next) { prev = c; }
  if(prev) { prev->next = node->next; } else { parent->first = node->next; }
  if(parent->last == node) { parent->last = prev; }
  parent->child_count -= 1;
}

internal String8
md_join_lines(Arena *arena, String8List *lines)
{
  return str8_list_join(arena, lines, &(StringJoin){.sep = str8_lit_comp("\n")});
}

internal String8
md_plain_text_from_inlines(Arena *arena, MD_InlineArray inlines)
{
  String8List parts = {0};
  for EachIndex(i, inlines.count)
  {
    MD_Inline *in = &inlines.v[i];
    switch(in->kind)
    {
      default:{}break;
      case MD_InlineKind_Text:
      case MD_InlineKind_Code:      { str8_list_push(arena, &parts, in->string); }break;
      case MD_InlineKind_SoftBreak:
      case MD_InlineKind_HardBreak: { str8_list_push(arena, &parts, str8_lit(" ")); }break;
    }
  }
  return str8_list_join(arena, &parts, 0);
}

////////////////////////////////
//~ Link Reference Definitions

internal B32
md_ref_lookup(MD_BlockParser *p, Arena *scratch, String8 label, String8 *url_out)
{
  String8 key = md_normalize_label(scratch, label);
  for(MD_LinkRef *r = p->first_ref; r != 0; r = r->next)
  {
    if(str8_match(r->label, key, 0)) { *url_out = r->url; return 1; }
  }
  return 0;
}

// Parses a link destination at s[*pos]; returns 1 on success.
internal B32
md_parse_link_destination(String8 s, U64 *pos, String8 *out)
{
  U64 i = *pos;
  if(i < s.size && s.str[i] == '<')
  {
    for(U64 k = i+1; k < s.size; k += 1)
    {
      if(s.str[k] == '\\') { k += 1; continue; }
      if(s.str[k] == '\n' || s.str[k] == '<') { return 0; }
      if(s.str[k] == '>') { *out = str8_substr(s, r1u64(i+1, k)); *pos = k+1; return 1; }
    }
    return 0;
  }
  U64 depth = 0;
  U64 k = i;
  for(;k < s.size; k += 1)
  {
    U8 c = s.str[k];
    if(c == '\\' && k+1 < s.size && md_char_is_ascii_punct(s.str[k+1])) { k += 1; continue; }
    if(c == '(') { depth += 1; }
    else if(c == ')') { if(depth == 0) { break; } depth -= 1; }
    else if(c <= ' ') { break; }
  }
  if(k == i || depth != 0) { return 0; }
  *out = str8_substr(s, r1u64(i, k));
  *pos = k;
  return 1;
}

internal B32
md_parse_link_title(String8 s, U64 *pos, String8 *out)
{
  U64 i = *pos;
  if(i >= s.size) { return 0; }
  U8 open = s.str[i];
  U8 close = (open == '(') ? ')' : open;
  if(open != '"' && open != '\'' && open != '(') { return 0; }
  for(U64 k = i+1; k < s.size; k += 1)
  {
    if(s.str[k] == '\\') { k += 1; continue; }
    if(s.str[k] == close) { *out = str8_substr(s, r1u64(i+1, k)); *pos = k+1; return 1; }
    if(open == '(' && s.str[k] == '(') { return 0; }
  }
  return 0;
}

// Resolves backslash escapes and entities in a URL or title.
internal String8
md_unescape(Arena *arena, String8 s)
{
  if(str8_find_needle(s, 0, str8_lit("\\"), 0) == s.size && str8_find_needle(s, 0, str8_lit("&"), 0) == s.size)
  {
    return s;
  }
  U8 *out = push_array_no_zero(arena, U8, s.size*4 + 1);
  U64 n = 0;
  for(U64 i = 0; i < s.size; i += 1)
  {
    U8 c = s.str[i];
    if(c == '\\' && i+1 < s.size && md_char_is_ascii_punct(s.str[i+1])) { out[n++] = s.str[i+1]; i += 1; continue; }
    if(c == '&')
    {
      U8 buf[8]; U64 sz = 0;
      U64 used = md_parse_entity(s, i, buf, &sz);
      if(used) { MemoryCopy(out+n, buf, sz); n += sz; i += used-1; continue; }
    }
    out[n++] = c;
  }
  return str8(out, n);
}

// Strips leading link reference definitions from a paragraph's text and
// records them. Returns the remaining text.
internal String8
md_extract_link_refs(MD_BlockParser *p, String8 text)
{
  for(;;)
  {
    U64 i = 0;
    for(;i < text.size && i < 3 && text.str[i] == ' '; i += 1);
    if(i >= text.size || text.str[i] != '[' || (i+1 < text.size && text.str[i+1] == '^')) { break; }
    U64 label_end = 0;
    for(U64 k = i+1; k < text.size && k < i+1000; k += 1)
    {
      if(text.str[k] == '\\') { k += 1; continue; }
      if(text.str[k] == '[') { break; }
      if(text.str[k] == ']') { label_end = k; break; }
    }
    if(label_end == 0 || label_end+1 >= text.size || text.str[label_end+1] != ':') { break; }
    String8 label = str8_substr(text, r1u64(i+1, label_end));
    if(md_str8_is_blank(label)) { break; }
    U64 pos = label_end + 2;
    U64 newlines = 0;
    for(;pos < text.size && char_is_space(text.str[pos]); pos += 1) { if(text.str[pos] == '\n') { newlines += 1; } }
    if(newlines > 1) { break; }
    String8 dest = {0};
    if(!md_parse_link_destination(text, &pos, &dest)) { break; }
    // optional title, then end of line
    U64 after_dest = pos;
    U64 tpos = pos;
    B32 saw_ws = 0;
    for(;tpos < text.size && char_is_space(text.str[tpos]); tpos += 1) { saw_ws = 1; }
    String8 title = {0};
    U64 end = after_dest;
    B32 ok = 0;
    if(saw_ws && md_parse_link_title(text, &tpos, &title))
    {
      U64 e = tpos;
      for(;e < text.size && md_char_is_space_or_tab(text.str[e]); e += 1);
      if(e >= text.size || text.str[e] == '\n') { end = e; ok = 1; }
    }
    if(!ok)
    {
      U64 e = after_dest;
      for(;e < text.size && md_char_is_space_or_tab(text.str[e]); e += 1);
      if(e < text.size && text.str[e] != '\n') { break; }
      end = e;
    }
    String8 key = md_normalize_label(p->arena, label);
    B32 exists = 0;
    for(MD_LinkRef *r = p->first_ref; r != 0; r = r->next) { if(str8_match(r->label, key, 0)) { exists = 1; break; } }
    if(!exists)
    {
      MD_LinkRef *ref = push_array(p->arena, MD_LinkRef, 1);
      ref->label = key;
      ref->url = md_unescape(p->arena, dest);
      SLLStackPush(p->first_ref, ref);
    }
    text = str8_skip(text, end < text.size ? end+1 : end);
  }
  return text;
}

////////////////////////////////
//~ Block Parsing: Line Helpers

internal void
md_find_next_nonspace(MD_BlockParser *p)
{
  U64 i = p->pos;
  for(;i < p->line.size && p->line.str[i] == ' '; i += 1);
  p->next_nonspace = i;
  p->indent = i - p->pos;
  p->indented = (p->indent >= 4);
  p->blank = (i >= p->line.size);
}

internal U8
md_peek(MD_BlockParser *p, U64 i)
{
  return (i < p->line.size) ? p->line.str[i] : 0;
}

internal String8
md_rest_of_line(MD_BlockParser *p)
{
  return str8_skip(p->line, p->pos);
}

internal B32
md_can_contain(MD_NodeKind parent, MD_NodeKind child)
{
  switch(parent)
  {
    case MD_NodeKind_Document:
    case MD_NodeKind_BlockQuote:
    case MD_NodeKind_Item:
    case MD_NodeKind_FootnoteDefinition:
    return child != MD_NodeKind_Item;
    case MD_NodeKind_List:
    return child == MD_NodeKind_Item;
    default: return 0;
  }
}

internal B32
md_accepts_lines(MD_NodeKind kind)
{
  return kind == MD_NodeKind_Paragraph || kind == MD_NodeKind_CodeBlock || kind == MD_NodeKind_HtmlBlock;
}

////////////////////////////////
//~ Block Parsing: Finalization

internal void md_finalize(MD_BlockParser *p, MD_Node *node);

internal void
md_close_unmatched(MD_BlockParser *p)
{
  if(!p->all_closed)
  {
    for(;p->old_tip != p->last_matched;)
    {
      MD_Node *parent = p->old_tip->parent;
      md_finalize(p, p->old_tip);
      p->old_tip = parent;
    }
    p->tip = p->last_matched;
    p->all_closed = 1;
  }
}

internal MD_Node *
md_add_child(MD_BlockParser *p, MD_NodeKind kind)
{
  md_close_unmatched(p);
  for(;!md_can_contain(p->tip->kind, kind);)
  {
    MD_Node *parent = p->tip->parent;
    md_finalize(p, p->tip);
    p->tip = parent;
  }
  MD_Node *n = md_node_alloc(p->arena, kind);
  n->range = r1u64(p->line_start, p->line_end);
  md_node_append(p->tip, n);
  p->tip = n;
  return n;
}

internal void
md_add_line(MD_BlockParser *p)
{
  str8_list_push(p->arena, &p->tip->lines, md_rest_of_line(p));
}

internal String8List md_split_table_row(Arena *arena, String8 line);
internal void md_table_add_row(MD_BlockParser *p, MD_Node *table, String8 line, B32 head);

internal void
md_finalize(MD_BlockParser *p, MD_Node *node)
{
  if(!node->open) { return; }
  node->open = 0;
  switch(node->kind)
  {
    default:{}break;
    case MD_NodeKind_Paragraph:
    {
      String8 text = md_join_lines(p->arena, &node->lines);
      text = md_extract_link_refs(p, text);
      MemoryZeroStruct(&node->lines);
      if(md_str8_is_blank(text))
      {
        md_node_remove(node);
      }
      else
      {
        node->text = text;
      }
    }break;
    case MD_NodeKind_CodeBlock:
    {
      if(!node->fenced)
      {
        // drop trailing blank lines
        String8List kept = {0};
        U64 keep = 0, idx = 0;
        for(String8Node *n = node->lines.first; n != 0; n = n->next, idx += 1)
        {
          if(!md_str8_is_blank(n->string)) { keep = idx+1; }
        }
        idx = 0;
        for(String8Node *n = node->lines.first; n != 0 && idx < keep; n = n->next, idx += 1)
        {
          str8_list_push(p->arena, &kept, n->string);
        }
        node->lines = kept;
      }
      node->text = md_join_lines(p->arena, &node->lines);
    }break;
    case MD_NodeKind_HtmlBlock:
    {
      node->text = md_join_lines(p->arena, &node->lines);
    }break;
    case MD_NodeKind_List:
    {
      node->tight = !node->tight; // `tight` holds "loose" while parsing
    }break;
    case MD_NodeKind_BlockQuote:
    {
      // GFM alerts: a first line of exactly [!KIND]
      MD_Node *first = node->first;
      if(first && first->kind == MD_NodeKind_Paragraph && first->text.size)
      {
        String8 text = first->text;
        U64 nl = str8_find_needle(text, 0, str8_lit("\n"), 0);
        String8 head = md_trim_spaces(str8_prefix(text, nl));
        local_persist struct { char *name; MD_AlertKind kind; } alerts[] =
        {
          {"[!NOTE]", MD_AlertKind_Note}, {"[!TIP]", MD_AlertKind_Tip}, {"[!IMPORTANT]", MD_AlertKind_Important},
          {"[!WARNING]", MD_AlertKind_Warning}, {"[!CAUTION]", MD_AlertKind_Caution},
        };
        for EachElement(i, alerts)
        {
          if(str8_match(head, str8_cstring(alerts[i].name), StringMatchFlag_CaseInsensitive))
          {
            node->alert = alerts[i].kind;
            String8 rest = (nl < text.size) ? str8_skip(text, nl+1) : str8_zero();
            if(md_str8_is_blank(rest)) { md_node_remove(first); }
            else { first->text = rest; }
            break;
          }
        }
      }
    }break;
  }
}

////////////////////////////////
//~ Block Parsing: Continuation

// 0 = matched, 1 = not matched, 2 = matched and the line is fully consumed
internal U32
md_continue(MD_BlockParser *p, MD_Node *c)
{
  switch(c->kind)
  {
    default: return 1;
    case MD_NodeKind_Document:
    case MD_NodeKind_List: return 0;
    case MD_NodeKind_BlockQuote:
    {
      if(!p->indented && md_peek(p, p->next_nonspace) == '>')
      {
        p->pos = p->next_nonspace + 1;
        if(md_peek(p, p->pos) == ' ') { p->pos += 1; }
        return 0;
      }
      return 1;
    }
    case MD_NodeKind_Item:
    {
      if(p->blank)
      {
        if(c->first == 0 && !c->item_saw_content) { return 1; }
        p->pos = p->next_nonspace;
        return 0;
      }
      if(p->indent >= c->content_indent)
      {
        p->pos += c->content_indent;
        return 0;
      }
      return 1;
    }
    case MD_NodeKind_FootnoteDefinition:
    {
      if(p->blank) { p->pos = p->next_nonspace; return 0; }
      if(p->indent >= 4) { p->pos += 4; return 0; }
      return 1;
    }
    case MD_NodeKind_CodeBlock:
    {
      if(c->fenced)
      {
        if(p->indent <= 3)
        {
          String8 rest = str8_skip(p->line, p->next_nonspace);
          U64 n = 0;
          for(;n < rest.size && rest.str[n] == c->marker_char; n += 1);
          if(n >= c->fence_len && md_str8_is_blank(str8_skip(rest, n)))
          {
            c->range.max = p->line_end;
            md_finalize(p, c);
            p->tip = c->parent;
            return 2;
          }
        }
        U64 strip = 0;
        for(;strip < c->fence_indent && md_peek(p, p->pos+strip) == ' '; strip += 1);
        p->pos += strip;
        return 0;
      }
      if(p->indent >= 4) { p->pos += 4; return 0; }
      if(p->blank) { p->pos = p->next_nonspace; return 0; }
      return 1;
    }
    case MD_NodeKind_HtmlBlock:
    {
      if(p->blank && (c->html_kind == 6 || c->html_kind == 7)) { return 1; }
      return 0;
    }
    case MD_NodeKind_Paragraph:
    {
      return p->blank ? 1 : 0;
    }
    case MD_NodeKind_Table:
    {
      if(p->blank) { return 1; }
      if(str8_find_needle(p->line, p->pos, str8_lit("|"), 0) >= p->line.size) { return 1; }
      return 0;
    }
  }
}

////////////////////////////////
//~ Block Parsing: Block Starts

read_only global char *md_html_block_tags[] =
{
  "address", "article", "aside", "base", "basefont", "blockquote", "body", "caption", "center", "col",
  "colgroup", "dd", "details", "dialog", "dir", "div", "dl", "dt", "fieldset", "figcaption", "figure",
  "footer", "form", "frame", "frameset", "h1", "h2", "h3", "h4", "h5", "h6", "head", "header", "hr",
  "html", "iframe", "legend", "li", "link", "main", "menu", "menuitem", "nav", "noframes", "ol",
  "optgroup", "option", "p", "param", "search", "section", "summary", "table", "tbody", "td", "tfoot",
  "th", "thead", "title", "tr", "track", "ul",
};

// Length of an HTML open or closing tag at s[0] == '<', or 0.
internal U64
md_html_tag_length(String8 s)
{
  U64 i = 1;
  B32 closing = 0;
  if(i < s.size && s.str[i] == '/') { closing = 1; i += 1; }
  if(i >= s.size || !char_is_alpha(s.str[i])) { return 0; }
  for(;i < s.size && (md_char_is_alnum(s.str[i]) || s.str[i] == '-'); i += 1);
  if(closing)
  {
    for(;i < s.size && char_is_space(s.str[i]); i += 1);
    return (i < s.size && s.str[i] == '>') ? i+1 : 0;
  }
  for(;;)
  {
    U64 ws = i;
    for(;i < s.size && char_is_space(s.str[i]); i += 1);
    if(i >= s.size) { return 0; }
    if(s.str[i] == '>') { return i+1; }
    if(s.str[i] == '/' && i+1 < s.size && s.str[i+1] == '>') { return i+2; }
    if(ws == i) { return 0; }
    // attribute name
    if(!(char_is_alpha(s.str[i]) || s.str[i] == '_' || s.str[i] == ':')) { return 0; }
    for(;i < s.size && (md_char_is_alnum(s.str[i]) || s.str[i] == '_' || s.str[i] == '.' || s.str[i] == ':' || s.str[i] == '-'); i += 1);
    U64 save = i;
    for(;i < s.size && char_is_space(s.str[i]); i += 1);
    if(i < s.size && s.str[i] == '=')
    {
      i += 1;
      for(;i < s.size && char_is_space(s.str[i]); i += 1);
      if(i >= s.size) { return 0; }
      U8 q = s.str[i];
      if(q == '"' || q == '\'')
      {
        U64 close = str8_find_needle(s, i+1, str8(&q, 1), 0);
        if(close >= s.size) { return 0; }
        i = close+1;
      }
      else
      {
        U64 start = i;
        for(;i < s.size && !char_is_space(s.str[i]) && s.str[i] != '"' && s.str[i] != '\'' && s.str[i] != '=' &&
            s.str[i] != '<' && s.str[i] != '>' && s.str[i] != '`'; i += 1);
        if(i == start) { return 0; }
      }
    }
    else
    {
      i = save;
    }
  }
}

internal U32
md_html_block_start_kind(String8 s, B32 in_paragraph)
{
  if(s.size < 2 || s.str[0] != '<') { return 0; }
  local_persist char *kind1[] = {"script", "pre", "style", "textarea"};
  for EachElement(i, kind1)
  {
    String8 tag = str8_cstring(kind1[i]);
    if(s.size > tag.size && str8_match(str8_substr(s, r1u64(1, 1+tag.size)), tag, StringMatchFlag_CaseInsensitive))
    {
      U8 c = (1+tag.size < s.size) ? s.str[1+tag.size] : 0;
      if(c == 0 || c == ' ' || c == '>' || c == '\t') { return 1; }
    }
  }
  if(str8_match(str8_prefix(s, 4), str8_lit("<!--"), 0)) { return 2; }
  if(str8_match(str8_prefix(s, 2), str8_lit("<?"), 0)) { return 3; }
  if(str8_match(str8_prefix(s, 9), str8_lit("<![CDATA["), 0)) { return 5; }
  if(s.str[1] == '!' && s.size > 2 && char_is_alpha(s.str[2])) { return 4; }
  {
    U64 i = 1;
    if(i < s.size && s.str[i] == '/') { i += 1; }
    U64 start = i;
    for(;i < s.size && md_char_is_alnum(s.str[i]); i += 1);
    String8 name = str8_substr(s, r1u64(start, i));
    U8 c = (i < s.size) ? s.str[i] : 0;
    if(name.size && (c == 0 || c == ' ' || c == '\t' || c == '>' || (c == '/' && i+1 < s.size && s.str[i+1] == '>')))
    {
      for EachElement(k, md_html_block_tags)
      {
        if(str8_match(name, str8_cstring(md_html_block_tags[k]), StringMatchFlag_CaseInsensitive)) { return 6; }
      }
    }
  }
  if(!in_paragraph)
  {
    U64 len = md_html_tag_length(s);
    if(len && md_str8_is_blank(str8_skip(s, len))) { return 7; }
  }
  return 0;
}

internal B32
md_html_block_ends(U32 kind, String8 line)
{
  switch(kind)
  {
    case 1:
    {
      local_persist char *ends[] = {"</script>", "</pre>", "</style>", "</textarea>"};
      for EachElement(i, ends)
      {
        if(str8_find_needle(line, 0, str8_cstring(ends[i]), StringMatchFlag_CaseInsensitive) < line.size) { return 1; }
      }
      return 0;
    }
    case 2: return str8_find_needle(line, 0, str8_lit("-->"), 0) < line.size;
    case 3: return str8_find_needle(line, 0, str8_lit("?>"), 0) < line.size;
    case 4: return str8_find_needle(line, 0, str8_lit(">"), 0) < line.size;
    case 5: return str8_find_needle(line, 0, str8_lit("]]>"), 0) < line.size;
  }
  return 0;
}

internal B32
md_is_thematic_break(String8 s)
{
  U8 c = 0;
  U64 n = 0;
  for EachIndex(i, s.size)
  {
    U8 ch = s.str[i];
    if(ch == ' ' || ch == '\t') { continue; }
    if(c == 0 && (ch == '-' || ch == '*' || ch == '_')) { c = ch; }
    if(ch != c) { return 0; }
    n += 1;
  }
  return n >= 3;
}

internal B32
md_is_table_delim_row(String8 s, U64 *count_out)
{
  s = md_trim_spaces(s);
  if(s.size == 0) { return 0; }
  B32 pipe = 0;
  U64 cells = 0;
  B32 in_cell = 0, cell_dash = 0;
  for EachIndex(i, s.size)
  {
    U8 c = s.str[i];
    if(c == '|')
    {
      pipe = 1;
      if(in_cell) { if(!cell_dash) { return 0; } cells += 1; }
      in_cell = 0; cell_dash = 0;
      continue;
    }
    if(c == '-' ) { in_cell = 1; cell_dash = 1; continue; }
    if(c == ':' ) { in_cell = 1; continue; }
    if(c == ' ' || c == '\t') { continue; }
    return 0;
  }
  if(in_cell) { if(!cell_dash) { return 0; } cells += 1; }
  if(!pipe && cells < 2) { return 0; }
  if(cells == 0) { return 0; }
  if(count_out) { *count_out = cells; }
  return 1;
}

internal String8List
md_split_table_row(Arena *arena, String8 s)
{
  String8List cells = {0};
  s = md_trim_spaces(s);
  if(s.size > 0 && s.str[0] == '|') { s = str8_skip(s, 1); }
  if(s.size > 0 && s.str[s.size-1] == '|' && !(s.size >= 2 && s.str[s.size-2] == '\\')) { s = str8_chop(s, 1); }
  U64 start = 0;
  for(U64 i = 0; i <= s.size; i += 1)
  {
    if(i < s.size && s.str[i] == '\\') { i += 1; continue; }
    if(i >= s.size || s.str[i] == '|')
    {
      U64 end = Min(i, s.size);
      str8_list_push(arena, &cells, md_trim_spaces(str8_substr(s, r1u64(start, end))));
      start = i+1;
    }
  }
  return cells;
}

internal void
md_table_add_row(MD_BlockParser *p, MD_Node *table, String8 line, B32 head)
{
  MD_Node *row = md_node_alloc(p->arena, head ? MD_NodeKind_TableHead : MD_NodeKind_TableRow);
  row->open = 0;
  row->range = r1u64(p->line_start, p->line_end);
  md_node_append(table, row);
  String8List cells = md_split_table_row(p->arena, line);
  String8Node *n = cells.first;
  for EachIndex(c, table->column_count)
  {
    MD_Node *cell = md_node_alloc(p->arena, MD_NodeKind_TableCell);
    cell->open = 0;
    cell->range = row->range;
    // `\|` is a literal pipe inside cells, even in code spans
    String8 text = n ? n->string : str8_zero();
    String8List parts = {0};
    U64 start = 0;
    for(U64 i = 0; i+1 < text.size; i += 1)
    {
      if(text.str[i] == '\\' && text.str[i+1] == '|')
      {
        str8_list_push(p->arena, &parts, str8_substr(text, r1u64(start, i)));
        start = i+1;
        i += 1;
      }
    }
    str8_list_push(p->arena, &parts, str8_skip(text, start));
    cell->text = str8_list_join(p->arena, &parts, 0);
    md_node_append(row, cell);
    if(n) { n = n->next; }
  }
  table->range.max = p->line_end;
}

// 0 = no match, 1 = container started, 2 = leaf started
internal U32
md_try_block_starts(MD_BlockParser *p, MD_Node *container)
{
  U8 c = md_peek(p, p->next_nonspace);
  String8 rest = str8_skip(p->line, p->next_nonspace);

  //- block quote
  if(!p->indented && c == '>')
  {
    p->pos = p->next_nonspace + 1;
    if(md_peek(p, p->pos) == ' ') { p->pos += 1; }
    md_add_child(p, MD_NodeKind_BlockQuote);
    return 1;
  }

  //- ATX heading
  if(!p->indented && c == '#')
  {
    U64 n = 0;
    for(;n < rest.size && rest.str[n] == '#'; n += 1);
    if(n <= 6 && (n == rest.size || md_char_is_space_or_tab(rest.str[n])))
    {
      MD_Node *h = md_add_child(p, MD_NodeKind_Heading);
      h->level = n;
      String8 title = md_trim_spaces(str8_skip(rest, n));
      // heading attributes: trailing {#id .class}
      if(title.size && title.str[title.size-1] == '}')
      {
        U64 open = str8_find_needle_reverse(title, 0, str8_lit("{"), 0);
        if(open > 0 && open <= title.size) { title = md_trim_spaces(str8_prefix(title, open-1)); }
      }
      // closing sequence
      U64 end = title.size;
      for(;end > 0 && title.str[end-1] == '#'; end -= 1);
      if(end == 0) { title = str8_zero(); }
      else if(end < title.size && md_char_is_space_or_tab(title.str[end-1])) { title = md_trim_spaces(str8_prefix(title, end)); }
      h->text = title;
      h->open = 0;
      p->tip = h->parent;
      p->pos = p->line.size;
      return 2;
    }
  }

  //- fenced code
  if(!p->indented && (c == '`' || c == '~'))
  {
    U64 n = 0;
    for(;n < rest.size && rest.str[n] == c; n += 1);
    String8 info = md_trim_spaces(str8_skip(rest, n));
    if(n >= 3 && !(c == '`' && str8_find_needle(info, 0, str8_lit("`"), 0) < info.size))
    {
      MD_Node *code = md_add_child(p, MD_NodeKind_CodeBlock);
      code->fenced = 1;
      code->marker_char = c;
      code->fence_len = n;
      code->fence_indent = p->indent;
      U64 sp = 0;
      for(;sp < info.size && !md_char_is_space_or_tab(info.str[sp]); sp += 1);
      code->info = md_unescape(p->arena, str8_prefix(info, sp));
      p->pos = p->line.size;
      p->line_consumed = 1;
      return 2;
    }
  }

  //- HTML block
  if(!p->indented && c == '<')
  {
    U32 kind = md_html_block_start_kind(rest, container->kind == MD_NodeKind_Paragraph);
    if(kind)
    {
      MD_Node *html = md_add_child(p, MD_NodeKind_HtmlBlock);
      html->html_kind = kind;
      return 2;
    }
  }

  //- setext heading
  if(!p->indented && container->kind == MD_NodeKind_Paragraph && (c == '=' || c == '-'))
  {
    U64 n = 0;
    for(;n < rest.size && rest.str[n] == c; n += 1);
    if(md_str8_is_blank(str8_skip(rest, n)))
    {
      String8 text = md_join_lines(p->arena, &container->lines);
      text = md_extract_link_refs(p, text);
      if(!md_str8_is_blank(text))
      {
        container->kind = MD_NodeKind_Heading;
        container->level = (c == '=') ? 1 : 2;
        container->text = md_trim_spaces(text);
        MemoryZeroStruct(&container->lines);
        container->open = 0;
        container->range.max = p->line_end;
        p->tip = container->parent;
        p->pos = p->line.size;
        return 2;
      }
      MemoryZeroStruct(&container->lines);
      str8_list_push(p->arena, &container->lines, text);
    }
  }

  //- thematic break
  if(!p->indented && (c == '-' || c == '*' || c == '_') && md_is_thematic_break(rest))
  {
    MD_Node *rule = md_add_child(p, MD_NodeKind_Rule);
    rule->open = 0;
    p->tip = rule->parent;
    p->pos = p->line.size;
    return 2;
  }

  //- footnote definition
  if(!p->indented && c == '[' && md_peek(p, p->next_nonspace+1) == '^')
  {
    U64 close = str8_find_needle(rest, 2, str8_lit("]:"), 0);
    if(close < rest.size && close > 2 && str8_find_needle(str8_prefix(rest, close), 0, str8_lit(" "), 0) >= close)
    {
      MD_Node *fn = md_add_child(p, MD_NodeKind_FootnoteDefinition);
      fn->label = push_str8_copy(p->arena, str8_substr(rest, r1u64(2, close)));
      str8_list_push(p->arena, &p->footnote_labels, fn->label);
      p->pos = p->next_nonspace + close + 2;
      for(;md_peek(p, p->pos) == ' '; p->pos += 1);
      return 1;
    }
  }

  //- list item
  if(!p->indented || container->kind == MD_NodeKind_List)
  {
    B32 ordered = 0;
    U64 start_number = 0;
    U8 marker = 0;
    U64 marker_size = 0;
    if((c == '-' || c == '+' || c == '*'))
    {
      marker = c;
      marker_size = 1;
    }
    else if(char_is_digit(c, 10))
    {
      U64 d = 0;
      for(;d < rest.size && d < 9 && char_is_digit(rest.str[d], 10); d += 1);
      if(d < rest.size && (rest.str[d] == '.' || rest.str[d] == ')'))
      {
        ordered = 1;
        marker = rest.str[d];
        start_number = u64_from_str8(str8_prefix(rest, d), 10);
        marker_size = d+1;
      }
    }
    U8 after = (marker_size < rest.size) ? rest.str[marker_size] : 0;
    if(marker_size && (after == 0 || after == ' ' || after == '\t'))
    {
      String8 content = str8_skip(rest, marker_size);
      B32 blank_item = md_str8_is_blank(content);
      B32 interrupts = (container->kind == MD_NodeKind_Paragraph);
      if(!(interrupts && (blank_item || (ordered && start_number != 1))))
      {
        U64 spaces = 0;
        for(;spaces < content.size && content.str[spaces] == ' '; spaces += 1);
        U64 padding = (blank_item || spaces >= 5 || spaces == 0) ? marker_size + 1 : marker_size + spaces;
        U64 advance = (blank_item) ? content.size : (spaces >= 5 ? 1 : spaces);

        // reuse the enclosing list if the marker type matches
        MD_Node *list = 0;
        if(container->kind == MD_NodeKind_List && container->ordered == ordered && container->marker_char == marker)
        {
          list = container;
        }
        if(list == 0)
        {
          list = md_add_child(p, MD_NodeKind_List);
          list->ordered = ordered;
          list->marker_char = marker;
          list->start_number = start_number;
        }
        else
        {
          md_close_unmatched(p);
          p->tip = list;
          if(list->blank_pending) { list->tight = 1; } // `tight` = loose while open
        }
        MD_Node *item = md_add_child(p, MD_NodeKind_Item);
        item->content_indent = p->indent + padding;
        item->ordered = ordered;
        p->pos = p->next_nonspace + marker_size + advance;

        // GFM task list marker
        String8 body = md_rest_of_line(p);
        if(body.size >= 3 && body.str[0] == '[' && body.str[2] == ']' &&
           (body.str[1] == ' ' || body.str[1] == 'x' || body.str[1] == 'X') &&
           (body.size == 3 || md_char_is_space_or_tab(body.str[3])))
        {
          String8 after_marker = md_trim_spaces(str8_skip(body, 3));
          if(after_marker.size)
          {
            item->is_task = 1;
            item->task_checked = (body.str[1] != ' ');
            // the line is a slice of the source unless tabs were expanded
            if(p->line.str >= p->source.str && p->line.str < p->source.str + p->source.size)
            {
              item->task_offset = (U64)(p->line.str - p->source.str) + p->pos + 1;
            }
            else
            {
              String8 src_line = str8_substr(p->source, r1u64(p->line_start, p->line_end));
              U64 b0 = str8_find_needle(src_line, 0, str8_lit("["), 0);
              item->task_offset = p->line_start + b0 + 1;
            }
            p->pos += 3;
            for(;md_peek(p, p->pos) == ' '; p->pos += 1);
          }
        }
        if(!blank_item) { item->item_saw_content = 1; }
        return 1;
      }
    }
  }

  //- indented code
  if(p->indented && p->tip->kind != MD_NodeKind_Paragraph && !p->blank)
  {
    p->pos += 4;
    MD_Node *code = md_add_child(p, MD_NodeKind_CodeBlock);
    (void)code;
    return 2;
  }

  //- GFM table: the paragraph's last line is the header row
  if(!p->indented && container->kind == MD_NodeKind_Paragraph && container->lines.last)
  {
    U64 delim_cells = 0;
    String8 header_line = container->lines.last->string;
    if(md_is_table_delim_row(rest, &delim_cells) && str8_find_needle(header_line, 0, str8_lit("|"), 0) < header_line.size)
    {
      String8List header_cells = md_split_table_row(p->arena, header_line);
      if(header_cells.node_count == delim_cells)
      {
        // split the leading lines off into their own paragraph
        MD_Node *table = container;
        if(container->lines.node_count > 1)
        {
          String8List before = {0};
          for(String8Node *n = container->lines.first; n != container->lines.last; n = n->next)
          {
            str8_list_push(p->arena, &before, n->string);
          }
          MD_Node *para = md_node_alloc(p->arena, MD_NodeKind_Paragraph);
          para->lines = before;
          para->range = container->range;
          // insert before the container
          para->parent = container->parent;
          MD_Node *parent = container->parent;
          MD_Node *prev = 0;
          for(MD_Node *k = parent->first; k != container; k = k->next) { prev = k; }
          para->next = container;
          if(prev) { prev->next = para; } else { parent->first = para; }
          parent->child_count += 1;
          md_finalize(p, para);
        }
        table->kind = MD_NodeKind_Table;
        table->column_count = delim_cells;
        table->aligns = push_array(p->arena, MD_Align, delim_cells);
        String8List delims = md_split_table_row(p->arena, rest);
        U64 col = 0;
        for(String8Node *n = delims.first; n != 0 && col < delim_cells; n = n->next, col += 1)
        {
          String8 d = n->string;
          B32 l = (d.size && d.str[0] == ':');
          B32 r = (d.size && d.str[d.size-1] == ':');
          table->aligns[col] = (l && r) ? MD_Align_Center : r ? MD_Align_Right : l ? MD_Align_Left : MD_Align_None;
        }
        MemoryZeroStruct(&table->lines);
        md_table_add_row(p, table, header_line, 1);
        p->pos = p->line.size;
        return 2;
      }
    }
  }

  return 0;
}

////////////////////////////////
//~ Block Parsing: Lines

internal void
md_incorporate_line(MD_BlockParser *p, String8 line, U64 line_start, U64 line_end)
{
  p->line = line;
  p->line_start = line_start;
  p->line_end = line_end;
  p->pos = 0;
  p->line_consumed = 0;
  p->old_tip = p->tip;
  p->all_closed = 1;

  //- match open containers
  MD_Node *container = p->root;
  B32 all_matched = 1;
  for(;;)
  {
    MD_Node *last = container->last;
    if(last == 0 || !last->open) { break; }
    container = last;
    md_find_next_nonspace(p);
    U32 r = md_continue(p, container);
    if(r == 1) { container = container->parent; all_matched = 0; break; }
    if(r == 2) { goto end_of_line; }
  }
  p->all_closed = (container == p->old_tip);
  p->last_matched = container;

  //- open new blocks
  B32 matched_leaf = (container->kind != MD_NodeKind_Paragraph && md_accepts_lines(container->kind));
  B32 started = 0;
  for(;!matched_leaf;)
  {
    md_find_next_nonspace(p);
    U32 r = md_try_block_starts(p, container);
    if(r == 0) { p->pos = p->next_nonspace; break; }
    started = 1;
    container = p->tip;
    if(r == 2) { matched_leaf = 1; break; }
  }
  if(!matched_leaf) { md_find_next_nonspace(p); }

  //- add the rest of the line
  if(!started && !p->all_closed && !p->blank && p->tip->kind == MD_NodeKind_Paragraph && p->tip->open)
  {
    // lazy paragraph continuation
    md_add_line(p);
    for(MD_Node *n = p->tip; n != 0; n = n->parent) { n->range.max = Max(n->range.max, line_end); }
  }
  else
  {
    md_close_unmatched(p);
    MD_Node *tip = p->tip;

    //- blank lines make the enclosing lists loose if more content follows
    if(p->blank && !(tip->kind == MD_NodeKind_CodeBlock && tip->open))
    {
      for(MD_Node *n = tip; n != 0; n = n->parent)
      {
        if(n->kind == MD_NodeKind_List) { n->blank_pending = 1; }
      }
    }
    else if(!p->blank)
    {
      for(MD_Node *n = tip; n != 0; n = n->parent)
      {
        if(n->kind == MD_NodeKind_Item)
        {
          MD_Node *list = n->parent;
          if(list->blank_pending && n->first != 0) { list->tight = 1; }
          list->blank_pending = 0;
        }
      }
    }

    if(tip->open && (tip->kind == MD_NodeKind_CodeBlock || tip->kind == MD_NodeKind_HtmlBlock || tip->kind == MD_NodeKind_Paragraph))
    {
      if(!(tip->kind == MD_NodeKind_Paragraph && p->blank) && !p->line_consumed)
      {
        md_add_line(p);
      }
      if(tip->kind == MD_NodeKind_HtmlBlock && md_html_block_ends(tip->html_kind, md_rest_of_line(p)))
      {
        tip->range.max = line_end;
        md_finalize(p, tip);
        p->tip = tip->parent;
      }
    }
    else if(tip->kind == MD_NodeKind_Table && tip->open && !p->blank && !started)
    {
      md_table_add_row(p, tip, md_rest_of_line(p), 0);
    }
    else if(!p->blank && md_can_contain(tip->kind, MD_NodeKind_Paragraph))
    {
      md_add_child(p, MD_NodeKind_Paragraph);
      md_add_line(p);
    }
    if(!p->blank || (p->tip->kind == MD_NodeKind_CodeBlock && p->tip->open))
    {
      for(MD_Node *n = p->tip; n != 0; n = n->parent) { n->range.max = Max(n->range.max, line_end); }
    }
    if(!p->blank)
    {
      for(MD_Node *n = p->tip; n != 0; n = n->parent) { if(n->kind == MD_NodeKind_Item) { n->item_saw_content = 1; } }
    }
  }

  end_of_line:;
}

////////////////////////////////
//~ Inline Parsing

typedef struct MD_INode MD_INode;
struct MD_INode
{
  MD_INode *prev;
  MD_INode *next;
  MD_InlineKind kind;
  String8 string;
  String8 url;

  // delimiter runs (* _ ~ ^ ' ")
  U8 delim_char;
  U64 delim_count;
  U64 delim_orig;
  B32 can_open;
  B32 can_close;
  MD_INode *delim_prev;
  MD_INode *delim_next;
  B32 is_delim;

  // raw inline HTML (hidden after text, shown otherwise, like Zed)
  B32 is_html;
  
  // bracket openers become link/image starts, so text never merges into them
  B32 no_merge;
};

typedef struct MD_Bracket MD_Bracket;
struct MD_Bracket
{
  MD_Bracket *prev;
  MD_INode *node;
  MD_INode *prev_delim;
  U64 text_pos;                  // position after '[' in the source text
  B32 image;
  B32 footnote;
  B32 active;
};

typedef struct MD_InlineParser MD_InlineParser;
struct MD_InlineParser
{
  Arena *arena;
  MD_BlockParser *bp;
  String8 s;
  U64 pos;
  MD_INode *first;
  MD_INode *last;
  MD_INode *delim_top;
  MD_Bracket *bracket_top;
};

internal MD_INode *
md_inode_push(MD_InlineParser *ip, MD_InlineKind kind, String8 string)
{
  MD_INode *n = push_array(ip->arena, MD_INode, 1);
  n->kind = kind;
  n->string = string;
  DLLPushBack(ip->first, ip->last, n);
  return n;
}

internal void
md_inode_insert_after(MD_InlineParser *ip, MD_INode *at, MD_INode *n)
{
  DLLInsert(ip->first, ip->last, at, n);
}

internal void
md_inode_remove(MD_InlineParser *ip, MD_INode *n)
{
  DLLRemove(ip->first, ip->last, n);
}

internal void
md_delim_remove(MD_InlineParser *ip, MD_INode *d)
{
  if(d->delim_prev) { d->delim_prev->delim_next = d->delim_next; }
  if(d->delim_next) { d->delim_next->delim_prev = d->delim_prev; }
  else { ip->delim_top = d->delim_prev; }
  d->delim_prev = d->delim_next = 0;
  d->is_delim = 0;
}

internal void
md_ip_push_text(MD_InlineParser *ip, String8 s)
{
  if(s.size == 0) { return; }
  // extend the previous text node when it is contiguous
  if(ip->last && ip->last->kind == MD_InlineKind_Text && !ip->last->is_delim && !ip->last->is_html && !ip->last->no_merge &&
     ip->last->string.str + ip->last->string.size == s.str)
  {
    ip->last->string.size += s.size;
    return;
  }
  md_inode_push(ip, MD_InlineKind_Text, s);
}

internal void
md_scan_delims(MD_InlineParser *ip, U8 c)
{
  String8 s = ip->s;
  U64 start = ip->pos;
  U64 n = 0;
  if(c == '\'' || c == '"') { n = 1; }
  else { for(;start+n < s.size && s.str[start+n] == c; n += 1); }
  U32 before = md_codepoint_before(s, start);
  U32 after = md_codepoint_at(s, start+n);
  B32 after_ws = md_codepoint_is_whitespace(after);
  B32 after_p = md_codepoint_is_punct(after);
  B32 before_ws = md_codepoint_is_whitespace(before);
  B32 before_p = md_codepoint_is_punct(before);
  B32 left = !after_ws && (!after_p || before_ws || before_p);
  B32 right = !before_ws && (!before_p || after_ws || after_p);
  B32 can_open, can_close;
  if(c == '_')
  {
    can_open = left && (!right || before_p);
    can_close = right && (!left || after_p);
  }
  else if(c == '\'' || c == '"')
  {
    can_open = left && !right;
    can_close = right;
  }
  else
  {
    can_open = left;
    can_close = right;
  }
  String8 text = str8_substr(s, r1u64(start, start+n));
  if(c == '\'') { text = str8_lit("\xe2\x80\x99"); }
  if(c == '"')  { text = str8_lit("\xe2\x80\x9c"); }
  MD_INode *d = md_inode_push(ip, MD_InlineKind_Text, text);
  d->delim_char = c;
  d->delim_count = n;
  d->delim_orig = n;
  d->can_open = can_open;
  d->can_close = can_close;
  if(can_open || can_close || c == '\'' || c == '"')
  {
    d->is_delim = 1;
    d->delim_prev = ip->delim_top;
    if(ip->delim_top) { ip->delim_top->delim_next = d; }
    ip->delim_top = d;
  }
  ip->pos = start + n;
}

internal void
md_process_emphasis(MD_InlineParser *ip, MD_INode *stack_bottom)
{
  // find the first delimiter above the bottom
  MD_INode *closer = 0;
  for(MD_INode *d = ip->delim_top; d != 0 && d != stack_bottom; d = d->delim_prev) { closer = d; }
  for(;closer != 0;)
  {
    MD_INode *next_closer = closer->delim_next;
    if(!closer->can_close) { closer = next_closer; continue; }
    U8 c = closer->delim_char;
    MD_INode *opener = closer->delim_prev;
    B32 found = 0;
    for(;opener && opener != stack_bottom; opener = opener->delim_prev)
    {
      if(opener->delim_char == c && opener->can_open)
      {
        B32 odd = (closer->can_open || opener->can_close) &&
          (c == '*' || c == '_') &&
          ((opener->delim_orig + closer->delim_orig) % 3 == 0) &&
          !((opener->delim_orig % 3 == 0) && (closer->delim_orig % 3 == 0));
        B32 tilde_ok = (c != '~' && c != '^') || (opener->delim_count == closer->delim_count);
        if(!odd && tilde_ok) { found = 1; break; }
      }
    }
    if(c == '\'' || c == '"')
    {
      closer->string = (c == '\'') ? str8_lit("\xe2\x80\x99") : str8_lit("\xe2\x80\x9d");
      if(found)
      {
        opener->string = (c == '\'') ? str8_lit("\xe2\x80\x98") : str8_lit("\xe2\x80\x9c");
        md_delim_remove(ip, opener);
      }
      md_delim_remove(ip, closer);
      closer = next_closer;
      continue;
    }
    if(!found)
    {
      if(!closer->can_open) { md_delim_remove(ip, closer); }
      closer = next_closer;
      continue;
    }

    //- emit tags between opener and closer
    U64 use = 1;
    MD_InlineKind start_kind = MD_InlineKind_EmphasisStart, end_kind = MD_InlineKind_EmphasisEnd;
    if(c == '~')
    {
      use = closer->delim_count;
      if(use >= 2) { use = closer->delim_count; start_kind = MD_InlineKind_StrikeStart; end_kind = MD_InlineKind_StrikeEnd; }
      else { start_kind = MD_InlineKind_SubscriptStart; end_kind = MD_InlineKind_SubscriptEnd; }
    }
    else if(c == '^')
    {
      use = closer->delim_count;
      start_kind = MD_InlineKind_SuperscriptStart; end_kind = MD_InlineKind_SuperscriptEnd;
    }
    else if(closer->delim_count >= 2 && opener->delim_count >= 2)
    {
      use = 2;
      start_kind = MD_InlineKind_StrongStart; end_kind = MD_InlineKind_StrongEnd;
    }
    opener->delim_count -= use;
    closer->delim_count -= use;
    opener->string.size -= use;
    closer->string = str8_skip(closer->string, use);
    MD_INode *start_tag = push_array(ip->arena, MD_INode, 1);
    start_tag->kind = start_kind;
    md_inode_insert_after(ip, opener, start_tag);
    MD_INode *end_tag = push_array(ip->arena, MD_INode, 1);
    end_tag->kind = end_kind;
    md_inode_insert_after(ip, closer->prev, end_tag);

    // delimiters between opener and closer can no longer match
    for(MD_INode *d = closer->delim_prev; d && d != opener;)
    {
      MD_INode *prev = d->delim_prev;
      md_delim_remove(ip, d);
      d = prev;
    }
    if(opener->delim_count == 0) { md_delim_remove(ip, opener); md_inode_remove(ip, opener); }
    if(closer->delim_count == 0)
    {
      md_delim_remove(ip, closer);
      md_inode_remove(ip, closer);
      closer = next_closer;
    }
  }
  // remove remaining delimiters above the bottom
  for(;ip->delim_top && ip->delim_top != stack_bottom;)
  {
    MD_INode *d = ip->delim_top;
    md_delim_remove(ip, d);
  }
}

// Inline link tail after `]`: `(dest "title")`, `[ref]`, `[]` or nothing.
internal B32
md_parse_link_tail(MD_InlineParser *ip, U64 bracket_text_start, U64 close_pos, String8 *url_out, U64 *end_out)
{
  String8 s = ip->s;
  U64 i = close_pos + 1;
  Temp scratch = scratch_begin(&ip->arena, 1);
  B32 result = 0;

  //- inline: (dest "title")
  if(i < s.size && s.str[i] == '(')
  {
    U64 k = i+1;
    for(;k < s.size && char_is_space(s.str[k]); k += 1);
    String8 dest = {0};
    B32 ok = 1;
    if(k < s.size && s.str[k] == ')') { ok = 1; }
    else if(!md_parse_link_destination(s, &k, &dest)) { ok = 0; }
    if(ok)
    {
      U64 t = k;
      for(;t < s.size && char_is_space(s.str[t]); t += 1);
      String8 title = {0};
      if(t > k && md_parse_link_title(s, &t, &title))
      {
        for(;t < s.size && char_is_space(s.str[t]); t += 1);
      }
      else
      {
        t = k;
        for(;t < s.size && char_is_space(s.str[t]); t += 1);
      }
      if(t < s.size && s.str[t] == ')')
      {
        *url_out = md_unescape(ip->arena, dest);
        *end_out = t+1;
        result = 1;
      }
    }
  }

  //- reference: [label], [] or shortcut
  if(!result)
  {
    String8 label = str8_substr(s, r1u64(bracket_text_start, close_pos));
    U64 end = close_pos + 1;
    if(i < s.size && s.str[i] == '[')
    {
      U64 close = s.size;
      for(U64 k = i+1; k < s.size; k += 1)
      {
        if(s.str[k] == '\\') { k += 1; continue; }
        if(s.str[k] == '[') { break; }
        if(s.str[k] == ']') { close = k; break; }
      }
      if(close < s.size)
      {
        String8 ref = str8_substr(s, r1u64(i+1, close));
        if(!md_str8_is_blank(ref)) { label = ref; }
        end = close + 1;
      }
    }
    if(label.size && label.str[0] != '^')
    {
      String8 url = {0};
      if(md_ref_lookup(ip->bp, scratch.arena, label, &url))
      {
        *url_out = url;
        *end_out = end;
        result = 1;
      }
    }
  }
  scratch_end(scratch);
  return result;
}

internal void
md_handle_close_bracket(MD_InlineParser *ip)
{
  U64 close_pos = ip->pos;
  ip->pos += 1;
  MD_Bracket *opener = ip->bracket_top;
  if(opener == 0)
  {
    md_ip_push_text(ip, str8_lit("]"));
    return;
  }
  if(!opener->active)
  {
    ip->bracket_top = opener->prev;
    md_ip_push_text(ip, str8_lit("]"));
    return;
  }

  //- footnote reference [^label], when the label is defined
  if(opener->footnote)
  {
    String8 label = str8_substr(ip->s, r1u64(opener->text_pos+1, close_pos));
    B32 defined = 0;
    for(String8Node *n = ip->bp->footnote_labels.first; n != 0; n = n->next)
    {
      if(str8_match(n->string, label, StringMatchFlag_CaseInsensitive)) { defined = 1; break; }
    }
    if(defined && str8_find_needle(label, 0, str8_lit(" "), 0) >= label.size)
    {
      ip->bracket_top = opener->prev;
      for(;opener->node->next != 0;) { md_inode_remove(ip, opener->node->next); }
      for(;ip->delim_top && ip->delim_top != opener->prev_delim;) { md_delim_remove(ip, ip->delim_top); }
      opener->node->kind = MD_InlineKind_FootnoteRef;
      opener->node->string = label;
      return;
    }
  }

  String8 url = {0};
  U64 end = 0;
  if(!md_parse_link_tail(ip, opener->text_pos, close_pos, &url, &end))
  {
    ip->bracket_top = opener->prev;
    md_ip_push_text(ip, str8_lit("]"));
    return;
  }
  ip->pos = end;
  MD_INode *start = opener->node;
  start->kind = opener->image ? MD_InlineKind_ImageStart : MD_InlineKind_LinkStart;
  start->string = str8_zero();
  start->url = url;
  MD_INode *end_node = md_inode_push(ip, opener->image ? MD_InlineKind_ImageEnd : MD_InlineKind_LinkEnd, str8_zero());
  (void)end_node;
  md_process_emphasis(ip, opener->prev_delim);
  ip->bracket_top = opener->prev;
  if(!opener->image)
  {
    for(MD_Bracket *b = ip->bracket_top; b != 0; b = b->prev)
    {
      if(!b->image) { b->active = 0; }
    }
  }
}

internal B32
md_is_br_tag(String8 html)
{
  if(html.size < 4 || html.str[0] != '<') { return 0; }
  String8 inner = str8_skip(str8_chop(html, 1), 1);
  if(inner.size && inner.str[inner.size-1] == '/') { inner = str8_chop(inner, 1); }
  inner = str8_skip_chop_whitespace(inner);
  U64 sp = 0;
  for(;sp < inner.size && !char_is_space(inner.str[sp]); sp += 1);
  return str8_match(str8_prefix(inner, sp), str8_lit("br"), StringMatchFlag_CaseInsensitive);
}

// linkify-style bare URL detection: `scheme://...` with trailing punctuation
// and unbalanced closing brackets trimmed.
internal U64
md_linkify_length(String8 s, U64 i, U64 *start_out)
{
  // walk back from "://" to the scheme start
  if(!(i+3 <= s.size && s.str[i] == ':' && s.str[i+1] == '/' && s.str[i+2] == '/')) { return 0; }
  U64 start = i;
  for(;start > 0 && (char_is_alpha(s.str[start-1]) || char_is_digit(s.str[start-1], 10) || s.str[start-1] == '+' || s.str[start-1] == '-' || s.str[start-1] == '.'); start -= 1);
  for(;start < i && !char_is_alpha(s.str[start]); start += 1);
  if(start == i) { return 0; }
  U64 end = i+3;
  S64 parens = 0, brackets = 0;
  for(;end < s.size; end += 1)
  {
    U8 c = s.str[end];
    if(c <= ' ' || c == '<' || c == '>' || c == '"' || c == '`') { break; }
    if(c == '(') { parens += 1; }
    if(c == ')') { if(parens == 0) { break; } parens -= 1; }
    if(c == '[') { brackets += 1; }
    if(c == ']') { if(brackets == 0) { break; } brackets -= 1; }
  }
  for(;end > i+3;)
  {
    U8 c = s.str[end-1];
    if(c == '.' || c == ',' || c == ':' || c == ';' || c == '?' || c == '!' || c == '\'' || c == '*' || c == '_' || c == '~') { end -= 1; continue; }
    break;
  }
  if(end <= i+3) { return 0; }
  *start_out = start;
  return end - start;
}

internal void
md_parse_inlines_into(MD_InlineParser *ip)
{
  String8 s = ip->s;
  U64 text_start = 0;
#define MD_FlushText() do { if(ip->pos > text_start) { md_ip_push_text(ip, str8_substr(s, r1u64(text_start, ip->pos))); } } while(0)
  for(;ip->pos < s.size;)
  {
    U8 c = s.str[ip->pos];
    switch(c)
    {
      default: { ip->pos += 1; continue; }

      //- line endings
      case '\n':
      {
        // trailing spaces: two or more make a hard break
        U64 e = ip->pos;
        U64 spaces = 0;
        for(;e > text_start && s.str[e-1] == ' '; e -= 1) { spaces += 1; }
        if(e > text_start) { md_ip_push_text(ip, str8_substr(s, r1u64(text_start, e))); }
        else if(spaces && ip->last && ip->last->kind == MD_InlineKind_Text && !ip->last->is_delim)
        {
          String8 *t = &ip->last->string;
          for(;t->size && t->str[t->size-1] == ' ';) { t->size -= 1; }
        }
        md_inode_push(ip, spaces >= 2 ? MD_InlineKind_HardBreak : MD_InlineKind_SoftBreak, str8_zero());
        ip->pos += 1;
        for(;ip->pos < s.size && s.str[ip->pos] == ' '; ip->pos += 1);
        text_start = ip->pos;
        continue;
      }

      //- escapes
      case '\\':
      {
        MD_FlushText();
        U8 n = (ip->pos+1 < s.size) ? s.str[ip->pos+1] : 0;
        if(n == '\n')
        {
          md_inode_push(ip, MD_InlineKind_HardBreak, str8_zero());
          ip->pos += 2;
          for(;ip->pos < s.size && s.str[ip->pos] == ' '; ip->pos += 1);
        }
        else if(md_char_is_ascii_punct(n))
        {
          md_inode_push(ip, MD_InlineKind_Text, str8_substr(s, r1u64(ip->pos+1, ip->pos+2)));
          ip->pos += 2;
        }
        else
        {
          md_ip_push_text(ip, str8_substr(s, r1u64(ip->pos, ip->pos+1)));
          ip->pos += 1;
        }
        text_start = ip->pos;
        continue;
      }

      //- entities
      case '&':
      {
        U8 buf[8]; U64 sz = 0;
        U64 used = md_parse_entity(s, ip->pos, buf, &sz);
        if(used == 0) { ip->pos += 1; continue; }
        MD_FlushText();
        md_inode_push(ip, MD_InlineKind_Text, push_str8_copy(ip->arena, str8(buf, sz)));
        ip->pos += used;
        text_start = ip->pos;
        continue;
      }

      //- code spans
      case '`':
      {
        U64 n = 0;
        for(;ip->pos+n < s.size && s.str[ip->pos+n] == '`'; n += 1);
        U64 k = ip->pos + n;
        U64 close = s.size;
        for(;k < s.size;)
        {
          if(s.str[k] != '`') { k += 1; continue; }
          U64 m = 0;
          for(;k+m < s.size && s.str[k+m] == '`'; m += 1);
          if(m == n) { close = k; break; }
          k += m;
        }
        if(close == s.size)
        {
          ip->pos += n;
          continue;
        }
        MD_FlushText();
        String8 code = push_str8_copy(ip->arena, str8_substr(s, r1u64(ip->pos+n, close)));
        for EachIndex(j, code.size) { if(code.str[j] == '\n') { code.str[j] = ' '; } }
        if(code.size >= 2 && code.str[0] == ' ' && code.str[code.size-1] == ' ' && !md_str8_is_blank(code))
        {
          code = str8_substr(code, r1u64(1, code.size-1));
        }
        md_inode_push(ip, MD_InlineKind_Code, code);
        ip->pos = close + n;
        text_start = ip->pos;
        continue;
      }

      //- emphasis, strikethrough, superscript, smart quotes
      case '*': case '_': case '~': case '^': case '\'': case '"':
      {
        MD_FlushText();
        md_scan_delims(ip, c);
        text_start = ip->pos;
        continue;
      }

      //- smart dashes and ellipses
      case '-':
      {
        U64 n = 0;
        for(;ip->pos+n < s.size && s.str[ip->pos+n] == '-'; n += 1);
        if(n < 2) { ip->pos += 1; continue; }
        MD_FlushText();
        U64 em = 0, en = 0;
        if(n % 3 == 0) { em = n/3; }
        else if(n % 2 == 0) { en = n/2; }
        else if(n % 3 == 2) { em = (n-2)/3; en = 1; }
        else { em = (n-4)/3; en = 2; }
        String8List parts = {0};
        for EachIndex(j, em) { str8_list_push(ip->arena, &parts, str8_lit("\xe2\x80\x94")); }
        for EachIndex(j, en) { str8_list_push(ip->arena, &parts, str8_lit("\xe2\x80\x93")); }
        md_inode_push(ip, MD_InlineKind_Text, str8_list_join(ip->arena, &parts, 0));
        ip->pos += n;
        text_start = ip->pos;
        continue;
      }
      case '.':
      {
        if(ip->pos+2 < s.size && s.str[ip->pos+1] == '.' && s.str[ip->pos+2] == '.')
        {
          MD_FlushText();
          md_inode_push(ip, MD_InlineKind_Text, str8_lit("\xe2\x80\xa6"));
          ip->pos += 3;
          text_start = ip->pos;
          continue;
        }
        ip->pos += 1;
        continue;
      }

      //- links and images
      case '!':
      {
        if(ip->pos+1 < s.size && s.str[ip->pos+1] == '[')
        {
          MD_FlushText();
          MD_INode *n = md_inode_push(ip, MD_InlineKind_Text, str8_substr(s, r1u64(ip->pos, ip->pos+2)));
          n->no_merge = 1;
          MD_Bracket *b = push_array(ip->arena, MD_Bracket, 1);
          b->node = n;
          b->prev_delim = ip->delim_top;
          b->text_pos = ip->pos+2;
          b->image = 1;
          b->active = 1;
          b->prev = ip->bracket_top;
          ip->bracket_top = b;
          ip->pos += 2;
          text_start = ip->pos;
          continue;
        }
        ip->pos += 1;
        continue;
      }
      case '[':
      {
        MD_FlushText();
        B32 footnote = (ip->pos+1 < s.size && s.str[ip->pos+1] == '^');
        MD_INode *n = md_inode_push(ip, MD_InlineKind_Text, str8_substr(s, r1u64(ip->pos, ip->pos + (footnote ? 2 : 1))));
        n->no_merge = 1;
        MD_Bracket *b = push_array(ip->arena, MD_Bracket, 1);
        b->node = n;
        b->prev_delim = ip->delim_top;
        b->text_pos = ip->pos+1;
        b->footnote = footnote;
        b->active = 1;
        b->prev = ip->bracket_top;
        ip->bracket_top = b;
        ip->pos += footnote ? 2 : 1;
        text_start = ip->pos;
        continue;
      }
      case ']':
      {
        MD_FlushText();
        md_handle_close_bracket(ip);
        text_start = ip->pos;
        continue;
      }

      //- autolinks and inline HTML
      case '<':
      {
        String8 rest = str8_skip(s, ip->pos);
        U64 close = str8_find_needle(rest, 1, str8_lit(">"), 0);
        if(close < rest.size)
        {
          String8 inner = str8_substr(rest, r1u64(1, close));
          B32 has_space = 0;
          for EachIndex(j, inner.size) { if(inner.str[j] <= ' ' || inner.str[j] == '<') { has_space = 1; break; } }
          U64 colon = str8_find_needle(inner, 0, str8_lit(":"), 0);
          B32 scheme_ok = (colon >= 2 && colon <= 32 && colon < inner.size && char_is_alpha(inner.str[0]));
          for(U64 j = 1; scheme_ok && j < colon; j += 1)
          {
            U8 ch = inner.str[j];
            if(!(md_char_is_alnum(ch) || ch == '+' || ch == '.' || ch == '-')) { scheme_ok = 0; }
          }
          U64 at = str8_find_needle(inner, 0, str8_lit("@"), 0);
          B32 email_ok = (!scheme_ok && at > 0 && at+1 < inner.size && str8_find_needle(inner, at+1, str8_lit("."), 0) < inner.size);
          if(!has_space && (scheme_ok || email_ok))
          {
            MD_FlushText();
            MD_INode *start = md_inode_push(ip, MD_InlineKind_LinkStart, str8_zero());
            start->url = scheme_ok ? inner : push_str8f(ip->arena, "mailto:%S", inner);
            md_inode_push(ip, MD_InlineKind_Text, inner);
            md_inode_push(ip, MD_InlineKind_LinkEnd, str8_zero());
            ip->pos += close + 1;
            text_start = ip->pos;
            continue;
          }
        }
        U64 len = 0;
        if(str8_match(str8_prefix(rest, 4), str8_lit("<!--"), 0))
        {
          U64 e = str8_find_needle(rest, 4, str8_lit("-->"), 0);
          if(e < rest.size) { len = e + 3; }
        }
        else
        {
          len = md_html_tag_length(rest);
        }
        if(len)
        {
          MD_FlushText();
          String8 html = str8_prefix(rest, len);
          if(md_is_br_tag(html))
          {
            md_inode_push(ip, MD_InlineKind_HardBreak, str8_zero());
          }
          else
          {
            MD_INode *n = md_inode_push(ip, MD_InlineKind_Text, html);
            n->is_html = 1;
          }
          ip->pos += len;
          text_start = ip->pos;
          continue;
        }
        ip->pos += 1;
        continue;
      }
    }
  }
  MD_FlushText();
#undef MD_FlushText
  md_process_emphasis(ip, 0);
}

internal void
md_inline_array_push(Arena *arena, MD_InlineArray *array, U64 *cap, MD_Inline in)
{
  if(array->count == *cap)
  {
    U64 new_cap = (*cap)*2 + 16;
    MD_Inline *bigger = push_array(arena, MD_Inline, new_cap);
    MemoryCopy(bigger, array->v, sizeof(MD_Inline)*array->count);
    array->v = bigger;
    *cap = new_cap;
  }
  array->v[array->count++] = in;
}

internal MD_InlineArray
md_inlines_from_text(MD_BlockParser *bp, String8 text)
{
  Arena *arena = bp->arena;
  MD_InlineParser ip_ = {arena, bp, text};
  MD_InlineParser *ip = &ip_;
  md_parse_inlines_into(ip);
  
  //- flatten. Like Zed's parser: consecutive text merges into one run
  // (inline HTML after text joins the run as "", so it is hidden; inline
  // HTML anywhere else shows as raw text), and runs outside links are
  // searched for bare URLs.
  MD_InlineArray result = {0};
  U64 cap = 0;
  S64 link_depth = 0;
  for(MD_INode *n = ip->first; n != 0;)
  {
    if(n->kind == MD_InlineKind_Text)
    {
      String8List parts = {0};
      if(n->is_html)
      {
        str8_list_push(arena, &parts, n->string);
        n = n->next;
      }
      else for(;n != 0 && n->kind == MD_InlineKind_Text; n = n->next)
      {
        if(!n->is_html) { str8_list_push(arena, &parts, n->string); }
      }
      String8 t = str8_list_join(arena, &parts, 0);
      if(t.size == 0) { continue; }
      U64 from = 0;
      if(link_depth == 0)
      {
        for(U64 i = 0; i < t.size; i += 1)
        {
          if(t.str[i] != ':') { continue; }
          U64 start = 0;
          U64 len = md_linkify_length(t, i, &start);
          if(len == 0 || start < from) { continue; }
          if(start > from) { md_inline_array_push(arena, &result, &cap, (MD_Inline){MD_InlineKind_Text, str8_substr(t, r1u64(from, start))}); }
          String8 url = str8_substr(t, r1u64(start, start+len));
          md_inline_array_push(arena, &result, &cap, (MD_Inline){MD_InlineKind_LinkStart, str8_zero(), url});
          md_inline_array_push(arena, &result, &cap, (MD_Inline){MD_InlineKind_Text, url});
          md_inline_array_push(arena, &result, &cap, (MD_Inline){MD_InlineKind_LinkEnd});
          from = start + len;
          i = from;
        }
      }
      if(from < t.size) { md_inline_array_push(arena, &result, &cap, (MD_Inline){MD_InlineKind_Text, str8_skip(t, from)}); }
      continue;
    }
    if(n->kind == MD_InlineKind_LinkStart || n->kind == MD_InlineKind_ImageStart) { link_depth += 1; }
    if(n->kind == MD_InlineKind_LinkEnd || n->kind == MD_InlineKind_ImageEnd) { link_depth -= 1; }
    md_inline_array_push(arena, &result, &cap, (MD_Inline){n->kind, n->string, n->url});
    n = n->next;
  }
  return result;
}

////////////////////////////////
//~ Metadata Blocks (frontmatter)

// Zed's yaml_frontmatter_candidate + parse_metadata_table_rows
internal U64
md_parse_frontmatter(MD_BlockParser *p, MD_Node *root)
{
  String8 text = p->source;
  B32 yaml = str8_match(str8_prefix(text, 3), str8_lit("---"), 0);
  B32 toml = str8_match(str8_prefix(text, 3), str8_lit("+++"), 0);
  if(!yaml && !toml) { return 0; }
  String8 fence = str8_prefix(text, 3);
  U64 i = 3;
  for(;i < text.size && text.str[i] != '\n' && char_is_space(text.str[i]); i += 1);
  if(i >= text.size || text.str[i] != '\n') { return 0; }
  U64 content_start = i+1;
  // the first content line may not be blank
  U64 k = content_start;
  for(;k < text.size && (text.str[k] == ' ' || text.str[k] == '\t'); k += 1);
  if(k >= text.size || text.str[k] == '\n' || text.str[k] == '\r') { return 0; }
  U64 line_start = content_start;
  for(;line_start < text.size;)
  {
    U64 nl = str8_find_needle(text, line_start, str8_lit("\n"), 0);
    String8 line = str8_substr(text, r1u64(line_start, nl));
    String8 trimmed = md_trim_spaces(line);
    B32 is_close = str8_match(trimmed, fence, 0) || (yaml && str8_match(trimmed, str8_lit("..."), 0));
    if(is_close && (line.size == 0 || line.str[0] != ' '))
    {
      U64 end = (nl < text.size) ? nl+1 : nl;
      MD_Node *meta = md_node_alloc(p->arena, MD_NodeKind_MetadataBlock);
      meta->open = 0;
      meta->range = r1u64(0, end);
      String8 content = str8_substr(text, r1u64(content_start, line_start));
      if(content.size && content.str[content.size-1] == '\n') { content.size -= 1; }
      if(content.size && content.str[content.size-1] == '\r') { content.size -= 1; }
      meta->text = content;
      // simple `key: value` rows
      String8List lines = str8_split(p->arena, content, (U8 *)"\n", 1, 0);
      B32 rows_ok = (lines.node_count > 0);
      meta->meta_keys = push_array(p->arena, String8, lines.node_count);
      meta->meta_values = push_array(p->arena, String8, lines.node_count);
      for(String8Node *n = lines.first; n != 0 && rows_ok; n = n->next)
      {
        String8 l = n->string;
        if(l.size && l.str[l.size-1] == '\r') { l.size -= 1; }
        if(l.size == 0 || char_is_space(l.str[0])) { rows_ok = 0; break; }
        U64 colon = str8_find_needle(l, 0, str8_lit(":"), 0);
        if(colon >= l.size) { rows_ok = 0; break; }
        String8 key = md_trim_spaces(str8_prefix(l, colon));
        String8 value = md_trim_spaces(str8_skip(l, colon+1));
        if(key.size == 0 || value.size == 0) { rows_ok = 0; break; }
        meta->meta_keys[meta->meta_count] = key;
        meta->meta_values[meta->meta_count] = value;
        meta->meta_count += 1;
      }
      if(!rows_ok) { meta->meta_count = 0; }
      md_node_append(root, meta);
      return end;
    }
    if(nl >= text.size) { break; }
    line_start = nl+1;
  }
  return 0;
}

////////////////////////////////
//~ Parser Entry Point

internal void
md_parse_inlines_rec(MD_BlockParser *p, MD_Node *node)
{
  for(MD_Node *c = node->first; c != 0; c = c->next)
  {
    if(md_node_is_text_block(c))
    {
      String8 text = c->text;
      if(c->kind == MD_NodeKind_Paragraph)
      {
        // trailing whitespace of the last line is not a hard break
        for(;text.size && (text.str[text.size-1] == ' ' || text.str[text.size-1] == '\n'); text.size -= 1);
      }
      c->inlines = md_inlines_from_text(p, text);
    }
    md_parse_inlines_rec(p, c);
  }
}

internal void
md_count_words_rec(MD_Node *node, U64 *count)
{
  for(MD_Node *c = node->first; c != 0; c = c->next)
  {
    for EachIndex(i, c->inlines.count)
    {
      MD_Inline *in = &c->inlines.v[i];
      if(in->kind != MD_InlineKind_Text && in->kind != MD_InlineKind_Code) { continue; }
      B32 in_word = 0;
      for EachIndex(k, in->string.size)
      {
        B32 sp = char_is_space(in->string.str[k]);
        if(!sp && !in_word) { *count += 1; }
        in_word = !sp;
      }
    }
    md_count_words_rec(c, count);
  }
}

internal MD_Document
md_parse(Arena *arena, String8 source)
{
  Temp scratch = scratch_begin(&arena, 1);
  MD_BlockParser p_ = {0};
  MD_BlockParser *p = &p_;
  p->arena = arena;
  p->source = source;
  p->root = md_node_alloc(arena, MD_NodeKind_Document);
  p->tip = p->root;

  //- frontmatter
  U64 body_start = md_parse_frontmatter(p, p->root);

  //- lines (tabs expanded to 4-column stops)
  for(U64 start = body_start; start < source.size;)
  {
    U64 nl = str8_find_needle(source, start, str8_lit("\n"), 0);
    U64 end = (nl < source.size) ? nl+1 : nl;
    String8 line = str8_substr(source, r1u64(start, nl));
    if(line.size && line.str[line.size-1] == '\r') { line.size -= 1; }
    if(str8_find_needle(line, 0, str8_lit("\t"), 0) < line.size)
    {
      String8List parts = {0};
      U64 col = 0, seg = 0;
      for EachIndex(i, line.size)
      {
        if(line.str[i] == '\t')
        {
          str8_list_push(arena, &parts, str8_substr(line, r1u64(seg, i)));
          col += i - seg;
          U64 spaces = 4 - (col % 4);
          str8_list_push(arena, &parts, str8_prefix(str8_lit("    "), spaces));
          col += spaces;
          seg = i+1;
        }
      }
      str8_list_push(arena, &parts, str8_skip(line, seg));
      line = str8_list_join(arena, &parts, 0);
    }
    md_incorporate_line(p, line, start, end);
    start = end;
  }
  for(;p->tip != 0;)
  {
    MD_Node *parent = p->tip->parent;
    md_finalize(p, p->tip);
    p->tip = parent;
  }

  //- inline content
  md_parse_inlines_rec(p, p->root);

  MD_Document doc = {0};
  doc.source = source;
  doc.root = p->root;
  doc.end = p->root->last ? p->root->last->range.max : 0;
  md_count_words_rec(p->root, &doc.word_count);
  scratch_end(scratch);
  return doc;
}
