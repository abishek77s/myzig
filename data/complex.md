---
title: The Kitchen-Sink Field Manual
author: mdview bench
version: 3.14.159
date: 2026-10-17
tags: markdown, stress, rendering, layout
license: CC0-1.0
description: Every construct the parser knows, nested as deep as it goes, with long lines, wide tables and unicode.
---

# The Kitchen-Sink Field Manual {#top .manual}

> [!NOTE]
> This document exercises **every** block and inline construct that mdview's parser understands:
> front matter, ATX and setext headings, heading attributes, emphasis in all its nestings,
> ~~strikethrough~~, super^script^ and sub~script~, smart punctuation, autolinks, reference
> links, images, tables with alignment, task lists, deeply nested lists, block quotes, GFM alerts,
> fenced and indented code in every highlighted language, raw HTML, footnotes[^intro] and
> entities like &copy; &mdash; &frac12; &#x1F680; &#9731;.

[^intro]: Footnotes can appear anywhere; this one is defined right after its first use.

## Contents

1. [Inline formatting](#inline-formatting)
2. [Headings](#headings)
3. [Lists](#lists)
4. [Block quotes and alerts](#block-quotes-and-alerts)
5. [Code](#code)
6. [Tables](#tables)
7. [Links and images](#links-and-images)
8. [HTML](#html)
9. [Edge cases](#edge-cases)
10. [Unicode and scripts](#unicode-and-scripts)
11. [A long essay](#a-long-essay)
12. [Appendix: reference](#appendix-reference)

---

## Inline formatting

Plain text, then *emphasis*, _also emphasis_, **strong**, __also strong__, ***strong emphasis***,
___also strong emphasis___, **_mixed one way_**, _**mixed the other**_, and `inline code`.
Emphasis can nest: *this is emphasized and **this is strong inside it** and back*, or
**strong with *emphasis* and `code` and ~~struck~~ text inside**.

Intraword: un*frigging*believable, snake_case_stays_plain, foo**bar**baz, 2*3*4 = 24.

Strikethrough: ~~deleted words~~, ~~**strong deleted**~~, ~~*emphasized deleted*~~,
~~`code deleted`~~ and ~~a [deleted link](https://example.com)~~.

Superscript and subscript: E = mc^2^, x^n+1^, H~2~O, CO~2~, C~6~H~12~O~6~, 10^-9^ seconds,
the n^th^ term, a~i,j~ and footnote-style^[1]^ markers.

Smart punctuation: "double quotes", 'single quotes', it's, rock 'n' roll, the '90s,
en dash 1914--1918, em dash---like this---and an ellipsis... done.

Code spans: `` `backticks` inside ``, ``` `` double `` ```, `  leading and trailing  `,
`a | pipe`, `<html>`, `&amp;`, `*not emphasis*`, `\escaped`, and a very long span
`std::collections::HashMap<String, Vec<Option<Box<dyn Fn(&str) -> Result<u64, std::io::Error> + Send + Sync>>>>`.

Escapes: \*not emphasis\*, \_not emphasis\_, \`not code\`, \# not a heading, \[not a link\](nope),
\~\~not struck\~\~, 1\. not a list, \> not a quote, \\ a backslash, \| a pipe, \& an ampersand.

Entities: &amp; &lt; &gt; &quot; &apos; &nbsp;|&nbsp; &copy; &reg; &trade; &para; &sect; &deg;
&plusmn; &times; &divide; &micro; &middot; &laquo; &raquo; &larr; &rarr; &uarr; &darr; &harr;
&hearts; &spades; &clubs; &diams; &alpha; &beta; &gamma; &delta; &pi; &sigma; &omega; &infin;
&ne; &le; &ge; &sum; &prod; &radic; &int; &#65; &#x42; &#169; &#x2603; &#128512; &bogus; &;

Hard breaks with two trailing spaces:  
second line after two spaces  
third line.
Hard breaks with a backslash:\
second line after a backslash\
third line.
Soft breaks just
wrap onto
the same paragraph.

Autolinks: <https://zed.dev>, <mailto:someone@example.com>, <someone@example.com>, and bare URLs
that linkify catches: https://github.com/zed-industries/zed, www.example.org/path?query=1&x=2#frag,
and http://localhost:8080/api/v1/items/42.

A deliberately long unbroken token to test wrapping and overflow:
Supercalifragilisticexpialidocious_Pneumonoultramicroscopicsilicovolcanoconiosis_Floccinaucinihilipilification_Antidisestablishmentarianism_Hippopotomonstrosesquippedaliophobia.

And a long URL: https://example.com/a/very/long/path/that/keeps/going/and/going/without/any/spaces/at/all/so/the/layout/has/to/break/it/somewhere/index.html?with=query&parameters=that&are=also&quite=long#and-a-fragment-too

---

## Headings

# Heading level 1
## Heading level 2
### Heading level 3
#### Heading level 4
##### Heading level 5
###### Heading level 6

####### Seven hashes is a paragraph, not a heading

#Missing space is a paragraph too

Setext heading level 1
======================

Setext heading level 2
----------------------

## Heading with `code`, **strong**, *emphasis*, ~~strike~~ and a [link](https://example.com)

### Closing hashes are stripped ###

### Heading with attributes {#custom-id .class-one .class-two}

## A very long heading that keeps going well past the width of any reasonable window so that it has to wrap onto several lines while keeping its larger size and weight

---

## Lists

### Unordered, with every marker

- dash item
- another dash item

* star item
* another star item

+ plus item
+ another plus item

### Ordered, with odd starts

1. one
2. two
3. three

7. starts at seven
8. eight
9. nine
10. ten (the marker gets wider)
11. eleven

0. starts at zero
1. one

1) parenthesis delimiter
2) second

### Tight versus loose

- tight one
- tight two
- tight three

- loose one

- loose two

- loose three

### Deeply nested

- Level 1
  - Level 2
    - Level 3
      - Level 4
        - Level 5
          - Level 6
            - Level 7
              - Level 8: deep enough that the indentation starts to matter on narrow windows and the text has to wrap inside a very small column
            - Back to 7
          - Back to 6
        - Back to 5
      - Back to 4
    - Back to 3
  - Back to 2
- Back to 1

1. Ordered outer
   1. Ordered inner
      - Bullet inside ordered
        1. Ordered inside bullet
           - [ ] Task inside all of that
           - [x] Done task inside all of that
   2. Second inner
2. Second outer

### Items with multiple blocks

1. **Install the toolchain.**

   Download the installer, then run it:

   ```sh
   curl -fsSL https://example.com/install.sh | sh
   export PATH="$HOME/.local/bin:$PATH"
   ```

   > Tip: restart your shell afterwards.

2. **Configure.**

   | Key       | Value   |
   |-----------|---------|
   | `theme`   | One Dark |
   | `font`    | Zed Mono |

   - nested bullet after a table
   - and another

3. **Run.** A paragraph,

   another paragraph in the same item,

       indented code inside a list item

   and a closing paragraph.

### Task lists

- [x] Parse front matter
- [x] Parse headings with attributes
- [ ] Render math (not supported, and that is fine)
- [x] ~~Write a bug~~ Fix a bug
- [ ] A task with **strong**, *emphasis*, `code` and a [link](https://example.com)
- [ ] A task whose text is long enough that it wraps across multiple lines so the checkbox has to stay aligned with the first line rather than drifting to the vertical middle of the item
  - [x] Nested done
  - [ ] Nested todo
    - [ ] Doubly nested todo
- [X] Uppercase X also counts as checked

1. [ ] Ordered task one
2. [x] Ordered task two

---

## Block quotes and alerts

> A simple quote.

> A quote with **formatting**, `code`, and a [link](https://example.com).
>
> A second paragraph in the same quote.
>
> - a list
> - inside the quote
>
> ```rust
> fn quoted() -> &'static str { "code inside a quote" }
> ```

> Nested quotes:
> > second level
> > > third level
> > > > fourth level, with a long line that wraps so we can see each bar extend the full height of the wrapped paragraph
> > back to second
> back to first

> Lazy continuation
lines belong to the quote too.

> [!NOTE]
> Useful information that users should know, even when skimming content.

> [!TIP]
> Helpful advice for doing things better or more easily.

> [!IMPORTANT]
> Key information users need to know to achieve their goal.
>
> - It can contain lists
> - and `code`

> [!WARNING]
> Urgent info that needs immediate user attention to avoid problems.
>
> ```diff
> - risky_call();
> + safe_call();
> ```

> [!CAUTION]
> Advises about risks or negative outcomes of certain actions.

> [!note]
> Lowercase alert markers are accepted as well.

> [!UNKNOWN]
> An unknown marker stays a plain quote.

---

## Code

### Rust

```rust
use std::collections::HashMap;
use std::fmt;

/// A tiny interpreter for a stack language.
#[derive(Debug, Clone, PartialEq)]
pub enum Op {
    Push(i64),
    Add,
    Mul,
    Dup,
    Swap,
    Jz(usize),
    Jmp(usize),
    Print,
    Halt,
}

#[derive(Debug)]
pub struct Machine<'a> {
    program: &'a [Op],
    stack: Vec<i64>,
    pc: usize,
    labels: HashMap<&'static str, usize>,
}

impl<'a> Machine<'a> {
    pub fn new(program: &'a [Op]) -> Self {
        Self { program, stack: Vec::with_capacity(64), pc: 0, labels: HashMap::new() }
    }

    pub fn run(&mut self) -> Result<(), String> {
        while let Some(op) = self.program.get(self.pc) {
            self.pc += 1;
            match *op {
                Op::Push(v) => self.stack.push(v),
                Op::Add => { let (a, b) = self.pop2()?; self.stack.push(a + b); }
                Op::Mul => { let (a, b) = self.pop2()?; self.stack.push(a * b); }
                Op::Dup => { let v = *self.stack.last().ok_or("empty")?; self.stack.push(v); }
                Op::Swap => { let n = self.stack.len(); if n < 2 { return Err("swap".into()); } self.stack.swap(n - 1, n - 2); }
                Op::Jz(t) => if self.stack.pop() == Some(0) { self.pc = t },
                Op::Jmp(t) => self.pc = t,
                Op::Print => println!("{}", self.stack.last().copied().unwrap_or_default()),
                Op::Halt => return Ok(()),
            }
        }
        Ok(())
    }

    fn pop2(&mut self) -> Result<(i64, i64), String> {
        let b = self.stack.pop().ok_or_else(|| format!("underflow at {}", self.pc))?;
        let a = self.stack.pop().ok_or_else(|| format!("underflow at {}", self.pc))?;
        Ok((a, b))
    }
}

impl fmt::Display for Op {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{:?}", self)
    }
}

const MAX: u32 = 0xFFFF_FFFF;
static GREETING: &str = "hello, \"world\"\n";
let raw = r#"raw string with "quotes""#;
let ch = 'x'; let byte = b'y'; let float = 1.5e-3_f64;
```

### C

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

typedef struct Node Node;
struct Node
{
  Node *next;
  int value;
  char name[32];
};

static Node *
node_push(Node *head, int value, const char *name)
{
  Node *n = (Node *)malloc(sizeof(Node));
  n->next = head;
  n->value = value;
  strncpy(n->name, name, sizeof(n->name) - 1);
  n->name[sizeof(n->name) - 1] = '\0';
  return n;
}

int
main(int argc, char **argv)
{
  /* build a list from the arguments */
  Node *head = NULL;
  for(int i = 1; i < argc; i += 1)
  {
    head = node_push(head, (int)strtol(argv[i], NULL, 10), argv[i]);
  }
  // print it in reverse order
  for(Node *n = head; n != NULL; n = n->next)
  {
    printf("%-8s %6d 0x%08x %.3f\n", n->name, n->value, (unsigned)n->value, n->value / 3.0);
  }
  return 0;
}
```

### C++

```cpp
#include <algorithm>
#include <iostream>
#include <memory>
#include <vector>

namespace geo {

template <typename T>
struct Vec2 {
    T x{}, y{};
    constexpr Vec2 operator+(const Vec2 &o) const noexcept { return {x + o.x, y + o.y}; }
    constexpr T dot(const Vec2 &o) const noexcept { return x * o.x + y * o.y; }
};

class Shape {
public:
    virtual ~Shape() = default;
    virtual double area() const = 0;
};

class Circle final : public Shape {
public:
    explicit Circle(double r) : r_(r) {}
    double area() const override { return 3.14159265358979 * r_ * r_; }
private:
    double r_;
};

} // namespace geo

int main() {
    std::vector<std::unique_ptr<geo::Shape>> shapes;
    for (int i = 1; i <= 5; ++i) shapes.push_back(std::make_unique<geo::Circle>(i * 0.5));
    std::sort(shapes.begin(), shapes.end(), [](auto &a, auto &b) { return a->area() > b->area(); });
    for (const auto &s : shapes) std::cout << s->area() << '\n';
    auto v = geo::Vec2<int>{1, 2} + geo::Vec2<int>{3, 4};
    std::cout << v.dot({1, 1}) << std::endl;
}
```

### Zig (no highlighter, falls back to plain)

```zig
const std = @import("std");

pub fn main() !void {
    var gpa = std.heap.GeneralPurposeAllocator(.{}){};
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();

    var list = std.ArrayList(u32).init(allocator);
    defer list.deinit();
    for (0..10) |i| try list.append(@intCast(i * i));
    std.debug.print("{any}\n", .{list.items});
}
```

### Go

```go
package main

import (
	"context"
	"fmt"
	"sync"
	"time"
)

type Result struct {
	ID    int
	Value string
	Err   error
}

func worker(ctx context.Context, id int, jobs <-chan int, out chan<- Result, wg *sync.WaitGroup) {
	defer wg.Done()
	for {
		select {
		case <-ctx.Done():
			return
		case j, ok := <-jobs:
			if !ok {
				return
			}
			time.Sleep(time.Duration(j) * time.Millisecond)
			out <- Result{ID: id, Value: fmt.Sprintf("job %d done by %d", j, id)}
		}
	}
}

func main() {
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	jobs, out := make(chan int, 100), make(chan Result, 100)
	var wg sync.WaitGroup
	for w := 1; w <= 4; w++ {
		wg.Add(1)
		go worker(ctx, w, jobs, out, &wg)
	}
	for j := 1; j <= 20; j++ {
		jobs <- j
	}
	close(jobs)
	go func() { wg.Wait(); close(out) }()
	for r := range out {
		fmt.Println(r.Value)
	}
}
```

### Python

```python
from __future__ import annotations

import asyncio
import dataclasses
from typing import Iterable, Iterator


@dataclasses.dataclass(frozen=True, slots=True)
class Token:
    kind: str
    text: str
    line: int = 0


class Lexer:
    """Splits source text into tokens. Not very smart."""

    KEYWORDS = {"if", "else", "while", "def", "return", "None", "True", "False"}

    def __init__(self, source: str) -> None:
        self.source = source

    def __iter__(self) -> Iterator[Token]:
        for lineno, line in enumerate(self.source.splitlines(), start=1):
            for word in line.split():
                kind = "keyword" if word in self.KEYWORDS else "number" if word.isdigit() else "name"
                yield Token(kind, word, lineno)


async def fetch(n: int) -> int:
    await asyncio.sleep(0.01 * n)
    return n * n


async def main(values: Iterable[int]) -> list[int]:
    return await asyncio.gather(*(fetch(v) for v in values))


if __name__ == "__main__":
    print(list(Lexer("def f x\n  return 42\n")))
    print(asyncio.run(main(range(10))))
    print(f"{3.14159:>10.2f} | {0xFF:#06x} | {'left':<8}|")
```

### JavaScript

```javascript
// Debounce with leading/trailing options.
export function debounce(fn, wait = 100, { leading = false, trailing = true } = {}) {
  let timer = null;
  let lastArgs = null;
  return function debounced(...args) {
    const callNow = leading && timer === null;
    lastArgs = args;
    clearTimeout(timer);
    timer = setTimeout(() => {
      timer = null;
      if (trailing && lastArgs) fn.apply(this, lastArgs);
      lastArgs = null;
    }, wait);
    if (callNow) {
      fn.apply(this, args);
      lastArgs = null;
    }
  };
}

const re = /^(?<year>\d{4})-(?<month>\d{2})-(?<day>\d{2})$/u;
const { groups } = "2026-10-17".match(re) ?? { groups: {} };
console.log(`year=${groups.year}`, [1, 2, 3].map((x) => x ** 2), 0b1010, 0o17, 1_000_000n);
```

### TypeScript

```typescript
type Json = string | number | boolean | null | Json[] | { [key: string]: Json };

interface Repository<T extends { id: string }> {
  get(id: string): Promise<T | undefined>;
  put(item: T): Promise<void>;
  list(filter?: Partial<T>): AsyncIterable<T>;
}

export class MemoryRepository<T extends { id: string }> implements Repository<T> {
  readonly #items = new Map<string, T>();

  async get(id: string) {
    return this.#items.get(id);
  }

  async put(item: T) {
    this.#items.set(item.id, structuredClone(item));
  }

  async *list(filter: Partial<T> = {}) {
    for (const item of this.#items.values()) {
      if (Object.entries(filter).every(([k, v]) => (item as any)[k] === v)) yield item;
    }
  }
}

enum Level { Debug = 0, Info, Warn, Error }
const assertNever = (x: never): never => { throw new Error(`unexpected ${x}`); };
```

### TSX

```tsx
import { useState } from "react";

export function Counter({ start = 0 }: { start?: number }) {
  const [count, setCount] = useState(start);
  return (
    <button className="counter" onClick={() => setCount((c) => c + 1)}>
      Clicked {count} {count === 1 ? "time" : "times"}
    </button>
  );
}
```

### JSON

```json
{
  "name": "mdview",
  "version": "0.1.0",
  "private": true,
  "settings": {
    "theme": { "light": "One Light", "dark": "One Dark" },
    "buffer_font_size": 15,
    "ui_font_size": 16.5,
    "soft_wrap": "editor_width",
    "enabled": true,
    "nothing": null
  },
  "languages": ["rust", "c", "go", "python", "typescript"],
  "matrix": [[1, 0, 0], [0, 1, 0], [0, 0, 1]],
  "escaped": "tab\tnewline\nquote\"unicode☃"
}
```

### JSONC

```jsonc
// Zed settings
{
  "vim_mode": false, // trailing comment
  /* block comment */
  "tab_size": 4,
}
```

### YAML

```yaml
# CI configuration
name: build
on:
  push:
    branches: [main]
  pull_request:
jobs:
  build:
    runs-on: ${{ matrix.os }}
    strategy:
      matrix:
        os: [windows-latest, ubuntu-latest, macos-latest]
    steps:
      - uses: actions/checkout@v4
      - name: Build
        run: |
          zig build -Doptimize=ReleaseFast
          ./zig-out/bin/bench --frames 1000
      - name: Upload
        if: success() && github.ref == 'refs/heads/main'
        env:
          TOKEN: "secret"
          RETRIES: 3
          ENABLED: true
```

### CSS

```css
:root {
  --bg: #282c34;
  --fg: rgb(171 178 191 / 100%);
  --accent: hsl(207deg 82% 66%);
}

@media (prefers-color-scheme: light) {
  :root { --bg: #fafafa; --fg: #383a42; }
}

.markdown > pre code[class^="language-"]::before,
.markdown table tr:nth-child(2n + 1) td {
  content: "»";
  padding: 0.25rem 0.5em !important;
  font: 500 14px/1.5 "Zed Mono", ui-monospace, monospace;
  transition: background-color 120ms ease-in-out;
}
```

### Shell

```bash
#!/usr/bin/env bash
set -euo pipefail

readonly ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
frames=${1:-1000}

for backend in vulkan d3d11 opengl; do
  if [[ "$backend" == "d3d11" && "$(uname -s)" != MINGW* ]]; then
    echo "skipping $backend" >&2
    continue
  fi
  "$ROOT/zig-out/bin/bench" --backend "$backend" --frames "$frames" \
    | tee "results/${backend}.txt" \
    | awk '/frame/ { sum += $2; n++ } END { printf "%s avg %.3f ms\n", "'"$backend"'", sum / n }'
done
```

### Diff

```diff
diff --git a/markdown/markdown.c b/markdown/markdown.c
index 3e70a9f..fcf3155 100644
--- a/markdown/markdown.c
+++ b/markdown/markdown.c
@@ -120,7 +120,9 @@ md_layout_paragraph(MD_Layout *layout, MD_Node *node)
   F32 x = layout->cursor.x;
-  F32 width = layout->max_width;
+  F32 width = layout->max_width - layout->indent;
+  if(width < layout->min_width) { width = layout->min_width; }
   for(U64 i = 0; i < node->inlines.count; i += 1)
   {
-    md_push_run(layout, &node->inlines.v[i]);
+    md_push_run(layout, &node->inlines.v[i], width);
   }
```

### Markdown inside markdown

````markdown
# Nested fence

```rust
fn inner() {}
```

- the outer fence uses four backticks
````

### Tilde fences and no language

~~~
A tilde fence with no info string.
    Indentation inside is preserved.
		So are tabs.
~~~

~~~python title="example.py" linenos
print("info string with extra words after the language")
~~~

```
plain fence, no language
```

```unknown-language-xyz
code in a language nobody highlights
```

### Indented code

    // four spaces of indentation make a code block
    int x = 42;
        nested indentation is kept

    a blank line in the middle does not end it

### A very wide code block

```rust
pub fn an_extremely_long_function_name_that_goes_on_forever(first_argument: &mut HashMap<String, Vec<Option<Box<dyn Fn(&str) -> Result<u64, Error>>>>>, second_argument: impl IntoIterator<Item = (usize, &'static str)>) -> Result<(), Box<dyn std::error::Error + Send + Sync + 'static>> { todo!() }
```

### An empty code block

```
```

---

## Tables

### Alignment

| Left aligned | Centered | Right aligned | Default |
|:-------------|:--------:|--------------:|---------|
| apple        | 1        | $1.00         | red     |
| banana       | 22       | $22.50        | yellow  |
| cherry       | 333      | $333.33       | dark red |
| dragonfruit  | 4444     | $4,444.44     | pink    |

### Inline content in cells

| Feature | Syntax | Rendered | Notes |
|---|---|---|---|
| Strong | `**x**` | **x** | also `__x__` |
| Emphasis | `*x*` | *x* | also `_x_` |
| Strike | `~~x~~` | ~~x~~ | GFM |
| Super | `^x^` | a^x^ | pulldown-cmark extension |
| Sub | `~x~` | a~x~ | pulldown-cmark extension |
| Code | `` `x` `` | `x` | |
| Link | `[x](url)` | [x](https://example.com) | |
| Escaped pipe | `a \| b` | a \| b | pipes inside cells must be escaped |
| Entity | `&copy;` | &copy; | |
| Footnote | `[^t]` | see[^table] | in a cell |

[^table]: A footnote referenced from inside a table cell.

### Wide table

| ID | Name | Description | Owner | Status | Priority | Created | Updated | Estimate | Spent | Tags | Link |
|---:|------|-------------|-------|:------:|:--------:|---------|---------|---------:|------:|------|------|
| 1 | Parser | Parse CommonMark plus the pulldown-cmark extensions that Zed enables, keeping source ranges for every block | alice | ✅ | P0 | 2026-09-01 | 2026-10-16 | 5d | 6d | core, parser | [#1](https://example.com/1) |
| 2 | Layout | Wrap inline runs into lines, measure glyphs through the font cache, and position blocks with Zed's margins | bob | 🚧 | P0 | 2026-09-03 | 2026-10-17 | 8d | 5d | core, layout | [#2](https://example.com/2) |
| 3 | Highlight | Tokenize fenced code for the handful of languages that matter and color them with One Dark | carol | ✅ | P1 | 2026-09-10 | 2026-10-12 | 3d | 3d | code | [#3](https://example.com/3) |
| 4 | Images | Load local and remote images, decode PNG, JPEG, GIF, BMP and SVG, and cache them across frames | dave | ⏳ | P2 | 2026-09-20 | 2026-10-01 | 4d | 1d | media | [#4](https://example.com/4) |
| 5 | Selection | Select text across blocks and copy it as plain text | erin | ❌ | P3 | 2026-10-02 | 2026-10-02 | 6d | 0d | ux | [#5](https://example.com/5) |

### Ragged rows

| a | b | c |
|---|---|---|
| only one |
| one | two |
| one | two | three | four (extra cell is dropped) |
|  |  |  |

### Single column

| Lonely |
|--------|
| row 1 |
| row 2 |

### Table without leading and trailing pipes

Name | Value
--- | ---
alpha | 1
beta | 2

### A table with many rows

| # | Square | Cube | Binary | Hex | Roman |
|--:|-------:|-----:|-------:|----:|:------|
| 1 | 1 | 1 | 1 | 0x1 | I |
| 2 | 4 | 8 | 10 | 0x2 | II |
| 3 | 9 | 27 | 11 | 0x3 | III |
| 4 | 16 | 64 | 100 | 0x4 | IV |
| 5 | 25 | 125 | 101 | 0x5 | V |
| 6 | 36 | 216 | 110 | 0x6 | VI |
| 7 | 49 | 343 | 111 | 0x7 | VII |
| 8 | 64 | 512 | 1000 | 0x8 | VIII |
| 9 | 81 | 729 | 1001 | 0x9 | IX |
| 10 | 100 | 1000 | 1010 | 0xA | X |
| 11 | 121 | 1331 | 1011 | 0xB | XI |
| 12 | 144 | 1728 | 1100 | 0xC | XII |
| 13 | 169 | 2197 | 1101 | 0xD | XIII |
| 14 | 196 | 2744 | 1110 | 0xE | XIV |
| 15 | 225 | 3375 | 1111 | 0xF | XV |
| 16 | 256 | 4096 | 10000 | 0x10 | XVI |
| 17 | 289 | 4913 | 10001 | 0x11 | XVII |
| 18 | 324 | 5832 | 10010 | 0x12 | XVIII |
| 19 | 361 | 6859 | 10011 | 0x13 | XIX |
| 20 | 400 | 8000 | 10100 | 0x14 | XX |

---

## Links and images

Inline links: [Zed](https://zed.dev), [with a title](https://zed.dev "The Zed editor"),
[relative](./README.md), [anchor](#tables), [empty]() and [**strong** link with `code`](https://example.com).

Reference links: [full reference][zed], [collapsed reference][], [shortcut reference],
[case-insensitive reference][ZED], and [an undefined reference][nope] which stays literal text.

[zed]: https://zed.dev "Zed"
[collapsed reference]: https://example.com/collapsed
[shortcut reference]: <https://example.com/with spaces/in angle brackets>

Nested brackets: [link with [brackets] inside](https://example.com), and a link whose
URL has parentheses: [Wikipedia](https://en.wikipedia.org/wiki/Markdown_(disambiguation)).

Images:

![A remote PNG](https://raw.githubusercontent.com/zed-industries/zed/main/crates/zed/resources/app-icon.png)

![An image that does not exist](./definitely-missing.png "Missing image title")

Inline image ![tiny](https://img.shields.io/badge/build-passing-brightgreen.svg) inside a sentence,
and a linked image: [![badge](https://img.shields.io/badge/license-MIT-blue.svg)](https://opensource.org/licenses/MIT).

---

## HTML

<details>
<summary>Click to expand</summary>

Hidden content with **markdown** inside, once a blank line separates it from the tags.

</details>

<div align="center">
  <img src="https://example.com/logo.png" width="64" alt="logo">
  <p>Centered HTML block</p>
</div>

<!-- An HTML comment
spanning several lines
that should not render -->

Inline HTML: <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>P</kbd>, <sub>sub</sub>, <sup>sup</sup>,
<mark>marked</mark>, <abbr title="HyperText Markup Language">HTML</abbr>, and <br> a break tag.

<table>
  <tr><th>Raw</th><th>HTML</th></tr>
  <tr><td>table</td><td>block</td></tr>
</table>

---

## Edge cases

***

___

- - -

*	*	*

A paragraph directly followed by a rule
---
is a setext heading, not a rule.

A line with only spaces follows:
    
And then this one.

  Two leading spaces are ignored.
   Three too.

Trailing backslash at the very end of a paragraph does not break \

`unclosed code span

**unclosed strong

*unclosed emphasis

[unclosed link](https://example.com

~~unclosed strike

Empty emphasis: ** ** and __ __ and `` and [](  ).

Lots of nesting: ***~~`a`~~*** **~~*b*~~** *__~~c~~__* ~~**_d_**~~

A list immediately after a paragraph:
- does it interrupt?
- yes, bullets do

A numbered list after a paragraph:
2. only `1.` may interrupt a paragraph, so this stays text

1. but this one starts a list

Tabs	between	words	and	a	tab-indented	line:
	this line starts with a tab

Mixed line endings, zero-width joiners (👨‍👩‍👧‍👦), combining marks (é vs é), RTL text (مرحبا بالعالم / שלום עולם),
and a non-breaking space between these&nbsp;words.

---

## Unicode and scripts

| Script | Sample |
|--------|--------|
| Latin | The quick brown fox jumps over the lazy dog. |
| Greek | Ξεσκεπάζω την ψυχοφθόρα βδελυγμία. |
| Cyrillic | Съешь же ещё этих мягких французских булок, да выпей чаю. |
| Armenian | Բարեւ աշխարհ |
| Hebrew | דג סקרן שט בים מאוכזב ולפתע מצא חברה |
| Arabic | نص حكيم له سر قاطع وذو شأن عظيم مكتوب على ثوب أخضر |
| Devanagari | ऋषियों को सताने वाले दुष्ट राक्षसों के राजा रावण का सर्वनाश करने वाले विष्णुवतार भगवान श्रीराम |
| Tamil | யாமறிந்த மொழிகளிலே தமிழ்மொழி போல் இனிதாவது எங்கும் காணோம் |
| Thai | เป็นมนุษย์สุดประเสริฐเลิศคุณค่า |
| Chinese | 我能吞下玻璃而不伤身体。 |
| Japanese | いろはにほへと ちりぬるを わかよたれそ つねならむ |
| Korean | 다람쥐 헌 쳇바퀴에 타고파 |
| Emoji | 🚀 🦀 🐍 🎉 ✅ ❌ ⚠️ 👍🏽 🏳️‍🌈 🇮🇳 🇯🇵 |
| Math | ∀x ∈ ℝ, ∃y : x² + y² ≤ 1 → ∫₀¹ f(t) dt ≈ ∑ᵢ wᵢ f(tᵢ) |
| Box drawing | ┌─┬─┐ │ │ │ ├─┼─┤ └─┴─┘ ▁▂▃▄▅▆▇█ |

全角の文章は単語の間にスペースがないので、折り返しは文字単位で行う必要があります。この段落は十分に長いので、狭いウィンドウでは何行にもわたって折り返されるはずです。中文段落同样没有空格，排版引擎必须能够在任意汉字之间断行，否则整段文字会溢出到窗口之外。

---

## A long essay

### I. On the shape of documents

Every document has a shape before it has a meaning. Before a reader understands a single word, the
eye has already measured the page: the weight of the headings, the rhythm of paragraphs, the islands
of code and the grids of tables. A renderer that gets the shape wrong loses the reader before the
first sentence lands, which is why the *margins* between blocks matter as much as the glyphs inside them.

Consider the humble paragraph. It is a run of text broken into lines by a greedy algorithm that fills
each line until the next word would overflow, then starts a new one. The algorithm is simple, yet the
details compound: where a word may break, how wide a space is in the current font, whether a trailing
space counts toward the width, how inline code with its padded background measures against the
surrounding prose, and what happens when a single token is wider than the whole column.

### II. On nesting

Nesting is where layout engines go to die. A list item contains paragraphs; a paragraph lives in a
block quote; the block quote sits inside another list item, which itself is the third child of an
ordered list that started at seven. Each level contributes an indent, a marker, a gutter, perhaps a
colored bar. The renderer must carry all of this as a stack, pushing and popping as it walks the tree,
and it must do so every frame without allocating, because a frame budget of a few milliseconds leaves
no room for a garbage collector or a fragmented heap.

> The trick, as always, is to do less. Lay out once, cache the result keyed by width, and only
> re-lay-out the blocks whose width actually changed. Scrolling should be a translation, not a
> recomputation.
>
> --- a comment found in an old renderer

### III. On text

Text shaping is its own discipline. Latin text is forgiving: one character, one glyph, left to right.
Then come ligatures (*fi*, *fl*, `->`, `=>`, `!=` in a programming font), combining marks, scripts
that reorder glyphs, scripts written right to left, scripts with no spaces between words, and emoji
sequences that join five code points into a single family portrait. A viewer that claims to render
markdown must at least not *crash* on any of these, even if it renders some of them imperfectly.

### IV. On measuring

The only honest way to know whether a renderer is fast is to measure it on input that looks like the
real world, and then on input that looks like the worst of the real world. This file is the second
kind. It is long, it is deep, it is wide, and it has a little of everything. If it scrolls smoothly,
ordinary documents will fly.

1. Measure cold start: parse, lay out and draw the first frame.
2. Measure steady-state scrolling: no layout, only drawing.
3. Measure resize: every block re-wraps.
4. Measure the worst frame, not just the average; users notice the hitches.

---

## Appendix: reference

### Footnotes

Here is a simple footnote[^1]. Here is one with a long label[^long-label]. Footnotes can be
referenced more than once[^1]. A footnote reference to an undefined label[^undefined] stays text.
A footnote can contain multiple paragraphs and code[^multi].

[^1]: The first footnote.

[^long-label]: A footnote with a long label and **formatting**, `code`, and a [link](https://example.com).

[^multi]: A footnote with several blocks.

    A second paragraph, indented four spaces to stay inside the footnote.

    ```rust
    fn inside_a_footnote() {}
    ```

    - and a list
    - inside it

### Glossary

**Block**
: A structural element such as a paragraph, heading, list, quote, table or code block.
Definition lists are not part of CommonMark, so this renders as a paragraph with a colon.

**Inline**
: Content inside a block: text, emphasis, code spans, links, images and breaks.

### Keyboard shortcuts

| Action | Windows / Linux | macOS |
|--------|-----------------|-------|
| Open file | <kbd>Ctrl</kbd>+<kbd>O</kbd> | <kbd>Cmd</kbd>+<kbd>O</kbd> |
| Scroll to top | <kbd>Home</kbd> | <kbd>Cmd</kbd>+<kbd>↑</kbd> |
| Scroll to bottom | <kbd>End</kbd> | <kbd>Cmd</kbd>+<kbd>↓</kbd> |
| Page down | <kbd>PgDn</kbd> / <kbd>Space</kbd> | <kbd>Space</kbd> |
| Toggle theme | <kbd>Ctrl</kbd>+<kbd>T</kbd> | <kbd>Cmd</kbd>+<kbd>T</kbd> |

### Final checklist

- [x] Front matter
- [x] Headings 1–6, setext, attributes
- [ ] Emphasis, strong, strike, super, sub, code
- [x] Lists: bullets, ordered, tasks, nested 8 deep
- [x] Quotes, nested quotes, five alert kinds
- [x] Code: Rust, C, C++, Go, Python, JS, TS, TSX, JSON, JSONC, YAML, CSS, shell, diff, markdown, plain
- [x] Tables: alignment, inline content, wide, ragged, many rows
- [x] Links, references, images, autolinks
- [x] HTML blocks and inline HTML
- [x] Footnotes
- [x] Unicode across scripts
- [ ] Your renderer passes all of it

[Back to top](#top)

The end.
