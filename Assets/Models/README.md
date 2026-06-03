# RenderBuilder AI models

Place local GGUF models under this directory. The default chat integration looks for:

```text
Assets/Models/gemma-4-E4B-it/gemma-4-E4B-it-Q4_K_M.gguf
```

Recommended first model:

- Source: https://huggingface.co/ggml-org/gemma-4-E4B-it-GGUF
- File: `gemma-4-E4B-it-Q4_K_M.gguf`
- Upstream model card: https://huggingface.co/google/gemma-4-E4B

GGUF files are intentionally ignored by Git because they are multi-gigabyte binaries.
