param(
    [Parameter(Mandatory = $true)][string]$AssimpRoot,
    [Parameter(Mandatory = $true)][string]$AssimpBuildRoot,
    [Parameter(Mandatory = $true)][string]$AssimpLibName,
    [Parameter(Mandatory = $true)][string]$DirectXTexProject,
    [Parameter(Mandatory = $true)][string]$DirectXTexLibDir,
    [Parameter(Mandatory = $true)][string]$LlamaCppRoot,
    [Parameter(Mandatory = $true)][string]$LlamaCppBuildRoot,
    [Parameter(Mandatory = $true)][string]$LlamaServerExe,
    [Parameter(Mandatory = $true)][string]$MSBuildPath,
    [Parameter(Mandatory = $true)][string]$Configuration
)

$ErrorActionPreference = 'Stop'

function Invoke-WithMutex {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][scriptblock]$Body
    )

    $mutex = [System.Threading.Mutex]::new($false, $Name)
    try {
        [void]$mutex.WaitOne()
        & $Body
    }
    finally {
        $mutex.ReleaseMutex()
        $mutex.Dispose()
    }
}

Invoke-WithMutex -Name "Local\RenderBuilderDirectXTex-$Configuration" -Body {
    $directXTexLib = Join-Path $DirectXTexLibDir 'DirectXTex.lib'
    if (!(Test-Path $directXTexLib)) {
        & $MSBuildPath $DirectXTexProject /p:Platform=x64 /p:Configuration=$Configuration /v:minimal
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
    }
}

Invoke-WithMutex -Name "Local\RenderBuilderAssimp-$Configuration" -Body {
    $cachePath = Join-Path $AssimpBuildRoot 'CMakeCache.txt'
    $assimpLibPath = Join-Path (Join-Path (Join-Path $AssimpBuildRoot 'lib') $Configuration) $AssimpLibName

    if (!(Test-Path $cachePath)) {
        cmake -S $AssimpRoot -B $AssimpBuildRoot -G "Visual Studio 17 2022" -A x64 `
            -DASSIMP_BUILD_TESTS=OFF `
            -DASSIMP_BUILD_ASSIMP_TOOLS=OFF `
            -DBUILD_SHARED_LIBS=OFF `
            -DASSIMP_INSTALL=OFF `
            -DASSIMP_WARNINGS_AS_ERRORS=OFF
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
    }

    if (!(Test-Path $assimpLibPath)) {
        cmake --build $AssimpBuildRoot --config $Configuration --target assimp --parallel
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
    }
}

Invoke-WithMutex -Name "Local\RenderBuilderLlamaCpp-$Configuration" -Body {
    $cachePath = Join-Path $LlamaCppBuildRoot 'CMakeCache.txt'

    if (!(Test-Path $cachePath)) {
        cmake -S $LlamaCppRoot -B $LlamaCppBuildRoot -G "Visual Studio 17 2022" -A x64 `
            -DBUILD_SHARED_LIBS=OFF `
            -DLLAMA_BUILD_TESTS=OFF `
            -DLLAMA_BUILD_TOOLS=ON `
            -DLLAMA_BUILD_EXAMPLES=OFF `
            -DLLAMA_BUILD_SERVER=ON `
            -DLLAMA_BUILD_APP=OFF
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
    }

    if (!(Test-Path $LlamaServerExe)) {
        cmake --build $LlamaCppBuildRoot --config $Configuration --target llama-server --parallel
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
    }
}
