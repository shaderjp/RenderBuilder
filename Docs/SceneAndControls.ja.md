# シーンファイルと基本操作

このページでは、RenderBuilder でシーンファイルを読み込み、Viewport で確認しながら shader / material / LookDev 設定を調整する基本操作を説明します。

Sponza / Bistro など大型 scene のスクリーンショットと確認ポイントは [シーン例とスクリーンショット](SceneExamples.ja.md) に分けています。

![モデル読み込み後の全体画面](<../images/RenderBuilder Shader Editor 2026_05_24 23_24_17.png>)

## 画面構成

RenderBuilder は Dear ImGui Docking ベースの UI です。各 panel は tab をドラッグして配置を変えられます。
各 panel は tab の閉じるボタンで閉じられます。閉じた panel は menu bar の `Window` メニューから再表示できます。`Window > Show All` を選ぶと、すべての panel をまとめて表示します。

![Bistro scene with docked panels and AI Chat](<../images/RenderBuilder Shader Editor 2026_06_04 23_12_38.png>)

| Panel | 主な用途 |
| --- | --- |
| Viewport | D3D12 preview target を表示し、camera 操作や turntable preview を行います。 |
| Scene / Asset Browser | project root、読み込み中の scene、geometry 情報、sky / environment / view setting、scene/project 操作を表示します。 |
| Material Inspector | material ごとの shader set、PBR factor、texture slot、normal map 設定を編集します。 |
| Shader Editor | HLSL shader set の source、entry point、compile 操作を管理します。 |
| Compile Diagnostics | shader compile、scene import、texture load、project load/save の結果を表示します。 |
| Renderer Stats | backend、adapter、frame time、preview target size、feature support、active environment などを確認します。 |
| Automation | local MCP control bridge の状態と最後の command を確認します。 |
| AI Chat | local `llama-server` / GGUF model の状態、prompt、AI action の履歴を表示します。 |

![組み込みCubeの初期表示](<../images/RenderBuilder Shader Editor 2026_05_24 23_22_59.png>)

## 対応ファイル

### Scene

`File > Open Scene...` または `Scene / Asset Browser` の `Load Scene...` から scene file を開きます。
現在の主な対象は Assimp 経由の glTF / GLB、FBX、OBJ です。

読み込みに成功すると、Viewport が読み込んだ mesh に更新され、Scene / Asset Browser に vertex / index / draw / material 数が表示されます。読み込みに失敗した場合や参照 asset が見つからない場合は、Compile Diagnostics と Scene / Asset Browser の diagnostics に内容が表示されます。

### Texture

Material texture は DirectXTex 経由で読み込みます。DDS、TGA、HDR と、WIC 対応画像形式の PNG / JPEG / BMP / TIFF などを扱えます。

Material Inspector では、material ごとに次の texture slot を設定できます。

| Slot | 用途 |
| --- | --- |
| Base Color | albedo / base color |
| Normal | tangent-space normal map |
| Roughness | roughness map |
| Metallic | metallic map |
| Occlusion | ambient occlusion map |
| Emissive | emissive map |

imported material が texture を持っている場合は、その texture が既定で使われます。slot の override を有効にすると、project 側で選んだ texture path が優先されます。

## Scene の開き方

1. `File > Open Scene...` を選びます。
2. `.gltf`、`.glb`、`.fbx`、`.obj` のいずれかを選びます。
3. 読み込み後、Viewport に model が表示されます。
4. Scene / Asset Browser で geometry count と diagnostics を確認します。
5. Material Inspector で material ごとの texture / shader assignment を確認します。

scene を開いていない場合は、組み込み preview cube が表示されます。shader や camera 操作の確認だけであれば、この状態でも利用できます。

## Asset Browser

Scene / Asset Browser の `Assets` section では、`Assets/`、`Shaders/`、現在の project / scene 周辺、Project JSON が参照している asset を一覧できます。

