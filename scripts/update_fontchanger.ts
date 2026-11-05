import { execFileSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import process from "node:process";

const repoRoot = path.resolve(import.meta.dirname, "..");
const toolsDir = path.join(repoRoot, "tools");
const targetFontChangerDir = path.join(toolsDir, "fontchanger");
const customRenderCppPath = path.join(
  targetFontChangerDir,
  "FontChanger.FontEditor",
  "CommandLineRender.cpp"
);
const noticePath = path.join(targetFontChangerDir, "NOTICE");

const UPSTREAM_REPO = "https://github.com/Soreepeong/FFXIV-FontChanger.git";
const PROXY_URL = "http://127.0.0.1:7897";

// 上游 sln 引用了未随本仓库引入的 C# 工程（Dalamud 插件 / Presets / ShaderGen / UnicodeDataGenerator），
// 直接留着会让 msbuild 报 MSB3202（项目文件不存在）并使构建失败，故同步后剔除。
const UNVENDORED_PROJECTS = [
  "FontChanger.DalamudPlugin",
  "FontChanger.Presets",
  "FontChanger.ShaderGen",
  "FontChanger.UnicodeDataGenerator",
];

/** 从 .sln 中移除指定名称的工程条目及其配置行，保留原有换行风格。 */
function stripUnvendoredProjects(sln: string): string {
  const eol = sln.includes("\r\n") ? "\r\n" : "\n";
  const removedGuids = new Set<string>();
  const kept: string[] = [];
  let skipping = false;

  for (const line of sln.split(/\r?\n/)) {
    if (skipping) {
      if (line.trim() === "EndProject") {
        skipping = false;
      }
      continue;
    }
    const projectLine =
      /^Project\("[^"]+"\)\s*=\s*"([^"]+)"\s*,\s*"[^"]+"\s*,\s*"\{([^}]+)\}"\s*$/.exec(line.trim());
    if (projectLine && UNVENDORED_PROJECTS.includes(projectLine[1])) {
      removedGuids.add(projectLine[2].toUpperCase());
      skipping = true;
      continue;
    }
    kept.push(line);
  }

  const result: string[] = [];
  for (const line of kept) {
    const configLine = /^\s*\{([^}]+)\}\.[^=]*=/.exec(line);
    if (configLine && removedGuids.has(configLine[1].toUpperCase())) {
      continue;
    }
    result.push(line);
  }

  const blank = eol + eol;
  return result.join(eol).replace(eol === "\r\n" ? /\r\n{3,}/g : /\n{3,}/g, blank);
}

function getEnvWithProxy(): NodeJS.ProcessEnv {
  return {
    ...process.env,
    HTTP_PROXY: process.env.HTTP_PROXY || PROXY_URL,
    HTTPS_PROXY: process.env.HTTPS_PROXY || PROXY_URL,
    ALL_PROXY: process.env.ALL_PROXY || PROXY_URL,
  };
}

function runGit(args: string[], cwd: string = repoRoot): string {
  try {
    const res = execFileSync("git", args, {
      cwd,
      env: getEnvWithProxy(),
      encoding: "utf-8",
      stdio: ["ignore", "pipe", "pipe"],
    });
    return res.trim();
  } catch (err: unknown) {
    const msg = err instanceof Error ? err.message : String(err);
    throw new Error(`Git 执行失败: ${msg}`);
  }
}

