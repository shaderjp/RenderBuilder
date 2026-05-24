#include "RenderBuilderShaderABI.hlsli"

RBPixelInput VSMain(RBVertexInput input)
{
    RBPixelInput output;
    const float4 worldPosition = mul(float4(input.position, 1.0), gModel);
    output.position = mul(float4(input.position, 1.0), gModelViewProjection);
    output.worldNormal = normalize(mul(float4(input.normal, 0.0), gModel).xyz);
    output.worldTangent = normalize(mul(float4(input.tangent.xyz, 0.0), gModel).xyz);
    output.tangentSign = input.tangent.w;
    output.texcoord = input.texcoord;
    output.worldPosition = worldPosition.xyz;
    return output;
}

float4 PSMain(RBPixelInput input) : SV_Target0
{
    const float3 normal = normalize(input.worldNormal);
    const float3 tangentCandidate = input.worldTangent - normal * dot(normal, input.worldTangent);
    const float3 tangent = tangentCandidate * rsqrt(max(dot(tangentCandidate, tangentCandidate), 1.0e-6));
    const float3 bitangentCandidate = cross(normal, tangent) * input.tangentSign;
    const float3 bitangent = bitangentCandidate * rsqrt(max(dot(bitangentCandidate, bitangentCandidate), 1.0e-6));
    float3 sampledNormal = gNormalTexture.Sample(gLinearWrapSampler, input.texcoord).xyz * 2.0 - 1.0;
    sampledNormal.xy *= float2(gNormalStrength, gNormalStrength * gNormalGreenScale);
    sampledNormal = normalize(sampledNormal);
    const bool hasNormal = (gMaterialTextureMask & RB_TEXTURE_NORMAL) != 0;
    const float3 shadingNormal = hasNormal ? normalize(sampledNormal.x * tangent + sampledNormal.y * bitangent + sampledNormal.z * normal) : normal;
    const float3 lightDirection = normalize(-gLightDirectionIntensity.xyz);
    const float ndotl = saturate(dot(shadingNormal, lightDirection));
    const float checker = (fmod(floor(input.texcoord.x * 8.0) + floor(input.texcoord.y * 8.0), 2.0) == 0.0) ? 1.0 : 0.35;
    const float3 fallbackColor = lerp(float3(0.1, 0.38, 0.72), float3(0.9, 0.54, 0.18), checker);
    const float4 textureColor = gBaseColorTexture.Sample(gLinearWrapSampler, input.texcoord);
    const bool hasBaseColor = (gMaterialTextureMask & RB_TEXTURE_BASE_COLOR) != 0;
    const float3 authoredColor = hasBaseColor ? textureColor.rgb : fallbackColor;
    const float3 baseColor = authoredColor * gBaseColorFactor.rgb;
    const float roughnessSample = ((gMaterialTextureMask & RB_TEXTURE_ROUGHNESS) != 0) ? gRoughnessTexture.Sample(gLinearWrapSampler, input.texcoord).r : 1.0;
    const float metallicSample = ((gMaterialTextureMask & RB_TEXTURE_METALLIC) != 0) ? gMetallicTexture.Sample(gLinearWrapSampler, input.texcoord).r : 1.0;
    const float roughness = saturate(roughnessSample * gRoughnessFactor);
    const float metallic = saturate(metallicSample * gMetallicFactor);
    const float diffuse = 0.14 + ndotl * gLightDirectionIntensity.w * lerp(1.12, 0.72, saturate(roughness));
    const float3 color = lerp(baseColor * diffuse, baseColor * (0.08 + diffuse * 0.42), saturate(metallic));
    return float4(color, gBaseColorFactor.a);
}
