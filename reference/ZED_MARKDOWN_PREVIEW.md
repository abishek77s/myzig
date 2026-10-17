# Zed Markdown Preview: how it renders, how it's styled, how big things are

A reference for `mdview`, based on Zed's source in `reference/zed`.
All paths below are relative to `reference/zed/`, written as `file:line`.

Two crates do the work:

| Crate | File | Lines | Role |
|---|---|---|---|
| `markdown_preview` | `crates/markdown_preview/src/markdown_preview_view.rs` | 4688 | The workspace item (pane/tab): editor sync, scrolling, the outer layout, font zoom, images, links |
| `markdown_preview` | `crates/markdown_preview/src/markdown_preview_settings.rs` | 27 | `max_width` and auto-open settings |
| `markdown` | `crates/markdown/src/markdown.rs` | 9379 | `Markdown` entity (source + parse), `MarkdownStyle`, `MarkdownElement` (layout and paint) |
| `markdown` | `crates/markdown/src/parser.rs` | 2087 | pulldown-cmark wrapper that produces a flat event list with source ranges |
| `markdown` | `crates/markdown/src/html/*.rs` | ~2500 | Renders raw HTML blocks into the same builder |
| `markdown` | `crates/markdown/src/mermaid.rs` | 1836 | Mermaid diagrams rendered as images |

---

## 1. Pipeline overview

```
Editor buffer ──(EditorEvent::Edited, 200 ms debounce)──► Markdown::reset(source)
      │                                                         │
      │                                     background thread: parse_markdown_with_options
      │                                     (pulldown-cmark → Vec<(Range, MarkdownEvent)>,
      │                                      plus languages, code highlights, data: images)
      │                                                         │
      ▼                                                         ▼
MarkdownPreviewView::render ──► MarkdownElement::request_layout:
   (outer div, rem size,          walk the events once, building a tree of gpui `div`s
    scroll container, max width)  (div_stack) and `StyledText` lines (one per text run of
                                  a block), then hand that tree to taffy for layout
                                                ▼
                                  prepaint (hitbox, autoscroll) ► paint (chips, selection,
                                  search highlights, glyphs, mouse listeners)
```

Each frame rebuilds the element tree from the cached `ParsedMarkdown`. Parsing only runs when the source changes.

---

## 2. The preview view (`markdown_preview_view.rs`)

### 2.1 Creation and modes
- `MarkdownPreviewView` struct: `markdown_preview_view.rs:70-88`. It holds an `Entity<Markdown>`, a `ScrollHandle`, a `RetainAllImageCache`, a `base_directory` for relative images, and `hovered_url`.
- Modes `Default` (bound to one editor) and `Follow` (tracks the active editor): `:90-96`.
- Actions are registered at `:136-240`: `OpenPreview`, `OpenPreviewToTheSide`, `OpenFollowingPreview`, plus optional auto-open of `.md` files when `open_markdown_files_in_preview` is set (`:139-197`).
- `new()` at `:381-466` creates the `Markdown` entity with these options (`:395-401`):
  ```rust
  MarkdownOptions { parse_html: true, render_mermaid_diagrams: true,
                    parse_heading_slugs: true, render_metadata_blocks: true, .. }
  ```

### 2.2 Live update from the editor
- `set_editor` subscribes to editor events (`:552-605`):
  - `Edited` / `BufferEdited` / `DirtyChanged` trigger a debounced reparse (`:565-570`).
  - `SelectionsChanged` syncs the preview to the cursor (`:577-589`).
- Debounce: `REPARSE_DEBOUNCE = 200ms` (`:62`). `schedule_markdown_update` waits on the timer, then copies the whole rope to a string and calls `markdown.reset(contents)` (`:669-720`).
- Only one debounced task runs at a time (`:656-658`).
- Scroll sync: `sync_preview_to_source_index` (`:742-755`) calls `Markdown::request_autoscroll_to_source_index` and `set_active_root_for_source_index`. That highlights the top-level block that contains the cursor (see §4.9).
- Clicking the preview moves the editor cursor (`on_source_click`, `:1165-1176`). Toggling a checkbox rewrites `[ ]`/`[x]` in the buffer (`:1191-1220`).

