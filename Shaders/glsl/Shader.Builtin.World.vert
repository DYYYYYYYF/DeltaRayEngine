#version 450

layout (location = 0) in vec3 vPosition;
layout (location = 1) in vec3 vNormal;
layout (location = 2) in vec2 vTexcoord;
layout (location = 3) in vec4 vColor;
layout (location = 4) in vec4 vTangent;

layout (set = 0, binding = 0, std140) uniform GlobalUniformObject{
	mat4 projection;
	mat4 view;
	vec4 ambient_color;
	vec3 view_position;
	int mode;
	float global_time;
}GlobalUBO;

layout (push_constant) uniform PushConstants{
	mat4 model;
}PushConstant;

layout (location = 0) out int out_mode;
layout (location = 1) out struct out_dto{
	vec2 vTexcoord;
	vec3 vNormal;
	vec4 vAmbientColor;
	vec3 vViewPosition;
	vec3 vFragPosition;
	vec3 vVertPosition;
	vec4 vColor;
	vec4 vTangent;
}OutDto;

void main(){
	OutDto.vTexcoord = vTexcoord;
	OutDto.vColor = vColor;
	OutDto.vAmbientColor = GlobalUBO.ambient_color;
	OutDto.vViewPosition = GlobalUBO.view_position;
	OutDto.vFragPosition = vec3(PushConstant.model * vec4(vPosition, 1.0f));
	// 法线矩阵：model 的 3x3 部分的逆转置，以正确处理非均匀缩放
	mat3 normalMatrix = transpose(inverse(mat3(PushConstant.model)));

	vec3 N = normalize(normalMatrix * vNormal);
	OutDto.vNormal = N;

	// 切线：同一矩阵变换后做 Gram-Schmidt 正交化，并对退化输入做保护
	vec3 T = normalMatrix * vTangent.xyz;
	if (dot(T, T) < 1e-8) {
		vec3 up = (abs(N.z) < 0.999) ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
		T = cross(up, N);
	}
	T = normalize(T - dot(T, N) * N);
	OutDto.vTangent = vec4(T, vTangent.w);
	gl_Position = GlobalUBO.projection * GlobalUBO.view * PushConstant.model * vec4(vPosition, 1.0f);

	out_mode = GlobalUBO.mode;
	OutDto.vVertPosition = gl_Position.xyz / 255.0f;
}
