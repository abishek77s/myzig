// markdown: the counterpart of Zed's crates/markdown/src/markdown.rs.
//
//  - MD_Style mirrors `MarkdownStyle::themed(MarkdownFont::Preview, ..)` after
//    `with_preview_overrides`, resolved to device pixels for one rem size.
//  - md_element_build mirrors `MarkdownElement::request_layout`: one walk over
//    the parsed document that pushes and pops styled boxes (Zed's `div_stack`)
//    and flushes inline content into one shaped text per block
//    (`StyledText` + `RenderedLine`), with the same margins, borders, padding
//    and colors.
//  - md_element_layout stands in for taffy: block flow with CSS margin
//    collapsing (gpui divs default to `Display::Block`), flex rows for list
//    items, max-content grids for tables, wrapped text.
//  - md_element_paint mirrors `paint`: backgrounds and borders in tree order,
//    and per line: code chips, then selection, then glyphs.
//
// One gpui quirk matters for sizes: `TextRun` has no font size, so a whole
// text block uses the font size and line height of the div chain it sits in
// (inline code is therefore body-sized), while family, weight, slant, color
// and decorations come per run from the builder's text style stack.

#ifndef MARKDOWN_H
#define MARKDOWN_H

////////////////////////////////
//~ Fonts

typedef struct MD_Fonts MD_Fonts;
struct MD_Fonts
{
  FNT_Tag sans;                  // .ZedSans = IBM Plex Sans
  FNT_Tag sans_italic;
  FNT_Tag sans_semibold;         // 600, also what 700 (BOLD) resolves to
  FNT_Tag sans_semibold_italic;
  FNT_Tag mono;                  // .ZedMono = Lilex
  FNT_Tag mono_italic;
  FNT_Tag mono_bold;
  FNT_Tag mono_bold_italic;
  FNT_Tag icons;
};

typedef enum MD_FontFamily
{
  MD_FontFamily_Sans,
  MD_FontFamily_Mono,
}
MD_FontFamily;

////////////////////////////////
//~ Style (MarkdownStyle for MarkdownFont::Preview)

typedef struct MD_Style MD_Style;
struct MD_Style
{
  TH_Theme *theme;
  MD_Fonts *fonts;
  F32 rem;                       // device px per rem (= preview font size)
  F32 scale;                     // device px per logical px

  F32 paragraph_spacing;         // px(16)
  F32 paragraph_line_height;     // relative(1.5)
  F32 list_spacing;              // px(12)
  Vec2F32 table_cell_padding;    // (px(10), px(4))
  F32 code_block_padding;        // px(12)
  F32 code_block_margin;         // px(16) top and bottom
  F32 code_block_radius;         // px(6)
  F32 code_block_font_size;      // buffer font size = preview font size
  F32 code_block_line_height;    // relative(buffer_line_height) = 1.618
  F32 heading_margin_top;        // px(24)
  F32 heading_margin_bottom;     // px(12)
  F32 heading_line_height;       // relative(1.25)
  F32 heading_sizes[7];          // rems 1.75 1.4 1.2 1.0 0.875 0.85
  B32 heading_weight_semibold;   // markdown_preview.heading_font_weight = 600
};

internal MD_Style md_style_preview(TH_Theme *theme, MD_Fonts *fonts, F32 rem, F32 scale);
internal F32 md_rems(MD_Style *style, F32 rems);
internal F32 md_px(MD_Style *style, F32 px);

////////////////////////////////
//~ Text (StyledText + RenderedLine)

typedef U32 MD_RunFlags;
enum
{
  MD_RunFlag_Bold       = (1<<0),
  MD_RunFlag_Italic     = (1<<1),
  MD_RunFlag_Underline  = (1<<2),
  MD_RunFlag_Strike     = (1<<3),
  MD_RunFlag_Chip       = (1<<4),  // inline code background
};

typedef struct MD_TextRun MD_TextRun;
struct MD_TextRun
{
  U64 start;
  U64 end;
  MD_FontFamily family;
  MD_RunFlags flags;
  Vec4F32 color;                 // sRGB
  Vec4F32 underline_color;
  Vec4F32 chip_color;
  U64 link;                      // 1-based index into the element's links, 0 = none
};

typedef struct MD_TextFrag MD_TextFrag;
struct MD_TextFrag
{
  U64 run;
  U64 start;
  U64 end;
  F32 x;
  F32 width;
};

typedef struct MD_TextLine MD_TextLine;
struct MD_TextLine
{
  U64 start;
  U64 end;
  F32 width;
  U64 first_frag;
  U64 frag_count;
};

typedef struct MD_Text MD_Text;
struct MD_Text
{
  String8 string;
  MD_TextRun *runs;
  U64 run_count;
  F32 font_size;                 // device px, from the div chain
  F32 line_height;               // device px, pixel-snapped
  MD_Align align;
  B32 wrap;

  // layout
  F32 layout_width;
  MD_TextLine *lines;
  U64 line_count;
  MD_TextFrag *frags;
  U64 frag_count;
  F32 width;                     // widest line
  F32 ascent;
  F32 descent;

  // selection space
  U64 global_start;
  struct MD_Box *box;
};

////////////////////////////////
//~ Boxes (the div tree)

