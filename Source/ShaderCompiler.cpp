#include "ShaderCompiler.h"

#include <dxcapi.h>

#include <stdexcept>

namespace
{
void ThrowIfFailed(HRESULT hr, const char* message)
{
    if (FAILED(hr))
    {
        throw std::runtime_error(message);
    }
}

std::string ToNarrow(const std::wstring& value)
{
    std::string result;
    result.reserve(value.size());
    for (wchar_t ch : value)
    {
        result.push_back(ch >= 0 && ch <= 0x7f ? static_cast<char>(ch) : '?');
    }
    return result;
}
}

namespace rb
{
DxcShaderCompiler::DxcShaderCompiler()
{
    ThrowIfFailed(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&m_utils)), "DxcCreateInstance(CLSID_DxcUtils) failed.");
    ThrowIfFailed(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&m_compiler)), "DxcCreateInstance(CLSID_DxcCompiler) failed.");
    ThrowIfFailed(m_utils->CreateDefaultIncludeHandler(&m_includeHandler), "CreateDefaultIncludeHandler failed.");
}

ShaderCompileResult DxcShaderCompiler::Compile(const ShaderCompileRequest& request)
{
    ShaderCompileResult output;

    DxcBuffer sourceBuffer = {};
    sourceBuffer.Ptr = request.source.data();
    sourceBuffer.Size = request.source.size();
    sourceBuffer.Encoding = DXC_CP_UTF8;

    std::vector<std::wstring> ownedArgs;
    ownedArgs.reserve(16);
    ownedArgs.push_back(L"-E");
    ownedArgs.push_back(request.entryPoint);
    ownedArgs.push_back(L"-T");
    ownedArgs.push_back(request.profile);
    ownedArgs.push_back(L"-HV");
    ownedArgs.push_back(L"2021");
    ownedArgs.push_back(L"-I");
    ownedArgs.push_back(request.includeDirectory);
    ownedArgs.push_back(L"-Qstrip_reflect");
    if (request.debug)
    {
        ownedArgs.push_back(L"-Zi");
        ownedArgs.push_back(L"-Qembed_debug");
        ownedArgs.push_back(L"-Od");
    }
    else
    {
        ownedArgs.push_back(L"-O3");
    }
    if (request.spirv)
    {
        ownedArgs.push_back(L"-spirv");
        ownedArgs.push_back(L"-fvk-use-dx-position-w");
        ownedArgs.push_back(L"-D");
        ownedArgs.push_back(L"VULKAN=1");
    }

    std::vector<LPCWSTR> args;
    args.reserve(ownedArgs.size());
    for (const std::wstring& arg : ownedArgs)
    {
        args.push_back(arg.c_str());
    }

    Microsoft::WRL::ComPtr<IDxcResult> result;
    const HRESULT compileHr = m_compiler->Compile(
        &sourceBuffer,
        args.data(),
        static_cast<UINT32>(args.size()),
        m_includeHandler.Get(),
        IID_PPV_ARGS(&result));

    if (FAILED(compileHr))
    {
        output.diagnostics = "DXC invocation failed for " + ToNarrow(request.entryPoint) + ".";
        return output;
    }

    Microsoft::WRL::ComPtr<IDxcBlobUtf8> errors;
    result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
    if (errors && errors->GetStringLength() > 0)
    {
        output.diagnostics.assign(errors->GetStringPointer(), errors->GetStringPointer() + errors->GetStringLength());
    }

    HRESULT status = S_OK;
    result->GetStatus(&status);
    output.succeeded = SUCCEEDED(status);
    if (!output.succeeded)
    {
        if (output.diagnostics.empty())
        {
            output.diagnostics = "DXC failed without diagnostic text.";
        }
        return output;
    }

    Microsoft::WRL::ComPtr<IDxcBlob> objectBlob;
    result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&objectBlob), nullptr);
    if (!objectBlob)
    {
        output.succeeded = false;
        output.diagnostics += "\nDXC did not produce shader bytecode.";
        return output;
    }

    const auto* begin = static_cast<const std::uint8_t*>(objectBlob->GetBufferPointer());
    output.bytecode.assign(begin, begin + objectBlob->GetBufferSize());
    if (output.diagnostics.empty())
    {
        output.diagnostics = "Compiled " + ToNarrow(request.entryPoint) + " as " + ToNarrow(request.profile) + ".";
    }
    return output;
}
}
