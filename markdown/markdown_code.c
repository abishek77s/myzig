////////////////////////////////
//~ Languages (name / code fence name / path suffixes, as in Zed's configs)

typedef struct MD_LanguageInfo MD_LanguageInfo;
struct MD_LanguageInfo
{
  MD_Language lang;
  char *names;                   // space separated, matched case-insensitively
  char *suffixes;                // space separated, matched exactly
};

read_only global MD_LanguageInfo md_language_table[] =
{
  {MD_Language_Shell,      "shell_script bash", "sh bash bashrc bash_profile zsh zshrc profile envrc bats"},
  {MD_Language_C,          "c", "c"},
  {MD_Language_Cpp,        "c++", "cc hh cpp cppm h hpp cxx hxx c++ h++ ipp inl ino ixx cu cuh C H"},
  {MD_Language_Css,        "css", "css postcss pcss"},
  {MD_Language_Diff,       "diff", "diff patch"},
  {MD_Language_Go,         "go", "go"},
  {MD_Language_JavaScript, "javascript", "js jsx mjs cjs"},
  {MD_Language_TypeScript, "typescript tsx", "ts cts mts tsx"},
  {MD_Language_Json,       "json jsonc", "json jsonc geojson"},
  {MD_Language_Python,     "python", "py pyi mpy"},
  {MD_Language_Rust,       "rust", "rs"},
  {MD_Language_Yaml,       "yaml", "yml yaml"},
  {MD_Language_Markdown,   "markdown", "md mdx markdown MD"},
};

internal B32
md_word_in_list(String8 word, char *list, StringMatchFlags flags)
{
  String8 l = str8_cstring(list);
  for(U64 i = 0; i < l.size;)
  {
    U64 e = i;
    for(;e < l.size && l.str[e] != ' '; e += 1);
    String8 item = str8_substr(l, r1u64(i, e));
    // config names use spaces ("Shell Script"); the table spells them with '_'
    if(item.size == word.size)
    {
      B32 match = 1;
      for EachIndex(k, word.size)
      {
        U8 a = word.str[k], b = item.str[k];
        if(b == '_') { b = ' '; }
        if(flags & StringMatchFlag_CaseInsensitive) { a = lower_from_char(a); b = lower_from_char(b); }
        if(a != b) { match = 0; break; }
      }
      if(match) { return 1; }
    }
    i = e+1;
  }
  return 0;
}

internal MD_Language
md_language_from_name_or_extension(String8 name)
{
  for EachElement(i, md_language_table)
  {
    MD_LanguageInfo *info = &md_language_table[i];
    if(md_word_in_list(name, info->names, StringMatchFlag_CaseInsensitive) ||
       md_word_in_list(name, info->suffixes, 0))
    {
      return info->lang;
    }
  }
  return MD_Language_None;
}

internal MD_Language
md_language_from_info(String8 info)
{
  MD_Language lang = md_language_from_name_or_extension(info);
  if(lang == MD_Language_None)
  {
    U64 sp = 0;
    for(;sp < info.size && !char_is_space(info.str[sp]); sp += 1);
    if(sp < info.size) { lang = md_language_from_name_or_extension(str8_prefix(info, sp)); }
  }
  return lang;
}

////////////////////////////////
//~ Lexer Tables

typedef struct MD_LexerSpec MD_LexerSpec;
struct MD_LexerSpec
{
  char *keywords;
  char *types;
  char *constants;
  char *line_comment;
  char *line_comment2;
  B32 block_comments;            // /* */
  B32 hash_comments;
  B32 preproc;                   // C-style #directives
  B32 single_quote_strings;
  B32 backtick_strings;
  B32 rust_lifetimes;
  B32 capitalized_types;
  B32 variables;                 // plain identifiers get `variable`
};

read_only global MD_LexerSpec md_lexer_c =
{
  "auto break case const continue default do else enum extern for goto if inline register restrict return sizeof static struct switch typedef union volatile while _Alignas _Alignof _Atomic _Bool _Generic _Noreturn _Static_assert _Thread_local",
  "char double float int long short signed unsigned void bool size_t ssize_t int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t uintptr_t intptr_t ptrdiff_t wchar_t FILE",
  "NULL true false",
  "//", 0, 1, 0, 1, 1, 0, 0, 0, 1,
};

