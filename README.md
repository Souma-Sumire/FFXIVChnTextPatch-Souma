# FFXIVChnTextPatch-Souma

本项目修改自 [FFXIVChnTextPatch-GP](https://github.com/GpointChen/FFXIVChnTextPatch-GP)

使用汉化补丁是属于修改客户端的作弊行为，使用本汉化即表示你自愿承担一切后果

## 可以使用上一个游戏版本的汉化吗？

**不可以**，这会导致游戏文件不匹配，导致各种各样的 bug（包括但不限于看不见NPC、对象无法交互、物品效果错误、无法发动技能等），轻则游戏崩溃，重则封号。

FF14的热更新也算是一个新版本。举例，以下版本均为互不兼容

- 7.05-001
- 7.05-hf1-002
- 7.05-hf2-003

若此时游戏处于 7.05-hf2（小版本为7.05 + 第二次热更新），此时你应当使用 **7.0-hf2** 开头的汉化补丁。

## 如何使用

### 方法一：借助 Dalamud 插件 Penumbra 进行热加载

从 [Release](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/releases/) 下载最新版本附件的 *\[兼容版本号\]-**penumbra**-v\[第N版\]\.zip*

#### 第一次使用时

1. 打开 Penumbra 的设置页面
1. 在 `Settings - Advanced` 关闭 `Auto Deduplicate on Import` 选项，否则导入后的 *Deduplicate* 过程将非常久
1. 在 `Settings - Advanced` 开启 `Wait for Plugins on Startup` 选项，否则无法正确加载汉化
1. 导入任一字体 Mod，用于显示中文字体，例如本项目提供的 [国服同款字体](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/releases/download/v2.1.6/ChnAXIS.pmp) / [其他字体](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/wiki/%E8%87%AA%E5%88%B6%E6%B8%B8%E6%88%8F%E5%AD%97%E4%BD%93)
1. 导入文本汉化 Mod （`.pmp` 文件）
1. 在 `Mods` 页面，启用这两个 Mod（选中后勾选 `Enabled`），并可按需调整 Options 设置
1. 重新启动游戏，才能生效

#### 以后更新

- 快捷更新方法：将 `.pmp` 文件使用压缩软件进行解压，将得到文件夹的直接替换以前旧的 Mod 文件夹即可（存放在 Penumbra 设置的 Mod 目录中），即可直接更新为新版本
- 笨比更新方法：如果你看不懂上一条，则你需要：上号、在插件里删掉旧 Mod、再添加新 Mod、再重启游戏

### 方法二：覆盖游戏文件

从 [Release](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/releases/) 下载最新版本附件的 *\[兼容版本号\]-**overwrite**-v\[第N版\]\.zip*

将这6个文件覆盖到游戏目录下 `FINAL FANTASY XIV\game\sqpack\ffxiv\`

如果你需要恢复原文件，使用对应版本001中发布的 backup.zip 文件即可（仅在每个 Release 版本的 001 中发布一次，后续不再重复上传）

### 方法三：使用汉化器

1. 版本更新后，需等待我更新，否则用了也报错。
1. 右上角绿色按钮下载本仓库ZIP
1. 解压后，启动 `FFXIVChnTextPatchGP.exe`
1. 点击右上角「设置」
    1. 「游戏路径」：选择FFXIV游戏根目录（例如：D:\SquareEnix\FINAL FANTASY XIV - A Realm Reborn）
    1. 「档案语言」：选择CSV
    1. 「原始語言」：日文或英文，根据你的客户端语言选择
    1. 勾选「替换字体」、「替换文本」
    1. 点击确认

## 推荐搭配

- UI 标题图片翻译 Mod： [karaipsum/Simplified Chinese UI Replacement Mod](https://www.nexusmods.com/finalfantasy14/mods/2048)
- 聊天文本翻译工具：[Tataru Assistant](https://home.gamer.com.tw/artwork.php?sn=5323128)

## 常见问题

### 更新之前要还原吗？

不知道。可能不用，但我每次都会先还原。

### 如何设置ACT解析插件语言？

与原客户端语言保持一致，例如英文或日文。设置后需要重启ACT才会生效（这是解析插件的问题）

### 别人看得到我的汉化吗？

注意！当你展示道具 `<item>` 时，别人会如实看到你所展示的物品名称中文（以发送者的语言显示）

### 喊话任务的处理？

国服已有的内容会统一替换为E端文本，国服未更新的任务则以你的客户端原文为准

### 任务文本有误？

因为任务量极大，我不可能逐个检查每个任务文本。

所以任务文本会直接照搬国服文本，若存在大技改导致职业任务文本变更（例如 7.0 的机工、武僧），需你自行尝试他改成了什么技能，并且将这个情况反馈给我，否则你等多久也不会解决（我不是魔法师，不会平白无故的发现这些“错误”）

### 如何更换字体？

参考 [Wiki](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/wiki/%E8%87%AA%E5%88%B6%E6%B8%B8%E6%88%8F%E5%AD%97%E4%BD%93) 内容

### 故意未汉化的文本

为了正常使用 Cactbot Raidboss 功能，以下文本未被翻译：

- 敌人名称
- 倒计时相关的提示语句
- 封锁区域的提示语句

### 我希望汉化敌人名称

若使用 Penumbra 热加载，在 Mod 的 Options 设置界面，勾选 BNpcName 汉化即可

若使用汉化器汉化，参考 `resource\options` 文件夹内说明

### 与 Cactbot Radar 的兼容性问题

由于 LogMessage.csv#1350 被汉化，导致 Cactbot Radar 在切线后不会清空之前找到的怪物列表。考虑到使用 Radar 的人较少，不做兼容性修复（也就是不翻译这句话）

如有 Radar 使用需求的可以将 Radar 链接替换为我修改过源码实现兼容的这个链接：`https://souma.diemoe.net/cactbot/ui/radar/radar.html`

### 与 Dalamud Plugins（卫月插件）的兼容性题

若在汉化后，发现个别 Dalamud 插件功能无法使用，可尝试更换汉化方式，**也许**能解决

### 其他问题

- 先浏览：[提问的智慧](https://github.com/ryanhanwu/How-To-Ask-Questions-The-Smart-Way/blob/main/README-zh_CN.md) / [如何有效地报告 Bug](https://www.chiark.greenend.org.uk/~sgtatham/bugs-cn.html)
- 发起 Issues 或加入QQ群 `231937107` 寻求帮助（不要问文档里说过的问题）

## 贡献和支持

如果这个项目对你有所帮助，欢迎打赏支持（微信）！

![image](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/assets/33572696/1fec3974-0b6d-43df-9afc-2d760c33f9b5)

若你觉得本汉化项目对你有帮助，欢迎给予 Star 以示鼓励！

[![Star History Chart](https://api.star-history.com/svg?repos=Souma-Sumire/FFXIVChnTextPatch-Souma&type=Timeline)](https://star-history.com/#Souma-Sumire/FFXIVChnTextPatch-Souma&Timeline)
