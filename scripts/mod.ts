// 删掉上次的 exd 文件夹 -> FFXIV Data Explorer -> 国际服的 0a0000 -> 全选导出 Raw 到 E:\Github\FFXIVChnTextPatch-Souma\scripts

import fs from "fs";
import path from "path";
import { csvName, folderName } from "./names.ts";

const MINOR_VERSION_MODIFIED = false; // 游戏版本号变更，MINOR 版本号 +1

const csvNames = Object.keys(csvName).map((v) => v.toLowerCase());
const csvNamesObj = Object.fromEntries(Object.entries(csvName).map(([k, v]) => [k.toLowerCase(), v]));
const modName = "Chinese Text Patch";
const sourceDir = path.join(__dirname, "exd");
const targetDir = path.join(__dirname, modName, "exd");
const oldVersion = require(path.join(__dirname, modName, "meta.json")).Version;

const jsonMeta = {
  FileVersion: 3,
  Name: modName,
  Author: "Souma",
  Description: `国际服汉化补丁`,
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
  ModTags: [],
};

const jsonDefaultMod = {
  Name: "",
  Description: "",
  Files: {} as any,
  FileSwaps: {} as any,
  Manipulations: [] as any[],
};

const SpecialOptionsGroup = {
  Version: 0,
  Name: "Special Options",
  Description: "特殊选项（重启游戏后成效）",
  Image: "",
  Priority: 0,
  Type: "Multi",
  DefaultSettings: 6,
  Options: [] as any[],
};

const NormalOptionsGroup = {
  Version: 0,
  Name: "Normal Options",
  Description: "可选项 单文件（重启游戏后成效）",
  Image: "",
  Priority: 0,
  Type: "Multi",
  DefaultSettings: 16383,
  Options: [] as any[],
};

const FolderOptionsGroup = {
  Version: 0,
  Name: "Folder Options",
  Description: "可选项 文件夹（重启游戏后成效）",
  Image: "",
  Priority: 0,
  Type: "Multi",
  DefaultSettings: 16383,
  Options: [] as any[],
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

const specialOptions = {
  BNpcName: "警告：此选项会导致 Cactbot 默认触发器失效，并且影响 Logs 上传结果。",
  Item: "警告：此选项会导致发送“展示道具”时，在别人眼中也会变成中文！",
  Quest: "警告：此选项会导致发送“任务”时，在别人眼中也会变成中文！",
};

// 清空exd文件夹
fs.rmSync(targetDir, { recursive: true, force: true });

// 递归遍历目录并复制文件
function traverseAndCopyFiles(currentDir: string, stack = 0) {
  // 读取当前目录下的所有文件和子目录
  fs.readdirSync(currentDir).forEach((file) => {
    const filePath = path.join(currentDir, file);
    const stat = fs.statSync(filePath);

    if (stat.isDirectory()) {
      // 如果是目录，则递归遍历
      traverseAndCopyFiles(filePath, stack + 1);
    } else if (stat.isFile() && (file.includes("_ja") || file.includes("_en") || file.includes("_ko") || file.includes("_tc"))) {
      const rawName = file.replace(/^([^_]+).*?_.+$/, "$1");
      const relativePath = path.relative(sourceDir, filePath);
      const targetPath = path.join(targetDir, relativePath);
      const targetDirPath = path.dirname(targetPath);
      const stackIsZero = stack === 0;

      if (stackIsZero) {
        if (!csvNames.includes(rawName)) {
          // 第一层目录下文件 ${file} 不在文件列表中，跳过
          return;
        }
      }
      const name = stackIsZero ? rawName : relativePath.match(/^[^\\]+/)?.[0] ?? "";
      const cn = stackIsZero ? csvNamesObj[name] : folderName[name as keyof typeof folderName];
      if (!cn) {
        console.error(`找不到 ${name} 的中文名，请检查 names.ts 文件`);
        return;
      }
      const humpName = Object.keys(csvName).find((v) => v.toLowerCase() === name.toLowerCase())!;
      const group = specialOptions[humpName as keyof typeof specialOptions] ? SpecialOptionsGroup : NormalOptionsGroup;
      if (stackIsZero && optionsNames.includes(name)) {
        const key = `汉化${cn}（${humpName}）${specialOptions[humpName as keyof typeof specialOptions] ?? ""}`;
        if (!group.Options.find((v) => v.Name === key)) {
          group.Options.push({
            Name: key,
            Description: "",
            Files: {},
            FileSwaps: {},
            Manipulations: [],
            Priority: 1,
          });
        }
        group.Options.find((v) => v.Name === key).Files[`exd/${relativePath.replace(/\\/g, "/")}`] = `exd\\${relativePath}`;
      } else if (!stackIsZero) {
        const key = `汉化${folderName[name as keyof typeof folderName]}（${name}）文件夹`;
        if (!FolderOptionsGroup.Options.find((v) => v.Name === key)) {
          FolderOptionsGroup.Options.push({
            Name: key,
            Description: "",
            Files: {},
            FileSwaps: {},
            Manipulations: [],
            Priority: 1,
          });
        }
        FolderOptionsGroup.Options.find((v) => v.Name === key).Files[`exd/${relativePath.replace(/\\/g, "/")}`] = `exd\\${relativePath}`;
      } else {
        jsonDefaultMod.Files[`exd/${relativePath.replace(/\\/g, "/")}`] = `exd\\${relativePath}`;
      }

      // 确保目标目录存在，如果不存在则创建
      fs.mkdirSync(targetDirPath, { recursive: true });

      // 复制文件到目标目录
      fs.copyFileSync(filePath, targetPath);
    }
  });
}

traverseAndCopyFiles(sourceDir, 0);

fs.writeFileSync(path.join(__dirname, modName, "meta.json"), JSON.stringify(jsonMeta, null, 2));
fs.writeFileSync(path.join(__dirname, modName, "default_mod.json"), JSON.stringify(jsonDefaultMod, null, 2));
fs.writeFileSync(path.join(__dirname, modName, "group_001_special options.json"), JSON.stringify(SpecialOptionsGroup, null, 2));
fs.writeFileSync(path.join(__dirname, modName, "group_002_normal options.json"), JSON.stringify(NormalOptionsGroup, null, 2));
fs.writeFileSync(path.join(__dirname, modName, "group_003_folder options.json"), JSON.stringify(FolderOptionsGroup, null, 2));

console.log("制作完成");