### 2.3 Outer layout (`Render`, `:1849-1980`)
```rust
div()                                   // :1858
  .image_cache(..).key_context("MarkdownPreview")
  .w_full().flex_1().min_h_0().relative()
  .bg(editor_background)                // :1852-1855, :1884
  .child(
    WithRemSize::new(preview_font_size) // :1886  ← 1rem = preview font size
      .size_full().child(
        div().id("markdown-preview-scroll-container")
          .size_full().overflow_y_scroll()
          .track_scroll(&scroll_handle)
          .restrict_scroll_to_axis()
          .p_4()                        // :1893  16px padding at 1rem = 16px
          .child(
            div().w_full()
              .when_some(max_width, |d, w| d.max_w(w).mx_auto()) // :1952-1956, centered column
              .child(right_click_menu(.. markdown_element ..))))  // :1899
  .custom_scrollbars(.. Vertical ..)    // :1961-1967
  .when_some(hovered_url, LinkPreview at bottom-left) // :1968-1978
```

Key sizing facts:
- **Rem is overridden.** `WithRemSize` (`crates/ui/src/utils/with_rem_size.rs:64,78`) sets `window.rem_size` to the preview font size. Every `rems(..)` value and every gpui spacing helper (`p_4`, `mb_2`, …) inside the preview therefore scales with the preview font size. Values written as `px(..)` do not scale.
- **Content width:** `max_width` defaults to **800px**, centered (`assets/settings/default.json:128-134`). `limit_content_width: false` disables the limit (`markdown_preview_settings.rs:16-20`).
- **Theme:** the preview can use its own theme via `markdown_preview.theme` (`resolve_preview_theme`, `:1066-1071`). In that case `MarkdownStyle::themed_with_overrides` gets the colors of that theme (`:1097-1107`).

