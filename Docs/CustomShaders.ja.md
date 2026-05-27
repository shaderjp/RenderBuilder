# 独自シェーダの作り方

このページでは、RenderBuilder で独自 HLSL shader set を作るための基本手順と、`Shaders/RenderBuilderShaderABI.hlsli` が提供する ABI を説明します。

現在の独自シェーダ対応は Raster VS/PS が対象です。LookDev PBR と Shadow は D3D12 backend 側の共通 state / resource を使っており、独自 pixel shader からも同じ ABI を参照できます。ただし shadow map の caster pass は RenderBuilder 内部の標準 pass で生成されます。

## 導入手順

1. `Shaders/RenderBuilderShaderABI.hlsli` を include します。
2. entry point は既定で `VSMain` / `PSMain` にします。
3. profile は既定で `vs_6_9` / `ps_6_9` を使います。
4. Shader Editor で HLSL を編集し、`Compile Active` で runtime compile します。
5. Material Inspector で material ごとに shader set を割り当てます。

最小構成:

```hlsl
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
    const bool hasBaseColor = (gMaterialTextureMask & RB_TEXTURE_BASE_COLOR) != 0;
    const float3 textureColor = hasBaseColor
        ? gBaseColorTexture.Sample(gLinearWrapSampler, input.texcoord).rgb
        : float3(0.75, 0.72, 0.68);
    return float4(textureColor * gBaseColorFactor.rgb, gBaseColorFactor.a);
}
```

## 入出力構造体

`RBVertexInput` は RenderBuilder の mesh vertex layout です。

| Field | Semantic | 内容 |
| --- | --- | --- |
| `position` | `POSITION` | object/local position |
| `normal` | `NORMAL` | object/local normal |
| `texcoord` | `TEXCOORD0` | UV |
| `tangent` | `TANGENT` | xyz が tangent、w が bitangent sign |

`RBPixelInput` は標準 shader が使う VS -> PS の受け渡しです。

| Field | 内容 |
| --- | --- |
| `position` | clip space position / `SV_Position` |
| `worldNormal` | world space normal |
| `texcoord` | UV |
| `worldPosition` | world space position |
| `worldTangent` | world space tangent |
| `tangentSign` | bitangent sign |

独自 shader でも `RBPixelInput` を使うと、Material texture、normal map、shadow、LookDev lighting を扱いやすくなります。

## Constant Buffer

### `RenderBuilderScene` / `b0`

| Field | 内容 |
| --- | --- |
| `gModelViewProjection` | object/local から clip space への行列 |
| `gModel` | object/local から world space への行列 |
| `gViewProjectionInverse` | Viewport 背景や ray direction 用の inverse view-projection |
| `gCameraPositionTime.xyz` | camera world position |
| `gCameraPositionTime.w` | elapsed time |
| `gLightDirectionIntensity.xyz` | Sun direction |
| `gLightDirectionIntensity.w` | Sun intensity |
| `gShadowViewProjection` | world space から Sun shadow clip space への行列 |

### `RenderBuilderMaterial` / `b1`

| Field | 内容 |
| --- | --- |
| `gBaseColorFactor` | material base color factor。a は alpha |
| `gMaterialTextureMask` | 有効な texture slot の bit mask |
| `gNormalStrength` | normal map の強さ |
| `gNormalGreenScale` | normal map Y。green flip 有効時は `-1` |
| `gRoughnessFactor` | roughness factor |
| `gMetallicFactor` | metallic factor |
| `gOcclusionStrength` | AO の強さ |
| `gAlphaCutoff` | Mask alpha の threshold |
| `gAlphaMode` | `0=Opaque`、`1=Mask`、`2=Blend` |
| `gEmissiveFactor` | emissive RGB と強度 a |
| `gPackedOcclusionRoughnessMetallic` | packed ORM 使用時 `1` |

Texture mask の bit:

| Bit | Slot |
| --- | --- |
| `RB_TEXTURE_BASE_COLOR` | Base Color |
| `RB_TEXTURE_NORMAL` | Normal |
| `RB_TEXTURE_ROUGHNESS` | Roughness |
| `RB_TEXTURE_METALLIC` | Metallic |
| `RB_TEXTURE_OCCLUSION` | Occlusion |
| `RB_TEXTURE_EMISSIVE` | Emissive |

