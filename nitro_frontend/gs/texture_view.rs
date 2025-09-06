use std::time::Duration;

use crossbeam_channel::Sender;
use gpui::*;
use gpui_component::{checkbox::Checkbox, h_flex, Sizable};
use wgpu::wgc::api::Metal;

use nitro_core::{
    extensions::BitManipulation,
    gs_render::{GSRenderTexture, GSRenderTextureCacheKey},
    hw::gs::GraphicSynthesizer,
    renderer::compositor::Compositor,
    FrontendMessage,
};

use crate::frontend::{NitroFrontendState, Theme};

pub struct TextureList {
    compositor: Entity<Compositor>,
    state: Entity<NitroFrontendState>,
    frontend_message_sender: Sender<FrontendMessage>,
}

impl TextureList {
    pub fn new(
        _window: &mut gpui::Window,
        _cx: &mut Context<Self>,
        state: Entity<NitroFrontendState>,
        frontend_message_sender: Sender<FrontendMessage>,
        compositor: Entity<Compositor>,
    ) -> Self {
        Self {
            compositor,
            state,
            frontend_message_sender,
        }
    }
}

#[derive(IntoElement)]
pub struct TextureListItem {
    idx: usize,
    value: (GSRenderTextureCacheKey, GSRenderTexture),
}

impl TextureListItem {
    pub fn new(idx: usize, value: (GSRenderTextureCacheKey, GSRenderTexture)) -> Self {
        Self { idx, value }
    }
}

impl RenderOnce for TextureListItem {
    fn render(self, __: &mut gpui::Window, _cx: &mut App) -> impl gpui::IntoElement {
        let metal_tex = unsafe {
            self.value
                .1
                 .0
                .as_hal::<Metal, _, _>(|texture| texture.unwrap().raw_handle().clone())
        };

        let wrap_s = (self.value.0.clamp >> 0) & 0x3;
        let wrap_t = (self.value.0.clamp >> 2) & 0x3;

        let wrap_horizontal = match wrap_s {
            0 => "REPEAT",
            0x01 => "CLAMP",
            0x02 => "REGION_CLAMP",
            0x03 => "REGION_REPEAT",
            _ => "UNK",
        };

        let wrap_vertical = match wrap_t {
            0 => "REPEAT",
            0x01 => "CLAMP",
            0x02 => "REGION_CLAMP",
            0x03 => "REGION_REPEAT",
            _ => "UNK",
        };

        h_flex().px_1().py_1().w_full().flex().text_sm().child(
            div()
                .id(self.value.0.tw as usize)
                .whitespace_nowrap()
                .font_family("Courier")
                .flex()
                .gap_x_2()
                .child(
                    div()
                        .h(px(128.))
                        .border_1()
                        .border_color(Theme::nitro().border)
                        .child(
                            surface(metal_tex)
                                .object_fit(gpui::ObjectFit::Fill)
                                .w(px(128.))
                                .h(px(128.))
                                .opacity(0.)
                                .with_animation(
                                    "fade-anim",
                                    Animation::new(Duration::from_millis(1250)),
                                    |label, delta| label.opacity(delta),
                                ),
                        ),
                )
                .child(
                    div()
                        .flex()
                        .flex_col()
                        .child(div().text_color(Theme::nitro().muted).child(format!(
                            "TEX{} {}x{} @ 0x{:08x}",
                            self.idx, self.value.0.tw, self.value.0.th, self.value.0.tbp
                        )))
                        .child(format!("wrap x: {}, y: {}", wrap_horizontal, wrap_vertical))
                        .child(format!(
                            "{}",
                            match self.value.0.psm {
                                0x00 => "psmct32".to_owned(),
                                0x01 => "psmct24".to_owned(),
                                0x02 => "psmct16".to_owned(),
                                0x13 => "psmt8".to_owned(),
                                0x14 => "psmt4".to_owned(),
                                0x24 => "psmt4hl".to_owned(),
                                0x1b => "psmt8h".to_owned(),
                                0x32 => "psmz16".to_owned(),
                                _ => format!("unsupported format {:08x}", self.value.0.psm),
                            }
                        ))
                        .child(if GraphicSynthesizer::is_clut_format(self.value.0.psm) {
                            format!(
                                "csm{} @ 0x{:08x} {}",
                                if self.value.0.tex0.check_bit(55) {
                                    2
                                } else {
                                    1
                                },
                                self.value.0.cbp,
                                match self.value.0.cpsm {
                                    0 => "psmct32",
                                    2 => "psmct16",
                                    0xa => "psmct16s",
                                    _ => unreachable!(),
                                }
                            )
                        } else {
                            format!("")
                        })
                        .opacity(0.)
                        .with_animation(
                            "fade-anim",
                            Animation::new(Duration::from_millis(250)),
                            |label, delta| label.opacity(delta),
                        ),
                )
                .w_full()
                .bg(Theme::nitro().bg)
                .rounded_sm()
                .p_2()
                .shadow_lg()
                .rounded(px(8.0))
                .border_1()
                .border_color(Theme::nitro().grid)
                .flex_grow(),
        )
    }
}

impl Render for TextureList {
    fn render(&mut self, _window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        let tex_count = self.compositor.read(cx).textures.len();

        div()
            .bg(Theme::nitro().bg_card)
            .p_1()
            .child(
                div().p(px(8.)).child(
                    Checkbox::new("view_tex_cluts")
                        .with_size(px(120.))
                        .label("View texture CLUTs")
                        .on_click(cx.listener(|this, checked, window, cx| {
                            this.state.update(cx, |state, cx| {
                                state.view_tex_cluts = *checked;

                                this.frontend_message_sender
                                    .send(FrontendMessage::SetViewTexCLUTs(*checked))
                                    .unwrap();

                                cx.notify();
                            });
                        }))
                        .bg(rgba(0x000000))
                        .checked(self.state.read(cx).view_tex_cluts),
                ),
            )
            .child(
                uniform_list(
                    cx.entity().clone(),
                    "gs-texturelist",
                    tex_count,
                    |this, range, _window, cx| {
                        let mut items = Vec::with_capacity(range.len());
                        let compositor = this.compositor.read(cx);
                        let rs = range.start;
                        let mut i = 0;
                        for idx in range {
                            let tex = &compositor.textures[rs..][i];
                            items.push(TextureListItem::new(rs + idx, tex.clone()));
                            i += 1;
                        }
                        items
                    },
                )
                .size_full(),
            )
            .size_full()
    }
}
