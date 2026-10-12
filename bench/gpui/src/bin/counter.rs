//! MyGo's counter-native in gpui-fast: a count and three buttons.
use gpui::{
    App, Bounds, Context, Window, WindowBounds, WindowOptions, div, prelude::*, px, rgb, size,
};
use gpui_platform::application;

struct Counter {
    n: i64,
}

fn button(id: &'static str, label: &'static str, primary: bool) -> gpui::Stateful<gpui::Div> {
    let (bg, hover, fg) = if primary {
        (rgb(0x2563eb), rgb(0x1d4ed8), rgb(0xffffff))
    } else {
        (rgb(0xf4f4f5), rgb(0xe9e9ec), rgb(0x18181b))
    };
    div()
        .id(id)
        .px(px(14.))
        .py(px(6.))
        .rounded(px(6.))
        .bg(bg)
        .hover(move |s| s.bg(hover))
        .text_color(fg)
        .when(!primary, |d| d.border_1().border_color(rgb(0xd9d9de)))
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
            .bg(rgb(0xffffff))
            .text_color(rgb(0x18181b))
            .child(div().text_size(px(56.)).font_weight(gpui::FontWeight::BOLD).child(self.n.to_string()))
            .child(
                div()
                    .flex()
                    .gap(px(8.))
                    .child(button("dec", "−", false).on_click(cx.listener(|this, _, _, cx| {
                        this.n -= 1;
                        cx.notify();
                    })))
                    .child(button("reset", "Reset", false).on_click(cx.listener(|this, _, _, cx| {
                        this.n = 0;
                        cx.notify();
                    })))
                    .child(button("inc", "+", true).on_click(cx.listener(|this, _, _, cx| {
                        this.n += 1;
                        cx.notify();
                    }))),
            )
            .child(div().text_size(px(12.)).text_color(rgb(0x71717a)).child("↑ and ↓ count too"))
    }
}

fn main() {
    application().run(|cx: &mut App| {
        let bounds = Bounds::centered(None, size(px(360.), px(260.)), cx);
        cx.open_window(
            WindowOptions { window_bounds: Some(WindowBounds::Windowed(bounds)), ..Default::default() },
            |_, cx| cx.new(|_| Counter { n: 0 }),
        )
        .unwrap();
        cx.activate(true);
    });
}
