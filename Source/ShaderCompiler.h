#pragma once

#include "EditorTypes.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <ObjIdl.h>
#include <OleAuto.h>
#include <Unknwn.h>

#include <dxcapi.h>
#include <wrl/client.h>

namespace rb
{
class IShaderCompiler
{
public:
    virtual ~IShaderCompiler() = default;
    virtual ShaderCompileResult Compile(const ShaderCompileRequest& request) = 0;
};

class DxcShaderCompiler final : public IShaderCompiler
{
public:
    DxcShaderCompiler();
    ShaderCompileResult Compile(const ShaderCompileRequest& request) override;

private:
    Microsoft::WRL::ComPtr<IDxcUtils> m_utils;
    Microsoft::WRL::ComPtr<IDxcCompiler3> m_compiler;
    Microsoft::WRL::ComPtr<IDxcIncludeHandler> m_includeHandler;
};
}