async function main(): Promise<void> {
  console.log("[1/5] 检查代理与上游仓库配置...");
  console.log(`代理地址: ${process.env.HTTP_PROXY || PROXY_URL}`);
  console.log(`上游仓库: ${UPSTREAM_REPO}`);

  const tempCloneDir = path.join(repoRoot, ".temp_upstream_fontchanger");
  if (fs.existsSync(tempCloneDir)) {
    fs.rmSync(tempCloneDir, { recursive: true, force: true });
  }

  // 1. 备份本地定制的 CommandLineRender.cpp
  let backupCustomRenderContent = "";
  if (fs.existsSync(customRenderCppPath)) {
    backupCustomRenderContent = fs.readFileSync(customRenderCppPath, "utf-8");
    console.log("[2/5] 已备份本地定制的 CommandLineRender.cpp（包含 --export-raw 与 --export-ttmp 实现）");
  } else {
    console.warn("[警告] 未找到现有的 CommandLineRender.cpp，将在同步后需手动检查接口");
  }

  // 2. 浅克隆上游最新代码（包含 submodule xivres）
  console.log("[3/5] 正在拉取上游最新源码（含子模块）...");
  runGit([
    "clone",
    "--depth",
    "1",
    "--recurse-submodules",
    "--shallow-submodules",
    UPSTREAM_REPO,
    tempCloneDir,
  ]);

  const latestCommit = runGit(["rev-parse", "HEAD"], tempCloneDir);
  const commitDate = runGit(["log", "-1", "--format=%cd", "--date=iso"], tempCloneDir);
  console.log(`上游最新 Commit: ${latestCommit} (${commitDate})`);

  // 防御检查：确保 xivres 子模块被正确下载
  const xivresCheckPath = path.join(
    tempCloneDir,
    "xivres",
    "xivres",
    "include",
    "xivres",
    "common.h"
  );
  if (!fs.existsSync(xivresCheckPath)) {
    throw new Error("子模块 xivres 未能正确拉取，中止更新以保护本地文件");
  }

  // 3. 同步核心目录
  console.log("[4/5] 正在同步底层核心库与预设文件...");
  const dirsToCopy = [
    "FontChanger.FixedSizeFont",
    "xivres",
    "Presets",
    "FontChanger.Presets.Native",
  ];
  const filesToCopy = ["FFXIV-FontChanger.sln", "LICENSE"];

  for (const dir of dirsToCopy) {
    const srcDir = path.join(tempCloneDir, dir);
    const dstDir = path.join(targetFontChangerDir, dir);
    if (fs.existsSync(srcDir)) {
      if (fs.existsSync(dstDir)) {
        fs.rmSync(dstDir, { recursive: true, force: true });
      }
      fs.cpSync(srcDir, dstDir, { recursive: true });
    }
  }

  for (const file of filesToCopy) {
    const srcFile = path.join(tempCloneDir, file);
    const dstFile = path.join(targetFontChangerDir, file);
    if (fs.existsSync(srcFile)) {
      fs.copyFileSync(srcFile, dstFile);
    }
  }

  // 剔除 sln 中未随仓库引入的 C# 工程引用，否则 msbuild 会因项目文件缺失而失败（MSB3202）。
  const solutionPath = path.join(targetFontChangerDir, "FFXIV-FontChanger.sln");
  if (fs.existsSync(solutionPath)) {
    const originalSln = fs.readFileSync(solutionPath, "utf-8");
    const trimmedSln = stripUnvendoredProjects(originalSln);
    if (trimmedSln !== originalSln) {
      fs.writeFileSync(solutionPath, trimmedSln, "utf-8");
      console.log("已移除 sln 中未引入的 C# 工程引用（仅保留原生工程）");
    }
  }

  // 内置的「默认方案」是上游 Presets/Default@96.json 的副本（go:embed 只能读包目录），
  // 同步上游后一并刷新，避免两者悄悄漂移。
  const upstreamDefaultPreset = path.join(targetFontChangerDir, "Presets", "Default@96.json");
  const embeddedDefaultPreset = path.join(
    repoRoot,
    "patcher",
    "internal",
    "fontengine",
    "presets",
    "default.json",
  );
  if (fs.existsSync(upstreamDefaultPreset)) {
    fs.copyFileSync(upstreamDefaultPreset, embeddedDefaultPreset);
    console.log("已刷新内置默认方案 patcher/internal/fontengine/presets/default.json");
  }

  // 同步 FontChanger.FontEditor 工程配置及头文件（排除 CommandLineRender.cpp）
  const fontEditorSrc = path.join(tempCloneDir, "FontChanger.FontEditor");
  const fontEditorDst = path.join(targetFontChangerDir, "FontChanger.FontEditor");
  if (fs.existsSync(fontEditorSrc)) {
    if (!fs.existsSync(fontEditorDst)) {
      fs.mkdirSync(fontEditorDst, { recursive: true });
    }
    const entries = fs.readdirSync(fontEditorSrc);
    for (const entry of entries) {
      if (entry.toLowerCase() === "commandlinerender.cpp") {
        continue;
      }
      const s = path.join(fontEditorSrc, entry);
      const d = path.join(fontEditorDst, entry);
      const stat = fs.statSync(s);
      if (stat.isDirectory()) {
        fs.cpSync(s, d, { recursive: true });
      } else {
        fs.copyFileSync(s, d);
      }
    }
  }

  // 恢复本地定制的 CommandLineRender.cpp
  if (backupCustomRenderContent) {
    fs.writeFileSync(customRenderCppPath, backupCustomRenderContent, "utf-8");
    console.log("已恢复本地定制的 CommandLineRender.cpp");
  }

  // 更新 NOTICE 记录
  const noticeContent = `FFXIV-FontChanger (Engine Subcomponent)
Copyright (c) 2022-2026 Soreepeong
Licensed under the Apache License, Version 2.0.

Upstream repository: https://github.com/Soreepeong/FFXIV-FontChanger
Synced commit: ${latestCommit}
Synced date: ${new Date().toISOString()}
`;
  fs.writeFileSync(noticePath, noticeContent, "utf-8");

  // 4. 清理临时目录
  console.log("[5/5] 清理临时克隆目录...");
  fs.rmSync(tempCloneDir, { recursive: true, force: true });

  console.log("\n[完成] 上游 FFXIV-FontChanger 同步成功！");
  console.log(`- 同步 Commit: ${latestCommit}`);
  console.log("- 本地定制文件 CommandLineRender.cpp 已安全保留。");
  console.log("- 可提交变更并运行 GitHub Actions (.github/workflows/build_fontchanger.yml) 重新编译。");
}

main().catch((err) => {
  console.error("\n[错误] 同步过程中断:", err instanceof Error ? err.message : err);
  process.exit(1);
});