| 操作 | 動作 |
| --- | --- |
| Refresh Assets | asset 一覧を再スキャンします。 |
| Filter | Models / Textures / HDRI / Shaders / Projects / Missing で表示を絞り込みます。 |
| Search | file name、path、source label を検索します。 |
| Load Model | 選択した model asset を scene として読み込みます。 |
| Use as HDRI | 選択した HDRI / DDS asset を environment texture として読み込みます。 |
| Load Into Active Shader | 選択した HLSL を active shader set に読み込み、compile します。 |
| Open Project | 選択した `.renderbuilder.json` を開きます。 |
| Assign Texture Slot | 選択した texture asset を指定 material / texture slot の override として割り当てます。 |

missing asset は asset 一覧上で赤く表示され、`Missing` filter でまとめて確認できます。Project JSON を開いたときに見つからない scene / texture / HDRI / shader source も、ここから path 単位で確認できます。

## Viewport 操作

Viewport 内の描画領域に mouse cursor があるとき、または Viewport が active/focused のときだけ camera 操作が反映されます。左 drag で model を回転しても、Viewport の canvas 外であれば ImGui window の移動が優先されます。

| 操作 | 動作 |
| --- | --- |
| 左 drag | orbit camera |
| 中 drag / 右 drag | pan camera |
| mouse wheel | dolly / zoom |
| W / S | forward / backward |
| A / D | left / right |
| Q / E | down / up |
| Shift + WASDQE | camera move を高速化 |
| Home | camera を scene に合わせて reset |

camera state は Project JSON に保存されます。project を開き直すと、最後に保存した target / yaw / pitch / distance が復元されます。

## Material と Shader の割り当て

Material Inspector には imported material ごとの section が表示されます。

`Shader` combo で、その material に割り当てる shader set を選びます。`LookDev PBR` は標準の PBR preview 用 shader set です。独自 HLSL を使う場合は Shader Editor で shader set を作成し、material 側で選択します。

Material Inspector では次の値を編集できます。

| 項目 | 説明 |
| --- | --- |
| Base Color Factor | texture と乗算される base color / alpha です。 |
| Roughness Factor | roughness texture がない場合、または texture 値と合わせて使う roughness です。 |
| Metallic Factor | metallic texture がない場合、または texture 値と合わせて使う metallic です。 |
| Occlusion Strength | occlusion texture の効きを調整します。 |
| Emissive Color / Intensity | emissive の色と強度です。 |
| Alpha Mode / Alpha Cutoff | Opaque / Mask / Blend と mask threshold です。 |
| Normal Strength | normal map の強さです。 |
| Flip Normal Green | DirectX / OpenGL 系 normal map の Y 向き違いを補正します。 |
| Packed ORM | 1枚の texture に occlusion / roughness / metallic が詰められた map を使います。`R=AO`、`G=Roughness`、`B=Metallic` として評価されます。Bistro_v5_2 の `Specular` DDS はこの形式として自動検出されます。 |

Shader compile に失敗した場合、Viewport は最後に成功した PSO を維持します。エラー内容は Compile Diagnostics に表示されるため、preview を壊さずに shader を修正できます。

## Shader Editor の基本

Shader Editor では、active shader set の HLSL source、VS / PS entry、VS / PS profile、material への shader set 割り当てを編集できます。

| 操作 | 動作 |
| --- | --- |
| `Build > Compile Shader` | active shader set を compile します。 |
| `Build > Compile All Shader Sets` | すべての shader set を compile します。 |
| `Ctrl+Enter` | active shader set を compile します。 |
| `Compile` button | active shader set を compile します。 |
| `Compile All` button | project 内の shader set をまとめて compile します。 |
| `Open Shader...` | HLSL file を読み込みます。 |
| `Reload Default` | default raster shader を読み込みます。 |
| `Reload LookDev PBR` | LookDev PBR shader を読み込みます。 |
| `New` | 新しい shader set を作成します。 |
| `Duplicate` | active shader set を複製し、memory source として編集できる状態にします。 |
| `Delete` | active shader set を削除します。使用中の material は別の shader set に退避されます。 |

`Shader Set Manager` では各 shader set の compile 結果、last-good PSO の有無、使用 material 数、VS / PS profile を一覧できます。`Material Shader Assignments` では material ごとに shader set を切り替えられ、現在の PSO が active / last-good / fallback のどれで描画されるかを確認できます。

初期 entry point は `VSMain` / `PSMain` です。標準 ABI を使う shader は `Shaders/RenderBuilderShaderABI.hlsli` を include します。

