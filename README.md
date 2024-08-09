# FFXIVChnTextPatch-Souma

本项目修改自 [FFXIVChnTextPatch-GP](https://github.com/GpointChen/FFXIVChnTextPatch-GP)

## 声明

使用汉化补丁是属于修改客户端的作弊行为，使用本汉化即表示你自愿承担一切后果。

## 下载 & 安装

### 方法一：直接覆盖游戏文件

- 优点：简单
- 缺点：文件大，更新麻烦，需要额外备份原文件

从 [Release](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/releases/) 下载最新版本附件的 overwrite.zip，并参考  Wiki：[通过覆盖文件汉化](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/wiki/%E9%80%9A%E8%BF%87%E8%A6%86%E7%9B%96%E6%96%87%E4%BB%B6%E6%B1%89%E5%8C%96)

如果你需要恢复原文件，使用 backup.zip 文件即可（仅在每个版本的 001 中发布一次，后续不再重复上传）

### 方法二：使用 Penumbra 加载

- 优点：文件小，更新方便，不改动原始文件，可以选择性汉化（尚未实装）
- 门槛：你需要会使用 Dalamud & Peanumbra
- 缺点：需要依赖 Dalamud & Penumbra

从 [Release](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/releases/) 下载最新版本附件的 penumbra.zip，并参考 Wiki：[使用 Penumbra 加载](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/wiki/%E4%BD%BF%E7%94%A8-Penumbra-%E5%8A%A0%E8%BD%BD)

### 方法三：使用汉化器汉化

- 优点：可以选择性汉化，可以随时拉取最新分支csv（借助Git）
- 门槛：你需要熟练使用 Git
- 缺点：同样是修改本地文件，需要额外备份原文件

参考 Wiki：[使用汉化器汉化](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/wiki/%E4%BD%BF%E7%94%A8%E6%B1%89%E5%8C%96%E5%99%A8%E6%B1%89%E5%8C%96)

### 推荐搭配

- 主线文本翻译：[Tataru Assistant](https://home.gamer.com.tw/artwork.php?sn=5323128)
- UI 标题图片翻译： [karaipsum/Simplified Chinese UI Replacement Mod](https://www.nexusmods.com/finalfantasy14/mods/2048)

## 常见问题

### 可以使用旧版本的 Release 吗？

**不可以！** 这是 **非常不安全且傻逼** 的行为

只要游戏有更新包了就是一个新的版本，即使是所谓的“热更新”，只要你启动的时候有更新，就算是新的版本。此时，以前的汉化全部作废，不可以使用。不要自作聪明。

### 更新之前要还原吗？

我不知道。我要负责发备份文件的，我肯定还原啊，你们用不用还原我哪知道……谁试出来了告诉我谢谢你

### 如何设置ACT解析插件语言？

与原客户端语言保持一致，例如英文或日文。设置后需要重启ACT才会生效（这是ACT的毛病）

### 别人看得到我的汉化吗？

注意！当你展示道具 `<item>` 时，别人会如实看到你所展示的物品名称中文（以发送者的语言显示）

### 喊话任务的处理？

国服已有的内容会统一替换为E端文本，国服未更新的任务则以你的客户端原文为准

### 我是工匠需要查外网攻略，不希望道具名称被汉化。我该怎么办？

使用汉化器汉化，在详细设置中不勾选 `Item.csv` 即可。

### 任务文本有误？

因为任务量极大，我不可能逐个检查每个任务文本。

所以任务文本会直接照搬国服文本，若存在大技改导致职业任务文本变更（例如 7.0 的机工、武僧），请你自行尝试他改成了什么技能，并且将这个情况反馈给我，否则你等多久也不会解决（我不是魔法师，不会平白无故的发现这些“错误”）

### 如何更换字体？

请参考 [Wiki](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/wiki/%E8%87%AA%E5%88%B6%E6%B8%B8%E6%88%8F%E5%AD%97%E4%BD%93) 内容

### 故意未汉化的文本

为了正常使用 Cactbot Raidboss 功能，以下文本未被翻译：

- 敌人名称
- 倒计时相关的提示语句
- 封锁区域的提示语句

### 与 Cactbot Radar 的兼容性问题

由于 LogMessage.csv#1350 被汉化，导致 Cactbot Radar 在切线后不会清空之前找到的怪物列表。考虑到使用 Radar 的人较少，不做兼容性修复（也就是不翻译这句话）

如有 Radar 使用需求的可以将 Radar 链接替换为我修改过源码实现兼容的这个链接：`https://souma.diemoe.net/cactbot/ui/radar/radar.html`

### 与 Dalamud Plugins（卫月插件）的兼容性

若在汉化后，发现个别 Dalamud 插件功能无法使用，可尝试更换汉化方式（Mod 式 / 覆盖式），也许能解决

### 其他问题

- 请先浏览：[提问的智慧](https://github.com/ryanhanwu/How-To-Ask-Questions-The-Smart-Way/blob/main/README-zh_CN.md) / [如何有效地报告 Bug](https://www.chiark.greenend.org.uk/~sgtatham/bugs-cn.html)
- 发起 Issues 或加入QQ群 `231937107` 寻求帮助

谁他妈也不是你的保姆，**不要问文档里说过的问题**！连中文都看不懂的话，就别玩国际服了行吗 QAQ

## 贡献和支持

如果这个项目对你有所帮助，欢迎打赏支持（微信）！

![image](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/assets/33572696/1fec3974-0b6d-43df-9afc-2d760c33f9b5)

若你觉得本汉化项目对你有帮助，欢迎给予 Star 以示鼓励！

[![Star History Chart](https://api.star-history.com/svg?repos=Souma-Sumire/FFXIVChnTextPatch-Souma&type=Timeline)](https://star-history.com/#Souma-Sumire/FFXIVChnTextPatch-Souma&Timeline)

### 赞助名单

感谢以下大佬的支持（仅统计2024年之后的赞赏码渠道）：

- 2024年02月13日 匿名
- 2024年06月28日 A******
- 2024年06月29日 邱**
- 2024年06月30日 匿名
- 2024年06月30日 黑*
- 2024年07月16日 L********
- 2024年07月16日 匿名
- 2024年07月21日 E*********
- 2024年08月02日 面*
- 2024年08月02日 匿名
- 2024年08月02日 吉***
- 2024年08月02日 C******
- 2024年08月03日 匿名
- 2024年08月04日 匿名
- 2024年08月05日 匿名
- 2024年08月05日 匿名
- 2024年08月05日 匿名
