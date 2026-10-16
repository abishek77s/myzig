// theme: the subset of Zed's `ThemeColors`, `StatusColors` and `SyntaxTheme`
// that the markdown preview reads, filled from Zed's bundled One Dark and
// One Light themes (reference/zed/assets/themes/one/one.json).
//
// Colors are kept in sRGB, as Zed stores them. gpui composites in sRGB while
// the RAD renderer blends in linear space, so translucent colors are
// composited onto their known background here (th_over) and drawn opaque.

#ifndef THEME_H
#define THEME_H

////////////////////////////////
//~ Syntax Highlight Kinds (tree-sitter capture names Zed's themes style)

typedef enum TH_Syntax
{
  TH_Syntax_Plain,
  TH_Syntax_Keyword,
  TH_Syntax_String,
  TH_Syntax_StringEscape,
  TH_Syntax_Comment,
  TH_Syntax_Number,
  TH_Syntax_Boolean,
  TH_Syntax_Constant,
  TH_Syntax_Function,
  TH_Syntax_Type,
  TH_Syntax_Property,
  TH_Syntax_Attribute,
  TH_Syntax_Tag,
  TH_Syntax_Operator,
  TH_Syntax_Punctuation,
  TH_Syntax_PunctuationBracket,
  TH_Syntax_Variable,
  TH_Syntax_VariableSpecial,
  TH_Syntax_Preproc,
  TH_Syntax_Title,
  TH_Syntax_Label,
  TH_Syntax_DiffPlus,
  TH_Syntax_DiffMinus,
  TH_Syntax_COUNT
}
TH_Syntax;

////////////////////////////////
//~ Theme Colors

typedef struct TH_Colors TH_Colors;
struct TH_Colors
{
  // ThemeColors
  Vec4F32 background;
  Vec4F32 editor_background;
  Vec4F32 editor_foreground;
  Vec4F32 text;
  Vec4F32 text_muted;
  Vec4F32 text_accent;
  Vec4F32 border;
  Vec4F32 border_variant;
  Vec4F32 title_bar_background;
  Vec4F32 panel_background;
  Vec4F32 surface_background;
  Vec4F32 elevated_surface_background;
  Vec4F32 element_background;
  Vec4F32 element_selection_background;
  Vec4F32 search_match_background;
  Vec4F32 scrollbar_thumb_background;
  Vec4F32 scrollbar_thumb_hover_background;
  Vec4F32 scrollbar_thumb_active_background;
  Vec4F32 icon_accent;

  // StatusColors
  Vec4F32 info;
  Vec4F32 success;
  Vec4F32 warning;
  Vec4F32 error;

  // SyntaxTheme (Plain = no highlight, i.e. inherited text color)
  Vec4F32 syntax[TH_Syntax_COUNT];
};

typedef enum TH_Kind
{
  TH_Kind_OneDark,
  TH_Kind_OneLight,
  TH_Kind_COUNT
}
TH_Kind;

typedef struct TH_Theme TH_Theme;
struct TH_Theme
{
  String8 name;
  B32 is_light;
  TH_Colors colors;
};

////////////////////////////////
//~ Color Helpers

internal Vec4F32 th_rgba(U32 hex);
internal Vec4F32 th_alpha(Vec4F32 c, F32 a);
internal Vec4F32 th_opacity(Vec4F32 c, F32 factor);
internal Vec4F32 th_over(Vec4F32 bg, Vec4F32 fg);
internal Vec4F32 th_linear(Vec4F32 srgba);

////////////////////////////////
//~ Themes

internal TH_Theme th_theme_from_kind(TH_Kind kind);

#endif // THEME_H
