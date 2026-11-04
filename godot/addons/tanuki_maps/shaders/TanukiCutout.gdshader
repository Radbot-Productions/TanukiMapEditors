shader_type spatial;
render_mode blend_mix, depth_draw_opaque, cull_back, diffuse_burley, specular_schlick_ggx;
group_uniforms BaseColor;
uniform vec4 base_color : source_color = vec4(1.0);
uniform sampler2D base_color_texture : source_color, hint_default_white, filter_linear_mipmap, repeat_enable;
group_uniforms Surface;
uniform float roughness : hint_range(0.0, 1.0) = 0.7;
uniform float metallic : hint_range(0.0, 1.0) = 0.0;
uniform sampler2D roughness_texture : hint_default_white, filter_linear_mipmap, repeat_enable;
uniform int roughness_channel : hint_enum("Red", "Green", "Blue", "Alpha") = 0;
uniform sampler2D metallic_texture : hint_default_white, filter_linear_mipmap, repeat_enable;
uniform int metallic_channel : hint_enum("Red", "Green", "Blue", "Alpha") = 0;
group_uniforms Normal;
uniform sampler2D normal_texture : hint_normal, filter_linear_mipmap, repeat_enable;
uniform float normal_strength : hint_range(0.0, 10.0) = 1.0;
group_uniforms Occlusion;
uniform sampler2D ao_texture : hint_default_white, filter_linear_mipmap, repeat_enable;
uniform int ao_channel : hint_enum("Red", "Green", "Blue", "Alpha") = 0;
group_uniforms UVTransform;
uniform vec2 uv_tiling = vec2(1.0);
uniform vec2 uv_offset = vec2(0.0);
group_uniforms;
float channel_value(vec4 value, int channel) {
    if (channel == 1) { return value.g; }
    if (channel == 2) { return value.b; }
    if (channel == 3) { return value.a; }
    return value.r;
}
void vertex() { UV = UV * uv_tiling + uv_offset; }
uniform float alpha_cutoff : hint_range(0.0, 1.0) = 0.5;
void fragment() {
    vec4 color = texture(base_color_texture, UV) * base_color;
    ALBEDO = color.rgb;
    ROUGHNESS = roughness * channel_value(texture(roughness_texture, UV), roughness_channel);
    METALLIC = metallic * channel_value(texture(metallic_texture, UV), metallic_channel);
    NORMAL_MAP = texture(normal_texture, UV).rgb;
    NORMAL_MAP_DEPTH = normal_strength;
    AO = channel_value(texture(ao_texture, UV), ao_channel);
    ALPHA = color.a; ALPHA_SCISSOR_THRESHOLD = alpha_cutoff;
}
