#ifndef RENDERBUILDER_SHADER_ABI_HLSLI
#define RENDERBUILDER_SHADER_ABI_HLSLI

cbuffer RenderBuilderScene : register(b0)
{
    float4x4 gModelViewProjection;
    float4x4 gModel;
    float4 gCameraPositionTime;
    float4 gLightDirectionIntensity;
};

cbuffer RenderBuilderMaterial : register(b1)
{
    float4 gBaseColorFactor;
    uint gMaterialTextureMask;
    float gNormalStrength;
    float gNormalGreenScale;
    float gRoughnessFactor;
    float gMetallicFactor;
    float3 gMaterialPadding;
};

static const uint RB_TEXTURE_BASE_COLOR = 1u << 0;
static const uint RB_TEXTURE_NORMAL = 1u << 1;
static const uint RB_TEXTURE_ROUGHNESS = 1u << 2;
static const uint RB_TEXTURE_METALLIC = 1u << 3;

Texture2D gBaseColorTexture : register(t0);
Texture2D gNormalTexture : register(t1);
Texture2D gRoughnessTexture : register(t2);
Texture2D gMetallicTexture : register(t3);
SamplerState gLinearWrapSampler : register(s0);

struct RBVertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
    float4 tangent : TANGENT;
};

struct RBPixelInput
{
    float4 position : SV_Position;
    float3 worldNormal : NORMAL;
    float2 texcoord : TEXCOORD0;
    float3 worldPosition : TEXCOORD1;
    float3 worldTangent : TANGENT;
    float tangentSign : TEXCOORD2;
};

#endif
