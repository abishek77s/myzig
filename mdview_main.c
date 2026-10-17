// mdview: a Markdown preview that matches Zed's, built on the RAD Debugger
// layers (base -> window_manager -> font_provider -> render -> font_cache ->
// draw).
//
// The code follows the layout of Zed's sources:
//
//   theme/                 One Dark / One Light colors      (crates/theme, assets/themes/one)
//   markdown/              parser, code highlighting,
//                          MarkdownStyle + MarkdownElement  (crates/markdown)
//   markdown_preview/      settings and the preview view    (crates/markdown_preview)
//   outline_panel/         the headings sidebar             (crates/outline_panel)
//   recent_panel/          the recent files sidebar         (crates/sidebar)
//   icons/                 Zed's SVG icons                  (assets/icons, crates/ui Icon)
//   mdview_main.c          window, events, frame loop       (crates/zed/src/main.rs)
//
// reference/ZED_MARKDOWN_PREVIEW.md describes the Zed side.

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
#include "theme/theme.h"
#include "icons/icons.h"
#include "markdown/markdown_inc.h"
#include "markdown_preview/markdown_preview_inc.h"
#include "outline_panel/outline_panel_inc.h"
#include "recent_panel/recent_panel_inc.h"

//- [c]
#include "base/base_inc.c"
#include "window_manager/window_manager_inc.c"
#include "shell/shell_inc.c"
#include "font_provider/font_provider_inc.c"
#include "render/render_inc.c"
#include "font_cache/font_cache.c"
#include "draw/draw.c"
#if !defined(STBI_INCLUDE_STB_IMAGE_H)
# define STB_IMAGE_IMPLEMENTATION
# define STBI_NO_STDIO
# define STBI_ONLY_PNG
# define STBI_ONLY_JPEG
# define STBI_ONLY_GIF
# define STBI_ONLY_BMP
# include "third_party/stb/stb_image.h"
#endif
#if !defined(NANOSVG_H)
# pragma warning(push)
# pragma warning(disable: 4456 4457 4701 4996)
# define NANOSVG_IMPLEMENTATION
# define NANOSVGRAST_IMPLEMENTATION
# include "third_party/nanosvg/nanosvg.h"
# include "third_party/nanosvg/nanosvgrast.h"
# pragma warning(pop)
#endif
#include "theme/theme.c"
#include "icons/icons.c"
#include "markdown/markdown_inc.c"
#include "markdown_preview/markdown_preview_inc.c"
#include "outline_panel/outline_panel_inc.c"
#include "recent_panel/recent_panel_inc.c"

////////////////////////////////
//~ App State

typedef struct MV_State MV_State;
struct MV_State
{
  Arena *arena;
  B32 quit;
  U64 frame_index;
  U64 frame_depth;
  WM_Window os;
  R_Handle r;
  MDP_View view;
  OP_Panel outline;
  RP_Panel recent;
  B32 pick_file_requested;
  FNT_Tag caption_font;          // Segoe Fluent Icons / Segoe MDL2 Assets, or zero
  S32 caption_pressed;           // MV_Caption under a left press, -1 = none
};

global MV_State *mv = 0;

////////////////////////////////
//~ Welcome Document

