////////////////////////////////
//~ Color Helpers

internal Vec4F32
th_rgba(U32 hex)
{
  return rgba_from_u32(hex);
}

internal Vec4F32
th_alpha(Vec4F32 c, F32 a)
{
  c.w = a;
  return c;
}

// Hsla::opacity: scales the existing alpha
internal Vec4F32
th_opacity(Vec4F32 c, F32 factor)
{
  c.w *= factor;
  return c;
}

// Source-over composite in sRGB space (what gpui's blending produces);
// keeps bg's alpha, like `Hsla::blend`.
internal Vec4F32
th_over(Vec4F32 bg, Vec4F32 fg)
{
  F32 a = fg.w;
  if(a >= 1.f) { return fg; }
  if(a <= 0.f) { return bg; }
  Vec4F32 result =
  {
    bg.x*(1-a) + fg.x*a,
    bg.y*(1-a) + fg.y*a,
    bg.z*(1-a) + fg.z*a,
    bg.w,
  };
  return result;
}

internal Vec4F32
th_linear(Vec4F32 srgba)
{
  return linear_from_srgba(srgba);
}

////////////////////////////////
//~ One Dark / One Light (assets/themes/one/one.json)

internal TH_Theme
th_theme_from_kind(TH_Kind kind)
{
  TH_Theme theme = {0};
  TH_Colors *c = &theme.colors;
  switch(kind)
  {
    default:
    case TH_Kind_OneDark:
    {
      theme.name = str8_lit("One Dark");
      c->background                        = th_rgba(0x3b414dff);
      c->editor_background                 = th_rgba(0x282c33ff);
      c->editor_foreground                 = th_rgba(0xacb2beff);
      c->text                              = th_rgba(0xdce0e5ff);
      c->text_muted                        = th_rgba(0xa9afbcff);
      c->text_accent                       = th_rgba(0x74ade8ff);
      c->border                            = th_rgba(0x464b57ff);
      c->border_variant                    = th_rgba(0x363c46ff);
      c->title_bar_background              = th_rgba(0x3b414dff);
      c->panel_background                  = th_rgba(0x2f343eff);
      c->surface_background                = th_rgba(0x2f343eff);
      c->elevated_surface_background       = th_rgba(0x2f343eff);
      c->element_background                = th_rgba(0x2e343eff);
      c->element_selection_background      = th_rgba(0x74ade83d); // players[0].selection
      c->search_match_background           = th_rgba(0x74ade866);
      c->search_active_match_background    = th_rgba(0xe8af7466);
      c->ghost_element_hover               = th_rgba(0x363c46ff);
      c->text_placeholder                  = th_rgba(0x878a98ff);
      c->border_focused                    = th_rgba(0x47679eff);
      c->scrollbar_thumb_background        = th_rgba(0xc8ccd44c);
      c->scrollbar_thumb_hover_background  = th_rgba(0x363c46ff);
      c->scrollbar_thumb_active_background = th_rgba(0xc8ccd480);
      c->icon_accent                       = th_rgba(0x74ade8ff);
      c->info                              = th_rgba(0x74ade8ff);
      c->success                           = th_rgba(0xa1c181ff);
      c->warning                           = th_rgba(0xdec184ff);
      c->error                             = th_rgba(0xd07277ff);
      c->syntax[TH_Syntax_Plain]              = c->text;
      c->syntax[TH_Syntax_Keyword]            = th_rgba(0xb477cfff);
      c->syntax[TH_Syntax_String]             = th_rgba(0xa1c181ff);
      c->syntax[TH_Syntax_StringEscape]       = th_rgba(0x878e98ff);
      c->syntax[TH_Syntax_Comment]            = th_rgba(0x5d636fff);
      c->syntax[TH_Syntax_Number]             = th_rgba(0xbf956aff);
      c->syntax[TH_Syntax_Boolean]            = th_rgba(0xbf956aff);
      c->syntax[TH_Syntax_Constant]           = th_rgba(0xdfc184ff);
      c->syntax[TH_Syntax_Function]           = th_rgba(0x73ade9ff);
      c->syntax[TH_Syntax_Type]               = th_rgba(0x6eb4bfff);
      c->syntax[TH_Syntax_Property]           = th_rgba(0xd07277ff);
      c->syntax[TH_Syntax_Attribute]          = th_rgba(0x74ade8ff);
      c->syntax[TH_Syntax_Tag]                = th_rgba(0x74ade8ff);
      c->syntax[TH_Syntax_Operator]           = th_rgba(0x6eb4bfff);
      c->syntax[TH_Syntax_Punctuation]        = th_rgba(0xacb2beff);
      c->syntax[TH_Syntax_PunctuationBracket] = th_rgba(0xb2b9c6ff);
      c->syntax[TH_Syntax_Variable]           = th_rgba(0xacb2beff);
      c->syntax[TH_Syntax_VariableSpecial]    = th_rgba(0xbf956aff);
      c->syntax[TH_Syntax_Preproc]            = th_rgba(0xb477cfff);
      c->syntax[TH_Syntax_Title]              = th_rgba(0xd07277ff);
      c->syntax[TH_Syntax_Label]              = th_rgba(0x74ade8ff);
      c->syntax[TH_Syntax_DiffPlus]           = th_rgba(0x98c379ff);
      c->syntax[TH_Syntax_DiffMinus]          = th_rgba(0xe06c75ff);
    }break;
    case TH_Kind_OneLight:
    {
      theme.name = str8_lit("One Light");
      theme.is_light = 1;
      c->background                        = th_rgba(0xdcdcddff);
      c->editor_background                 = th_rgba(0xfafafaff);
      c->editor_foreground                 = th_rgba(0x242529ff);
      c->text                              = th_rgba(0x242529ff);
      c->text_muted                        = th_rgba(0x58585aff);
      c->text_accent                       = th_rgba(0x5c78e2ff);
      c->border                            = th_rgba(0xc9c9caff);
      c->border_variant                    = th_rgba(0xdfdfe0ff);
      c->title_bar_background              = th_rgba(0xdcdcddff);
      c->panel_background                  = th_rgba(0xebebecff);
      c->surface_background                = th_rgba(0xebebecff);
      c->elevated_surface_background       = th_rgba(0xebebecff);
      c->element_background                = th_rgba(0xebebecff);
      c->element_selection_background      = th_rgba(0x5c78e23d);
      c->search_match_background           = th_rgba(0x5c79e266);
      c->search_active_match_background    = th_rgba(0xd0a92366);
      c->ghost_element_hover               = th_rgba(0xdfdfe0ff);
      c->text_placeholder                  = th_rgba(0x7e8086ff);
      c->border_focused                    = th_rgba(0x7d82e8ff);
      c->scrollbar_thumb_background        = th_rgba(0x383a414c);
      c->scrollbar_thumb_hover_background  = th_rgba(0xdfdfe0ff);
      c->scrollbar_thumb_active_background = th_rgba(0x383a4180);
      c->icon_accent                       = th_rgba(0x5c78e2ff);
      c->info                              = th_rgba(0x5c78e2ff);
      c->success                           = th_rgba(0x669f59ff);
      c->warning                           = th_rgba(0xa48819ff);
      c->error                             = th_rgba(0xd36151ff);
      c->syntax[TH_Syntax_Plain]              = c->text;
      c->syntax[TH_Syntax_Keyword]            = th_rgba(0xa449abff);
      c->syntax[TH_Syntax_String]             = th_rgba(0x649f57ff);
      c->syntax[TH_Syntax_StringEscape]       = th_rgba(0x7c7e86ff);
      c->syntax[TH_Syntax_Comment]            = th_rgba(0xa2a3a7ff);
      c->syntax[TH_Syntax_Number]             = th_rgba(0xad6e25ff);
      c->syntax[TH_Syntax_Boolean]            = th_rgba(0xad6e25ff);
      c->syntax[TH_Syntax_Constant]           = th_rgba(0xc18401ff);
      c->syntax[TH_Syntax_Function]           = th_rgba(0x5b79e3ff);
      c->syntax[TH_Syntax_Type]               = th_rgba(0x3882b7ff);
      c->syntax[TH_Syntax_Property]           = th_rgba(0xd3604fff);
      c->syntax[TH_Syntax_Attribute]          = th_rgba(0x5c78e2ff);
      c->syntax[TH_Syntax_Tag]                = th_rgba(0x5c78e2ff);
      c->syntax[TH_Syntax_Operator]           = th_rgba(0x3882b7ff);
      c->syntax[TH_Syntax_Punctuation]        = th_rgba(0x242529ff);
      c->syntax[TH_Syntax_PunctuationBracket] = th_rgba(0x4d4f52ff);
      c->syntax[TH_Syntax_Variable]           = th_rgba(0x242529ff);
      c->syntax[TH_Syntax_VariableSpecial]    = th_rgba(0xad6e25ff);
      c->syntax[TH_Syntax_Preproc]            = th_rgba(0xa449abff);
      c->syntax[TH_Syntax_Title]              = th_rgba(0xd3604fff);
      c->syntax[TH_Syntax_Label]              = th_rgba(0x5c78e2ff);
      c->syntax[TH_Syntax_DiffPlus]           = th_rgba(0x50a14fff);
      c->syntax[TH_Syntax_DiffMinus]          = th_rgba(0xe45649ff);
    }break;
  }
  return theme;
}
