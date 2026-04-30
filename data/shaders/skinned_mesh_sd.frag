#version 450 core

#extension GL_ARB_bindless_texture : require

layout (location = 1) uniform float alpha_test;
layout (location = 2) uniform bool show_lighting;
layout (location = 3) uniform vec3 light_direction;
layout (location = 5) uniform int layer_index;
layout (location = 10) uniform bool is_team_color;

struct LayerTextureIds {
	uint albedo;
	uint normal;
	uint orm;
	uint emissive;
	uint team_color;
	uint environment;
	uint _pad0;
	uint _pad1;
};

layout(std430, binding = 9) buffer TextureHandles {
	sampler2D textures[];
};

layout(std430, binding = 10) buffer LayerTexturesBuf {
	LayerTextureIds layer_textures[];
};

in vec2 UV;
in vec3 Normal;
in vec4 vertexColor;
in vec3 team_color;

out vec4 color;

void main() {
	LayerTextureIds ids = layer_textures[layer_index];
	sampler2D diffuse = textures[ids.albedo];

	if (is_team_color) {
		color = vec4(team_color * texture(diffuse, UV).r, 1.f) * vertexColor;
	} else {
		color = texture(diffuse, UV) * vertexColor;
	}

	if (vertexColor.a == 0.0 || color.a < alpha_test) {
		discard;
	}

	if (show_lighting) {
		float contribution = (dot(Normal, -light_direction) + 1.f) * 0.5f;
		color.rgb *= clamp(contribution, 0.f, 1.f);
	}
}
