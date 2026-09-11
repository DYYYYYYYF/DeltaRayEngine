#version 450

layout (location = 0) in vec3 vPosition;
layout (location = 1) in vec2 vTexcoord;

layout (set = 0, binding = 0, std140) uniform GlobalUniformObject{
    mat4 light_space_matrix;
    float global_time;
}GlobalUBO;

layout (location = 0) out float out_time;
layout (location = 1) out struct out_dto{
    vec2 vTexcoord;
    vec4 ambient_color;
    vec3 view_position;
}OutDto;

void main(){
    // 使用 gl_VertexIndex 来选择矩形的四个顶点
    if (gl_VertexIndex == 0) {
        vec2 fragPosition = vec2(-1.0, 1.0);  // 左上角 0
        gl_Position = vec4(fragPosition, 0.0, 1.0);
        OutDto.vTexcoord = vec2(0, 0);
    } else if (gl_VertexIndex == 1) {
        vec2 fragPosition = vec2(1.0, -1.0);  // 右下角 1
        gl_Position = vec4(fragPosition, 0.0, 1.0);
        OutDto.vTexcoord = vec2(1, 1);
    } else if (gl_VertexIndex == 2) {
        vec2 fragPosition = vec2(-1.0, -1.0); // 左下角 2
        gl_Position = vec4(fragPosition, 0.0, 1.0);
        OutDto.vTexcoord = vec2(0, 1);
    } else if (gl_VertexIndex == 3) {
        vec2 fragPosition = vec2(1.0, 1.0);   // 右上角 3
        gl_Position = vec4(fragPosition, 0.0, 1.0);
        OutDto.vTexcoord = vec2(1, 0);
    }

    // Copy props
    out_time = GlobalUBO.global_time;
    OutDto.ambient_color = vec4(0.18, 0.18, 0.18, 1.0);
    OutDto.view_position = vec3(0.0, 0.0, 0.0);
}