### `RenderBuilderLookDev` / `b2`

| Field | 内容 |
| --- | --- |
| `gSunColorIntensity.rgb` | Sun color |
| `gSunColorIntensity.a` | Sun intensity |
| `gEnvironmentOptions.x` | HDRI rotation yaw |
| `gEnvironmentOptions.y` | environment intensity |
| `gEnvironmentOptions.z` | background mode |
| `gEnvironmentOptions.w` | environment texture が有効なら `1` |
| `gViewOptions.x` | exposure |
| `gViewOptions.y` | gamma |
| `gViewOptions.z` | tone mapper |
| `gViewOptions.w` | display mode |
| `gIblOptions.x` | environment texture の最大 mip |
| `gIblOptions.w` | IBL intensity 用の内部値 |
| `gSkyTopColor` | SkyColor top |
| `gSkyHorizonColor` | SkyColor horizon |
| `gShadowOptions.x` | shadow enabled |
| `gShadowOptions.y` | shadow strength |
| `gShadowOptions.z` | shadow bias |
| `gShadowOptions.w` | PCF softness の UV step |

互換性のため、Sun intensity は `gSunColorIntensity.a` と `gLightDirectionIntensity.w` の両方に入っています。LookDevPBR と同じ direct lighting に寄せる場合は、現状の標準 shader と同じく `gSunColorIntensity.rgb * gSunColorIntensity.a * gLightDirectionIntensity.w` を使ってください。

## Texture / Sampler Binding

| Resource | Register | 内容 |
| --- | --- | --- |
| `gBaseColorTexture` | `t0` | Base Color |
| `gNormalTexture` | `t1` | Normal |
| `gRoughnessTexture` | `t2` | Roughness |
| `gMetallicTexture` | `t3` | Metallic |
| `gOcclusionTexture` | `t4` | Occlusion |
| `gEmissiveTexture` | `t5` | Emissive |
| `gEnvironmentTexture` | `t6` | HDRI / environment texture |
| `gShadowTexture` | `t7` | Sun shadow map |
| `gLinearWrapSampler` | `s0` | material / environment sampling |
| `gShadowComparisonSampler` | `s1` | shadow map comparison sampling |

texture slot が未設定の場合も fallback texture が bound されています。ただし意図しない sampling を避けるため、必ず `gMaterialTextureMask` で slot の有無を確認してください。

## Normal Map

tangent-space normal を使う場合は、`gNormalStrength` と `gNormalGreenScale` を反映します。

```hlsl
float3 BuildShadingNormal(RBPixelInput input)
{
    const float3 vertexNormal = normalize(input.worldNormal);
    const float3 tangentCandidate = input.worldTangent - vertexNormal * dot(vertexNormal, input.worldTangent);
    const float3 tangent = tangentCandidate * rsqrt(max(dot(tangentCandidate, tangentCandidate), 1.0e-6));
    const float3 bitangent = normalize(cross(vertexNormal, tangent) * input.tangentSign);

    float3 sampledNormal = gNormalTexture.Sample(gLinearWrapSampler, input.texcoord).xyz * 2.0 - 1.0;
    sampledNormal.xy *= float2(gNormalStrength, gNormalStrength * gNormalGreenScale);
    sampledNormal = normalize(sampledNormal);

    const bool hasNormal = (gMaterialTextureMask & RB_TEXTURE_NORMAL) != 0;
    return hasNormal
        ? normalize(sampledNormal.x * tangent + sampledNormal.y * bitangent + sampledNormal.z * vertexNormal)
        : vertexNormal;
}
```

glTF/OpenGL 系と DirectX 系で normal map の Y 向きが違う場合は、Material Inspector の `Flip Normal Green` で `gNormalGreenScale` が切り替わります。

## Shadow の使い方

shadow map は RenderBuilder の Sun direction と scene bounds から生成されます。独自 shader では `gShadowViewProjection`、`gShadowTexture`、`gShadowComparisonSampler`、`gShadowOptions` を使って shadow visibility を求めます。

