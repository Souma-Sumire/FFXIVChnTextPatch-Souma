# FFXIVChnTextPatch-Souma

基于 [FFXIVChnTextPatch-GP](https://github.com/GpointChen/FFXIVChnTextPatch-GP) 2022年10月18日版本修改

## 重要提醒

- 使用汉化补丁属于修改客户端行为，使用者需自愿承担风险
- **不可使用旧版本汉化**，会导致游戏文件不匹配和各种游戏问题
- 不要展示道具，别人看到的也是中文
- ACT解析插件语言与原客户端语言保持一致，例如英文或日文，设置后需要重启ACT

## 使用方法

### 方法一：使用汉化工具

1. 从 [Release](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/releases) 下载 `Source code(zip)`
1. 运行 `FFXIVChnTextPatchGP.exe`，根据界面提示进行汉化。

### 方法二：覆盖游戏文件

1. 从 [Release](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/releases) 下载覆盖版本 ZIP 文件（如`7.16.v1.zip`）
1. 覆盖游戏文件：
    - Windows：`...\FANTASY XIV\game\sqpack\ffxiv\`
    - macOS：`$HOME/Library/Application Support/FINAL FANTASY XIV ONLINE/Bottles/published_Final_Fantasy/drive_c/Program Files (x86)/SquareEnix/FINAL FANTASY XIV - A Realm Reborn/game/sqpack/ffxiv/`

### 方法三：Dalamud 插件 Penumbra 热加载

1. 首次使用这种方式时，[需要设置](#首次使用时-penumbra-需要设置)
1. 从 [Release](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/releases) 下载最新 pmp 文件（如`7.16.v1.pmp`）
1. 导入 Mod，重启游戏生效

#### 首次使用时 Penumbra 需要设置

1. 在 `Settings - Advanced` 关闭 `Auto Deduplicate on Import` 选项，否则导入后的 *Deduplicate* 过程将非常久
1. 在 `Settings - Advanced` 开启 `Wait for Plugins on Startup` 选项，否则无法正确加载汉化
1. 导入 [字体 Mod](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/releases/download/v2.4.4/HarmonyOS.Sans.pmp)

## 推荐搭配

- UI 标题图片翻译 Mod [karaipsum/Simplified Chinese UI Replacement Mod](https://www.nexusmods.com/finalfantasy14/mods/2048)
- 聊天文本翻译工具 [Tataru Assistant](https://home.gamer.com.tw/artwork.php?sn=5323128)

## 捐赠（微信）

![image](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/assets/33572696/1fec3974-0b6d-43df-9afc-2d760c33f9b5)
