import { execFileSync, spawn } from "node:child_process";
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

function run(cmd: string, args: string[], cwd: string, extraEnv?: Record<string, string>): void {
  const env = extraEnv ? { ...buildEnv, ...extraEnv } : buildEnv;
  if (isWin && (cmd === "pnpm" || cmd === "npm")) {
    execFileSync(process.env.ComSpec || "cmd.exe", ["/d", "/s", "/c", cmd, ...args], {
      cwd,
      stdio: "inherit",
      env,
    });
  } else {
    execFileSync(cmd, args, {
      cwd,
      stdio: "inherit",
      env,
    });
  }
}

function runAsync(cmd: string, args: string[], cwd: string, extraEnv?: Record<string, string>): Promise<void> {
  const env = extraEnv ? { ...buildEnv, ...extraEnv } : buildEnv;
  return new Promise((resolve, reject) => {
    const child = spawn(cmd, args, { cwd, stdio: "inherit", env });
    child.on("error", reject);
    child.on("close", (code) => {
      if (code === 0) {
        resolve();
      } else {
        reject(new Error(`命令退出码 ${code ?? "unknown"}: ${cmd} ${args.join(" ")}`));
      }
    });
  });
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

function buildFrontend(): void {
  const nodeModules = path.join(frontendDir, "node_modules");
  if (!fs.existsSync(nodeModules)) {
    try {
      run("pnpm", ["install", "--frozen-lockfile"], frontendDir);
    } catch {
      run("pnpm", ["install"], frontendDir);
    }
  }
  run("pnpm", ["run", "build"], frontendDir);
}

async function buildMacOS(appVersion: string, goTags: string, releaseBuild: boolean): Promise<void> {
  const appName = "FFXIVChnTextPatch-SM-v2";
  const binDir = path.join(patcherDir, "build", "bin");
  const appPath = path.join(binDir, `${appName}.app`);
  const contentsDir = path.join(appPath, "Contents");
  const macosDir = path.join(contentsDir, "MacOS");
  const resourcesDir = path.join(contentsDir, "Resources");
  const tmpArchDir = path.join(binDir, ".universal-tmp");
  const zipPath = path.join(repoRoot, `${appName}-macOS.zip`);

  fs.rmSync(appPath, { recursive: true, force: true });
  fs.rmSync(tmpArchDir, { recursive: true, force: true });
  if (releaseBuild) {
    fs.rmSync(zipPath, { force: true });
  }
  fs.mkdirSync(macosDir, { recursive: true });
  fs.mkdirSync(resourcesDir, { recursive: true });
  fs.mkdirSync(tmpArchDir, { recursive: true });

  const ldflags = `-s -w -X main.appVersion=${appVersion}`;
  const macCommonEnv = {
    GOOS: "darwin",
    CGO_ENABLED: "1",
    CGO_CFLAGS: "-mmacosx-version-min=12.0",
    CGO_LDFLAGS: "-mmacosx-version-min=12.0",
    MACOSX_DEPLOYMENT_TARGET: "12.0",
  };

  try {
    const universalBin = path.join(macosDir, appName);
    if (releaseBuild) {
      const amd64Bin = path.join(tmpArchDir, `${appName}-amd64`);
      const arm64Bin = path.join(tmpArchDir, `${appName}-arm64`);
      await Promise.all([
        runAsync(
          "go",
          ["build", "-tags", goTags, "-trimpath", "-buildvcs=false", "-ldflags", ldflags, "-o", amd64Bin, "."],
          patcherDir,
          { ...macCommonEnv, GOARCH: "amd64" },
        ),
        runAsync(
          "go",
          ["build", "-tags", goTags, "-trimpath", "-buildvcs=false", "-ldflags", ldflags, "-o", arm64Bin, "."],
          patcherDir,
          { ...macCommonEnv, GOARCH: "arm64" },
        ),
      ]);
      run("lipo", ["-create", "-output", universalBin, amd64Bin, arm64Bin], patcherDir);
    } else {
      const localArch = process.arch === "arm64" ? "arm64" : "amd64";
      run(
        "go",
        ["build", "-tags", goTags, "-trimpath", "-buildvcs=false", "-ldflags", ldflags, "-o", universalBin, "."],
        patcherDir,
        { ...macCommonEnv, GOARCH: localArch },
      );
    }
    fs.chmodSync(universalBin, 0o755);

    const iconSrc = path.join(frontendDir, "public", "appicon.png");
    let hasIcns = false;
    if (fs.existsSync(iconSrc)) {
      const iconsetDir = path.join(tmpArchDir, "icons.iconset");
      const icnsOut = path.join(resourcesDir, "icons.icns");
      try {
        fs.mkdirSync(iconsetDir, { recursive: true });
        for (const [size, name] of [
          [16, "icon_16x16.png"],
          [32, "icon_16x16@2x.png"],
          [32, "icon_32x32.png"],
          [64, "icon_32x32@2x.png"],
          [128, "icon_128x128.png"],
          [256, "icon_128x128@2x.png"],
          [256, "icon_256x256.png"],
          [512, "icon_256x256@2x.png"],
          [512, "icon_512x512.png"],
          [1024, "icon_512x512@2x.png"],
        ] as const) {
          execFileSync("sips", ["-z", String(size), String(size), iconSrc, "--out", path.join(iconsetDir, name)], {
            stdio: "ignore",
          });
        }
        execFileSync("iconutil", ["-c", "icns", iconsetDir, "-o", icnsOut], { stdio: "ignore" });
        hasIcns = fs.existsSync(icnsOut);
      } catch {
        hasIcns = false;
      }
    }

    const bundleVersion = appVersion === "dev" ? "0.0.0" : appVersion.replace(/[^0-9.].*$/, "") || "1.0.0";
    const infoPlist = `<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundlePackageType</key>
  <string>APPL</string>
  <key>CFBundleName</key>
  <string>${appName}</string>
  <key>CFBundleDisplayName</key>
  <string>FFXIV 汉化器</string>
  <key>CFBundleIdentifier</key>
  <string>com.souma.ffxivchntextpatch</string>
  <key>CFBundleVersion</key>
  <string>${bundleVersion}</string>
  <key>CFBundleShortVersionString</key>
  <string>${bundleVersion}</string>
  <key>CFBundleExecutable</key>
  <string>${appName}</string>
  ${hasIcns ? "<key>CFBundleIconFile</key>\n  <string>icons.icns</string>" : ""}
  <key>LSMinimumSystemVersion</key>
  <string>12.0</string>
  <key>NSHighResolutionCapable</key>
  <true/>
</dict>
</plist>
`;
    fs.writeFileSync(path.join(contentsDir, "Info.plist"), infoPlist, "utf-8");

    run("codesign", ["--force", "--deep", "--sign", "-", appPath], patcherDir);
    if (!releaseBuild) {
      console.log(`\n构建成功: ${appPath}`);
      return;
    }
    run("ditto", ["-c", "-k", "--sequesterRsrc", "--keepParent", appPath, zipPath], repoRoot);
    if (!fs.existsSync(zipPath)) {
      throw new Error(`打包结束但未找到 ${zipPath}`);
    }
    console.log(`\n构建成功: ${zipPath}`);
  } finally {
    fs.rmSync(tmpArchDir, { recursive: true, force: true });
  }
}

function buildWindows(appVersion: string, goTags: string): void {
  const outExe = path.join(repoRoot, "FFXIVChnTextPatch-SM-v2.exe");
  if (fs.existsSync(outExe)) {
    try {
      const fd = fs.openSync(outExe, "r+");
      fs.closeSync(fd);
    } catch {
      throw new Error(`目标文件 [${outExe}] 正在运行或被占用，请先退出程序后再试。`);
    }
  }

  const normalSyso = path.join(patcherDir, "icon_windows_amd64.syso");
  const elevatedSyso = path.join(patcherDir, "icon_elevated.syso.bin");
  const backupSyso = path.join(patcherDir, "icon_windows_amd64.syso.bak");
  let restored = false;

  const restoreSyso = () => {
    if (restored) return;
    restored = true;
    if (fs.existsSync(backupSyso)) {
      try {
        fs.copyFileSync(backupSyso, normalSyso);
        fs.unlinkSync(backupSyso);
      } catch (err) {
        console.warn(`[警告] 恢复默认 syso 失败: ${err}`);
      }
    }
  };

  if (fs.existsSync(elevatedSyso) && fs.existsSync(normalSyso)) {
    fs.copyFileSync(normalSyso, backupSyso);
    fs.copyFileSync(elevatedSyso, normalSyso);
  }

  try {
    const ldflags = `-H windowsgui -s -w -X main.appVersion=${appVersion}`;
    run("go", ["build", "-tags", goTags, "-trimpath", "-ldflags", ldflags, "-o", outExe, "."], patcherDir);
    console.log(`\n构建成功: ${outExe}`);
  } finally {
    restoreSyso();
  }
}

async function main(): Promise<void> {
  const appVersion = resolveAppVersion();
  const releaseBuild = process.argv.includes("--release") || process.env.PATCHER_RELEASE === "1";
  console.log(`\n构建版本: ${appVersion}（${releaseBuild ? "发布构建" : "本地开发构建"}）`);

  // 字体编译组件不再打进主程序：运行时按需下载到数据目录（见 ensureFontCompiler），
  // 拿不到时改用同样按需下载的预编译字体包，构建本身不依赖这个二进制。
  // development tag 让本地构建只校验工作区 resource/，不向云端同步 CSV 与字体；
  // 发布构建不带该 tag，保持「内置词条始终为云端最新」的行为。
  const goTags = releaseBuild ? "desktop,production" : "desktop,production,development";

  buildFrontend();
  if (isMac) {
    await buildMacOS(appVersion, goTags, releaseBuild);
  } else {
    buildWindows(appVersion, goTags);
  }
}

main().catch((err) => {
  const message = err instanceof Error ? err.message : String(err);
  console.error(`\n构建失败: ${message}`);
  process.exit(1);
});
