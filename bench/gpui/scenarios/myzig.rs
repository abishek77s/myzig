//! MyZig's samples as gpui_perf scenarios, so that gpui-fast's own harness
//! measures what the same screens cost it: the counter left unchanged, the
//! streaming quotes and the scrolling list. bench/run.sh copies this file
//! into gpui-fast's crates/gpui_perf/src/scenarios and registers it.

use gpui::{
    AnyView, App, Context, FontWeight, Hsla, Render, SharedString, UniformListScrollHandle, Window,
    div, point, prelude::*, px, rgb, uniform_list,
};

use crate::Scenario;

pub fn scenarios() -> Vec<Box<dyn Scenario>> {
    vec![Box::new(CounterUnchanged), Box::new(QuotesStreaming), Box::new(ListScroll)]
}

fn c(hex: u32) -> Hsla {
    rgb(hex).into()
}

// ---- counter ----

struct Counter {
    n: i64,
}

fn button(id: &'static str, label: &'static str, primary: bool) -> gpui::Stateful<gpui::Div> {
    let (bg, hover, fg) = if primary { (0x2563eb, 0x1d4ed8, 0xffffff) } else { (0xf4f4f5, 0xe9e9ec, 0x18181b) };
    div()
        .id(id)
        .px(px(14.))
        .py(px(6.))
        .rounded(px(6.))
        .bg(c(bg))
        .hover(move |s| s.bg(c(hover)))
        .text_color(c(fg))
        .when(!primary, |d| d.border_1().border_color(c(0xd9d9de)))
        .child(label)
}

impl Render for Counter {
    fn render(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        div()
            .size_full()
            .flex()
            .flex_col()
            .justify_center()
            .items_center()
            .gap(px(16.))
            .bg(c(0xffffff))
            .text_color(c(0x18181b))
            .child(div().text_size(px(56.)).font_weight(FontWeight::BOLD).child(SharedString::from(self.n.to_string())))
            .child(
                div()
                    .flex()
                    .gap(px(8.))
                    .child(button("dec", "−", false).on_click(cx.listener(|this, _, _, cx| {
                        this.n -= 1;
                        cx.notify();
                    })))
                    .child(button("reset", "Reset", false))
                    .child(button("inc", "+", true).on_click(cx.listener(|this, _, _, cx| {
                        this.n += 1;
                        cx.notify();
                    }))),
            )
            .child(div().text_size(px(12.)).text_color(c(0x71717a)).child("↑ and ↓ count too"))
    }
}

struct CounterUnchanged;

impl Scenario for CounterUnchanged {
    fn name(&self) -> &'static str {
        "myzig-counter-unchanged"
    }
    fn description(&self) -> &'static str {
        "MyZig's counter, drawn again with nothing changed"
    }
    fn build(&self, _window: &mut Window, cx: &mut App) -> AnyView {
        cx.new(|_| Counter { n: 0 }).into()
    }
    fn step(&self, _root: &AnyView, _frame: usize, _window: &mut Window, _cx: &mut App) {}
}

// ---- quotes ----

const ROWS: usize = 40;

/// The PCG of Go's math/rand/v2 is not needed: any stream of the same
/// shape costs the same. A xorshift keeps it dependency-free.
struct Rng(u64);

impl Rng {
    fn next(&mut self) -> u64 {
        self.0 ^= self.0 << 13;
        self.0 ^= self.0 >> 7;
        self.0 ^= self.0 << 17;
        self.0
    }
    fn below(&mut self, n: u64) -> u64 {
        self.next() % n
    }
    fn float(&mut self) -> f64 {
        (self.next() >> 11) as f64 / (1u64 << 53) as f64
    }
}

struct Quotes {
    symbols: Vec<SharedString>,
    price: [f64; ROWS],
    open: [f64; ROWS],
    volume: [u64; ROWS],
    rng: Rng,
    ticks: u64,
}

impl Quotes {
    fn new() -> Self {
        let mut rng = Rng(42);
        let mut q = Quotes { symbols: Vec::new(), price: [0.; ROWS], open: [0.; ROWS], volume: [0; ROWS], rng: Rng(0), ticks: 0 };
        for i in 0..ROWS {
            let s: String = (0..4).map(|_| (b'A' + rng.below(26) as u8) as char).collect();
            q.symbols.push(s.into());
            q.price[i] = 20. + rng.float() * 480.;
            q.open[i] = q.price[i];
            q.volume[i] = rng.below(1_000_000);
        }
        q.rng = rng;
        q
    }
    fn tick(&mut self) {
        self.ticks += 1;
        for _ in 0..ROWS / 4 {
            let i = self.rng.below(ROWS as u64) as usize;
            self.price[i] *= 1. + (self.rng.float() - 0.5) * 0.004;
            self.volume[i] += self.rng.below(500);
        }
    }
}

fn cell(text: impl Into<SharedString>, w: f32, end: bool) -> gpui::Div {
    let d = div().w(px(w)).flex().child(text.into());
    if end { d.justify_end() } else { d }
}

