// DeferredLighting.vert.hlsl - 延迟光照顶点着色器

// 全局uniform对象 - 参考原始GLSL GlobalUniformObject
struct GlobalUniformObject
{
    float4x4 light_space_matrix;
    // 以下光照参数全部由场景中的方向光 Actor 配置，经全局 UBO 上传
    float4 ambient_color;
    float4 light_direction;
    float4 light_color;
    float4 light_intensity;
    float global_time;
    float shadow_bias;
    float shadow_strength;
};

// 顶点着色器输入（全屏四边形）
struct VSInput
{
    uint vertex_id : SV_VertexID;
};

// 顶点着色器输出
struct VSOutput
{
    float4 position : SV_POSITION;
    [[vk::location(0)]] float  out_time        : TEXCOORD0;
    [[vk::location(1)]] float2 vTexcoord       : TEXCOORD1;
    [[vk::location(2)]] float4 ambient_color   : COLOR0;
    [[vk::location(3)]] float3 view_position   : VECTOR0;
};

// 资源绑定
[[vk::binding(0, 0)]] ConstantBuffer<GlobalUniformObject> GlobalUBO;

// 顶点着色器主函数
VSOutput main(VSInput input)
{
    VSOutput output;
    
    // 使用顶点ID生成全屏四边形
    if (input.vertex_id == 0)
    {
        float2 fragPosition = float2(-1.0, 1.0);  // 左上角
        output.position = float4(fragPosition, 0.0, 1.0);
        output.vTexcoord = float2(0, 0);
    }
    else if (input.vertex_id == 1)
    {
        float2 fragPosition = float2(1.0, -1.0);  // 右下角
        output.position = float4(fragPosition, 0.0, 1.0);
        output.vTexcoord = float2(1, 1);
    }
    else if (input.vertex_id == 2)
    {
        float2 fragPosition = float2(-1.0, -1.0); // 左下角
        output.position = float4(fragPosition, 0.0, 1.0);
        output.vTexcoord = float2(0, 1);
    }
    else if (input.vertex_id == 3)
    {
        float2 fragPosition = float2(1.0, 1.0);   // 右上角
        output.position = float4(fragPosition, 0.0, 1.0);
        output.vTexcoord = float2(1, 0);
    }
    
    // 传递数据
    output.out_time = GlobalUBO.global_time;
    // 环境光由光照 Actor 通过全局 UBO 提供
    output.ambient_color = GlobalUBO.ambient_color;
    output.view_position = float3(0, 0, 0); // 需要从别处获取
    
    return output;
}