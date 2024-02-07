# FFXIVChnTextPatch-Souma

本项目基于 [FFXIVChnTextPatch-GP](https://github.com/GpointChen/FFXIVChnTextPatch-GP)

## 特别注意事项

本程序采用修改客户端文件的方式加载中文资源。

使用本程序即表示您已了解此操作违反了官方规定，并确认愿意自行承担由此可能带来的一切后果。

在使用 `展示道具属性`、`<pos>`、`<flag>` 功能时，将会发送 **中文** 字符。

请确保 ACT 解析插件的语言设置与您的客户端语言版本相匹配。

## 汉化步骤

### 傻瓜式

  1. 请务必开启高清贴图，否则可能导致 DirectX 崩溃。[设置方法](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/wiki/%E5%BC%80%E5%90%AF%E9%AB%98%E6%B8%85%E8%B4%B4%E5%9B%BE)
  1. 下载最新的 [Release](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/releases/)
  1. 将压缩包中的文件覆盖到 `..\FINAL FANTASY XIV\game\sqpack\ffxiv\`
  1. 当游戏版本更新时，请等待新的 Release 更新。
  1. 绝对禁止在游戏版本更新后使用旧版本的 Release 文件。这会导致游戏随时崩溃，并增加封号风险。

### 手动式

  1. 请务必开启高清贴图，否则可能导致 DirectX 崩溃。[设置方法](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/wiki/%E5%BC%80%E5%90%AF%E9%AB%98%E6%B8%85%E8%B4%B4%E5%9B%BE)
  1. 使用 Git 克隆本仓库到本地，新手可使用 Git 图形化工具 [Github Desktop](https://desktop.github.com/)
  1. 运行 `FFXIVChnTextPatchGP.exe`
      - 游戏路径：选择你的游戏安装目录（应包含 boot 和 game 文件夹）
      - 档案语言：选择CSV
      - 原始语言：根据你的客户端选择日文或英文
      - 勾选替换字体和替换文本
      - 点击确认

如果遇到任何问题，请先还原原始游戏文件，然后重新操作。

## 其他

### UI图片文字

推荐使用 [karaipsum/Simplified Chinese UI Replacement Mod](https://www.nexusmods.com/finalfantasy14/mods/2048) 来汉化 UI 图片文字。

### 字体

如果你希望更改中文字体，请参考 [Wiki](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/wiki/%E8%87%AA%E5%88%B6%E6%B8%B8%E6%88%8F%E5%AD%97%E4%BD%93)

## 捐赠

![image](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/assets/33572696/33b547e0-f8d4-41ba-9d76-a813a8053daa)

