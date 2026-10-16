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
#include "markdown/markdown_inc.h"
#include "markdown_preview/markdown_preview_inc.h"

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
#include "theme/theme.c"
#include "markdown/markdown_inc.c"
#include "markdown_preview/markdown_preview_inc.c"

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
  B32 pick_file_requested;
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
"| `Ctrl` + `O`, `F5` | Open, reload |\n"
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
  String8 icons[] =
  {
    push_str8f(scratch.arena, "%S/data/icons.ttf", exe_dir),
    push_str8f(scratch.arena, "%S/icons.ttf", rad_data),
  };
  f->icons = mv_font_from_candidates(icons, ArrayCount(icons), f->sans);
  scratch_end(scratch);
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

internal void
mv_open_path(String8 path, B32 keep_scroll)
{
  if(mdp_view_open_path(&mv->view, path, keep_scroll)) { mv_update_title(); }
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
    B32 wait = (mv->frame_index > 2) && !mdp_view_is_animating(view);
    events = wm_get_events(scratch.arena, wait);
  }

  //- the file on disk changed
  mdp_view_poll_file(view);

  //- the draw layer must be begun before text is measured
  dr_begin_frame(view->fonts.icons);

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
      case WM_EventKind_Press:
      {
        if(ev->modifiers == WM_Modifier_Ctrl)
        {
          switch(ev->key)
          {
            default:{}break;
            case WM_Key_O: { mv->pick_file_requested = 1; taken = 1; }break;
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
    if(!taken) { mdp_view_handle_event(view, mv->os, ev); }
  }

  //- native file picker
  if(mv->pick_file_requested && mv->frame_depth == 1)
  {
    mv->pick_file_requested = 0;
    String8 initial = view->path.size ? str8_chop_last_slash(view->path) : get_process_info()->initial_path;
    String8 picked = sh_pick_file(scratch.arena, str8_lit("Open Markdown File"), initial);
    if(picked.size) { mv_open_path(picked, 0); }
  }

  //- draw
  DR_Bucket *bucket = dr_bucket_make();
  DR_BucketScope(bucket)
  {
    mdp_view_draw(view, mv->os);
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

  mv->os = wm_window_open(r2f32p(0, 0, 1100, 900), WM_WindowFlag_UseDefaultPosition, str8_lit("Preview"));
  mv->r = r_window_equip(mv->os);
  mv_load_fonts(&mv->view.fonts);

  String8 path = cmdline->inputs.first ? cmdline->inputs.first->string : str8_zero();
  if(path.size == 0 || !mdp_view_open_path(&mv->view, path, 0))
  {
    String8 error = mv->view.error;
    mdp_view_load_text(&mv->view, str8_zero(), str8_zero(), mv_welcome_text);
    mv->view.error = error;
  }
  mv_update_title();

  for(;!update(););

  md_images_reset();
  r_window_unequip(mv->os, mv->r);
  wm_window_close(mv->os);
}