read_only global MD_LexerSpec md_lexer_cpp =
{
  "alignas alignof and asm auto break case catch class concept const consteval constexpr constinit const_cast continue co_await co_return co_yield decltype default delete do dynamic_cast else enum explicit export extern final for friend goto if inline mutable namespace new noexcept not operator or override private protected public register reinterpret_cast requires return sizeof static static_assert static_cast struct switch template this thread_local throw try typedef typeid typename union using virtual volatile while",
  "bool char char8_t char16_t char32_t double float int long short signed unsigned void wchar_t size_t auto",
  "nullptr NULL true false",
  "//", 0, 1, 0, 1, 1, 0, 0, 0, 1,
};

read_only global MD_LexerSpec md_lexer_rust =
{
  "as async await break const continue crate dyn else enum extern fn for if impl in let loop match mod move mut pub ref return static struct super trait type union unsafe use where while yield",
  "bool char str u8 u16 u32 u64 u128 usize i8 i16 i32 i64 i128 isize f32 f64 Self",
  "true false",
  "//", 0, 1, 0, 0, 0, 0, 1, 1, 1,
};

read_only global MD_LexerSpec md_lexer_go =
{
  "break case chan const continue default defer else fallthrough for func go goto if import interface map package range return select struct switch type var",
  "bool byte complex64 complex128 error float32 float64 int int8 int16 int32 int64 rune string uint uint8 uint16 uint32 uint64 uintptr any",
  "true false nil iota",
  "//", 0, 1, 0, 0, 1, 1, 0, 0, 1,
};

read_only global MD_LexerSpec md_lexer_js =
{
  "as async await break case catch class const continue debugger default delete do else export extends finally for from function get if import in instanceof let new of return set static super switch this throw try typeof var void while with yield",
  "",
  "true false null undefined NaN Infinity",
  "//", 0, 1, 0, 0, 1, 1, 0, 1, 1,
};

read_only global MD_LexerSpec md_lexer_ts =
{
  "abstract as async await break case catch class const continue debugger declare default delete do else enum export extends finally for from function get if implements import in infer instanceof interface is keyof let namespace new of private protected public readonly return satisfies set static super switch this throw try type typeof var void while with yield",
  "any bigint boolean never number object string symbol unknown void",
  "true false null undefined NaN Infinity",
  "//", 0, 1, 0, 0, 1, 1, 0, 1, 1,
};

read_only global MD_LexerSpec md_lexer_python =
{
  "and as assert async await break class continue def del elif else except finally for from global if import in is lambda nonlocal not or pass raise return try while with yield match case print",
  "int float str bool bytes list dict set tuple object type",
  "True False None",
  0, 0, 0, 1, 0, 1, 0, 0, 1, 1,
};

read_only global MD_LexerSpec md_lexer_shell =
{
  "if then else elif fi case esac for while until do done in function select time return export local readonly declare unset shift source exit",
  "",
  "true false",
  0, 0, 0, 1, 0, 1, 0, 0, 0, 0,
};

read_only global MD_LexerSpec md_lexer_css =
{
  "",
  "",
  "",
  0, 0, 1, 0, 0, 1, 0, 0, 0, 0,
};

read_only global MD_LexerSpec md_lexer_json =
{
  "",
  "",
  "true false null",
  "//", 0, 1, 0, 0, 0, 0, 0, 0, 0,
};

read_only global MD_LexerSpec md_lexer_yaml =
{
  "",
  "",
  "true false null yes no on off True False Null",
  0, 0, 0, 1, 0, 1, 0, 0, 0, 0,
};

////////////////////////////////
//~ Highlighting

internal B32
md_ident_char(U8 c)
{
  return char_is_alpha(c) || char_is_digit(c, 10) || c == '_' || c >= 0x80;
}

internal void
md_highlight_push(Arena *arena, MD_HighlightArray *arr, U64 *cap, U64 start, U64 end, TH_Syntax syntax)
{
  if(end <= start || syntax == TH_Syntax_Plain) { return; }
  if(arr->count && arr->v[arr->count-1].end == start && arr->v[arr->count-1].syntax == syntax)
  {
    arr->v[arr->count-1].end = end;
    return;
  }
  if(arr->count == *cap)
  {
    U64 new_cap = (*cap)*2 + 64;
    MD_HighlightRun *bigger = push_array(arena, MD_HighlightRun, new_cap);
    MemoryCopy(bigger, arr->v, sizeof(MD_HighlightRun)*arr->count);
    arr->v = bigger;
    *cap = new_cap;
  }
  arr->v[arr->count++] = (MD_HighlightRun){start, end, syntax};
}

