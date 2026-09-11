// Shadow.vert.hlsl - Shadow深度通道顶点着色器

// 全局uniform对象
struct GlobalUniformObject
{
    float4x4 light_space_matrix;
};

// 推送常量结构
struct PushConstants
{
    float4x4 model;
};

// 顶点着色器输入
struct VSInput
{
    [[vk::location(0)]] float3 vPosition  : POSITION;
};

// 顶点着色器输出 - 对应片段着色器输入
struct VSOutput
{
    float4 position : SV_POSITION;
};

// 资源绑定
[[vk::binding(0, 0)]] ConstantBuffer<GlobalUniformObject> GlobalUBO;
[[vk::push_constant]] ConstantBuffer<PushConstants> PushConstant;

// 顶点着色器主函数
VSOutput main(VSInput input)
{
    VSOutput output;

    // 计算光源空间位置(Shadow深度写入)
    output.position = mul(GlobalUBO.light_space_matrix, mul(PushConstant.model, float4(input.vPosition, 1.0f)));

    return output;
}
