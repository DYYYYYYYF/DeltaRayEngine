// DeferredLighting.frag.hlsl - 延迟光照片段着色器

// 全局uniform对象 - 参考原始GLSL GlobalUniformObject
struct GlobalUniformObject
{
    float4x4 light_space_matrix;
    // 以下光照参数全部由场景中的方向光 Actor 配置，经全局 UBO 上传
    float4 ambient_color;
    float4 light_direction;   // xyz = 光源指向场景的单位方向
    float4 light_color;
    float4 light_intensity;
    float global_time;
    float shadow_bias;
    float shadow_strength;    // 0 = 无阴影，1 = 全阴影
};

// 常量定义
static const int SAMP_ALBEDO = 0;
static const int SAMP_NORMAL = 1;
static const int SAMP_POSITION = 2;
static const int SAMP_SHADOW = 3;

// 实例uniform对象 - 参考原始GLSL InstanceUniformObject
struct InstanceUniformObject
{
    int debug_mode;
};

// 光源结构定义
struct DirectionalLight
{
    float3 direction;
    float4 color;
};

struct PointLight
{
    float3 position;
    float4 color;
    float fconstant;
    float linear_attenuation;
    float quadratic;
};

// 片段着色器输入 - 对应原始GLSL的in_dto结构
struct PSInput
{
    [[vk::location(0)]] float  global_time     : TEXCOORD0;
    [[vk::location(1)]] float2 vTexcoord       : TEXCOORD1;
    [[vk::location(2)]] float4 ambient_color   : COLOR0;
    [[vk::location(3)]] float3 view_position   : VECTOR0;
};

// 像素着色器输出
struct PSOutput
{
    float4 FragColor : SV_TARGET0;
};

// 资源绑定
[[vk::binding(0, 0)]] ConstantBuffer<GlobalUniformObject> GlobalUBO;
[[vk::binding(0, 1)]] ConstantBuffer<InstanceUniformObject> InstanceUBO;
[[vk::binding(1, 1)]] Texture2D Samplers[4];
[[vk::binding(1, 1)]] SamplerState DefaultSampler[4];

// 方向光不再使用硬编码常量：方向、颜色、强度、阴影偏置/强度全部来自 GlobalUBO，
// 数据由场景中的方向光 Actor 配置并逐帧上传。

static PointLight point_light_0 = 
{
    float3(-5.5f, 10.0f, -5.5f),
    float4(0.0f, 1.0f, 0.0f, 1.0f),
    1.0f,   // Constant
    0.35f,  // Linear
    0.44f   // Quadratic
};

static PointLight point_light_1 = 
{
    float3(5.5f, 10.0f, -5.5f),
    float4(1.0f, 0.0f, 0.0f, 1.0f),
    1.0f,   // Constant
    0.35f,  // Linear
    0.44f   // Quadratic
};

// PBR常量
static const float PI = 3.14159265359;

// Fresnel-Schlick近似
float3 fresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0f - F0) * pow(1.0f - cosTheta, 5.0f);
}

// GGX分布函数
float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0f);
    float NdotH2 = NdotH * NdotH;

    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0f) + 1.0f);
    denom = PI * denom * denom;

    return nom / denom;
}

// Smith几何遮蔽函数
float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0f);
    float k = (r * r) / 8.0f;

    float nom = NdotV;
    float denom = NdotV * (1.0f - k) + k;

    return nom / denom;
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0f);
    float NdotL = max(dot(N, L), 0.0f);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