internal U64
md_scan_string(String8 s, U64 i, U8 quote)
{
  U64 k = i+1;
  for(;k < s.size; k += 1)
  {
    if(s.str[k] == '\\') { k += 1; continue; }
    if(s.str[k] == quote) { return k+1; }
    if(s.str[k] == '\n' && quote != '`') { return k; }
  }
  return s.size;
}

internal U64
md_scan_number(String8 s, U64 i)
{
  U64 k = i;
  if(k+1 < s.size && s.str[k] == '0' && (s.str[k+1] == 'x' || s.str[k+1] == 'X' || s.str[k+1] == 'b' || s.str[k+1] == 'o')) { k += 2; }
  for(;k < s.size && (md_ident_char(s.str[k]) || s.str[k] == '.'); k += 1)
  {
    if(s.str[k] == '.' && !(k+1 < s.size && char_is_digit(s.str[k+1], 10))) { break; }
  }
  return k;
}

internal MD_HighlightArray
md_highlights_lines(Arena *arena, MD_Language lang, String8 code)
{
  // line-oriented languages: diff, yaml keys, markdown (no highlights)
  MD_HighlightArray result = {0};
  U64 cap = 0;
  for(U64 i = 0; i < code.size;)
  {
    U64 nl = str8_find_needle(code, i, str8_lit("\n"), 0);
    String8 line = str8_substr(code, r1u64(i, nl));
    if(lang == MD_Language_Diff && line.size)
    {
      TH_Syntax syn = TH_Syntax_Plain;
      if(line.str[0] == '+') { syn = TH_Syntax_DiffPlus; }
      else if(line.str[0] == '-') { syn = TH_Syntax_DiffMinus; }
      else if(line.str[0] == '@') { syn = TH_Syntax_Function; }
      md_highlight_push(arena, &result, &cap, i, nl, syn);
    }
    i = nl+1;
  }
  return result;
}

