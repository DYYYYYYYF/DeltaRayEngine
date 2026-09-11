struct UBO
{
    float4x4 proj;
    float4x4 view;
    float4 ambient_color;
    float3 view_position;
    int mode;
    float global_time;
};

struct PushConstant
{
    float4x4 model;
};

struct VSInput
{
    [[vk::location(0)]] float3 vPosition : VECTOR;
    [[vk::location(1)]] float3 vNormal   : NORMAL0;
    [[vk::location(2)]] float2 vTexCoord : TEXCOORD0;
    [[vk::location(3)]] float4 vColor    : COLOR0;
    [[vk::location(4)]] float4 vTangent  : POSITION0;
};

struct VSOutput
{
    float4 outPosition      : SV_POSITION;
    int outMode             : INT0;
    float2 outTexcoord      : TEXCOORD0;
    float3 outNormal        : NORMAL0;
    float4 outAmbientColor  : COLOR0;
    float3 outViewPosition  : VECTOR0;
    float3 outFragPosition  : VECTOR1;
    float3 outVertPosition  : VECTOR2;
    float4 outColor         : COLOR1;
    float4 outTangent       : POSITION0;
};

[[vk::binding(0, 0)]] ConstantBuffer<UBO> ubo;
[[vk::push_constant]] ConstantBuffer<PushConstant> push_constants;

VSOutput main(VSInput input) 
{
    float4 Position = float4(input.vPosition, 1.0f);
    
	VSOutput output = (VSOutput)0;
    output.outPosition = mul(ubo.proj, mul(ubo.view, mul(push_constants.model, Position)));
    output.outColor = input.vColor;
	output.outTexcoord = input.vTexCoord;
    output.outAmbientColor = ubo.ambient_color;
    output.outViewPosition = ubo.view_position;
    output.outFragPosition = mul(push_constants.model, float4(input.vPosition, 1.0f)).xyz;
    // 法线矩阵：model 的 3x3 部分的逆转置，以正确处理非均匀缩放。
    // 注：glslc 的 HLSL 前端基于 glslang，不提供 inverse() intrinsic，此处手工展开：
    //     逆转置 = 伴随矩阵 / 行列式，即 normalMatrix = cofactor(M) / det(M)
    float3x3 M = (float3x3)push_constants.model;

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
    output.outNormal = N;

    // 切线：同一矩阵变换后做 Gram-Schmidt 正交化，并对退化输入做保护
    float3 T = mul(normalMatrix, input.vTangent.xyz);
    if (dot(T, T) < 1e-8f) {
        float3 up = (abs(N.z) < 0.999f) ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
        T = cross(up, N);
    }
    T = normalize(T - dot(T, N) * N);
    output.outTangent = float4(T, input.vTangent.w);

    output.outMode = ubo.mode;
    output.outVertPosition = output.outPosition / 255.0f;

	return output;
}