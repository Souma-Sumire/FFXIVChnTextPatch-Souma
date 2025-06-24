# FFXIVChnTextPatch-Souma

基于 [FFXIVChnTextPatch](https://github.com/reusu/FFXIVChnTextPatch)、[FFXIVChnTextPatch-GP](https://github.com/GpointChen/FFXIVChnTextPatch-GP)

---

## 汉化游戏

无论使用下列哪种方法，都需要高分辨率贴图，否则游戏会因字体渲染问题导致DX崩溃![系统设置](https://github.com/user-attachments/assets/7aa1867e-1151-47aa-85a2-f7694ca8b56b)

### 方式一：使用汉化工具

1. 在 [最新发行版](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/releases)，下载 `Assets` 中的 `Source code (zip)` 文件
1. 确保游戏文件为 **原版未修改**
1. 运行 `FFXIVChnTextPatchGP.exe`，按界面提示操作。
   - 如需漢化為繁體中文，請先執行根目錄下的 `converter.exe` 並輸入 `1` 對 CSV 檔案進行轉換，再執行 `FFXIVChnTextPatchGP.exe`。

  > 汉化执行后，原始文件会自动备份至 backup 文件夹内。
  > ⚠️ 请勿重复执行汉化，否则备份文件也会被覆盖。

### 方式二：Penumbra 热加载

1. 在 [最新发行版](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/releases)，下载 `Assets` 中的 `mod.pmp` 文件
1. 首次使用前，需要对 Penumbra 进行设置：
   - ❌ 关闭 `Advanced` - `Auto Deduplicate on Import`
   - ✅ 启用 `Advanced` - `Wait for Plugins on Startup`
   - 安装任一 [字体 Mod](#现成字体-mod)
1. 导入 Mod 并重启游戏
   > 或手动解压 `.pmp`，在启动游戏前将其复制至 Penumbra 的 Mod 文件夹中

### 方式三：覆盖文件
  >
  > 注意，此版本汉化了敌人名称。

1. 在 [最新发行版](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/releases)，下载 `Assets` 中的 `overwrite.zip` 文件
1. 文件覆盖到图示位置（建议提前备份原文件）![image](https://github.com/user-attachments/assets/7e87c0ff-4ad8-4c2f-ba67-d605fec0619f)

  > macOS路径：`$HOME/Library/Application Support/FINAL FANTASY XIV ONLINE/Bottles/published_Final_Fantasy/drive_c/Program Files (x86)/SquareEnix/FINAL FANTASY XIV - A Realm Reborn/game/sqpack/ffxiv/`

## 与第三方插件的兼容性

- 如果你需要将 ACT 日志上传至 FFLOGS 网站，则不要汉化敌人名称（BNpcName），否则会影响 ACT 日志的生成。
- 如果你需要使用 Cactbot - Raidboss 自带触发器，则不要汉化敌人名称（BNpcName），否则无法使用。
- 如果你需要使用 Cactbot - Rader 雷达，则不要对系统日志（LogMessage）、敌人名称（BNpcName）进行汉化，否则无法使用。

## 推荐搭配

- UI 标题翻译：[karaipsum/Simplified Chinese UI Replacement Mod](https://www.nexusmods.com/finalfantasy14/mods/2048)
- 聊天翻译工具：[Tataru Assistant](https://home.gamer.com.tw/artwork.php?sn=5323128)

## 修改游戏字体

### 现成字体 Mod

- [HarmonyOS Sans](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/releases/download/v2.11.5/HarmonyOS.Sans.pmp)
- [ChnAXIS](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/releases/download/v2.4.4/ChnAXIS.pmp)
- [四款替换用 UI 字体(黑|宋|圆) 国际服国服双版本](https://bbs.tggfl.com/topic/221/%E5%9B%9B%E6%AC%BE%E6%9B%BF%E6%8D%A2%E7%94%A8-ui-%E5%AD%97%E4%BD%93-%E9%BB%91-%E5%AE%8B-%E5%9C%86-%E5%9B%BD%E9%99%85%E6%9C%8D%E5%9B%BD%E6%9C%8D%E5%8F%8C%E7%89%88%E6%9C%AC)

### 自制字体工具

- [FFXIV-FontChanger（原版）](https://github.com/Soreepeong/FFXIV-FontChanger) / [中文汉化版](https://github.com/AtmoOmen/FFXIV-FontChanger)
- 教程：[B站视频](https://www.bilibili.com/video/BV1XG411g7Gt) / [Imgur图文教程](https://imgur.com/a/Cojm6og)

## 捐赠支持

如果你觉得项目有帮助，**点个 Star** 同样意义重大！

![捐赠二维码](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/assets/33572696/1fec3974-0b6d-43df-9afc-2d760c33f9b5)


---

```text
此项目仅供学习技术以及技术交流使用
严禁使用于任何商业用途
请下载后24小时内删除
使用汉化补丁属于修改客户端行为，使用者需自愿承担风险
```
