#version 450
layout(location=0) in vec4 vertexColor;
layout(location=1) in vec2 texCoord;
layout(set=0,binding=0) uniform sampler2D fontAtlas;
layout(location=0) out vec4 outputColor;
layout(push_constant) uniform Frame { vec2 size; int srgb; } frame;
vec3 linearize(vec3 c) {
  return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), greaterThan(c, vec3(0.04045)));
}
void main() {
  outputColor = vec4(frame.srgb != 0 ? linearize(vertexColor.rgb) : vertexColor.rgb, vertexColor.a * (texCoord.x >= 0.0 ? texture(fontAtlas, texCoord).r : 1.0));
}
