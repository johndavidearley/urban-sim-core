#version 450
layout(location=0) in vec2 position;
layout(location=1) in vec4 color;
layout(location=2) in vec2 uv;
layout(location=0) out vec4 vertexColor;
layout(location=1) out vec2 texCoord;
layout(push_constant) uniform Frame { vec2 size; int srgb; } frame;
void main() {
  gl_Position = vec4(position / frame.size * 2.0 - 1.0, 0.0, 1.0);
  vertexColor = color;
  texCoord = uv;
}