typedef enum MD_BoxKind
{
  MD_BoxKind_Block,              // display: block
  MD_BoxKind_Row,                // h_flex
  MD_BoxKind_Wrap,               // flex_row + flex_wrap (paragraphs holding images)
  MD_BoxKind_Grid,               // tables, metadata
  MD_BoxKind_Text,
  MD_BoxKind_Gutter,             // root block marker bar
  MD_BoxKind_Checkbox,
  MD_BoxKind_Icon,
  MD_BoxKind_Image,
  MD_BoxKind_CopyButton,
}
MD_BoxKind;

typedef struct MD_Image MD_Image;
struct MD_Image
{
  MD_Image *next;
  String8 key;
  R_Handle texture;
  Vec2S32 size;
  B32 failed;
};

typedef struct MD_Box MD_Box;
struct MD_Box
{
  MD_Box *first;
  MD_Box *last;
  MD_Box *next;
  MD_Box *parent;
  MD_BoxKind kind;

  // style
  F32 margin[4];                 // Side order: top, right, bottom, left
  F32 padding[4];
  F32 border[4];
  Vec4F32 border_color;
  Vec4F32 background;            // sRGB, alpha 0 = none
  F32 radius;
  B32 absolute;
  B32 bfc;                       // children's margins stay inside (flex items, grid cells)
  B32 flex_1;
  B32 shrink_to_fit;             // table inside its flex wrapper
  B32 items_center;
  F32 gap;
  F32 fixed_width;
  F32 fixed_height;
  U64 grid_columns;
  B32 grid_equal_columns;
  MD_Align align;
  B32 line_break;                // w_full h_0 spacer in a wrap box

  // div text style refinements (0 = inherit)
  F32 text_font_size;
  F32 text_line_height_rel;
  F32 text_line_height_abs;

  // content
  MD_Text *text;
  String8 icon;                  // icon font glyph
  F32 icon_size;
  Vec4F32 icon_color;
  B32 checked;
  MD_Image *image;
  U64 link;

  // source range of the div (push_div), for source-index lookups
  Rng1U64 src;
  
  // behavior
  B32 scroll_x;
  U64 key;                       // source offset, for per-block state
  U64 root_index;                // 1-based root block index
  B32 code_group;
  String8 code_text;

  // layout results
  Rng2F32 rect;                  // border box, element coordinates
  F32 content_width;             // scrollable width of a scroll_x box
  F32 eff_margin_top;
  F32 eff_margin_bottom;
  B32 collapse_top;
  B32 collapse_bottom;
};

typedef enum MD_Side
{
  MD_Side_Top,
  MD_Side_Right,
  MD_Side_Bottom,
  MD_Side_Left,
}
MD_Side;

////////////////////////////////
//~ Element

typedef struct MD_Link MD_Link;
struct MD_Link
{
  String8 url;
};

typedef struct MD_Element MD_Element;
struct MD_Element
{
  Arena *arena;
  MD_Box *root;
  MD_Link *links;
  U64 link_count;
  MD_Text **texts;
  U64 text_count;
  U64 text_total;
  U64 root_block_count;
  String8 *anchor_names;         // heading slugs, "footnote:<label>"
  MD_Box **anchor_boxes;
  U64 anchor_count;
  F32 width;
  F32 height;
};

typedef struct MD_ScrollState MD_ScrollState;
struct MD_ScrollState
{
  MD_ScrollState *next;
  U64 key;
  F32 offset;
};

// Per-frame interaction state the view owns and the element reads.
typedef struct MD_Interaction MD_Interaction;
struct MD_Interaction
{
  Vec2F32 mouse;                 // element coordinates
  B32 mouse_inside;
  U64 selection_start;
  U64 selection_end;
  MD_ScrollState *first_scroll;
  U64 copied_code_key;           // code block whose copy button shows "copied"
  U64 copied_code_until_us;
  U64 active_root_index;         // root block holding the "cursor" (link targets)
};

typedef struct MD_Hit MD_Hit;
struct MD_Hit
{
  U64 link;
  U64 checkbox;                  // task_offset + 1 of the hovered checkbox
  MD_Box *code_block;            // hovered code block (copy button group)
  B32 copy_button;
  MD_Box *scroll_box;
  U64 root_index;
  B32 over_text;
};

internal void md_images_reset(void);
internal MD_Element *md_element_build(Arena *arena, MD_Document *doc, MD_Style *style, String8 base_dir);
internal void md_element_layout(MD_Element *el, MD_Style *style, F32 width);
internal void md_element_paint(MD_Element *el, MD_Style *style, MD_Interaction *ix, Vec2F32 origin, Rng2F32 clip);
internal MD_Hit md_element_hit_test(MD_Element *el, MD_Interaction *ix, Vec2F32 p);
internal U64 md_element_offset_from_point(MD_Element *el, MD_Style *style, MD_Interaction *ix, Vec2F32 p);
internal Rng1U64 md_element_word_at(MD_Element *el, U64 offset);
internal String8 md_element_text_in_range(Arena *arena, MD_Element *el, U64 start, U64 end);
internal F32 *md_scroll_offset_ptr(MD_Interaction *ix, Arena *arena, U64 key);
internal MD_Text *md_element_first_text_in(MD_Element *el, MD_Box *box);
internal MD_Text *md_element_text_for_source_index(MD_Element *el, U64 source_index);
internal U64 md_box_root_index(MD_Box *box);

#endif // MARKDOWN_H