read_only global String8 mv_welcome_text = str8_lit_comp(
"# mdview\n"
"\n"
"A Markdown preview that renders like **Zed's**, on the *RAD Debugger* layers.\n"
"\n"
"## Opening files\n"
"\n"
"- Pass a path on the command line: `mdview README.md`\n"
"- Press `Ctrl+O` to pick a file, or drop a `.md` file on this window\n"
"- The preview reloads when the file changes on disk\n"
"- Started without a path, mdview reopens the last file\n"
"- Recent files are listed in the sidebar; click one to open it\n"
"\n"
"## Keys\n"
"\n"
"| Key | Action |\n"
"|:----|:-------|\n"
"| `Ctrl` + `=` / `-` / `0` | Font size up / down / reset |\n"
"| `PageUp` / `PageDown` | Scroll a page |\n"
"| `Up` / `Down` | Scroll |\n"
"| `Alt` + `Up` / `Down` | Previous / next block |\n"
"| `Ctrl` + `Home` / `End` | Top / bottom |\n"
"| `Ctrl` + `C` / `A` | Copy / select all |\n"
"| `Ctrl` + `F` | Search; `Enter` / `F3` next, `Shift` for previous |\n"
"| `Alt` + `C` / `W` | Match case / whole word |\n"
"| Right click | Copy, Copy as Markdown, Copy Link |\n"
"| `Ctrl` + `O`, `F5` | Open, reload |\n"
"| `Ctrl` + `Alt` + `J` | Recent files sidebar |\n"
"| `Ctrl` + `B` | Outline panel; type to filter, arrows to move |\n"
"| `Ctrl` + `T` | One Dark / One Light |\n"
"\n"
"## What renders\n"
"\n"
"1. Headings, paragraphs, *emphasis*, **strong**, ~~strikethrough~~\n"
"2. `inline code`, [links](https://zed.dev) and bare URLs like https://example.com\n"
"3. Nested lists\n"
"   - like this one\n"
"     - and this\n"
"\n"
"- [x] task lists\n"
"- [ ] footnotes[^1]\n"
"\n"
"> [!NOTE]\n"
"> GitHub alerts, and plain block quotes.\n"
"\n"
"```rust\n"
"fn main() {\n"
"    let greeting = \"Hello\";\n"
"    println!(\"{greeting}, world!\"); // 42\n"
"}\n"
"```\n"
"\n"
"---\n"
"\n"
"[^1]: Footnotes render at the end, like this one.\n"
);

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
mv_load_fonts(MD_Fonts *f)
{
  Temp scratch = scratch_begin(0, 0);
  String8 exe_dir = get_process_info()->binary_path;
  // Zed's bundled .ZedSans (IBM Plex Sans) and .ZedMono (Lilex), in mdview/data/fonts
  String8 dirs[] =
  {
    push_str8f(scratch.arena, "%S/data/fonts", exe_dir),
    push_str8f(scratch.arena, "%S/../data/fonts", exe_dir),
  };
  String8 rad_data = push_str8f(scratch.arena, "%S/../../raddebugger/data", exe_dir);
#define MV_Font(field, file, fallback) do { \
String8 c_[] = { push_str8f(scratch.arena, "%S/" file, dirs[0]), push_str8f(scratch.arena, "%S/" file, dirs[1]) }; \
f->field = mv_font_from_candidates(c_, ArrayCount(c_), (fallback)); \
} while(0)
  {
    String8 fallback[] =
    {
#if OS_WINDOWS
      str8_lit("C:/Windows/Fonts/segoeui.ttf"),
#endif
      push_str8f(scratch.arena, "%S/segoeui.ttf", rad_data),
    };
    MV_Font(sans, "IBMPlexSans-Regular.ttf", mv_font_from_candidates(fallback, ArrayCount(fallback), fnt_tag_zero()));
  }
  MV_Font(sans_italic,          "IBMPlexSans-Italic.ttf",         f->sans);
  MV_Font(sans_semibold,        "IBMPlexSans-SemiBold.ttf",       f->sans);
  MV_Font(sans_semibold_italic, "IBMPlexSans-SemiBoldItalic.ttf", f->sans_semibold);
  {
    String8 fallback[] =
    {
      push_str8f(scratch.arena, "%S/JetBrainsMono-Regular.ttf", rad_data),
#if OS_WINDOWS
      str8_lit("C:/Windows/Fonts/consola.ttf"),
#endif
    };
    MV_Font(mono, "Lilex-Regular.ttf", mv_font_from_candidates(fallback, ArrayCount(fallback), f->sans));
  }
  MV_Font(mono_italic,      "Lilex-Italic.ttf",     f->mono);
  MV_Font(mono_bold,        "Lilex-Bold.ttf",       f->mono);
  MV_Font(mono_bold_italic, "Lilex-BoldItalic.ttf", f->mono_bold);
