# RenderBuilder MCP Bridge

This is a local stdio MCP server that forwards semantic LookDev/material commands to a running RenderBuilder instance over a Windows named pipe.

RenderBuilder must be started with local control enabled:

```powershell
Bin\x64\Debug\RenderBuilder.exe --enable-local-control
```

Build the bridge:

```powershell
npm install
npm run build
```

Run it from an MCP client with:

```powershell
node Tools\RenderBuilderMcp\dist\index.js
```

The bridge only talks to `\\.\pipe\RenderBuilder.Control` and does not expose a network listener.