// PBR光照计算
float4 PBR(PointLight light, float3 norm, float3 albedo, float3 camPos, float3 fragPos, float metallic, float roughness, float ao)
{
    // 视线方向和法线
    float3 N = normalize(norm);
    float3 V = normalize(camPos - fragPos);

    // 环境光的基本反射率
    float3 F0 = float3(0.04f, 0.04f, 0.04f); // 非金属表面的F0反射率
    F0 = lerp(F0, albedo, metallic);

    // 计算光线方向和半程向量
    float3 L = normalize(light.position - fragPos);
    float3 H = normalize(V + L);

    // 计算距离衰减
    float distance = length(light.position - fragPos);
    float attenuation = 1.0 / (light.fconstant + light.linear_attenuation * distance + light.quadratic * distance * distance);
    float3 radiance = light.color.rgb * light.color.a * attenuation;

    // Cook-Torrance BRDF
    float NDF = DistributionGGX(N, H, roughness); // 法线分布
    float G = GeometrySmith(N, V, L, roughness);  // 几何遮蔽
    float3 F = fresnelSchlick(max(dot(H, V), 0.0f), F0); // 菲涅耳

    float3 nominator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0f) * max(dot(N, L), 0.0f) + 0.001f;
    float3 specular = nominator / denominator;

    // 漫反射
    float3 kS = F;
    float3 kD = float3(1.0f, 1.0f, 1.0f) - kS;
    kD *= 1.0f - metallic;

    float NdotL = max(dot(N, L), 0.0f);
    float3 Lo = (kD * albedo / PI + specular) * radiance * NdotL;

    // 环境光（近似处理）
    float3 ambient = float3(0.03f, 0.03f, 0.03f) * albedo * ao;
    float3 color = ambient + Lo;

    // HDR映射 
    float3 HDR_Map_Param = float3(1.0f, 1.0f, 1.0f);
    color = color / (color + HDR_Map_Param);
    // gamma校正
    float gamma_correct_param = 2.2f;
    color = pow(color, float3(1.0f / gamma_correct_param, 1.0f / gamma_correct_param, 1.0f / gamma_correct_param));

    return float4(color, 1.0f) * GlobalUBO.light_intensity;
}

// 阴影计算(3x3 PCF)
float CalculateShadow(float3 worldPosition, float3 worldNormal)
{
    float4 lightSpacePos = mul(GlobalUBO.light_space_matrix, float4(worldPosition, 1.0));
    float3 proj = lightSpacePos.xyz / lightSpacePos.w;
    proj = proj * 0.5 + 0.5;                       // NDC -> [0,1]
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0) return 0.0;
    float bias = max(GlobalUBO.shadow_bias * (1.0 - dot(worldNormal, normalize(-GlobalUBO.light_direction.xyz))), 0.0015);
    float shadow = 0.0;
    float2 texelSize = 1.0 / float2(2048.0, 2048.0);
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float closest = Samplers[SAMP_SHADOW].Sample(DefaultSampler[SAMP_SHADOW], proj.xy + float2(x, y) * texelSize).r;
            shadow += (proj.z - bias > closest) ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;                            // 3x3 PCF
}

// 方向光计算
float4 CalculateDirectionalLight(DirectionalLight light, float3 normal, float3 view_direction, float3 albedo, float metallic, float roughness, float4 ambient_color)
{
    float fDiffuseFactor = max(dot(normal, -light.direction), 0.0f);
    
    float3 HalfDirection = normalize(-view_direction - light.direction);
    float SpecularFactor = pow(max(dot(HalfDirection, normal), 0.0f), lerp(128.0, 1.0, roughness));
    
    float4 Ambient = float4(ambient_color.rgb * albedo, 1.0);
    float4 Diffuse = float4(albedo * fDiffuseFactor, 1.0);
    float4 Specular = float4(light.color.rgb * SpecularFactor * (1.0 - metallic), 1.0);
    
    return (Ambient + Diffuse + Specular) * GlobalUBO.light_intensity;
}

