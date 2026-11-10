// 删掉上次的 exd 文件夹 -> FFXIV Data Explorer -> 国际服的 0a0000 -> 全选导出 Raw 到 D:\Github\FFXIVChnTextPatch-Souma\scripts

import fs from "node:fs";
import path from "node:path";
import { csvName, folderName } from "./names.ts";

interface ModOption {
  Name: string;
  Description: string;
  Files: Record<string, string>;
  FileSwaps: Record<string, string>;
  Manipulations: unknown[];
  Priority?: number;
}

interface ModOptionsGroup {
  Version: number;
  Name: string;
  Description: string;
  Image: string;
  Priority: number;
  Type: string;
  DefaultSettings: number;
  Options: ModOption[];
}

const MINOR_VERSION_MODIFIED = false; // 游戏版本号变更，MINOR 版本号 +1

const csvNames = Object.keys(csvName).map((v) => v.toLowerCase());
const csvNamesObj = Object.fromEntries(Object.entries(csvName).map(([k, v]) => [k.toLowerCase(), v]));
const modName = "Chinese Text Patch";
const baseDir = import.meta.dirname;
const sourceDir = path.join(baseDir, "exd");
const targetDir = path.join(baseDir, modName, "exd");
const metaJsonPath = path.join(baseDir, modName, "meta.json");
const oldMeta = JSON.parse(fs.readFileSync(metaJsonPath, "utf8")) as { Version: string };
const oldVersion = oldMeta.Version;

const jsonMeta = {
  FileVersion: 3,
  Name: modName,
  Author: "Souma",
  Description: "国际服汉化补丁",
  Image: "",
  Version: oldVersion
    .split(".")
    .map(Number)
    .map((num: number, index: number) => {
      if (MINOR_VERSION_MODIFIED) {
        if (index === 1) return num + 1;
        if (index === 2) return 0;
      }
      return index === 2 ? num + 1 : num;
    })
    .join("."),
  Website: "https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma",
  ModTags: [] as string[],
};

const jsonDefaultMod: ModOption = {
  Name: "",
  Description: "",
  Files: {},
  FileSwaps: {},
  Manipulations: [],
};

const SpecialOptionsGroup: ModOptionsGroup = {
  Version: 0,
  Name: "Special Options",
  Description: "特殊选项（重启游戏后生效）",
  Image: "",
  Priority: 0,
  Type: "Multi",
  DefaultSettings: 6,
  Options: [],
};

const NormalOptionsGroup: ModOptionsGroup = {
  Version: 0,
  Name: "Normal Options",
  Description: "可选项 单文件（重启游戏后生效）",
  Image: "",
  Priority: 0,
  Type: "Multi",
  DefaultSettings: 16383,
  Options: [],
};

const FolderOptionsGroup: ModOptionsGroup = {
  Version: 0,
  Name: "Folder Options",
  Description: "可选项 文件夹（重启游戏后生效）",
  Image: "",
  Priority: 0,
  Type: "Multi",
  DefaultSettings: 16383,
  Options: [],
};

// 最多32个选项，penumbra的限制
const optionsNames = [
  "BNpcName",
  "Action",
  "ActionTransient",
  "CraftAction",
  "Balloon",
  "Completion",
  "InstanceContentTextData",
  "Item",
  "Leve",
  "NpcYell",
  "Orchestrion",
  "PlaceName",
  "Quest",
  "Status",
  "Title",
  "Addon",
  "Lobby",
].map((v) => v.toLowerCase());

const specialOptions: Record<string, string> = {
  BNpcName: "警告：此选项会导致 Cactbot 默认触发器失效，并且影响 Logs 上传结果。",
  Item: "警告：此选项会导致发送“展示道具”时，在别人眼中也会变成中文！",
  Quest: "警告：此选项会导致发送“任务”时，在别人眼中也会变成中文！",
};

fs.rmSync(targetDir, { recursive: true, force: true });

function traverseAndCopyFiles(currentDir: string, stack = 0): void {
  fs.readdirSync(currentDir).forEach((file) => {
    const filePath = path.join(currentDir, file);
    const stat = fs.statSync(filePath);

    if (stat.isDirectory()) {
      traverseAndCopyFiles(filePath, stack + 1);
    } else if (stat.isFile() && (file.includes("_ja") || file.includes("_en") || file.includes("_ko") || file.includes("_tc"))) {
      const rawName = file.replace(/^([^_]+).*?_.+$/, "$1");
      const relativePath = path.relative(sourceDir, filePath);
      // Penumbra 清单里的游戏内部路径用反斜杠，macOS 的 path.relative 给的是正斜杠。
      const gamePath = `exd\\${relativePath.replace(/\//g, "\\")}`;
      const targetPath = path.join(targetDir, relativePath);
      const targetDirPath = path.dirname(targetPath);
      const stackIsZero = stack === 0;

      if (stackIsZero && !csvNames.includes(rawName)) {
        return;
      }

      const name = stackIsZero ? rawName : (relativePath.split(/[\\/]/)[0] ?? "");
      const cn = stackIsZero ? csvNamesObj[name] : folderName[name as keyof typeof folderName];
      if (!cn) {
        console.error(`找不到 ${name} 的中文名，请检查 names.ts 文件`);
        return;
      }

      const humpName = Object.keys(csvName).find((v) => v.toLowerCase() === name.toLowerCase()) ?? name;
      const group = specialOptions[humpName] ? SpecialOptionsGroup : NormalOptionsGroup;

      if (stackIsZero && optionsNames.includes(name)) {
        const key = `汉化${cn}（${humpName}）${specialOptions[humpName] ?? ""}`;
        let opt = group.Options.find((v) => v.Name === key);
        if (!opt) {
          opt = {
            Name: key,
            Description: "",
            Files: {},
            FileSwaps: {},
            Manipulations: [],
            Priority: 1,
          };
          group.Options.push(opt);
        }
        opt.Files[`exd/${relativePath.replace(/\\/g, "/")}`] = gamePath;
      } else if (!stackIsZero) {
        const key = `汉化${folderName[name as keyof typeof folderName]}（${name}）文件夹`;
        let opt = FolderOptionsGroup.Options.find((v) => v.Name === key);
        if (!opt) {
          opt = {
            Name: key,
            Description: "",
            Files: {},
            FileSwaps: {},
            Manipulations: [],
            Priority: 1,
          };
          FolderOptionsGroup.Options.push(opt);
        }
        opt.Files[`exd/${relativePath.replace(/\\/g, "/")}`] = gamePath;
      } else {
        jsonDefaultMod.Files[`exd/${relativePath.replace(/\\/g, "/")}`] = gamePath;
      }

      fs.mkdirSync(targetDirPath, { recursive: true });
      fs.copyFileSync(filePath, targetPath);
    }
  });
}

traverseAndCopyFiles(sourceDir, 0);

fs.writeFileSync(path.join(baseDir, modName, "meta.json"), JSON.stringify(jsonMeta, null, 2));
fs.writeFileSync(path.join(baseDir, modName, "default_mod.json"), JSON.stringify(jsonDefaultMod, null, 2));
fs.writeFileSync(path.join(baseDir, modName, "group_001_special options.json"), JSON.stringify(SpecialOptionsGroup, null, 2));
fs.writeFileSync(path.join(baseDir, modName, "group_002_normal options.json"), JSON.stringify(NormalOptionsGroup, null, 2));
fs.writeFileSync(path.join(baseDir, modName, "group_003_folder options.json"), JSON.stringify(FolderOptionsGroup, null, 2));

console.log("制作完成");
