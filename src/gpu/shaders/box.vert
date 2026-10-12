// The Vulkan build of instances.zig's vertex shader: compile with
// `zig build shaders` (glslangValidator) after an edit.
#version 450
layout(location = 0) in vec4 a_rect;
layout(location = 1) in vec4 a_clip;
layout(location = 2) in vec4 a_color;
layout(location = 3) in vec4 a_border_color;
layout(location = 4) in vec4 a_params;
layout(location = 5) in vec4 a_uv;
layout(push_constant) uniform Push { vec2 viewport; } u;
layout(location = 0) out vec2 v_pos;
layout(location = 1) out vec2 v_uv;
layout(location = 2) flat out vec4 v_rect;
layout(location = 3) flat out vec4 v_clip;
layout(location = 4) flat out vec4 v_color;
layout(location = 5) flat out vec4 v_border_color;
layout(location = 6) flat out vec4 v_params;
layout(location = 7) flat out vec4 v_seg;
void main() {
    vec2 corner = vec2(float(gl_VertexIndex & 1), float((gl_VertexIndex >> 1) & 1));
    vec2 pos = a_rect.xy + corner * a_rect.zw;
    v_pos = pos;
    v_uv = mix(a_uv.xy, a_uv.zw, corner);
    v_rect = a_rect;
    v_clip = a_clip;
    v_color = a_color;
    v_border_color = a_border_color;
    v_params = a_params;
    v_seg = a_uv;
    // Vulkan's y points down, as the scene's does.
    gl_Position = vec4(pos / u.viewport * 2.0 - 1.0, 0.0, 1.0);
}
