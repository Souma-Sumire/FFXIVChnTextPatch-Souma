# FFXIVChnTextPatch-Souma

本项目修改自 [FFXIVChnTextPatch-GP](https://github.com/GpointChen/FFXIVChnTextPatch-GP)

## 汉化

1. 推荐 [开启高分辨率贴图](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/wiki/%E5%BC%80%E5%90%AF%E9%AB%98%E6%B8%85%E8%B4%B4%E5%9B%BE)，否则可能会导致 DirectX 崩溃（未实测）
1. 请从 [Release](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/releases/) 下载

推荐搭配 [karaipsum/Simplified Chinese UI Replacement Mod](https://www.nexusmods.com/finalfantasy14/mods/2048) 使用

## 众所周知

- 修改客户端属于作弊行为，使用本汉化即表示你自愿承担一切后果
- 请不要在公共场合发送中文（当你展示道具 `<item>` 时，别人会如实看到你所展示的物品名称中文）
- 低调使用，不建议开直播、公开讨论、截图上传至社交媒体（尤其是境外平台）

## 常见问题解答（QA）

### 该版本与 GpointChen 的汉化补丁有什么不同？

| 项目 | FFXIVChnTextPatch-GP | FFXIVChnTextPatch-Souma |
| :---: | :---: | :---: |
| 显示语言 | 繁体中文、繁体机转简体中文 | 原生简体中文 |
| 汉化国服已有的文本 | √ | √ |
| 汉化新版本文本 | × | √ |
| Cactbot 正常运行 | × | √ |

### 可以使用旧版本的 Release 吗？

游戏版本更新后，应等待 Release 的新版本发布

使用旧版本的 Release 会导致游戏崩溃、数据非法等问题

### 如何设置ACT解析插件语言？

与原客户端语言保持同步，例如英文或日文

### 喊话任务的处理？

国服已有的内容会统一替换为E端文本，国服未更新的任务则以你的客户端原文为准。

### 如何更换字体？

请参考 [Wiki](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/wiki/%E8%87%AA%E5%88%B6%E6%B8%B8%E6%88%8F%E5%AD%97%E4%BD%93) 内容

### 故意未汉化的文本

为了正常使用 Cactbot，以下文本未被翻译：

- 敌人名称
- 倒计时相关的提示语句

### 与 Dalamud Plugins（卫月插件）的兼容性

若在汉化后，发现个别 Dalamud 插件功能无法使用，可尝试更换汉化方式，也许就会解决。

- 例如你在国际服使用国服的插件，则建议使用替换文件型汉化，这样可以解决 SelectString 不匹配导致的不工作，但无法解决 Sig 特征码不匹配导致的不工作。（本条仅7.0前有效，大版本更新后大概率需要更新特征码）
- 例如你在国际服使用国际服插件，在替换文件型汉化后插件无法使用了，则建议使用 Mod 型汉化，这样汉化不会影响到该 Dalamud 插件的文本获取。

### 其他问题

- 我并非你的保姆，因此提出过于愚蠢的问题将被无视
- 为了更好的解决你的问题，请先浏览：[提问的智慧](https://github.com/ryanhanwu/How-To-Ask-Questions-The-Smart-Way/blob/main/README-zh_CN.md) / [如何有效地报告 Bug](https://www.chiark.greenend.org.uk/~sgtatham/bugs-cn.html)
- 发起 Issues 或加入QQ群 `231937107` 寻求帮助

## 捐赠

![image](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/assets/33572696/1fec3974-0b6d-43df-9afc-2d760c33f9b5)
