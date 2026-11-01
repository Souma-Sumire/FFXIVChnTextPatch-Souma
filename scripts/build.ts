import { execFileSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import process from "node:process";

const repoRoot = path.resolve(import.meta.dirname, "..");
const patcherDir = path.join(repoRoot, "patcher");
const frontendDir = path.join(patcherDir, "frontend");

const isWin = process.platform === "win32";
const isMac = process.platform === "darwin";

function run(cmd: string, args: string[], cwd: string): void {
  if (isWin && (cmd === "pnpm" || cmd === "npm")) {
    execFileSync(process.env.ComSpec || "cmd.exe", ["/d", "/s", "/c", cmd, ...args], {
      cwd,
      stdio: "inherit",
      env: { ...process.env, LANG: "C.UTF-8" },
    });
  } else {
    execFileSync(cmd, args, {
      cwd,
      stdio: "inherit",
      env: { ...process.env, LANG: "C.UTF-8" },
    });
  }
}

// 1. 构建前端产物
const nodeModules = path.join(frontendDir, "node_modules");
if (!fs.existsSync(nodeModules)) {
  try {
    run("pnpm", ["install", "--frozen-lockfile"], frontendDir);
  } catch {
    run("pnpm", ["install"], frontendDir);
  }
}
run("pnpm", ["run", "build"], frontendDir);

// 2. 编译 Go 目标程序
if (isMac) {
  let hasWails = false;
  try {
    execFileSync("wails", ["version"], { stdio: "ignore" });
    hasWails = true;
  } catch {
    hasWails = false;
  }

  if (hasWails) {
    run("wails", ["build", "-platform", "darwin/universal", "-skipbindings", "-s", "-trimpath"], patcherDir);
    const appPath = path.join(patcherDir, "build", "bin", "FFXIVChnTextPatch-SM-v2.app");
    const zipPath = path.join(repoRoot, "FFXIVChnTextPatch-SM-v2-macOS.zip");
    if (fs.existsSync(appPath)) {
      run("ditto", ["-c", "-k", "--sequesterRsrc", "--keepParent", appPath, zipPath], repoRoot);
      console.log(`\n构建成功: ${zipPath}`);
    }
  } else {
    const outBin = path.join(repoRoot, "FFXIVChnTextPatch-SM-v2");
    run("go", ["build", "-tags", "desktop,production", "-trimpath", "-ldflags", "-s -w", "-o", outBin, "."], patcherDir);
    console.log(`\n构建成功: ${outBin}`);
  }
} else {
  const outExe = path.join(repoRoot, "FFXIVChnTextPatch-SM-v2.exe");
  if (fs.existsSync(outExe)) {
    try {
      const fd = fs.openSync(outExe, "r+");
      fs.closeSync(fd);
    } catch {
      console.error(`\n构建失败: 目标文件 [${outExe}] 正在运行或被占用，请先退出程序后再试。`);
      process.exit(1);
    }
  }
  run("go", ["build", "-tags", "desktop,production", "-trimpath", "-ldflags", "-H windowsgui -s -w", "-o", outExe, "."], patcherDir);
  console.log(`\n构建成功: ${outExe}`);
}
