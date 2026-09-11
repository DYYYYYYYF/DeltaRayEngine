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
    vec3 vViewPosition;
    vec3 vWorldPosition;
    vec4 vColor;
    vec4 vTangent;
    vec3 vBitangent;
}OutDto;

void main(){
    // 计算世界空间位置
    vec4 worldPosition = PushConstant.model * vec4(vPosition, 1.0f);
    OutDto.vWorldPosition = worldPosition.xyz;
    
    // 传递纹理坐标和颜色
    OutDto.vTexcoord = vTexcoord;
    OutDto.vColor = vColor;
    
    // 变换法线到世界空间：使用 model 的 3x3 部分的逆转置，以正确处理非均匀缩放
    mat3 normalMatrix = transpose(inverse(mat3(PushConstant.model)));

    vec3 N = normalize(normalMatrix * vNormal);
    OutDto.vNormal = N;

    // 计算(副)切线：用同一法线矩阵变换后做 Gram-Schmidt 正交化
    vec3 T = normalMatrix * vTangent.xyz;
    if (dot(T, T) < 1e-8) {
        // 退化保护：模型无 UV / 全为退化三角面时 tangent 为 (0,0,0)，避免 normalize 产生 NaN
        vec3 up = (abs(N.z) < 0.999) ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
        T = cross(up, N);
    }
    T = normalize(T - dot(T, N) * N);
    OutDto.vTangent = vec4(T, vTangent.w);
    OutDto.vBitangent = cross(N, T) * vTangent.w;
    
    // 输出模式
    out_mode = GlobalUBO.mode;

    // 摄像机位置
    OutDto.vViewPosition = GlobalUBO.view_position;
    
    // 计算最终位置
    gl_Position = GlobalUBO.projection * GlobalUBO.view * worldPosition;
}