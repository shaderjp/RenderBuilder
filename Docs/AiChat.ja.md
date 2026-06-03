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

## 使い方

1. `AI Chat` パネルで `llama-server` と `GGUF Model` の path を確認します。
2. `Load Model` を押します。
3. `Model: Ready` になるまで待ちます。読み込み中は `Send` が無効になります。
4. 入力欄に依頼を書いて `Send` を押します。
5. AI が GUI 変更を提案した場合は `Suggested Actions` に表示されます。
6. 内容を確認して `Apply Suggested Changes` を押すと、既存の local control handler 経由で反映されます。

`Auto Apply` は既定で無効です。安全確認が不要な運用になってから有効化してください。

AI の `reply` は既定で日本語になります。ユーザーが明示的に別の言語を指定した場合だけ、その言語で返します。`actions` の `method` と `params` は local control handler に渡す JSON なので、英語の識別子のままです。
