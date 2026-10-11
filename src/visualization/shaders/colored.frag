#version 450
layout(location=0) in vec4 vertexColor;
layout(location=0) out vec4 outputColor;
layout(push_constant) uniform Frame { vec2 size; int srgb; } frame;
vec3 linearize(vec3 c) {
  return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), greaterThan(c, vec3(0.04045)));
}
void main() {
  outputColor = vec4(frame.srgb != 0 ? linearize(vertexColor.rgb) : vertexColor.rgb, vertexColor.a);
}