// 点光源计算
float4 CalculatePointLight(PointLight light, float3 normal, float3 frag_position, float3 view_direction, float3 albedo, float metallic, float roughness, float4 ambient_color)
{
    float3 LightDirection = normalize(light.position - frag_position);
    float Diff = max(dot(normal, LightDirection), 0.0f);
    
    float3 ReflectDirection = reflect(-LightDirection, normal);
    float Spec = pow(max(dot(view_direction, ReflectDirection), 0.0f), lerp(128.0, 1.0, roughness));
    
    // 计算衰减
    float Distance = length(light.position - frag_position);
    float Attenuation = 1.0f / (light.fconstant + light.linear_attenuation * Distance + light.quadratic * (Distance * Distance));
    
    float4 Ambient = ambient_color;
    float4 Diffuse = light.color * Diff;
    float4 Specular = light.color * Spec * (1.0 - metallic);
    
    Diffuse *= float4(albedo, 1.0);
    Ambient *= float4(albedo, 1.0);
    
    // 应用衰减
    Diffuse *= Attenuation;
    Ambient *= Attenuation;
    Specular *= Attenuation;
    
    return (Ambient + Diffuse + Specular) * GlobalUBO.light_intensity;
}

// 片段着色器主函数
PSOutput main(PSInput input)
{
    PSOutput output;
    
    // 从G-Buffer采样数据
    float4 albedoMetallic = Samplers[SAMP_ALBEDO].Sample(DefaultSampler[SAMP_ALBEDO], input.vTexcoord);
    float4 normalRoughness = Samplers[SAMP_NORMAL].Sample(DefaultSampler[SAMP_NORMAL], input.vTexcoord);
    float4 positionDepth = Samplers[SAMP_POSITION].Sample(DefaultSampler[SAMP_POSITION], input.vTexcoord);
    
    // 解码G-Buffer数据
    float3 albedo = albedoMetallic.rgb;
    float metallic = albedoMetallic.a;
    
    float3 worldNormal = normalize(normalRoughness.rgb * 2.0 - 1.0); // 解码法线
    float roughness = normalRoughness.a;
    
    float3 worldPosition = positionDepth.rgb;
    float depth = positionDepth.a;
    

    // 早期深度测试 - 如果深度为0说明没有几何体
    if (depth == 0.0)
    {
        discard;
    }
    
    // 根据模式渲染不同内容
    if (InstanceUBO.debug_mode == 1)
    {
        // 法线可视化
        output.FragColor = float4(worldNormal * 0.5 + 0.5, 1.0);
    }
    else if (InstanceUBO.debug_mode == 2)
    {
        // 材质属性可视化
        output.FragColor = float4(metallic, roughness, 0.0, 1.0);
    }
    else if (InstanceUBO.debug_mode == 3)
    {
        // 深度可视化
        output.FragColor = float4(depth, depth, depth, 1.0);
    }
    else if (InstanceUBO.debug_mode == 4)
    {
        // 反照率可视化
        output.FragColor = float4(albedo, 1.0);
    }
    else
    {
        // 标准光照计算
        float3 viewDirection = normalize(input.view_position - worldPosition);

        // 方向光：参数来源于光照 Actor（GlobalUBO），不再使用硬编码常量
        DirectionalLight dir_light;
        dir_light.direction = normalize(GlobalUBO.light_direction.xyz);
        dir_light.color = GlobalUBO.light_color;

        // PBR光照计算
        output.FragColor = PBR(point_light_0, worldNormal, albedo, input.view_position, worldPosition, metallic, roughness, 1.0);
        
        // 方向光阴影(仅作用于方向光分量)，强度由光照 Actor 配置的 shadow_strength 缩放
        float shadow = CalculateShadow(worldPosition, worldNormal) * clamp(GlobalUBO.shadow_strength, 0.0, 1.0);
        
        // 添加方向光
        output.FragColor += CalculateDirectionalLight(dir_light, worldNormal, viewDirection, albedo, metallic, roughness, input.ambient_color) * (1.0 - shadow);
        
        // 添加第二个点光源
        output.FragColor += CalculatePointLight(point_light_1, worldNormal, worldPosition, viewDirection, albedo, metallic, roughness, input.ambient_color);
        
        // 应用环境光
        float3 ambient = input.ambient_color.rgb * albedo;
        output.FragColor.rgb += ambient;
        
        // 确保alpha为1
        output.FragColor.a = 1.0;
    }
    
    return output;
}