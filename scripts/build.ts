import { execFileSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import process from "node:process";

const repoRoot = path.resolve(import.meta.dirname, "..");
const patcherDir = path.join(repoRoot, "patcher");
const frontendDir = path.join(patcherDir, "frontend");

const isWin = process.platform === "win32";
const isMac = process.platform === "darwin";

// macOS 没有 C.UTF-8 locale，沿用系统 locale；只有 Windows 需要强制子进程输出 UTF-8。
const buildEnv = { ...process.env, ...(isWin ? { LANG: "C.UTF-8" } : {}) };

function run(cmd: string, args: string[], cwd: string): void {
  if (isWin && (cmd === "pnpm" || cmd === "npm")) {
    execFileSync(process.env.ComSpec || "cmd.exe", ["/d", "/s", "/c", cmd, ...args], {
      cwd,
      stdio: "inherit",
      env: buildEnv,
    });
  } else {
    execFileSync(cmd, args, {
      cwd,
      stdio: "inherit",
      env: buildEnv,
    });
  }
}

function resolveAppVersion(): string {
  if (process.env.APP_VERSION) {
    return process.env.APP_VERSION.replace(/^patcher-v?/, "").replace(/^v/, "");
  }

  const ref = process.env.GITHUB_REF_NAME || process.env.GITHUB_REF;
  if (ref) {
    const tag = ref.replace(/^refs\/tags\//, "");
    if (tag.startsWith("patcher-v")) {
      return tag.slice("patcher-v".length);
    }
    if (tag.startsWith("patcher-")) {
      return tag.slice("patcher-".length);
    }
    if (tag.startsWith("v")) {
      return tag.slice(1);
    }
    return tag;
  }

  try {
    const tag = execFileSync("git", ["describe", "--tags", "--match", "patcher-v*", "--exact-match"], {
      cwd: repoRoot,
      encoding: "utf-8",
      stdio: ["ignore", "pipe", "ignore"],
    }).trim();
    if (tag.startsWith("patcher-v")) {
      return tag.slice("patcher-v".length);
    }
    return tag;
  } catch {
    try {
      const desc = execFileSync("git", ["describe", "--tags", "--match", "patcher-v*", "--always"], {
        cwd: repoRoot,
        encoding: "utf-8",
        stdio: ["ignore", "pipe", "ignore"],
      }).trim();
      const clean = desc.replace(/^patcher-v?/, "");
      const match = clean.match(/^(\d+(?:\.\d+)+)/);
      return match ? match[1] : clean;
    } catch {
      return "dev";
    }
  }
}

const appVersion = resolveAppVersion();
const releaseBuild = process.argv.includes("--release") || process.env.PATCHER_RELEASE === "1";
console.log(`\n构建版本: ${appVersion}（${releaseBuild ? "发布构建" : "本地开发构建"}）`);

// 字体编译组件不再打进主程序：运行时按需下载到数据目录（见 ensureFontCompiler），
// 拿不到时改用同样按需下载的预编译字体包，构建本身不依赖这个二进制。
// development tag 让本地构建只校验工作区 resource/，不向云端同步 CSV 与字体；
// 发布构建不带该 tag，保持「内置词条始终为云端最新」的行为。
const goTags = releaseBuild ? "desktop,production" : "desktop,production,development";

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

  if (!hasWails && releaseBuild) {
    console.error("\n构建失败: 未检测到 Wails CLI，macOS 发布构建无法产出 .app 与 zip。请先执行 go install github.com/wailsapp/wails/v2/cmd/wails@v2.10.2");
    process.exit(1);
  }

  if (hasWails) {
    run("wails", ["build", "-platform", "darwin/universal", "-tags", goTags, "-skipfrontend", "-skipbindings", "-trimpath", "-ldflags", `-X main.appVersion=${appVersion}`], patcherDir);
    const appPath = path.join(patcherDir, "build", "bin", "FFXIVChnTextPatch-SM-v2.app");
    const zipPath = path.join(repoRoot, "FFXIVChnTextPatch-SM-v2-macOS.zip");
    if (!fs.existsSync(appPath)) {
      console.error(`\n构建失败: wails 构建结束但未找到 ${appPath}`);
      process.exit(1);
    }
    run("ditto", ["-c", "-k", "--sequesterRsrc", "--keepParent", appPath, zipPath], repoRoot);
    if (!fs.existsSync(zipPath)) {
      console.error(`\n构建失败: 打包结束但未找到 ${zipPath}`);
      process.exit(1);
    }
    console.log(`\n构建成功: ${zipPath}`);
  } else {
    const outBin = path.join(repoRoot, "FFXIVChnTextPatch-SM-v2");
    console.warn("\n[警告] 未检测到 Wails CLI，本次只编译命令行二进制，不会生成 .app 或 macOS zip；发布构建不可使用此模式。");
    run("go", ["build", "-tags", goTags, "-trimpath", "-ldflags", `-s -w -X main.appVersion=${appVersion}`, "-o", outBin, "."], patcherDir);
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
  const ldflags = `-H windowsgui -s -w -X main.appVersion=${appVersion}`;
  run("go", ["build", "-tags", goTags, "-trimpath", "-ldflags", ldflags, "-o", outExe, "."], patcherDir);
  console.log(`\n构建成功: ${outExe}`);
}