### 2.4 `MarkdownElement` configuration (`render_markdown_element`, `:1073-1189`)
- Style: `MarkdownStyle::themed(MarkdownFont::Preview, ..)` (`:1106`).
- Code blocks: `CodeBlockRenderer::Default { copy_button: VisibleOnHover, wrap_button: Hidden, border: false }` (`:1111-1115`).
- `.show_root_block_markers()` (`:1117`) turns on the left gutter bar for each block.
- `.image_resolver(resolve_preview_image)` (`:1118-1131`, implementation at `:1516-1582`):
  - `http(s)` URLs load as URIs.
  - `data:` URLs return `None` here because they are decoded at parse time.
  - A leading `/` or `\` resolves against the workspace root.
  - Other paths resolve relative to the file's folder. A file that doesn't exist yields `None`.

### 2.5 Font size and scrolling
- Preview font size: `ThemeSettings::markdown_preview_font_size` (`crates/theme_settings/src/settings.rs:467-473`). The lookup order is runtime zoom global, then `markdown_preview.font_size`, then `ui_font_size` (default **16**, `default.json:71`). The result is clamped to **6–100px** (`settings.rs:18-19, 705-707`).
- Zoom actions (`IncreaseBufferFontSize`, …) change the size by **±1px** (`:846-880`). With `persist` set, the new size is written to the settings file.
- Line scroll fallback = font size × `buffer_line_height` (comfortable = **1.618**, standard = 1.3; `crates/theme/src/buffer_line_height.rs:15-20`), at `:841-844`.
- `ScrollUp/Down` scroll by the top item's height, capped at `2 × rem` (`:934-970`). Page up/down scroll by the viewport height (`:909-932`).

---

## 3. Parsing (`parser.rs`, `Markdown::parse`)

- pulldown-cmark options (`parser.rs:13-23`): tables, footnotes (new and old), strikethrough, task lists, **smart punctuation**, heading attributes, `+++` metadata, GFM (alerts), super/subscript. YAML frontmatter is added on request (`:298`). Math, definition lists and wikilinks are deliberately off (`:1048-1050`).
- The output is a flat `Vec<(Range<usize>, MarkdownEvent)>`. Every event carries its **source byte range**, which drives selection, click-to-source, search and scroll sync.
- **Root blocks:** `ParseState::push_event` wraps every depth-0 block in `RootStart` / `RootEnd(index)` and records `root_block_starts` (`parser.rs:61-100`). Horizontal rules count as root blocks too (`:83-91`).
- `Markdown::reset` (`markdown.rs:1037-1063`) keeps the previous parse on screen until the new one finishes.
- `Markdown::parse` (`markdown.rs:1204-1228`) coalesces requests through `should_reparse`.
- Background parse (`markdown.rs:1230-1405`):
  - Runs pulldown-cmark.
  - Resolves fenced-code languages through the `LanguageRegistry`, trying the full info string and then its first word (`:1287-1312`).
  - Decodes base64 `data:` images into `images_by_source_offset` (`:1315-1338`).
  - Precomputes syntax highlights for code blocks (`compute_code_block_highlights`, `:1355`, defined at `:1635`).
  - Applies the result on the foreground thread and replays any pending heading scroll or autoscroll (`:1359-1404`).

---

## 4. Rendering (`MarkdownElement`, `markdown.rs`)

### 4.1 The builder model
`MarkdownElement` is a custom gpui `Element` (`impl Element`, `:2583`). All the work happens in `request_layout` (`:2595-3383`):

1. A `MarkdownElementBuilder` is created (`:2631`, struct at `:3769-3787`) with:
   - **`div_stack`**: nested gpui `div`s. The root div is refined by `container_style` (`:3885-3889`).
   - **`text_style_stack`**: `TextStyleRefinement`s pushed and popped for emphasis, links, headings and so on. The effective style is the base style with every refinement on the stack applied in order (`text_style()`, `:3931-3937`).
   - **`pending_line`**: a `String`, `Vec<TextRun>`, source mappings and inline-code chip ranges (`:3864-3871`).
2. One pass over the events (`:2649-3365`):
   - Block `Start` tags call `push_div` (`:3943-3973`). It flushes pending text, then pushes a styled div.
   - Inline tags push a text style.
   - Text appends to `pending_line` (`push_highlighted_text`, `:4153-4205`). One `TextRun` is created per syntax-highlight span.
   - `End` calls `pop_div` (`:4082-4086`), which flushes text and appends the finished div to its parent.
3. `flush_text` (`:4301-4339`) turns the pending line into one `StyledText`, wrapped in a `RenderedLineElement` (`:4358-4417`). **A paragraph's inline content is a single `StyledText`**: gpui's text system lays it out and wraps it to the div width. Inline formatting comes from runs, not nested elements.
4. `build()` (`:4341-4353`) returns the root element plus a `RenderedText` index (lines, links, footnote refs). Mouse hit-testing, selection and copy use that index.
5. Taffy lays out the tree (`:3380-3381`).

**Edge margins:** `push_div` sets margin-top to 0 when a block starts at source offset 0, and margin-bottom to 0 when it ends at the document end (`:3947-3970`). The document therefore has no leading or trailing gap beyond the container's `p_4`.

### 4.2 Paint order per line (`RenderedLineElement::paint`, `:4401-4416`)
1. Inline-code **chips** (`paint_code_chips`, `:4442-4485`): rounded quads, radius **4px**, outset **1px** horizontally, inset **10% of the line height** top and bottom, so they hug the glyphs.
2. Text-run backgrounds.
3. Selection and search **highlights** (`paint_highlights`, `:4489-4514`; ranges computed in `MarkdownHighlights::highlights_for_line`, `:3803-3840`).
4. Glyphs.

`prepaint` (`:3385-3402`) inserts a hitbox and runs `autoscroll` (`:2513-2557`). In controlled mode, autoscroll keeps the target **3 line-heights** from the top and bottom edges of the viewport.

### 4.3 Element → layout map

| Markdown | Where | Layout/styling |
|---|---|---|
| Paragraph | `push_markdown_paragraph` `:2012-2036` | `div().mb(paragraph_spacing).line_height(paragraph_line_height)` plus text-align |
| Heading | `push_markdown_heading` `:2043-2081`, `apply_heading_style` `:3502-3539` | `div().mt_4().mb_2()`, then the level size (`text_3xl`…`text_sm`), `mt_6` for H2–H6, H1/H2 bottom border, then `style.heading` refines margins and the level style sets the text |
| Block quote / GFM alert | `push_markdown_block_quote` `:2088-2137` | `div().pl_4().mb(paragraph_spacing).border_l_4().border_color(kind color)`. Alerts get an icon + bold label header row (`h_flex().gap_1().mb_1()`, `:2100-2123`) |
| List | `:2903-2913` | `div().pl_2p5()`. Top-level lists add `.mb(list_spacing)` |
| List item | `push_markdown_list_item` `:2252-2274`, bullet `:2914-2955` | `h_flex().items_start().gap_1().mb_1().line_height(paragraph_line_height)` holding `[bullet][div().flex_1().w_0()]`. The `w_0` makes the text wrap (`:2272`). Bullet is `"•"`, `"N."`, or a `Checkbox` centered in a box one line tall (`:2936-2949`) |
| Code block | `:2763-2895`, end `:3147-3223` | Outer `div().group("code_block").relative().w_full()` refined by `style.code_block` (padding, margins, border, background, radius), with a horizontal scrollbar when `code_block_overflow_x_scroll` (`:2848-2867`). Inner `div().overflow_x_scroll()` (`:2874-2887`). Copy button at `absolute().top_1().right_1().visible_on_hover("code_block")` (`:3177-3217`) |
| Inline code | `push_markdown_code_span` `:1885-1927` | Text with the `inline_code` style. The background is taken off the run and painted as a chip (`:1904`, see §4.2) |
| Emphasis / Strong / Strike | `:2957-2974` | Italic / `FontWeight::BOLD` + `colors.text` / 1px strikethrough |
| Link | `:2975-2987` | Pushes `style.link`. Range recorded for clicks |
| Image | `push_markdown_image` `:1929-2010` | `img(src).min_w_0().max_w_full().rounded_md().mr_1().mb_1()`. Its parent div becomes `flex_row().flex_wrap()` (`push_image_child`, `:3984-4016`). On failure shows the label "Failed to Load: alt" (`:3477-3500`) |
| Table | `:3032-3064` | `div().flex()` wrapping `div().grid().grid_cols_max_content(n).mb_2().border(px(1.5)).rounded_sm()` with a horizontal scrollbar |
| Table cell | `:3075-3130` | `flex_col().h_full()`, `border_l_1` when col > 0, `border_t_1` when row > 0, padding `table_cell_padding`. Header: `title_bar_background` + semibold (`:3065-3071`). Odd body rows: `panel_background` (zebra) |
| Rule `---` | `:3338-3348` | `div().border_b_1().my(paragraph_spacing).border_color(rule_color)` |
| Footnote def | `:2988-3017` | First one adds a separator `border_t_1().mt_2()`. Then `h_flex().gap_2().pt_1().mb_1().text_size(rems(0.85)).line_height(rems(1.3))` |
| Frontmatter | `push_metadata_block` `:2144-2208` | Simple `key: value` frontmatter renders as a 2-column grid (keys muted + semibold on `panel_background`). Anything else renders as a code block |
| HTML block | `:2896-2902`, `html/html_rendering.rs:40` | Parsed and rendered into the same builder |
| Mermaid | `:2763-2800`, `mermaid.rs:571` | Raster image with zoom, falling back to code |
| Soft / hard break | `:3349-3354`, `:4018-4033` | Soft break becomes `" "`, hard break `"\n"` (in flex-wrap containers: a `w_full().h_0()` spacer) |

### 4.4 Root-block gutter (`show_root_block_markers`)
- `push_root_block` (`:3975-3982`): `div().group("markdown-root-block").relative()` wrapping `div().pl_4()`. Every top-level block is indented **1rem**.
- `pop_root_block` (`:4053-4080`) adds an absolutely positioned bar: `w(px(4)).h_full().rounded_xs()`.
  - The bar uses `border` color on the block holding the editor cursor.
  - Other blocks show `border_variant` on hover.

---

## 5. Style values (`MarkdownStyle`)

The struct is at `markdown.rs:108-136` and the defaults at `:138-167`. The preview builds its style in two passes:

1. `themed_with_overrides(MarkdownFont::Preview, ..)` at `:186-349` sets the common values.
2. `with_preview_overrides` at `:351-413` replaces some of them for the preview.

Where a value is set in both, the second wins.

### 5.1 Fonts (`:193-238`)
| Thing | Source | Default |
|---|---|---|
| Body family | `markdown_preview.font_family`, falling back to `ui_font_family` (`settings.rs:436-440`) | `.ZedSans` (IBM Plex Sans) |
| Code family | `markdown_preview.code_font_family`, falling back to `buffer_font_family` (`settings.rs:444-448`) | `.ZedMono` |
| Heading weight | `markdown_preview.heading_font_weight` (`settings.rs:781-784`) | **600** (semibold) |
| Body size | `rems(1.0)` (`:230-232`, `:356-358`) | = preview font size |

### 5.2 Computed sizes at defaults (rem = 16px, buffer_line_height = 1.618)

| Element | Font size | Line height | Spacing | Source |
|---|---|---|---|---|
| Body text | 1rem = **16px** | `relative(1.5)` = **24px** (the 1.75× set at `:224` is overridden) | paragraph `mb` **16px** (px) | `:361-363` |
| H1 | 1.75rem = **28px**, w600 | 1.25 = 35px | margin-top **24px**, bottom **12px** (px), `pb_2` 8px + 1px bottom border `border_variant` | `:389-410`, `:3524-3526` |
| H2 | 1.4rem = **22.4px** | 1.25 | same margins, `pb_1` 4px + 1px border | `:400`, `:3527-3529` |
| H3 | 1.2rem = **19.2px** | 1.25 | 24 / 12px | `:401` |
| H4 | 1.0rem = **16px** | 1.25 | 24 / 12px | `:402` |
| H5 | 0.875rem = **14px** | 1.25 | 24 / 12px | `:403` |
| H6 | 0.85rem = **13.6px**, `text_muted` | 1.25 | 24 / 12px | `:404-407` |
| Inline code | 0.875rem = **14px**, code family, buffer weight, `colors.text` | inherited | chip bg `editor_foreground @ 8%`, radius 4px | `:290-298`, `:367-368`, `:4443` |
| Code block text | `buffer_font_size` for Preview = preview size → **16px** (an absolute px value, read from the preview size) | `relative(1.618)` ≈ **25.9px** | padding **12px**, margin-top/bottom **16px**, radius **6px**, 1px border `border_variant`, bg `editor_background` | `:257-289`, `:374-386` |
| Block quote | inherits, color `text_muted` | inherits | `pl_4` 16px, left border **4px**, `mb` 16px | `:372`, `:2125-2129` |
| List | — | `relative(1.5)` | `pl_2p5` 10px, top-level `mb` **12px**, item `mb_1` 4px, bullet gap 4px | `:364`, `:2908-2909`, `:2262-2264` |
| Table | header semibold | — | cell padding **10px × 4px**, outer border **1.5px**, radius `rounded_sm` 4px, `mb_2` 8px | `:365`, `:3050-3052` |
| Rule | — | — | 1px bottom border, `my` **16px** | `:3338-3343` |
| Image | — | — | `max_w_full`, radius 6px, `mr_1`/`mb_1` 4px | `:1990-1996` |
| Container | — | — | padding `p_4` 16px, max width **800px** centered | view `:1893`, `:1954` |

**Units:** gpui spacing helpers are rem-based (`0p5` = 0.125rem, `1` = 0.25rem, `2` = 0.5rem, `2p5` = 0.625rem, `4` = 1rem, `6` = 1.5rem; `crates/gpui_macros/src/styles.rs:920-1000`). Radii are rem-based too (`xs` 0.125, `sm` 0.25, `md` 0.375, `lg` 0.5rem; `styles.rs:1236-1253`). Border widths (`border_1`, `border_l_4`) are **px** (`styles.rs:1329+`). The `px(..)` values in `with_preview_overrides` (paragraph 16, heading 24/12, list 12, code padding 12, code margins 16) **do not scale** with preview zoom. Rem values (font sizes, `pl_4`, `p_4`, …) do.

**Heading margin order:** `div().mt_4().mb_2()` (`:2052`), then `apply_heading_style` adds `mt_6` for H2+ (`:3517-3520`), then `heading.style().refine(&style.heading)` (`:2068`) applies `margin.top = 24px`, `margin.bottom = 12px`. The last refinement wins, so every level ends up at 24 / 12px. The `text_3xl`…`text_sm` sizes from `apply_heading_style` (`:3508-3515`; values in `crates/gpui/src/styled.rs:552-588`) are also replaced, because `heading_level_styles` is `Some` for the preview (`:3534-3536`).

### 5.3 Colors (all from the theme; `:240-308`, `:2604-2616`)
| Role | Theme token |
|---|---|
| Background | `editor_background` (view) |
| Body text, headings, strong | `text` |
| Block quote text, H6, frontmatter keys | `text_muted` |
| Link | `text_accent` + 1px underline `text_accent @ 50%` (no link background in the preview, `:370`) |
| Rule, block quote border, table border | `border` |
| Code block border, heading underline | `border_variant` |
| Code block bg | `editor_background` |
| Inline code chip | `editor_foreground @ 8%` |
| Table header / zebra rows | `title_bar_background` / `panel_background` |
| Selection | `element_selection_background` |
| Search match / active | `search_match_background` / `search_active_match_background` |
| Alerts: Note, Tip, Important, Warning, Caution | `status.info`, `success`, `info`, `warning`, `error` (`:246-255`; icons and labels at `:2100-2107`) |
| Code syntax | `theme.syntax()` mapped through tree-sitter highlight IDs (`:4169-4201`) |

---

## 6. Notes for `mdview`

- **Use one rem.** Make 1rem = the preview font size (default 16), and scale spacing and fonts from it. The few fixed-px values (16/24/12/12/16) are a Zed quirk, and you may prefer to scale them too.
- **One text layout per block.** Each paragraph, heading or table cell is one shaped string with style runs. Wrapping happens inside it. Blocks are boxes in a vertical flow.
- **Keep source ranges on everything.** Zed's selection, copy-as-markdown, search, click-to-source and cursor-follow scrolling all come from the byte range on each event and line (`SourceMapping`, `:4774`).
- **Paint order per line:** code chips → run backgrounds → selection/search → glyphs.
- **First and last block have no outer margins** (`:3947-3970`). The content column is at most 800px wide, centered, with 16px padding.
