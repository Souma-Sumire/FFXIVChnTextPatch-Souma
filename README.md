# FFXIVChnTextPatch-Souma

## 使用

前往 [最新发布页 (Releases)](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/releases/latest) 下载对应系统的汉化器：
   - **Windows**：下载 `FFXIVChnTextPatch-SM-v2.exe`
   - **macOS**：下载 `FFXIVChnTextPatch-SM-v2-macOS.zip`（若提示无法验证开发者，可在终端执行 `xattr -cr FFXIVChnTextPatch-SM-v2.app`）

## 捐赠支持

![捐赠二维码](https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/assets/33572696/1fec3974-0b6d-43df-9afc-2d760c33f9b5)

## Developer

架构与实现说明见 [patcher/ARCHITECTURE.md](patcher/ARCHITECTURE.md)。

本地构建（使用工作区 `resource/` 里的 CSV 与字体，不向云端同步）：

```bash
pnpm run build
```

发布构建（发布版使用云端CSV，与用户侧相同）：

```bash
pnpm run build:release
```

CSV：

```bash
pnpm run csv
```

## 开源许可与致谢

- 本项目基于 [FFXIVChnTextPatch](https://github.com/reusu/FFXIVChnTextPatch) 与 [FFXIVChnTextPatch-GP](https://github.com/GpointChen/FFXIVChnTextPatch-GP) 开发构建。
- 游戏字体定制、多图层混合与排版预览引擎基于 [FFXIV-FontChanger](https://github.com/Soreepeong/FFXIV-FontChanger)（Copyright © 2022-2026 Soreepeong），该项目遵循 [Apache License 2.0](http://www.apache.org/licenses/LICENSE-2.0) 许可证。本项目在 `tools/fontchanger/` 目录下完整保留其原始版权声明、LICENSE 与 NOTICE 文件，并根据其开源协议规范记录了定制化修改。

## 免责声明

```text
此项目仅供学习技术以及技术交流使用
严禁使用于任何商业用途
请下载后24小时内删除
使用汉化补丁属于修改客户端行为，使用者需自愿承担风险
```