impl Render for Quotes {
    fn render(&mut self, _window: &mut Window, _cx: &mut Context<Self>) -> impl IntoElement {
        let header = div()
            .flex()
            .items_center()
            .gap(px(12.))
            .px(px(8.))
            .h(px(24.))
            .text_size(px(12.))
            .text_color(c(0x71717a))
            .child(cell("Symbol", 80., false))
            .child(cell("Price", 90., true))
            .child(cell("Change", 80., true))
            .child(cell("%", 80., true))
            .child(cell("Volume", 100., true));
        div()
            .size_full()
            .flex()
            .flex_col()
            .p(px(16.))
            .gap(px(2.))
            .bg(c(0xffffff))
            .text_color(c(0x18181b))
            .text_size(px(14.))
            .child(div().text_size(px(18.)).font_weight(FontWeight::BOLD).child(SharedString::from(format!("Streaming quotes · tick {}", self.ticks))))
            .child(header)
            .children((0..ROWS).map(|i| {
                let ch = self.price[i] - self.open[i];
                let col = if ch >= 0. { c(0x16a34a) } else { c(0xdc2626) };
                div()
                    .flex()
                    .items_center()
                    .gap(px(12.))
                    .px(px(8.))
                    .h(px(22.))
                    .rounded(px(4.))
                    .when(i % 2 == 0, |d| d.bg(c(0xf4f4f5)))
                    .child(cell(self.symbols[i].clone(), 80., false).font_weight(FontWeight::BOLD))
                    .child(cell(format!("{:.2}", self.price[i]), 90., true))
                    .child(cell(format!("{:.2}", ch), 80., true).text_color(col))
                    .child(cell(format!("{:.2}%", ch / self.open[i] * 100.), 80., true).text_color(col))
                    .child(cell(self.volume[i].to_string(), 100., true).text_color(c(0x71717a)))
            }))
    }
}

struct QuotesStreaming;

impl Scenario for QuotesStreaming {
    fn name(&self) -> &'static str {
        "myzig-quotes-streaming"
    }
    fn description(&self) -> &'static str {
        "MyZig's quotes: 40 rows, a quarter of whose prices move every frame"
    }
    fn build(&self, _window: &mut Window, cx: &mut App) -> AnyView {
        cx.new(|_| Quotes::new()).into()
    }
    fn step(&self, root: &AnyView, _frame: usize, _window: &mut Window, cx: &mut App) {
        root.clone().downcast::<Quotes>().unwrap().update(cx, |q, cx| {
            q.tick();
            cx.notify();
        });
    }
}

// ---- list ----

const COUNT: usize = 10_000;

struct Items {
    scroll: UniformListScrollHandle,
}

impl Render for Items {
    fn render(&mut self, _window: &mut Window, _cx: &mut Context<Self>) -> impl IntoElement {
        div()
            .size_full()
            .flex()
            .flex_col()
            .bg(c(0xffffff))
            .text_color(c(0x18181b))
            .text_size(px(14.))
            .child(
                div()
                    .flex()
                    .items_center()
                    .gap(px(8.))
                    .p(px(12.))
                    .bg(c(0xf4f4f5))
                    .child(div().text_size(px(16.)).font_weight(FontWeight::BOLD).child("Inbox"))
                    .child(div().text_size(px(12.)).text_color(c(0x71717a)).child(SharedString::from(format!("{COUNT} messages")))),
            )
            .child(
                uniform_list("items", COUNT, |range, _, _| {
                    range
                        .map(|i| {
                            let (dot, col) = if i % 3 == 0 { ("●", c(0x2563eb)) } else { ("○", c(0xd9d9de)) };
                            div()
                                .h(px(36.))
                                .flex()
                                .items_center()
                                .gap(px(12.))
                                .px(px(12.))
                                .child(div().text_size(px(10.)).text_color(col).child(dot))
                                .child(div().w(px(140.)).when(i % 7 == 0, |d| d.font_weight(FontWeight::BOLD)).child(SharedString::from(format!("Message {i}"))))
                                .child(div().flex_1().text_color(c(0x71717a)).child(SharedString::from(format!("Subject line for item {}, with a preview", i * 7919 % 10007))))
                                .child(div().text_size(px(12.)).text_color(c(0x71717a)).child(SharedString::from(format!("{:02}:{:02}", (i / 60) % 24, i % 60))))
                        })
                        .collect()
                })
                .track_scroll(&self.scroll)
                .flex_1(),
            )
    }
}

struct ListScroll;

impl Scenario for ListScroll {
    fn name(&self) -> &'static str {
        "myzig-list-scroll"
    }
    fn description(&self) -> &'static str {
        "MyZig's list: 10,000 rows of 36px scrolled 4px per frame"
    }
    fn build(&self, _window: &mut Window, cx: &mut App) -> AnyView {
        cx.new(|_| Items { scroll: UniformListScrollHandle::new() }).into()
    }
    fn step(&self, root: &AnyView, frame: usize, _window: &mut Window, cx: &mut App) {
        root.clone().downcast::<Items>().unwrap().update(cx, |s, cx| {
            let top = px(frame as f32 * 4.);
            s.scroll.0.borrow().base_handle.set_offset(point(px(0.), -top));
            cx.notify();
        });
    }
}