#undef MV_Font
  {
    String8 symbols[] =
    {
#if OS_WINDOWS
      str8_lit("C:/Windows/Fonts/seguisym.ttf"),
      str8_lit("C:/Windows/Fonts/segoeui.ttf"),
#endif
      str8_zero(),
    };
    f->symbols = mv_font_from_candidates(symbols, ArrayCount(symbols), f->sans);
  }
  String8 icons[] =
  {
    push_str8f(scratch.arena, "%S/data/icons.ttf", exe_dir),
    push_str8f(scratch.arena, "%S/icons.ttf", rad_data),
  };
  f->icons = mv_font_from_candidates(icons, ArrayCount(icons), f->sans);
  {
    // windows_caption_font: Segoe Fluent Icons on Windows 11, Segoe MDL2 Assets before
    String8 captions[] =
    {
#if OS_WINDOWS
      str8_lit("C:/Windows/Fonts/SegoeIcons.ttf"),
      str8_lit("C:/Windows/Fonts/segmdl2.ttf"),
#endif
      str8_zero(),
    };
    mv->caption_font = mv_font_from_candidates(captions, ArrayCount(captions), fnt_tag_zero());
  }
  scratch_end(scratch);
}

////////////////////////////////
//~ Title Bar
//
// Kitten's titlebar (reference/kitten/src/ui/titlebar.rs, after Zed's
// PlatformTitleBar) with the brand menu replaced by one button per shortcut:
// a 32px bar in title_bar_background with a 1px bottom border, the buttons
// (Zed's icons as IconButtons, with a tooltip naming the shortcut, or a
// label), and the Windows caption buttons. The open document is shown by the
// recent files sidebar rather than a tab.
// The bar is client area (WM_WindowFlag_CustomBorder): everything but the
// buttons is pushed as caption, so the OS drags, snaps and double-click
// maximizes, and the buttons handle their own clicks.

typedef struct MV_TitleButton MV_TitleButton;
struct MV_TitleButton
{
  String8 label;
  String8 shortcut;
  WM_Modifiers modifiers;
  WM_Key key;
  IC_Icon icon;                  // IC_Icon_Null for a labelled button
  IC_Icon icon_light;            // in One Light, if not the same
  IC_Icon icon_open;             // while the recent files sidebar is open, if not the same
};

read_only global MV_TitleButton mv_title_buttons[] =
{
  // the threads sidebar toggle from Zed's status bar, for the sidebar on the left
  {str8_lit_comp("Recent Files"), str8_lit_comp("Ctrl+Alt+J"), WM_Modifier_Ctrl|WM_Modifier_Alt, WM_Key_J, IC_Icon_SidebarLeftClosed, 0, IC_Icon_SidebarLeftOpen},
  {str8_lit_comp("Outline"), str8_lit_comp("Ctrl+B"), WM_Modifier_Ctrl, WM_Key_B,  IC_Icon_ListTree},
  {str8_lit_comp("Open"),    str8_lit_comp("Ctrl+O"), WM_Modifier_Ctrl, WM_Key_O,  IC_Icon_Folder},
  {str8_lit_comp("Reload"),  str8_lit_comp("F5"),     0,                WM_Key_F5, IC_Icon_RotateCw},
  {str8_lit_comp("Find"),    str8_lit_comp("Ctrl+F"), WM_Modifier_Ctrl, WM_Key_F,  IC_Icon_MagnifyingGlass},
  {str8_lit_comp("Theme"),   str8_lit_comp("Ctrl+T"), WM_Modifier_Ctrl, WM_Key_T,  IC_Icon_Moon, IC_Icon_Sun},
};

typedef enum MV_Caption
{
  MV_Caption_Minimize,
  MV_Caption_Maximize,
  MV_Caption_Close,
  MV_Caption_COUNT
}
MV_Caption;

typedef struct MV_TitleBar MV_TitleBar;
struct MV_TitleBar
{
  F32 scale;
  Rng2F32 rect;
  Rng2F32 buttons[ArrayCount(mv_title_buttons)];
  Rng2F32 captions[MV_Caption_COUNT];
};

#define MV_TITLE_LABEL_PX    12.5f
#define MV_TITLE_SHORTCUT_PX 11.5f

