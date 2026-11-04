shader_type sky;
uniform sampler2D panorama : filter_linear_mipmap, repeat_enable;
void sky()
{
    vec3 direction=vec3(EYEDIR.z,-EYEDIR.x,EYEDIR.y);
    vec2 uv=vec2(atan(direction.y,direction.x)/(2.0*PI)+0.5,0.5-asin(clamp(direction.z,-1.0,1.0))/PI);
    COLOR=texture(panorama,uv).rgb;
}
