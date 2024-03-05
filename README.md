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

在每次汉化开始前，请保证游戏文件是原版文件，再开始操作。

#### CRLF问题

`FFXIVChnTextPatchGP.exe` 似乎无法正确解析换行符为 LF 的 CSV 文件。而应是 CRLF 。

如果你汉化过程中没有任何错误，但游戏文本却没有被汉化。可以检查一下CSV是否被设置成了LF换行符。同时在 `debug.log` 应该也会存在对应的 java 报错文本。

## 常见问题解答（QA）

### 该版本与 GpointChen 的汉化补丁有什么不同？

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

### 发现部分旧主线未翻译

主线部分的汉化全部为程序自动化执行，会根据每一句话的唯一 ID 去国服解包中寻找对应文本填入，因此若你在跑主线（国服已更新的）中发现部分台词未被翻译，这是由于国际服对这部分的台词 ID 进行了修改，导致无法对应到国服文本。这不是 bug，因此无需汇报。

### 故意未汉化的文本

为了正常使用 Cactbot，以下文本未被翻译：

- 敌人名称
- 倒计时相关的提示语句
- 副本进入、离开的提示语句
- 部分情感动作（举手、鞠躬、告别等）语句

### 被放弃汉化的新文本

新版本更新时，精力有限，一些文本会被我跳过。

#### 确定不做的新文本

- 全部捕鱼人相关新文本
- 新成就说明文本（Achievement）
- 极长的新功能说明性文本（Addon）
- 掺杂了复杂指令码的新文本（ActionTransient、Addon、AddonTransient、LogMessage、Lobby）
- 新任务道具说明（EventItemHelp）
- 新道具描述（Item）
- 限定节日活动相关

#### 工作量不大时会做的新文本

- 对话台词（DefaultTalk、NpcYell、ContentTalk、InstanceContentTextData、Balloon、CustomTalk）
- 见闻录相关（AkatsukiNoteString、MYCWarResultNotebook、Adventure、VVDNotebookContents）
- 副本搜索器说明（ContentFinderConditionTransient）
- 新系统的UI文本（例如曾经的糖豆人联动、曾经的无人岛、曾经的多变迷宫）
- 新增宏的说明文本（TextCommand）
- 解说文本（DescriptionString）
- 新手教程文本（HowToPage）
- 坐骑相关文本（Mount，MountTransient）
- 九宫幻卡相关（TripleTriadCard）
- 木人挑战相关文本（DpsChallenge）
- 传送相关（Warp）

即使大量跳过的情况下每次版本更新也需要数十个小时的工作量，请理解。

### 其他问题

- 如有其他问题，请通过发起Issues或加入QQ群 `231937107` 寻求帮助。
- 我并非您的保姆，因此提出过于愚蠢的问题将被无视。[提问的智慧](https://github.com/ryanhanwu/How-To-Ask-Questions-The-Smart-Way/blob/main/README-zh_CN.md)

## 捐赠

![image](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/assets/33572696/33b547e0-f8d4-41ba-9d76-a813a8053daa)
