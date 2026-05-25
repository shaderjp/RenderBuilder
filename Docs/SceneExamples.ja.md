# シーン例とスクリーンショット

このページでは、RenderBuilder の LookDev / shader preview を大型 scene で確認するときの見どころと、現在のスクリーンショットをまとめます。

## Large Scene Gallery

glTF の Sponza 系 scene と Bistro_v5_2 の FBX scene を使い、Viewport、Material Inspector、Scene / Asset Browser、Renderer Stats を並べて確認しています。

![Sponza / Bistro large scene preview](<../images/RenderBuilder Shader Editor - _Untitled 2026_05_25 22_05_21.png>)

![Sponza / Bistro material and texture preview](<../images/RenderBuilder Shader Editor - _Untitled 2026_05_25 22_07_33.png>)

![Sponza / Bistro LookDev preview](<../images/RenderBuilder Shader Editor - _Untitled 2026_05_25 22_08_25.png>)

## glTF Sponza

Sponza のような glTF / GLB scene は、Assimp 経由で mesh、material、texture path を import します。読み込み後は Scene / Asset Browser の geometry count と diagnostics、Material Inspector の texture slot を確認します。

確認するポイント:

| 項目 | 確認内容 |
| --- | --- |
| Scene import | vertex / index / draw / material count が表示されること。 |
| Texture path | glTF から参照される texture が missing になっていないこと。 |
| Material slots | baseColor / normal / roughness / metallic / occlusion / emissive が意図した slot に入っていること。 |
| Display Mode | BaseColor、Normal、Roughness、Metallic、AO を切り替えて texture channel を確認すること。 |
| Camera | large scene では Home で camera を scene bounds に合わせてから orbit / pan / dolly します。 |

## Bistro_v5_2 FBX

`GrapicsSample/Bistro_v5_2` は git 管理対象外の参照 asset として置いています。RenderBuilder 側では次の3つの FBX を想定しています。

| Scene | 用途 |
| --- | --- |
| `BistroExterior.fbx` | 屋外 Bistro。material 数が多く、texture descriptor と load diagnostics の確認に向きます。 |
| `BistroInterior.fbx` | 屋内 Bistro。密な geometry と多数の material を使った preview 確認に向きます。 |
| `BistroInterior_Wine.fbx` | wine glass など追加 material を含む variant です。 |

Bistro_v5_2 の `Textures/*.dds` は GGX metal-rough PBR 前提です。`BaseColor`、`Normal`、`Emissive` は名前どおりの用途で、`Specular` DDS は通常の specular color ではなく packed ORM として扱います。

| Bistro texture | RenderBuilder の扱い |
| --- | --- |
| `*_BaseColor.dds` | Base Color。alpha は opacity 用。 |
| `*_Normal.dds` | DirectX normal map。通常は `Flip Normal Green` を off のまま使います。 |
| `*_Specular.dds` | Packed ORM。`R=Occlusion`、`G=Roughness`、`B=Metalness` として自動検出します。 |
| `*_Emissive.dds` | Emissive color。 |

実装上の対応:

| 対応 | 内容 |
| --- | --- |
| Packed ORM auto detect | FBX material の `Specular` texture だけが存在する場合、roughness / metallic / occlusion に同じ DDS を割り当て、Material Inspector の `Packed ORM` を有効にします。 |
| Descriptor capacity | Bistro Exterior の material 数を超えられるよう、D3D12 material texture descriptor の上限を拡張しています。 |
| Texture cache | 同じ DDS を複数 slot で参照する場合、texture resource は再利用し、SRV だけを各 slot に作ります。 |
| Diagnostics | packed ORM 検出数、material texture load count、unique uploaded material texture count を diagnostics で確認できます。 |

## Large Scene Checklist

大型 scene を開いたら、まず次を順番に確認します。

1. Compile Diagnostics に scene import と texture load の結果が出ていること。
2. Renderer Stats で draw count、frame time、preview target size を確認すること。
3. Material Inspector で `Packed ORM`、normal strength、green channel flip が scene に合っていること。
4. Display Mode を `BaseColor`、`Normal`、`Roughness`、`Metallic`、`AO` に切り替えて channel の破綻がないこと。
5. Project JSON に保存し、開き直して scene / camera / material / texture / shader / environment が復元されること。

## Known Limits

現在の texture upload は scene load 時にまとめて走ります。Sponza / Bistro 規模では読み込みが重くなるため、今後は progressive upload、cancelable load、texture streaming、material search の強化を進める予定です。