internal MV_TitleBar
mv_title_bar_layout(void)
{
  MV_TitleBar t = {0};
  MD_Fonts *fonts = &mv->view.fonts;
  F32 dpi = wm_dpi_from_window(mv->os);
  F32 s = t.scale = (dpi > 0) ? dpi/96.f : 1.f;
  Rng2F32 client = wm_client_rect_from_window(mv->os);
  F32 h = round_f32(32.f*s);                       // platform_title_bar_height on Windows
  t.rect = r2f32p(client.x0, client.y0, client.x1, client.y0 + h);

  //- caption buttons: 36px wide, the full height, at the right edge
  F32 caption_w = round_f32(36.f*s);
  for EachIndex(i, MV_Caption_COUNT)
  {
    F32 x1 = t.rect.x1 - (MV_Caption_COUNT - 1 - i)*caption_w;
    t.captions[i] = r2f32p(x1 - caption_w, t.rect.y0, x1, t.rect.y1);
  }

  //- shortcut buttons, styled as titlebar_context_button: 26px tall, px_2,
  // 2px apart, after the brand inset; icon buttons are square
  F32 button_h = round_f32(26.f*s);
  F32 y0 = t.rect.y0 + round_f32((h - button_h)/2);
  F32 x = t.rect.x0 + round_f32(8.f*s);
  for EachElement(i, mv_title_buttons)
  {
    MV_TitleButton *b = &mv_title_buttons[i];
    F32 w = button_h;
    if(b->icon == IC_Icon_Null)
    {
      w = 8.f*s + md_text_width(fonts->sans, round_f32(MV_TITLE_LABEL_PX*s), b->label) +
        6.f*s + md_text_width(fonts->mono, round_f32(MV_TITLE_SHORTCUT_PX*s), b->shortcut) + 8.f*s;
    }
    t.buttons[i] = r2f32p(x, y0, x + round_f32(w), y0 + button_h);
    x = t.buttons[i].x1 + round_f32(2.f*s);
  }
  return t;
}

internal S32
mv_title_button_at(MV_TitleBar *t, Vec2F32 pos)
{
  for EachElement(i, mv_title_buttons) { if(contains_2f32(t->buttons[i], pos)) { return (S32)i; } }
  return -1;
}

internal S32
mv_caption_at(MV_TitleBar *t, Vec2F32 pos)
{
  for EachIndex(i, MV_Caption_COUNT) { if(contains_2f32(t->captions[i], pos)) { return (S32)i; } }
  return -1;
}

// Tells the OS which parts of the bar are caption, for the next hit tests.
internal void
mv_title_bar_push_hit_areas(MV_TitleBar *t)
{
  wm_window_clear_custom_border_data(mv->os);
  wm_window_push_custom_edges(mv->os, round_f32(4.f*t->scale));
  wm_window_push_custom_title_bar(mv->os, t->rect.y1 - t->rect.y0);
  for EachElement(i, mv_title_buttons) { wm_window_push_custom_title_bar_client_area(mv->os, t->buttons[i]); }
  for EachIndex(i, MV_Caption_COUNT) { wm_window_push_custom_title_bar_client_area(mv->os, t->captions[i]); }
}

internal void
mv_caption_activate(MV_Caption caption)
{
  switch(caption)
  {
    default:{}break;
    case MV_Caption_Minimize: { wm_window_set_minimized(mv->os, 1); }break;
    case MV_Caption_Maximize: { wm_window_set_maximized(mv->os, !wm_window_is_maximized(mv->os)); }break;
    case MV_Caption_Close:    { mv->quit = 1; }break;
  }
}

