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
in vec3 tangent_light_direction;
in vec4 vertexColor;
in vec3 team_color;

out vec4 color;

void main() {
	LayerTextureIds ids = layer_textures[layer_index];

	color = texture(textures[ids.albedo], UV) * vertexColor;

	if (color.a < alpha_test) {
		discard;
	}

	if (show_lighting) {
		vec3 emissive_texel = texture(textures[ids.emissive], UV).rgb;
		vec4 orm_texel = texture(textures[ids.orm], UV);
		vec3 tc_texel = texture(textures[ids.team_color], UV).rgb;
		color.rgb = (color.rgb * (1 - orm_texel.w) + color.rgb * tc_texel.r * team_color * orm_texel.w);

		// normal is a 2 channel normal map so we have to deduce the 3rd value
		vec2 normal_texel = texture(textures[ids.normal], UV).xy * 2.0 - 1.0;
		vec3 normal = vec3(normal_texel, sqrt(1.0 - dot(normal_texel, normal_texel)));

		float lambert = clamp(dot(normal, -tangent_light_direction), 0.f, 1.f);
		color.rgb *= clamp(lambert + 0.1, 0.f, 1.f);
		color.rgb += emissive_texel;
	}
}
