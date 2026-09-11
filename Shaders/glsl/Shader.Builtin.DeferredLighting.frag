#version 450

layout (location = 0) out vec4 FragColor;

layout (set = 0, binding = 0, std140) uniform GlobalUniformObject{
    mat4 light_space_matrix;
    float global_time;
}GlobalUBO;

layout (set = 1, binding = 0) uniform InstanceUniformObject{
    vec4 light_intensity;
    int debug_mode;
}InstanceUBO;

const int SAMP_ALBEDO = 0;
const int SAMP_NORMAL = 1;
const int SAMP_POSITION = 2;
const int SAMP_SHADOW = 3;
const float SHADOW_BIAS = 0.005;
layout (set = 1, binding = 1) uniform sampler2D Samplers[4];

layout (location = 0) flat in float global_time;
layout (location = 1) in struct dto{
    vec2 vTexcoord;
    vec4 ambient_color;
    vec3 view_position;
}in_dto;

// 光源定义（与原World着色器保持一致）
struct DirectionalLight{
    vec3 direction;
    vec4 color;
};

struct PointLight{
    vec3 position;
    vec4 color;
    float fconstant;
    float linear;
    float quadratic;
};

DirectionalLight dir_light = DirectionalLight(
    vec3(-0.57735f, -0.57735f, -0.57735f),
    vec4(0.8f, 0.8f, 0.8f, 1.0f)
);

PointLight point_light_0 = PointLight(
    vec3(-5.5f, 10.0f, -5.5f),
    vec4(0.0f, 1.0f, 0.0f, 1.0f),
    1.0f,   // Constant
    0.35f,  // Linear
    0.44f   // Quadratic
);

PointLight point_light_1 = PointLight(
    vec3(5.5f, 10.0f, -5.5f),
    vec4(1.0f, 0.0f, 0.0f, 1.0f),
    1.0f,   // Constant
    0.35f,  // Linear
    0.44f   // Quadratic
);

// 函数声明
vec4 CalculateDirectionalLight(DirectionalLight light, vec3 normal, vec3 view_direction, vec3 albedo, float metallic, float roughness);
float CalculateShadow(vec3 worldPosition, vec3 worldNormal);
vec4 CalculatePointLight(PointLight light, vec3 normal, vec3 frag_position, vec3 view_direction, vec3 albedo, float metallic, float roughness);
vec4 PBR(PointLight light, vec3 norm, vec3 albedo, vec3 camPos, vec3 fragPos, float metallic, float roughness, float ao);

void main(){
    // 从G-Buffer采样数据 - 使用新的纹理名称
    vec4 albedoMetallic = texture(Samplers[SAMP_ALBEDO], in_dto.vTexcoord);
    vec4 normalRoughness = texture(Samplers[SAMP_NORMAL], in_dto.vTexcoord);
    vec4 positionDepth = texture(Samplers[SAMP_POSITION], in_dto.vTexcoord);
    
    // 解码G-Buffer数据
    vec3 albedo = albedoMetallic.rgb;
    float metallic = albedoMetallic.a;
    
    vec3 worldNormal = normalize(normalRoughness.rgb * 2.0 - 1.0); // 解码法线
    float roughness = normalRoughness.a;
    
    vec3 worldPosition = positionDepth.rgb;
    float depth = positionDepth.a;
    
    // 早期深度测试 - 如果深度为0说明没有几何体
    if (depth == 0.0) {
        discard;
    }
    
    // 根据模式渲染不同内容
    if (InstanceUBO.debug_mode == 1) {
        // 法线可视化
        FragColor = vec4(worldNormal * 0.5 + 0.5, 1.0);
    }
    else if (InstanceUBO.debug_mode == 2) {
        // 材质属性可视化
        FragColor = vec4(metallic, roughness, 0.0, 1.0);
    }
    else if (InstanceUBO.debug_mode == 3) {
        // 深度可视化
        FragColor = vec4(albedo, 1.0);
    }
    else if (InstanceUBO.debug_mode == 4) {
        // 反照率可视化
        FragColor = vec4(albedo, 1.0);
    }
    else {
        // 标准光照计算
        vec3 viewDirection = normalize(in_dto.view_position - worldPosition);
        
        // PBR光照计算
        FragColor = PBR(point_light_0, worldNormal, albedo, in_dto.view_position, worldPosition, metallic, roughness, 1.0);
        
        // 方向光阴影(仅作用于方向光分量)
        float shadow = CalculateShadow(worldPosition, worldNormal);

        // 添加方向光
        FragColor += CalculateDirectionalLight(dir_light, worldNormal, viewDirection, albedo, metallic, roughness) * (1.0 - shadow);

        // 添加第二个点光源
        //FragColor += CalculatePointLight(point_light_1, worldNormal, worldPosition, viewDirection, albedo, metallic, roughness);
        
        // 应用环境光
        vec3 ambient = in_dto.ambient_color.rgb * albedo;
        FragColor.rgb += ambient;
        
        // 确保alpha为1
        FragColor.a = 1.0;
    }
}

