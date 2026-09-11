#version 450

layout (location = 0) in vec3 vPosition;

layout (set = 0, binding = 0, std140) uniform GlobalUniformObject{
    mat4 light_space_matrix;
}GlobalUBO;

layout (push_constant) uniform PushConstants{
    mat4 model;
}PushConstant;

void main(){
    // 计算光源空间位置(Shadow深度写入)
    gl_Position = GlobalUBO.light_space_matrix * PushConstant.model * vec4(vPosition, 1.0f);
}
