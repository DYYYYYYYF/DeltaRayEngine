// GBuffer.vert.hlsl - G-Buffer顶点着色器

// 全局uniform对象 - 参考原始GLSL GlobalUniformObject
struct GlobalUniformObject
{
    float4x4 projection;
    float4x4 view;
    float4 ambient_color;
    float3 view_position;
    int mode;
    float global_time;
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
    [[vk::location(1)]] float3 vNormal    : NORMAL;
    [[vk::location(2)]] float2 vTexcoord  : TEXCOORD0;
    [[vk::location(3)]] float4 vColor     : COLOR;
    [[vk::location(4)]] float4 vTangent   : TANGENT;
};

// 顶点着色器输出 - 对应片段着色器输入
struct VSOutput
{
    float4 position : SV_POSITION;
    [[vk::location(0)]] int    out_mode        : INT0;
    [[vk::location(1)]] float2 vTexcoord       : TEXCOORD0;
    [[vk::location(2)]] float3 vNormal         : NORMAL0;
    [[vk::location(3)]] float3 vViewPosition   : VECTOR0;
    [[vk::location(4)]] float3 vWorldPosition  : VECTOR1;
    [[vk::location(5)]] float4 vColor          : COLOR0;
    [[vk::location(6)]] float4 vTangent        : TANGENT0;
    [[vk::location(7)]] float3 vBitangent      : VECTOR2;
};

// 资源绑定
[[vk::binding(0, 0)]] ConstantBuffer<GlobalUniformObject> GlobalUBO;
[[vk::push_constant]] ConstantBuffer<PushConstants> PushConstant;

// 顶点着色器主函数
VSOutput main(VSInput input)
{
    VSOutput output;
    
    // 计算世界空间位置
    float4 worldPosition = mul(PushConstant.model, float4(input.vPosition, 1.0f));
    output.vWorldPosition = worldPosition.xyz;
    
    // 传递纹理坐标和颜色
    output.vTexcoord = input.vTexcoord;
    output.vColor = input.vColor;
    
    // 变换法线到世界空间：使用 model 的 3x3 部分的逆转置，以正确处理非均匀缩放。
    // 注：glslc 的 HLSL 前端基于 glslang，不提供 inverse() intrinsic，此处手工展开：
    //     逆转置 = 伴随矩阵 / 行列式，即 normalMatrix = cofactor(M) / det(M)
    float3x3 M = (float3x3)PushConstant.model;

    float c00 = M[1][1] * M[2][2] - M[1][2] * M[2][1];
    float c01 = M[1][2] * M[2][0] - M[1][0] * M[2][2];
    float c02 = M[1][0] * M[2][1] - M[1][1] * M[2][0];
    float c10 = M[0][2] * M[2][1] - M[0][1] * M[2][2];
    float c11 = M[0][0] * M[2][2] - M[0][2] * M[2][0];
    float c12 = M[0][1] * M[2][0] - M[0][0] * M[2][1];
    float c20 = M[0][1] * M[1][2] - M[0][2] * M[1][1];
    float c21 = M[0][2] * M[1][0] - M[0][0] * M[1][2];
    float c22 = M[0][0] * M[1][1] - M[0][1] * M[1][0];

    float det = M[0][0] * c00 + M[0][1] * c01 + M[0][2] * c02;
    float invDet = (abs(det) > 1e-12f) ? (1.0f / det) : 0.0f;

    float3x3 normalMatrix = float3x3(
        c00, c01, c02,
        c10, c11, c12,
        c20, c21, c22) * invDet;

    float3 N = normalize(mul(normalMatrix, input.vNormal));
    output.vNormal = N;

    // 计算(副)切线：用同一法线矩阵变换后做 Gram-Schmidt 正交化
    float3 T = mul(normalMatrix, input.vTangent.xyz);
    if (dot(T, T) < 1e-8f) {
        // 退化保护：模型无 UV / 全为退化三角面时 tangent 为 (0,0,0)，避免 normalize 产生 NaN
        float3 up = (abs(N.z) < 0.999f) ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
        T = cross(up, N);
    }
    T = normalize(T - dot(T, N) * N);
    output.vTangent = float4(T, input.vTangent.w);
    output.vBitangent = cross(N, T) * input.vTangent.w;
    
    // 输出模式
    output.out_mode = GlobalUBO.mode;

    // 摄像机位置
    output.vViewPosition = GlobalUBO.view_position;
    
    // 计算最终位置
    output.position = mul(GlobalUBO.projection, mul(GlobalUBO.view, worldPosition));
    
    return output;
}