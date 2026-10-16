// markdown_code: code block languages and syntax highlighting.
//
// Zed resolves a fence's info string through its LanguageRegistry (by name or
// path suffix, then by the first word) and highlights with tree-sitter
// queries (markdown.rs `compute_code_block_highlights`). This file knows the
// languages Zed bundles (crates/grammars/src/*/config.toml) and approximates
// their highlight captures with a lexer, mapped to the same theme keys:
// identifiers are `variable`, fields `property`, calls `function`, and so on.

#ifndef MARKDOWN_CODE_H
#define MARKDOWN_CODE_H

typedef enum MD_Language
{
  MD_Language_None,
  MD_Language_Shell,
  MD_Language_C,
  MD_Language_Cpp,
  MD_Language_Css,
  MD_Language_Diff,
  MD_Language_Go,
  MD_Language_JavaScript,
  MD_Language_TypeScript,
  MD_Language_Json,
  MD_Language_Python,
  MD_Language_Rust,
  MD_Language_Yaml,
  MD_Language_Markdown,
  MD_Language_COUNT
}
MD_Language;

typedef struct MD_HighlightRun MD_HighlightRun;
struct MD_HighlightRun
{
  U64 start;
  U64 end;
  TH_Syntax syntax;
};

typedef struct MD_HighlightArray MD_HighlightArray;
struct MD_HighlightArray
{
  MD_HighlightRun *v;
  U64 count;
};

internal MD_Language md_language_from_info(String8 info);
internal MD_HighlightArray md_highlights_from_code(Arena *arena, MD_Language lang, String8 code);

#endif // MARKDOWN_CODE_H
