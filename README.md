# FFXIVChnTextPatch-Souma

基于 [FFXIVChnTextPatch](https://github.com/reusu/FFXIVChnTextPatch)、[FFXIVChnTextPatch-GP](https://github.com/GpointChen/FFXIVChnTextPatch-GP)

---

## 使用

### 汉化步骤

1. 本项目自带字体需要 **先开启** 高分辨率贴图，否则游戏会因字体渲染问题导致DX崩溃![系统设置](https://github.com/user-attachments/assets/7aa1867e-1151-47aa-85a2-f7694ca8b56b)
1. 下载本项目到本地
1. 确保游戏文件为 **最新版本的、原版的、未修改过的纯净游戏文件**。必须确认游戏文件与当前游戏版本 **完全一致**，包括 Hotfix 版本！
1. 运行 `FFXIVChnTextPatchGP.exe`，按界面提示操作。
   - 如需漢化為繁體中文，請先執行根目錄下的 `converter.exe` 並輸入 `1` 對 CSV 檔案進行轉換，再執行 `FFXIVChnTextPatchGP.exe`。

### 与第三方插件的兼容性

| 第三方插件                    | 不要汉化                        | 说明                                 |
| -------------------------- | ------------------------------- | ---------------------------------- |
| **FFLOGS 日志上传**              | 敌人名称（BNpcName）                  | 否则会影响 ACT 日志生成   |
| **Cactbot - Raidboss 触发器** | 敌人名称（BNpcName）                  | 否则无法匹配正则表达式；且解析插件语言需保持原本的语言（英语或日语）。 |
| **Cactbot - Radar 雷达**     | 敌人名称（BNpcName）、系统日志（LogMessage） | 否则雷达无法正常识别目标、识别副本区切换。                      |
| **Lifestream**             | 登录大厅（Lobby）                     | 否则自动登录等功能无法使用。                      |

表中未提及的插件，可自行举一反三，将相关字段保持为原文。[查询CSV文本范围](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/wiki/CSV%E6%96%87%E4%BB%B6)

### 额外注意事项

- 当你 `<t>` 时，别人看到的有可能是中文，目前尚未确定机制。[可至此Issues讨论](https://github.com/GpointChen/FFXIVChnTextPatch-GP/issues/526)，可能的表有 BNpcname（大概率安全）、EObjName（怀疑）、Treasure（未经过测试）。我没有时间测试，欢迎 PR。

## 修改中文字体

### 使用社区分享的成品

- [HarmonyOS Sans](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/releases/download/v2.16.2/HarmonyOS.Sans.pmp)
- [ChnAXIS](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/releases/download/v2.16.2/ChnAXIS.pmp)
- [四款替换用 UI 字体(黑|宋|圆) 国际服国服双版本](https://bbs.tggfl.com/topic/221/%E5%9B%9B%E6%AC%BE%E6%9B%BF%E6%8D%A2%E7%94%A8-ui-%E5%AD%97%E4%BD%93-%E9%BB%91-%E5%AE%8B-%E5%9C%86-%E5%9B%BD%E9%99%85%E6%9C%8D%E5%9B%BD%E6%9C%8D%E5%8F%8C%E7%89%88%E6%9C%AC)

### 我希望自制

- [FFXIV-FontChanger（原版）](https://github.com/Soreepeong/FFXIV-FontChanger) / [中文汉化版](https://github.com/AtmoOmen/FFXIV-FontChanger)
- 教程：[B站视频](https://www.bilibili.com/video/BV1XG411g7Gt) / [Imgur图文教程](https://imgur.com/a/Cojm6og)

## 捐赠支持

![捐赠二维码](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/assets/33572696/1fec3974-0b6d-43df-9afc-2d760c33f9b5)

## 许可证相关

本项目遵循 [GPL-3.0 License](https://www.gnu.org/licenses/gpl-3.0.html)，基于原项目 [FFXIVChnTextPatch-GP](https://github.com/GpointChen/FFXIVChnTextPatch-GP) 进行修改与扩展。

其中，以下部分与原项目保持完全一致：

- `jre/` Java 运行环境
- `FFXIVChnTextPatchGP.exe`：汉化工具主程序

以下内容为本项目原创或自行维护：

- `preset/`：字体文件预设JSON文件
- `resource/font/`：由本人制作的中文字体资源
- `resource/rawexd/`：由本人翻译及维护的游戏数据 CSV 文件
- `converter.exe`：由本人编写的 CSV 繁体转换工具

## 免责声明

```text
此项目仅供学习技术以及技术交流使用
严禁使用于任何商业用途
请下载后24小时内删除
使用汉化补丁属于修改客户端行为，使用者需自愿承担风险
```