## LookDev 設定

Scene / Asset Browser では Viewport の背景、lighting、tone mapping を調整できます。

| 項目 | 説明 |
| --- | --- |
| LookDev Preset | sky、environment、sun、tone mapping、display mode をまとめて切り替えます。初期 preset は `Default Studio`、`Neutral Gray`、`Outdoor HDRI` です。 |
| Save Current Preset | 現在の LookDev 設定を名前付き preset として保存します。 |
| Sky Top / Sky Horizon | SkyColor 背景の上端色と地平線色です。 |
| Load HDRI... | `.hdr` / `.dds` environment texture を読み込みます。 |
| Clear HDRI | 現在の HDRI を解除します。 |
| Background | `SkyColor`、`HDRI Background`、`Transparent Checker` を切り替えます。 |
| HDRI Rotation | environment の yaw 回転です。 |
| Environment Intensity | HDRI lighting / background の強度です。 |
| Sun Direction | sun light の向きです。 |
| Sun Color / Sun Intensity | sun light の色と強度です。 |
| Exposure | Viewport 表示の露出です。 |
| Gamma | display gamma です。 |
| Tone Mapper | `None`、`Reinhard`、`ACES` を切り替えます。 |
| Display Mode | Beauty / BaseColor / Normal / Roughness / Metallic / AO / Emissive / LightingOnly を切り替えます。 |
| Turntable | 自動 orbit preview を有効にします。 |
| Snapshot... | 現在の Viewport を画像として保存します。 |

HDRI の読み込みに失敗した場合や project 内の HDRI path が見つからない場合は、diagnostics を表示し、fallback environment または最後に成功した environment を維持します。

## Project JSON

`File > Save Project` または `Ctrl+S` で `.renderbuilder.json` project を保存します。`Save Project As...` では保存先を選べます。

Project JSON には主に次の状態が保存されます。

| 保存対象 | 内容 |
| --- | --- |
| Scene | `scenePath` |
| Camera | `viewportCamera.target`、`yaw`、`pitch`、`distance` |
| Sky / Environment | sky color、HDRI path、rotation、intensity、background mode、sun setting |
| View Settings | exposure、tone mapper、gamma、display mode、turntable |
| LookDev Presets | active preset と、名前付き LookDev preset 配列 |
| Shader | active shader set、source path、source text、entry point、profile |
| Materials | material name、shader set assignment、PBR factors、texture slot override、normal setting、alpha setting |

path は project file からの相対 path として保存されます。project file と asset folder をまとめて移動する場合も、相対関係が維持されていれば復元できます。

project を開くと、scene、material、texture、shader、camera、sky / environment、view settings が復元されます。missing scene、missing texture、missing HDRI がある場合は diagnostics に表示され、可能な範囲で fallback して起動します。

Project JSON の概略は次のような形です。

```json
{
  "backend": "D3D12",
  "scenePath": "Models/DamagedHelmet.gltf",
  "viewportCamera": {
    "target": [0.0, 0.0, 0.0],
    "yaw": 0.0,
    "pitch": 0.12,
    "distance": 4.0
  },
  "lookDevEnvironment": {
    "environmentPath": "Environments/studio.hdr",
    "backgroundMode": "Hdri"
  },
  "shaderSets": [],
  "materials": []
}
```

## トラブルシュート

| 症状 | 確認すること |
| --- | --- |
| scene が表示されない | Compile Diagnostics と Scene / Asset Browser の diagnostics を確認してください。asset path、format、texture の missing が表示されます。 |
| texture が反映されない | Material Inspector で対象 slot の override 状態と path を確認してください。 |
| normal map が反転して見える | `Flip Normal Green` を切り替えてください。 |
| shader compile 後に見た目が変わらない | Compile Diagnostics で compile 成功/失敗を確認してください。失敗時は最後に成功した PSO が維持されます。 |
| Viewport の camera が動かない | mouse cursor が Viewport の描画領域上にあるか、Viewport window が focused かを確認してください。 |

## 関連ドキュメント

| Document | 内容 |
| --- | --- |
| [シーン例とスクリーンショット](SceneExamples.ja.md) | glTF Sponza / Bistro_v5_2 の大型 scene preview、packed ORM、large scene checklist。 |
