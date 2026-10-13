// The Vulkan build of instances.zig's fragment shader.
#version 450
layout(location = 0) in vec2 v_pos;
layout(location = 1) in vec2 v_uv;
layout(location = 2) flat in vec4 v_rect;
layout(location = 3) flat in vec4 v_clip;
layout(location = 4) flat in vec4 v_color;
layout(location = 5) flat in vec4 v_border_color;
layout(location = 6) flat in vec4 v_params;
layout(location = 7) flat in vec4 v_seg;
layout(set = 0, binding = 0) uniform sampler2D u_atlas;
layout(set = 0, binding = 1) uniform sampler2D u_images;
layout(location = 0) out vec4 o_color;
float box(vec2 p, vec2 c, vec2 h, float r) {
    vec2 q = abs(p - c) - (h - r);
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}
float segment(vec2 p, vec4 s) {
    vec2 a = v_rect.xy + s.xy * v_rect.zw;
    vec2 d = v_rect.xy + s.zw * v_rect.zw - a;
    float l2 = dot(d, d);
    float t = l2 > 0.0 ? clamp(dot(p - a, d) / l2, 0.0, 1.0) : 0.0;
    return length(p - a - t * d);
}
void main() {
    if (v_pos.x < v_clip.x || v_pos.y < v_clip.y || v_pos.x >= v_clip.z || v_pos.y >= v_clip.w) discard;
    vec4 fill = vec4(v_color.rgb * v_color.a, v_color.a);
    if (v_params.z > 3.5) {
        vec2 half_texel = 0.5 / vec2(textureSize(u_images, 0));
        vec2 uv = clamp(v_uv, min(v_seg.xy, v_seg.zw) + half_texel, max(v_seg.xy, v_seg.zw) - half_texel);
        vec2 h = v_rect.zw * 0.5;
        float cover = clamp(0.5 - box(v_pos, v_rect.xy + h, h, v_params.x), 0.0, 1.0);
        o_color = texture(u_images, uv) * (v_color.a * cover);
        return;
    }
    if (v_params.z > 2.5) {
        vec2 h = v_seg.zw * 0.5;
        float d = box(v_pos, v_seg.xy + h, h, v_params.x);
        o_color = fill * (1.0 - smoothstep(-v_params.w, v_params.w, d));
        return;
    }
    if (v_params.z > 1.5) {
        float d = min(segment(v_pos, v_seg), segment(v_pos, v_border_color)) - v_params.y * 0.5;
        o_color = fill * clamp(0.5 - d, 0.0, 1.0);
        return;
    }
    if (v_params.z > 0.5) {
        o_color = fill * texture(u_atlas, v_uv).r;
        return;
    }
    vec2 h = v_rect.zw * 0.5;
    vec2 ctr = v_rect.xy + h;
    float r = v_params.x;
    float bw = v_params.y;
    float outer = clamp(0.5 - box(v_pos, ctr, h, r), 0.0, 1.0);
    if (bw > 0.0) {
        vec4 border = vec4(v_border_color.rgb * v_border_color.a, v_border_color.a);
        float inner = clamp(0.5 - box(v_pos, ctr, h - bw, max(r - bw, 0.0)), 0.0, 1.0);
        o_color = mix(border, fill, inner) * outer;
    } else {
        o_color = fill * outer;
    }
}
