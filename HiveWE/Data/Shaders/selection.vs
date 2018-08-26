#version 450 core

layout (location = 0) in vec2 vPosition;
layout (location = 1) uniform mat4 MVP;

void main() {
	gl_Position = MVP * vec4(vPosition, 0, 1);
}