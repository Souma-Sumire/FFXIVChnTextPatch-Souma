# FFXIVChnTextPatch-Souma

本项目在 [FFXIVChnTextPatch-GP](https://github.com/GpointChen/FFXIVChnTextPatch-GP) 的基础上进行修改，更适合简中人群使用。

## 汉化步骤

### 傻瓜式

  1. 请务必开启高清贴图，否则会导致 DirectX 崩溃。[设置方法](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/wiki/%E5%BC%80%E5%90%AF%E9%AB%98%E6%B8%85%E8%B4%B4%E5%9B%BE)
  1. 下载最新的 [Release](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/releases/)
  1. 将压缩包中的文件覆盖到 `..\FINAL FANTASY XIV\game\sqpack\ffxiv\`

### 手动式

  1. 请务必开启高清贴图，否则会导致 DirectX 崩溃。[设置方法](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/wiki/%E5%BC%80%E5%90%AF%E9%AB%98%E6%B8%85%E8%B4%B4%E5%9B%BE)
  1. 使用 Git 克隆本仓库到本地，新手可使用 Git 图形化工具 [Github Desktop](https://desktop.github.com/)
  1. 运行 `FFXIVChnTextPatchGP.exe`
      - 游戏路径：选择你的游戏安装目录（应包含 boot 和 game 文件夹）
      - 档案语言：选择CSV
      - 原始语言：根据你的客户端选择日文或英文
      - 勾选替换字体和替换文本
      - 点击确认

如果遇到任何问题，请先还原原始游戏文件，然后重新操作。

## 常见问题解答（QA）

### 1. 该版本与 GpointChen 的汉化补丁有什么不同？

| 项目 | FFXIVChnTextPatch-GP | FFXIVChnTextPatch-Souma |
| :---: | :---: | :---: |
| 显示语言 | 繁体中文、繁体机转简体中文 | 原生简体中文 |
| 汉化国服已有的文本 | √ | √ |
| 汉化新版本文本 | × | √ |
| Cactbot 正常运行 | × | √ |

### 使用汉化补丁是否安全？

本程序采用修改客户端文件的方式加载中文资源，使用本程序即表示您已了解此操作违反了官方规定，并确认愿意自行承担由此可能带来的一切后果。

### 如何在游戏版本更新后更新汉化补丁？

在游戏版本更新后，您应等待汉化补丁的新版本发布，并下载安装最新版本以确保兼容性。绝对不要继续使用旧版本的汉化补丁，因为这可能导致游戏崩溃和其他不稳定性问题。

### 在聊天对话框中使用汉化补丁是否安全？

请注意，在发送 `展示道具属性`、`<pos>`、`<flag>` 时，将会发送中文字符。

### 使用汉化补丁后应该如何设置ACT？

请确保 ACT 解析插件的语言设置与您的客户端语言版本相匹配。例如您的客户端语言版本为英文，则您应该将 ACT 解析插件的语言设置为英文。

### 我希望更换汉化补丁自带的中文字体

请参考 [Wiki](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/wiki/%E8%87%AA%E5%88%B6%E6%B8%B8%E6%88%8F%E5%AD%97%E4%BD%93) 内容。

### 如何汉化 UI 图片的部分？（例如切换场景、接取/完成任务时）

推荐搭配 [karaipsum/Simplified Chinese UI Replacement Mod](https://www.nexusmods.com/finalfantasy14/mods/2048) 为 UI 图片文字 进行汉化。

### 部分文本未被汉化

以下部分文本未被汉化：

- 敌人名称：若汉化则无法正常使用 Cactbot
- 新版本的鱼类相关：文本过多，难以翻译
- 部分 Addon 文字：指令码过于复杂


### 其他问题

请发起 Issues 或 加入QQ群 `231937107`

## 捐赠

![image](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/assets/33572696/33b547e0-f8d4-41ba-9d76-a813a8053daa)