internal MD_HighlightArray
md_highlights_from_code(Arena *arena, MD_Language lang, String8 code)
{
  MD_HighlightArray result = {0};
  U64 cap = 0;
  MD_LexerSpec *spec = 0;
  switch(lang)
  {
    default: return result;
    case MD_Language_Diff:
    case MD_Language_Markdown: return md_highlights_lines(arena, lang, code);
    case MD_Language_C:          spec = &md_lexer_c; break;
    case MD_Language_Cpp:        spec = &md_lexer_cpp; break;
    case MD_Language_Rust:       spec = &md_lexer_rust; break;
    case MD_Language_Go:         spec = &md_lexer_go; break;
    case MD_Language_JavaScript: spec = &md_lexer_js; break;
    case MD_Language_TypeScript: spec = &md_lexer_ts; break;
    case MD_Language_Python:     spec = &md_lexer_python; break;
    case MD_Language_Shell:      spec = &md_lexer_shell; break;
    case MD_Language_Css:        spec = &md_lexer_css; break;
    case MD_Language_Json:       spec = &md_lexer_json; break;
    case MD_Language_Yaml:       spec = &md_lexer_yaml; break;
  }
  String8 s = code;
  B32 line_start = 1;
  U8 prev_sig = 0;               // previous non-space character
  for(U64 i = 0; i < s.size;)
  {
    U8 c = s.str[i];
    if(c == '\n') { line_start = 1; i += 1; continue; }
    if(c == ' ' || c == '\t' || c == '\r') { i += 1; continue; }

    //- comments
    if((spec->line_comment && str8_match(str8_prefix(str8_skip(s, i), 2), str8_cstring(spec->line_comment), 0)) ||
       (spec->hash_comments && c == '#' && (lang != MD_Language_Shell || i == 0 || char_is_space(s.str[i-1]))))
    {
      U64 e = str8_find_needle(s, i, str8_lit("\n"), 0);
      md_highlight_push(arena, &result, &cap, i, e, TH_Syntax_Comment);
      i = e;
      continue;
    }
    if(spec->block_comments && c == '/' && i+1 < s.size && s.str[i+1] == '*')
    {
      U64 e = str8_find_needle(s, i+2, str8_lit("*/"), 0);
      e = (e < s.size) ? e+2 : s.size;
      md_highlight_push(arena, &result, &cap, i, e, TH_Syntax_Comment);
      i = e;
      continue;
    }

    //- preprocessor directives
    if(spec->preproc && c == '#' && line_start)
    {
      U64 e = i+1;
      for(;e < s.size && (s.str[e] == ' '); e += 1);
      for(;e < s.size && md_ident_char(s.str[e]); e += 1);
      md_highlight_push(arena, &result, &cap, i, e, TH_Syntax_Preproc);
      String8 directive = str8_skip_chop_whitespace(str8_substr(s, r1u64(i+1, e)));
      i = e;
      if(str8_match(directive, str8_lit("include"), 0))
      {
        for(;i < s.size && s.str[i] == ' '; i += 1);
        if(i < s.size && s.str[i] == '<')
        {
          U64 close = str8_find_needle(s, i, str8_lit(">"), 0);
          U64 nl = str8_find_needle(s, i, str8_lit("\n"), 0);
          if(close < nl) { md_highlight_push(arena, &result, &cap, i, close+1, TH_Syntax_String); i = close+1; }
        }
      }
      line_start = 0;
      continue;
    }
    line_start = 0;

    //- rust attributes and lifetimes
    if(lang == MD_Language_Rust && c == '#' && i+1 < s.size && (s.str[i+1] == '[' || s.str[i+1] == '!'))
    {
      U64 close = str8_find_needle(s, i, str8_lit("]"), 0);
      close = (close < s.size) ? close+1 : s.size;
      md_highlight_push(arena, &result, &cap, i, close, TH_Syntax_Attribute);
      i = close;
      continue;
    }
    if(spec->rust_lifetimes && c == '\'' && i+2 < s.size && md_ident_char(s.str[i+1]) && s.str[i+2] != '\'')
    {
      U64 e = i+1;
      for(;e < s.size && md_ident_char(s.str[e]); e += 1);
      md_highlight_push(arena, &result, &cap, i, e, TH_Syntax_Label);
      i = e;
      prev_sig = 'a';
      continue;
    }

    //- strings
    if(c == '"' || (c == '\'' && (spec->single_quote_strings || lang == MD_Language_Rust)) || (c == '`' && spec->backtick_strings))
    {
      U64 e = md_scan_string(s, i, c);
      TH_Syntax syn = TH_Syntax_String;
      // JSON / YAML keys are properties
      if(lang == MD_Language_Json || lang == MD_Language_Yaml)
      {
        U64 k = e;
        for(;k < s.size && (s.str[k] == ' ' || s.str[k] == '\t'); k += 1);
        if(k < s.size && s.str[k] == ':') { syn = TH_Syntax_Property; }
      }
      // python triple-quoted strings
      if(lang == MD_Language_Python && i+2 < s.size && s.str[i+1] == c && s.str[i+2] == c)
      {
        String8 triple = str8_substr(s, r1u64(i, i+3));
        U64 close = str8_find_needle(s, i+3, triple, 0);
        e = (close < s.size) ? close+3 : s.size;
      }
      md_highlight_push(arena, &result, &cap, i, e, syn);
      i = e;
      prev_sig = '"';
      continue;
    }

    //- shell variables
    if(lang == MD_Language_Shell && c == '$')
    {
      U64 e = i+1;
      if(e < s.size && s.str[e] == '{') { U64 close = str8_find_needle(s, e, str8_lit("}"), 0); e = (close < s.size) ? close+1 : s.size; }
      else { for(;e < s.size && md_ident_char(s.str[e]); e += 1); }
      md_highlight_push(arena, &result, &cap, i, e, TH_Syntax_Property);
      i = e;
      prev_sig = 'a';
      continue;
    }

    //- numbers
    if(char_is_digit(c, 10) || (c == '.' && i+1 < s.size && char_is_digit(s.str[i+1], 10) && !md_ident_char(prev_sig)))
    {
      U64 e = md_scan_number(s, i);
      md_highlight_push(arena, &result, &cap, i, e, TH_Syntax_Number);
      i = e;
      prev_sig = '0';
      continue;
    }

    //- identifiers
    if(md_ident_char(c) || (lang == MD_Language_Css && (c == '-' || c == '@')))
    {
      U64 e = i+1;
      for(;e < s.size && (md_ident_char(s.str[e]) || (lang == MD_Language_Css || lang == MD_Language_Yaml || lang == MD_Language_Shell) && s.str[e] == '-'); e += 1);
      String8 word = str8_substr(s, r1u64(i, e));
      U64 k = e;
      for(;k < s.size && (s.str[k] == ' ' || s.str[k] == '\t'); k += 1);
      U8 next = (k < s.size) ? s.str[k] : 0;
      TH_Syntax syn = spec->variables ? TH_Syntax_Variable : TH_Syntax_Plain;
      if(lang == MD_Language_Css)
      {
        if(word.str[0] == '@') { syn = TH_Syntax_Keyword; }
        else if(next == ':' && prev_sig != ':') { syn = TH_Syntax_Property; }
        else if(prev_sig == '.' || prev_sig == '#') { syn = TH_Syntax_Attribute; }
        else if(prev_sig == ':' ) { syn = TH_Syntax_Plain; }
        else { syn = TH_Syntax_Tag; }
      }
      else if(lang == MD_Language_Yaml)
      {
        if(next == ':' && (k+1 >= s.size || char_is_space(s.str[k+1]))) { syn = TH_Syntax_Property; }
        else if(md_word_in_list(word, spec->constants, 0)) { syn = TH_Syntax_Boolean; }
      }
      else if(lang == MD_Language_Shell)
      {
        if(md_word_in_list(word, spec->keywords, 0)) { syn = TH_Syntax_Keyword; }
        else if(prev_sig == 0 || prev_sig == '|' || prev_sig == ';' || prev_sig == '&' || prev_sig == '\n' || prev_sig == '(')
        {
          syn = TH_Syntax_Function;
        }
        else if(word.str[0] == '-') { syn = TH_Syntax_Plain; }
      }
      else if(md_word_in_list(word, spec->keywords, 0)) { syn = TH_Syntax_Keyword; }
      else if(md_word_in_list(word, spec->constants, 0))
      {
        syn = (str8_match(word, str8_lit("true"), 0) || str8_match(word, str8_lit("false"), 0) ||
               str8_match(word, str8_lit("True"), 0) || str8_match(word, str8_lit("False"), 0)) ? TH_Syntax_Boolean : TH_Syntax_Constant;
      }
      else if(str8_match(word, str8_lit("self"), 0) || str8_match(word, str8_lit("this"), 0)) { syn = TH_Syntax_VariableSpecial; }
      else if(md_word_in_list(word, spec->types, 0)) { syn = TH_Syntax_Type; }
      else if(lang == MD_Language_Rust && next == '!') { syn = TH_Syntax_Function; }
      else if(next == '(') { syn = TH_Syntax_Function; }
      else if(prev_sig == '.' && lang != MD_Language_Json) { syn = TH_Syntax_Property; }
      else
      {
        B32 all_caps = (word.size > 1);
        B32 has_alpha = 0;
        for EachIndex(j, word.size)
        {
          if(char_is_lower(word.str[j])) { all_caps = 0; }
          if(char_is_alpha(word.str[j])) { has_alpha = 1; }
        }
        if(all_caps && has_alpha) { syn = TH_Syntax_Constant; }
        else if(spec->capitalized_types && char_is_upper(word.str[0])) { syn = TH_Syntax_Type; }
      }
      md_highlight_push(arena, &result, &cap, i, e, syn);
      i = e;
      prev_sig = 'a';
      continue;
    }

    //- punctuation and operators
    {
      TH_Syntax syn = TH_Syntax_Operator;
      if(c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}') { syn = TH_Syntax_PunctuationBracket; }
      else if(c == ',' || c == ';' || c == '.' || c == ':') { syn = TH_Syntax_PunctuationBracket; }
      if(lang == MD_Language_Json || lang == MD_Language_Yaml || lang == MD_Language_Css)
      {
        syn = (syn == TH_Syntax_Operator && lang != MD_Language_Css) ? TH_Syntax_Plain : TH_Syntax_Punctuation;
      }
      if(lang == MD_Language_Shell) { syn = (c == '|' || c == '&' || c == '>' || c == '<' || c == '=') ? TH_Syntax_Operator : TH_Syntax_Plain; }
      md_highlight_push(arena, &result, &cap, i, i+1, syn);
      prev_sig = c;
      i += 1;
    }
  }
  return result;
}
