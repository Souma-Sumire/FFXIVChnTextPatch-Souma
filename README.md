# FFXIVChnTextPatch-Souma

基于 [FFXIVChnTextPatch-GP](https://github.com/GpointChen/FFXIVChnTextPatch-GP) 进行分支开发，专门为简体中文用户制作的版本。

## 特别注意

本程序采用修改客户端的方式加载中文资源。

使用本程序表示您已经了解这是违反官方规定的操作，并且确认愿意自行承担因使用程序而可能产生的任何后果。

当您使用 `展示道具属性`、`<pos>`、`<flag>` 功能时，将会发送 **中文** 字符。

使用此汉化时，请确保 ACT 解析插件的语言设置与您的客户端语言版本相匹配。

## 汉化步骤

### 傻瓜式

  1. 开启高清贴图，否则会DX崩溃。[查看设置方法](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/wiki/%E5%BC%80%E5%90%AF%E9%AB%98%E6%B8%85%E8%B4%B4%E5%9B%BE)
  1. 下载最新的 [Release](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/releases/latest)
  1. 打开游戏目录 `.\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ffxiv\`
  1. 备份以下6个原文件
      - 000000.win32.dat0
      - 000000.win32.index
      - 000000.win32.index2
      - 0a0000.win32.dat0
      - 0a0000.win32.index
      - 0a0000.win32.index2
  1. 使用压缩包内的文件覆盖这些文件。
  1. 游戏版本更新时，请：
      - 恢复第4步时备份的原版文件
      - 执行游戏更新
      - 等待Release更新后，重新执行上述操作
      - 若未更新Release，请等待。使用旧版本的文件将存在兼容性问题以及更高的违规风险

### 手动式

  1. 开启高清贴图，否则会DX崩溃。[查看设置方法](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/wiki/%E5%BC%80%E5%90%AF%E9%AB%98%E6%B8%85%E8%B4%B4%E5%9B%BE)
  1. 克隆本仓库到本地
  1. 打开游戏目录 `.\FINAL FANTASY XIV - A Realm Reborn\game\sqpack\ffxiv\`
  1. 备份以下6个原文件
      - 000000.win32.dat0
      - 000000.win32.index
      - 000000.win32.index2
      - 0a0000.win32.dat0
      - 0a0000.win32.index
      - 0a0000.win32.index2
  1. 运行 `FFXIVChnTextPatchGP.exe`
  1. 游戏路径：选择你的客户端路径，例如 `D:\game\FINAL FANTASY XIV Online`
  1. 档案语言：选择CSV
  1. 原始语言：选择你的客户端语言
  1. 勾选替换字体、替换文本
  1. 点击确认
  1. 游戏版本更新时，请：
      - 先恢复原版文件
      - 执行游戏更新
      - 拉取仓库最新版本 `git pull` 或使用Git图形化工具：[Github Desktop](https://desktop.github.com/)
      - 成功合并最新CSV后，重新执行上述操作
      - 若未更新CSV，请等待。使用旧版本的文件将存在兼容性问题以及更高的违规风险

## 修改中文字体

参考 [Wiki](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/wiki/%E8%87%AA%E5%88%B6%E6%B8%B8%E6%88%8F%E5%AD%97%E4%BD%93)

## 投喂

![image](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/assets/33572696/3a03b0fd-27ba-4062-b48a-009bf2ce637b)
