# AI Chat

RenderBuilder の AI Chat は、ローカルで起動した `llama-server` に OpenAI 互換 HTTP で問い合わせます。

## セットアップ

1. `llama.cpp` を submodule として追加します。

```powershell
git submodule add --depth 1 https://github.com/ggml-org/llama.cpp.git ThirdParty/llama.cpp
git submodule update --init --recursive ThirdParty/llama.cpp
```

ネットワークが不安定で通常 clone が `early EOF` になる場合は、partial clone してから submodule として吸収できます。

```powershell
git -c protocol.version=2 clone --filter=blob:none --depth 1 --no-checkout https://github.com/ggml-org/llama.cpp.git ThirdParty/llama.cpp
git -C ThirdParty/llama.cpp sparse-checkout init --cone
git -C ThirdParty/llama.cpp sparse-checkout set --skip-checks CMakeLists.txt cmake common ggml include scripts src tools vendor
git -C ThirdParty/llama.cpp checkout
git submodule add --force https://github.com/ggml-org/llama.cpp.git ThirdParty/llama.cpp
git submodule absorbgitdirs ThirdParty/llama.cpp
git config -f .gitmodules submodule.ThirdParty/llama.cpp.shallow true
```

2. GGUF model を配置します。

```text
Assets/Models/gemma-4-E4B-it/gemma-4-E4B-it-Q4_K_M.gguf
```

推奨 GGUF:

```text
https://huggingface.co/ggml-org/gemma-4-E4B-it-GGUF
```

3. RenderBuilder をビルドします。

MSBuild は `BuildThirdParty.ps1` 経由で `ThirdParty/llama.cpp` から `llama-server` target をビルドします。既定の AI Chat パネルは次の出力先を優先します。

```text
ThirdParty/llama.cpp/Build/x64/Release/bin/Release/llama-server.exe
ThirdParty/llama.cpp/Build/x64/Debug/bin/Debug/llama-server.exe
```

RenderBuilder から起動する `llama-server` には `--jinja --reasoning off --reasoning-budget 0` を付けています。Gemma 4 の thinking 出力が `reasoning_content` 側に分離されて、通常のチャット本文が空になるのを避けるためです。

### CUDA build

`BuildThirdParty.ps1` は既定で `LlamaCuda=Auto` として動きます。CUDA Toolkit の `nvcc` が見つかった場合は `llama.cpp` を `GGML_CUDA=ON` で構成し、見つからない場合は CPU build のまま進みます。

明示的に指定したい場合は MSBuild property を使います。

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" RenderBuilder.sln /m /p:Configuration=Release /p:Platform=x64 /p:LlamaCuda=ON
```

`LlamaCuda=ON` は CUDA Toolkit がない環境では失敗します。速度確認は Release build の `llama-server.exe` を優先してください。

## モデル状態

`Load Model` を押すと、AI Chat は `llama-server` を起動して `/v1/models` を定期的に確認します。

- `Starting`: `llama-server` の起動または HTTP endpoint の応答待ちです。
- `Loading model`: プロセスは動いていますが、GGUF model を読み込み中です。この間の `llama-server HTTP 503: Loading model` は通常の読み込み状態です。
- `Ready`: `/v1/models` が 200 を返しました。`Send` が有効になり、チャット送信できます。
- `Failed`: プロセス終了、path 間違い、または 5 分以内に Ready にならなかった状態です。

読み込み中は `Send` が無効になります。大きい GGUF を CPU/RAM/VRAM に展開するため、初回読み込みには数分かかる場合があります。

AI Chat パネルには診断として `Ready wait`、`Checks`、`Last probe` も表示します。`Last probe: HTTP 503` は `Loading model` と同じく読み込み中を示すことがあり、`Ready wait` が伸び続ける場合は VRAM/RAM や `GPU Layers`、`Context Tokens` を見直してください。

## 使い方

1. `AI Chat` パネルで `llama-server` と `GGUF Model` の path を確認します。
2. `Load Model` を押します。
3. `Model: Ready` になるまで待ちます。読み込み中は `Send` が無効になります。
4. 入力欄に依頼を書いて `Send` を押します。
5. AI が GUI 変更を提案した場合は `Suggested Actions` に表示されます。
6. 内容を確認して `Apply Suggested Changes` を押すと、既存の local control handler 経由で反映されます。

`Auto Apply` は既定で無効です。安全確認が不要な運用になってから有効化してください。

AI の `reply` は既定で日本語になります。ユーザーが明示的に別の言語を指定した場合だけ、その言語で返します。`actions` の `method` と `params` は local control handler に渡す JSON なので、英語の識別子のままです。

## 表示

AI Chat の入力欄とチャット履歴は 20px の文字サイズで表示します。RenderBuilder 全体の ImGui font は 16px のままなので、他の editor panel の密度は変わりません。

## 速度設定

- `GPU Layers`: CUDA build では `Auto` または `All` を選びます。CPU だけで動かす場合は `CPU` を選びます。
- `Context Tokens`: 速度優先では `4096`、さらに軽くする場合は `2048` に下げます。
- `Max Reply Tokens`: 通常の GUI 操作用途では `512` または `256` が扱いやすいです。
- `Threads`: CPU build の場合だけ調整します。`0` は llama.cpp の既定値です。

## トラブルシュート

- `Model: Loading model` のまま長い場合は、GPU layers、context tokens、利用可能な VRAM/RAM を見直してください。
- CUDA build になっているかは `ThirdParty/llama.cpp/Build/x64/<Configuration>/CMakeCache.txt` の `GGML_CUDA:BOOL=ON` で確認できます。
- `llama-server executable was not found.` が出る場合は、`ThirdParty/llama.cpp/Build/x64/.../llama-server.exe` が生成されているか確認してください。
- `GGUF model file was not found.` が出る場合は、`Assets/Models/gemma-4-E4B-it/gemma-4-E4B-it-Q4_K_M.gguf` を配置してください。
- `Failed` になった場合は `Stop Model` してから、設定を見直して `Load Model` し直してください。