float CalculateShadow(vec3 worldPosition, vec3 worldNormal){
    vec4 lightSpacePos = GlobalUBO.light_space_matrix * vec4(worldPosition, 1.0);
    vec3 proj = lightSpacePos.xyz / lightSpacePos.w;
    proj = proj * 0.5 + 0.5;                       // NDC -> [0,1]
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0) return 0.0;
    float bias = max(SHADOW_BIAS * (1.0 - dot(worldNormal, normalize(-dir_light.direction))), 0.0015);
    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(2048.0);
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float closest = texture(Samplers[SAMP_SHADOW], proj.xy + vec2(x, y) * texelSize).r;
            shadow += (proj.z - bias > closest) ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;                            // 3x3 PCF
}

vec4 CalculateDirectionalLight(DirectionalLight light, vec3 normal, vec3 view_direction, vec3 albedo, float metallic, float roughness){
    float fDiffuseFactor = max(dot(normal, -light.direction), 0.0f);
    
    vec3 HalfDirection = normalize(-view_direction - light.direction);
    float SpecularFactor = pow(max(dot(HalfDirection, normal), 0.0f), mix(128.0, 1.0, roughness));
    
    vec4 Ambient = vec4(vec3(in_dto.ambient_color * vec4(albedo, 1.0)), 1.0);
    vec4 Diffuse = vec4(vec3(albedo * fDiffuseFactor), 1.0);
    vec4 Specular = vec4(vec3(light.color * SpecularFactor * (1.0 - metallic)), 1.0);
    
    return (Ambient + Diffuse + Specular) * InstanceUBO.light_intensity;
}

vec4 CalculatePointLight(PointLight light, vec3 normal, vec3 frag_position, vec3 view_direction, vec3 albedo, float metallic, float roughness){
    vec3 LightDirection = normalize(light.position - frag_position);
    float Diff = max(dot(normal, LightDirection), 0.0f);
    
    vec3 ReflectDirection = reflect(-LightDirection, normal);
    float Spec = pow(max(dot(view_direction, ReflectDirection), 0.0f), mix(128.0, 1.0, roughness));
    
    // 计算衰减
    float Distance = length(light.position - frag_position);
    float Attenuation = 1.0f / (light.fconstant + light.linear * Distance + light.quadratic * (Distance * Distance));
    
    vec4 Ambient = in_dto.ambient_color;
    vec4 Diffuse = light.color * Diff;
    vec4 Specular = light.color * Spec * (1.0 - metallic);
    
    Diffuse *= vec4(albedo, 1.0);
    Ambient *= vec4(albedo, 1.0);
    
    // 应用衰减
    Diffuse *= Attenuation;
    Ambient *= Attenuation;
    Specular *= Attenuation;
    
    return (Ambient + Diffuse + Specular) * InstanceUBO.light_intensity;
}

//////////////////////////////   PBR   ////////////////////////////////////////
// 常量
const float PI = 3.14159265359;

// Fresnel-Schlick 近似
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0f - F0) * pow(1.0f - cosTheta, 5.0f);
}

// GGX 分布函数
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0f);
    float NdotH2 = NdotH * NdotH;

    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0f) + 1.0f);
    denom = PI * denom * denom;

    return nom / denom;
}

// Smith 几何遮蔽函数
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0f);
    float k = (r * r) / 8.0f;

    float nom = NdotV;
    float denom = NdotV * (1.0f - k) + k;

    return nom / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0f);
    float NdotL = max(dot(N, L), 0.0f);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

vec4 PBR(PointLight light, vec3 norm, vec3 albedo, vec3 camPos, vec3 fragPos, float metallic, float roughness, float ao){
    // 视线方向和法线
    vec3 N = normalize(norm);
    vec3 V = normalize(camPos - fragPos);

    // 环境光的基本反射率
    vec3 F0 = vec3(0.04f); // 非金属表面的 F0 反射率
    F0 = mix(F0, albedo, metallic);

    // 计算光线方向和半程向量
    vec3 L = normalize(light.position - fragPos);
    vec3 H = normalize(V + L);

    // 计算距离衰减
    float distance = length(light.position - fragPos);
    float attenuation = 1.0 / (light.fconstant + light.linear * distance + light.quadratic * distance * distance);
    vec3 radiance = light.color.rgb * light.color.a * attenuation;

    // Cook-Torrance BRDF
    float NDF = DistributionGGX(N, H, roughness); // 法线分布
    float G = GeometrySmith(N, V, L, roughness);  // 几何遮蔽
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0f), F0); // 菲涅耳

    vec3 nominator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0f) * max(dot(N, L), 0.0f) + 0.001f;
    vec3 specular = nominator / denominator;

    // 漫反射
    vec3 kS = F;
    vec3 kD = vec3(1.0f) - kS;
    kD *= 1.0f - metallic;

    float NdotL = max(dot(N, L), 0.0f);
    vec3 Lo = (kD * albedo / PI + specular) * radiance * NdotL;

    // 环境光（近似处理）
    vec3 ambient = vec3(0.03f) * albedo * ao;
    vec3 color = ambient + Lo;

    // HDR 映射 
    vec3 HDR_Map_Param = vec3(1.0f);
    color = color / (color + HDR_Map_Param);
    // gamma 校正
    float gamma_correct_param = 2.2f;
    color = pow(color, vec3(1.0f / gamma_correct_param));

    return vec4(color, 1.0f) * InstanceUBO.light_intensity;
}