// The Vulkan build of instances.zig's fragment shader.
#version 450
layout(location = 0) in vec2 v_pos;
layout(location = 1) in vec2 v_uv;
layout(location = 2) flat in vec4 v_rect;
layout(location = 3) flat in vec4 v_clip;
layout(location = 4) flat in vec4 v_color;
layout(location = 5) flat in vec4 v_border_color;
layout(location = 6) flat in vec4 v_params;
layout(set = 0, binding = 0) uniform sampler2D u_atlas;
layout(location = 0) out vec4 o_color;
float box(vec2 p, vec2 c, vec2 h, float r) {
    vec2 q = abs(p - c) - (h - r);
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}
void main() {
    if (v_pos.x < v_clip.x || v_pos.y < v_clip.y || v_pos.x >= v_clip.z || v_pos.y >= v_clip.w) discard;
    vec4 fill = vec4(v_color.rgb * v_color.a, v_color.a);
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
