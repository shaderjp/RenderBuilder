#!/usr/bin/env node

import net from "node:net";
import { randomUUID } from "node:crypto";
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";

const PIPE_NAME = "\\\\.\\pipe\\RenderBuilder.Control";
const REQUEST_TIMEOUT_MS = 30_000;

type RenderBuilderResponse = {
  id: string | null;
  ok: boolean;
  stateVersion?: number;
  result?: unknown;
  diagnostics?: unknown[];
  error?: {
    code: string;
    message: string;
  };
};

function sendRenderBuilder(method: string, params: Record<string, unknown> = {}): Promise<RenderBuilderResponse> {
  return new Promise((resolve, reject) => {
    const id = randomUUID();
    const socket = net.createConnection(PIPE_NAME);
    let buffer = "";
    let settled = false;
    const timer = setTimeout(() => {
      if (!settled) {
        settled = true;
        socket.destroy();
        reject(new Error("Timed out waiting for RenderBuilder local control response."));
      }
    }, REQUEST_TIMEOUT_MS);

    socket.setEncoding("utf8");
    socket.on("connect", () => {
      socket.write(`${JSON.stringify({ id, method, params })}\n`, "utf8");
    });
    socket.on("data", (chunk) => {
      buffer += chunk;
      const newline = buffer.indexOf("\n");
      if (newline < 0) {
        return;
      }

      const line = buffer.slice(0, newline).trim();
      if (!line) {
        return;
      }

      clearTimeout(timer);
      settled = true;
      socket.end();
      try {
        const response = JSON.parse(line) as RenderBuilderResponse;
        if (!response.ok) {
          reject(new Error(response.error?.message ?? "RenderBuilder local control request failed."));
          return;
        }
        resolve(response);
      } catch (error) {
        reject(error);
      }
    });
    socket.on("error", (error) => {
      if (!settled) {
        clearTimeout(timer);
        settled = true;
        reject(new Error(`Could not connect to RenderBuilder local control pipe. Start RenderBuilder with --enable-local-control. ${error.message}`));
      }
    });
    socket.on("close", () => {
      if (!settled) {
        clearTimeout(timer);
        settled = true;
        reject(new Error("RenderBuilder local control pipe closed before a response was received."));
      }
    });
  });
}

function textResult(response: RenderBuilderResponse) {
  return {
    content: [
      {
        type: "text" as const,
        text: JSON.stringify(response.result ?? response, null, 2),
      },
    ],
  };
}

function float3Range(min: number, max: number) {
  const component = z.number().min(min).max(max);
  return z.tuple([component, component, component]);
}

function float4Range(min: number, max: number) {
  const component = z.number().min(min).max(max);
  return z.tuple([component, component, component, component]);
}

const server = new McpServer({
  name: "renderbuilder-mcp",
  version: "0.1.0",
});

server.tool("renderbuilder_get_state", {}, async () => {
  return textResult(await sendRenderBuilder("get_state"));
});

server.tool("renderbuilder_get_diagnostics", {}, async () => {
  return textResult(await sendRenderBuilder("get_diagnostics"));
});

server.tool("renderbuilder_list_materials", {}, async () => {
  return textResult(await sendRenderBuilder("list_materials"));
});

server.tool(
  "renderbuilder_set_view_settings",
  {
    exposure: z.number().min(-16).max(16).optional(),
    gamma: z.number().min(0.1).max(5).optional(),
    toneMapper: z.enum(["None", "Reinhard", "Aces", "ACES"]).optional(),
    displayMode: z.enum(["Beauty", "BaseColor", "Normal", "Roughness", "Metallic", "AmbientOcclusion", "AO", "Emissive", "LightingOnly", "ShadowMask"]).optional(),
    turntableEnabled: z.boolean().optional(),
    turntableSpeed: z.number().min(-10).max(10).optional(),
  },
  async (params) => textResult(await sendRenderBuilder("set_view_settings", params)),
);

server.tool(
  "renderbuilder_set_environment_settings",
  {
    skyTopColor: float4Range(0, 1).optional(),
    skyHorizonColor: float4Range(0, 1).optional(),
    backgroundMode: z.enum(["SkyColor", "Hdri", "HDRI", "TransparentChecker"]).optional(),
    rotationYaw: z.number().min(-6.2831855).max(6.2831855).optional(),
    intensity: z.number().min(0).max(8).optional(),
  },
  async (params) => textResult(await sendRenderBuilder("set_environment_settings", params)),
);

server.tool(
  "renderbuilder_set_sun_settings",
  {
    sunDirection: float3Range(-1, 1).optional(),
    sunColor: float3Range(0, 10).optional(),
    sunIntensity: z.number().min(0).max(10).optional(),
  },
  async (params) => textResult(await sendRenderBuilder("set_sun_settings", params)),
);

server.tool(
  "renderbuilder_set_shadow_settings",
  {
    enabled: z.boolean().optional(),
    resolution: z.union([z.literal(1024), z.literal(2048), z.literal(4096)]).optional(),
    strength: z.number().min(0).max(1).optional(),
    bias: z.number().min(0).max(0.05).optional(),
    softness: z.number().min(0).max(8).optional(),
    fitScale: z.number().min(1).max(4).optional(),
  },
  async (params) => textResult(await sendRenderBuilder("set_shadow_settings", params)),
);

server.tool(
  "renderbuilder_set_camera",
  {
    target: float3Range(-1_000_000, 1_000_000).optional(),
    yaw: z.number().min(-1000).max(1000).optional(),
    pitch: z.number().min(-1.55).max(1.55).optional(),
    distance: z.number().min(0.001).max(10_000_000).optional(),
  },
  async (params) => textResult(await sendRenderBuilder("set_camera", params)),
);

server.tool(
  "renderbuilder_set_material_preview",
  {
    materialName: z.string().min(1),
    baseColorFactor: float4Range(0, 16).optional(),
    emissiveFactor: float4Range(0, 1000).optional(),
    roughnessFactor: z.number().min(0).max(1).optional(),
    metallicFactor: z.number().min(0).max(1).optional(),
    occlusionStrength: z.number().min(0).max(1).optional(),
    normalStrength: z.number().min(0).max(2).optional(),
    alphaMode: z.enum(["Opaque", "Mask", "Blend"]).optional(),
    alphaCutoff: z.number().min(0).max(1).optional(),
    flipNormalGreen: z.boolean().optional(),
    packedOcclusionRoughnessMetallic: z.boolean().optional(),
  },
  async (params) => textResult(await sendRenderBuilder("set_material_preview", params)),
);

await server.connect(new StdioServerTransport());