internal void
mv_title_bar_draw(MV_TitleBar *t)
{
  MD_Fonts *fonts = &mv->view.fonts;
  TH_Colors *colors = &mv->view.theme.colors;
  Vec2F32 mouse = wm_mouse_from_window(mv->os);
  F32 s = t->scale;
  F32 line = Max(1.f, round_f32(s));
  Vec4F32 bar_bg = colors->title_bar_background;
  Vec4F32 hover_bg = th_over(bar_bg, colors->ghost_element_hover);
  Vec4F32 selected_bg = th_over(bar_bg, colors->element_selection_background);

  //- the bar, and its bottom border
  dr_rect(t->rect, th_linear(bar_bg), 0, 0, 0);
  dr_rect(r2f32p(t->rect.x0, t->rect.y1 - line, t->rect.x1, t->rect.y1), th_linear(colors->border), 0, 0, 0);

  //- shortcut buttons: label in text_muted, the binding in the mono font, dim;
  // or the icon, muted, brightening on hover
  F32 label_px = round_f32(MV_TITLE_LABEL_PX*s);
  F32 shortcut_px = round_f32(MV_TITLE_SHORTCUT_PX*s);
  S32 tooltip = -1;
  for EachElement(i, mv_title_buttons)
  {
    MV_TitleButton *b = &mv_title_buttons[i];
    Rng2F32 r = t->buttons[i];
    B32 hovered = contains_2f32(r, mouse);
    if(hovered) { dr_rect(r, th_linear(hover_bg), 6.f*s, 0, 0.5f); }
    if(b->icon != IC_Icon_Null)
    {
      if(hovered) { tooltip = (S32)i; }
      IC_Icon icon = (b->icon_light && mv->view.theme_kind == TH_Kind_OneLight) ? b->icon_light : b->icon;
      if(b->icon_open && mv->recent.open) { icon = b->icon_open; }
      ic_draw(icon, r, IC_SIZE_SMALL_PX*s, hovered ? colors->text : colors->text_muted);
      continue;
    }
    F32 x = r.x0 + 8.f*s;
    mdp_draw_text_centered_y(fonts->sans, label_px, r, x, b->label, hovered ? colors->text : colors->text_muted);
    x += md_text_width(fonts->sans, label_px, b->label) + 6.f*s;
    mdp_draw_text_centered_y(fonts->mono, shortcut_px, r, x, b->shortcut, colors->text_placeholder);
  }

  //- caption buttons: ghost hover, Windows' close red
  B32 have_font = !fnt_tag_match(mv->caption_font, fnt_tag_zero());
  B32 maximized = wm_window_is_maximized(mv->os);
  for EachIndex(i, MV_Caption_COUNT)
  {
    Rng2F32 r = t->captions[i];
    B32 hovered = contains_2f32(r, mouse);
    B32 active = hovered && mv->caption_pressed == (S32)i;
    Vec4F32 fg = colors->text_muted;
    if(i == MV_Caption_Close && hovered)
    {
      Vec4F32 close_red = th_rgba(0xe81120ff);
      dr_rect(r, th_linear(active ? th_over(bar_bg, th_alpha(close_red, 0.8f)) : close_red), 0, 0, 0);
      fg = active ? th_over(close_red, th_rgba(0xffffffcc)) : th_rgba(0xffffffff);
    }
    else if(hovered)
    {
      dr_rect(r, th_linear(active ? selected_bg : hover_bg), 0, 0, 0);
      fg = colors->text;
    }
    if(have_font)
    {
      String8 glyph = (i == MV_Caption_Minimize) ? str8_lit("\xee\xa4\xa1") :         // U+E921
                      (i == MV_Caption_Close)    ? str8_lit("\xee\xa2\xbb") :         // U+E8BB
                      maximized                  ? str8_lit("\xee\xa4\xa3") :         // U+E923 restore
                                                   str8_lit("\xee\xa4\xa2");          // U+E922 maximize
      F32 px = round_f32(10.f*s);
      F32 w = md_text_width(mv->caption_font, px, glyph);
      mdp_draw_text_centered_y(mv->caption_font, px, r, (r.x0 + r.x1)/2 - w/2, glyph, fg);
    }
    else
    {
      // no icon font: a dash, a square, a cross in the sans font
      String8 glyph = (i == MV_Caption_Minimize) ? str8_lit("\xe2\x80\x93") : (i == MV_Caption_Close) ? str8_lit("\xc3\x97") : str8_lit("\xe2\x96\xa1");
      F32 px = round_f32(14.f*s);
      F32 w = md_text_width(fonts->sans, px, glyph);
      mdp_draw_text_centered_y(fonts->sans, px, r, (r.x0 + r.x1)/2 - w/2, glyph, fg);
    }
  }

  //- an icon button's tooltip (`Tooltip::for_action`): the label and its
  // binding on an elevated surface, rounded and bordered, below the button
  if(tooltip >= 0)
  {
    MV_TitleButton *b = &mv_title_buttons[tooltip];
    Rng2F32 r = t->buttons[tooltip];
    F32 pad_x = round_f32(8.f*s), gap = round_f32(16.f*s);
    F32 label_w = md_text_width(fonts->sans, label_px, b->label);
    F32 w = pad_x + label_w + gap + md_text_width(fonts->mono, shortcut_px, b->shortcut) + pad_x;
    F32 h = round_f32(26.f*s);
    F32 x0 = r.x0;
    F32 client_x1 = wm_client_rect_from_window(mv->os).x1;
    if(x0 + w > client_x1) { x0 = Max(0, client_x1 - w); }
    Rng2F32 tip = r2f32p(x0, t->rect.y1 + round_f32(4.f*s), x0 + round_f32(w), t->rect.y1 + round_f32(4.f*s) + h);
    dr_rect(tip, th_linear(colors->elevated_surface_background), 6.f*s, 0, 0.5f);
    dr_rect(tip, th_linear(colors->border_variant), 6.f*s, line, 0.5f);
    mdp_draw_text_centered_y(fonts->sans, label_px, tip, tip.x0 + pad_x, b->label, colors->text);
    mdp_draw_text_centered_y(fonts->mono, shortcut_px, tip, tip.x0 + pad_x + label_w + gap, b->shortcut, colors->text_muted);
  }
}