```hlsl
float SampleSunShadow(float3 worldPosition, float3 normal)
{
    if (gShadowOptions.x <= 0.5)
    {
        return 1.0;
    }

    const float4 shadowPosition = mul(float4(worldPosition, 1.0), gShadowViewProjection);
    const float3 shadowNdc = shadowPosition.xyz / max(shadowPosition.w, 1.0e-6);
    const float2 shadowUv = shadowNdc.xy * float2(0.5, -0.5) + float2(0.5, 0.5);
    if (shadowUv.x < 0.0 || shadowUv.x > 1.0 || shadowUv.y < 0.0 || shadowUv.y > 1.0 || shadowNdc.z < 0.0 || shadowNdc.z > 1.0)
    {
        return 1.0;
    }

    const float3 lightDirection = normalize(-gLightDirectionIntensity.xyz);
    const float slopeBias = gShadowOptions.z * lerp(2.5, 0.75, saturate(dot(normal, lightDirection)));
    const float compareDepth = saturate(shadowNdc.z - slopeBias);
    const float stepUv = max(gShadowOptions.w, 1.0 / 4096.0);

    float visibility = 0.0;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            visibility += gShadowTexture.SampleCmpLevelZero(
                gShadowComparisonSampler,
                shadowUv + float2(x, y) * stepUv,
                compareDepth);
        }
    }
    visibility /= 9.0;
    return lerp(1.0, visibility, saturate(gShadowOptions.y));
}
```

direct sun lighting には、ambient / IBL ではなく Sun light の項だけに shadow を掛けるのが基本です。

```hlsl
const float3 normal = BuildShadingNormal(input);
const float3 lightDirection = normalize(-gLightDirectionIntensity.xyz);
const float nDotL = saturate(dot(normal, lightDirection));
const float shadow = SampleSunShadow(input.worldPosition, normal);
const float3 sunRadiance = gSunColorIntensity.rgb * gSunColorIntensity.a * gLightDirectionIntensity.w;
const float3 directLighting = baseColor * sunRadiance * nDotL * shadow;
```

Shadow caster pass は内部 shader が担当します。alpha mask material は base color alpha と `gAlphaCutoff` で cutout shadow になり、Blend material は v1 では shadow caster から除外されます。

## LookDev 表示モード

`gViewOptions.w` には Display Mode が入ります。debug view を独自 shader 側でも合わせたい場合は、次の定数を使います。

| Constant | 内容 |
| --- | --- |
| `RB_DISPLAY_BEAUTY` | 通常表示 |
| `RB_DISPLAY_BASE_COLOR` | base color |
| `RB_DISPLAY_NORMAL` | normal |
| `RB_DISPLAY_ROUGHNESS` | roughness |
| `RB_DISPLAY_METALLIC` | metallic |
| `RB_DISPLAY_AO` | ambient occlusion |
| `RB_DISPLAY_EMISSIVE` | emissive |
| `RB_DISPLAY_LIGHTING_ONLY` | lighting only |
| `RB_DISPLAY_SHADOW_MASK` | shadow visibility |

独自 shader が display mode を無視しても構いません。その場合、Viewport の Display Mode を変えても出力は shader 側の実装どおりになります。

## よくある注意点

- `RenderBuilderShaderABI.hlsli` の cbuffer / texture register は固定 ABI です。独自 shader で同じ register を別用途に使わないでください。
- VS は `RBPixelInput` に `worldPosition`、`worldNormal`、`worldTangent`、`tangentSign` を正しく入れてください。normal / shadow / LookDev lighting の計算に必要です。
- material texture は fallback が bound されますが、slot が本当にあるかは `gMaterialTextureMask` で判断してください。
- packed ORM を使う material では、roughness texture の `R/G/B` を `AO/Roughness/Metallic` として扱うのが RenderBuilder の LookDevPBR と同じ運用です。
- invalid shader compile 時は最後に成功した PSO が維持されます。Compile Diagnostics の DXC error を見ながら修正してください。
- v1 の Vulkan backend は未実装です。HLSL ABI は backend 非依存を意識していますが、現時点の実描画確認は D3D12 が基準です。