////////////////////////////////
//~ Document

internal void
mv_update_title(void)
{
  Temp scratch = scratch_begin(0, 0);
  String8 name = mv->view.name.size ? mv->view.name : str8_lit("Welcome");
  // Zed's preview tab title
  wm_window_set_title(mv->os, push_str8f(scratch.arena, "Preview %S", name));
  scratch_end(scratch);
}

internal B32
mv_open_path(String8 path, B32 keep_scroll)
{
  B32 ok = mdp_view_open_path(&mv->view, path, keep_scroll);
  if(ok)
  {
    mv_update_title();
    rp_recent_push(&mv->recent, mv->view.path);
  }
  return ok;
}

////////////////////////////////
//~ Per-Frame Entry Point

internal B32
frame(void)
{
  Temp scratch = scratch_begin(0, 0);
  MDP_View *view = &mv->view;
  mv->frame_depth += 1;

  //- events. The window procedure re-enters frame() (to repaint while the
  // window is resized) from inside wm_get_events, so only the outermost frame
  // gathers events.
  WM_EventList events = {0};
  if(mv->frame_depth == 1)
  {
    B32 wait = (mv->frame_index > 2) && !mdp_view_is_animating(view) && !op_panel_is_animating(&mv->outline) && !rp_panel_is_animating(&mv->recent);
    events = wm_get_events(scratch.arena, wait);
  }

  //- the file on disk changed
  mdp_view_poll_file(view);

  //- the draw layer must be begun before text is measured
  dr_begin_frame(view->fonts.icons);

  //- the title bar sits above the preview
  MV_TitleBar title = mv_title_bar_layout();
  view->top_inset = title.rect.y1 - title.rect.y0;
  mv->outline.x0 = rp_panel_width_px(&mv->recent, mv->os);
  view->left_inset = mv->outline.x0 + op_panel_width_px(&mv->outline, mv->os);
  mv_title_bar_push_hit_areas(&title);

  for(WM_Event *ev = events.first; ev != 0; ev = ev->next)
  {
    if(!wm_window_match(ev->window, mv->os) && ev->kind != WM_EventKind_Wakeup) { continue; }
    B32 taken = 0;
    switch(ev->kind)
    {
      default:{}break;
      case WM_EventKind_WindowClose: { mv->quit = 1; taken = 1; }break;
      case WM_EventKind_FileDrop:
      {
        if(ev->strings.first) { mv_open_path(ev->strings.first->string, 0); }
        taken = 1;
      }break;
      case WM_EventKind_Release:
      {
        // caption buttons act on release over the button that was pressed
        if(ev->key == WM_Key_LeftMouseButton && mv->caption_pressed >= 0)
        {
          if(mv_caption_at(&title, ev->pos) == mv->caption_pressed) { mv_caption_activate((MV_Caption)mv->caption_pressed); }
          mv->caption_pressed = -1;
          taken = 1;
        }
      }break;
      case WM_EventKind_Press:
      {
        // a press on a shortcut button is that shortcut, handled below or by the view
        if(ev->key == WM_Key_LeftMouseButton && contains_2f32(title.rect, ev->pos))
        {
          S32 button = mv_title_button_at(&title, ev->pos);
          if(button < 0)
          {
            mv->caption_pressed = mv_caption_at(&title, ev->pos);
            taken = 1;
            break;
          }
          ev->key = mv_title_buttons[button].key;
          ev->modifiers = mv_title_buttons[button].modifiers;
        }
        if(ev->modifiers == (WM_Modifier_Ctrl|WM_Modifier_Alt) && ev->key == WM_Key_J)
        {
          mv->recent.open = !mv->recent.open;
          mv->recent.resizing = 0;
          taken = 1;
        }
        else if(ev->modifiers == WM_Modifier_Ctrl)
        {
          switch(ev->key)
          {
            default:{}break;
            case WM_Key_O: { mv->pick_file_requested = 1; taken = 1; }break;
            case WM_Key_B:
            {
              mv->outline.open = !mv->outline.open;
              mv->outline.focused = 0;
              mv->outline.resizing = 0;
              taken = 1;
            }break;
            case WM_Key_T: { view->theme_kind = (view->theme_kind == TH_Kind_OneDark) ? TH_Kind_OneLight : TH_Kind_OneDark; taken = 1; }break;
            case WM_Key_W: case WM_Key_Q: { mv->quit = 1; taken = 1; }break;
          }
        }
        else if(ev->modifiers == 0 && ev->key == WM_Key_F5 && view->path.size)
        {
          mv_open_path(view->path, 1);
          taken = 1;
        }
      }break;
    }
    if(!taken)
    {
      // the recent files first: its resize handle overlaps the outline's left edge
      taken = rp_panel_handle_event(&mv->recent, view, mv->os, ev);
      if(taken && ev->kind == WM_EventKind_Press) { mv->outline.focused = 0; }
    }
    if(!taken) { taken = op_panel_handle_event(&mv->outline, view, mv->os, ev); }
    if(!taken) { mdp_view_handle_event(view, mv->os, ev); }

    //- a recent file was clicked; one that is gone is forgotten
    String8 recent = rp_panel_take_activated(scratch.arena, &mv->recent);
    if(recent.size && !mv_open_path(recent, 0)) { rp_recent_remove(&mv->recent, recent); }
  }

  //- native file picker
  if(mv->pick_file_requested && mv->frame_depth == 1)
  {
    mv->pick_file_requested = 0;
    String8 initial = view->path.size ? str8_chop_last_slash(view->path) : get_process_info()->initial_path;
    String8 picked = sh_pick_file(scratch.arena, str8_lit("Open Markdown File"), initial);
    if(picked.size) { mv_open_path(picked, 0); }
  }

  //- the outline follows the preview
  mv->outline.x0 = rp_panel_width_px(&mv->recent, mv->os);
  view->left_inset = mv->outline.x0 + op_panel_width_px(&mv->outline, mv->os);
  op_panel_update(&mv->outline, view, mv->os);

  //- draw
  DR_Bucket *bucket = dr_bucket_make();
  DR_BucketScope(bucket)
  {
    mdp_view_draw(view, mv->os);
    op_panel_draw(&mv->outline, view, mv->os);
    rp_panel_draw(&mv->recent, view, mv->os);
    mv_title_bar_draw(&title);
  }

  //- submit
  r_begin_frame();
  r_window_begin_frame(mv->os, mv->r);
  dr_submit_bucket(mv->os, mv->r, bucket);
  r_window_end_frame(mv->os, mv->r);
  r_end_frame();

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
  Arena *arena = arena_alloc();
  mv = push_array(arena, MV_State, 1);
  mv->arena = arena;
  mdp_view_init(&mv->view);
  op_panel_init(&mv->outline);
  rp_panel_init(&mv->recent);
  rp_recent_load(&mv->recent);
  mv->caption_pressed = -1;

  mv->os = wm_window_open(r2f32p(0, 0, 1100, 900), WM_WindowFlag_UseDefaultPosition|WM_WindowFlag_CustomBorder, str8_lit("Preview"));
  mv->r = r_window_equip(mv->os);
  mv_load_fonts(&mv->view.fonts);

  String8 path = cmdline->inputs.first ? cmdline->inputs.first->string : str8_zero();
  if(path.size == 0 && mv->recent.path_count != 0 && file_path_exists(mv->recent.paths[0]))
  {
    path = push_str8_copy(arena, mv->recent.paths[0]);
  }
  if(path.size == 0 || !mv_open_path(path, 0))
  {
    String8 error = mv->view.error;
    mdp_view_load_text(&mv->view, str8_zero(), str8_zero(), mv_welcome_text);
    mv->view.error = error;
  }
  mv_update_title();

  for(;!update(););

  md_images_reset();
  ic_release_all();
  r_window_unequip(mv->os, mv->r);
  wm_window_close(mv->os);
}
