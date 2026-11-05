# 汉化器架构说明

Wails v2（Go 1.24 + 原生 TypeScript，无前端框架），产物为单文件 `FFXIVChnTextPatch-SM-v2.exe`。本文以「文件 + 函数名」定位实现。

## 1. 分层

| 层 | 包 | 职责 |
|---|---|---|
| 编排 | `app.go` | Wails 绑定、配置读写、启动探测、并发与取消、日志与进度事件 |
| 流程 | `internal/verifier` | 校验版本与备份状态、生成处理计划、执行与还原 |
| 改写 | `internal/replace` | 把资源写进 sqpack（追加 dat、改 index） |
| 格式 | `internal/sqpack`、`internal/exd`、`internal/csvx` | sqpack 索引/数据包、EXD/EXH、CSV |
| 文本 | `internal/pipeline`、`internal/text` | 译文规则链、`<hex:...>` 标签解析 |
| 字体 | `internal/fontengine` | 字体方案读写与区服校验、系统字体枚举、TTMP 导出、预览渲染 |
| 资源 | `internal/updater` | 资源下载同步、程序更新检查 |
| 环境 | `internal/gamecfg`、`internal/gameproc`、`internal/gamescan`、`internal/region` | 游戏配置、进程互斥、目录扫描、区服语言映射 |

```mermaid
flowchart TD
    UI["前端 main.ts"] -->|Wails 绑定| APP["app.go 编排"]
    APP -->|资源同步| UPD["updater"]
    APP -->|汉化/还原| VER["verifier"]
    APP -->|改写| REP["replace"]
    VER --> REP
    REP --> SQP["sqpack"]
    REP --> EXD["exd / csvx"]
    REP --> PIPE["pipeline / text"]
    VER -->|校验哈希| SQP
    APP -->|进程检查| PROC["gameproc"]
    APP -->|目录探测| SCAN["gamescan"]
    APP -->|区服语言| REG["region"]
```

## 2. 两套云端数据

用途不同，不可混用。

| | 汉化资源 | 官方原版备份 |
|---|---|---|
| 位置 | 仓库 `assets-v2` 分支（孤儿分支，只留最新一次构建；旧的 `assets` 分支冻结给旧版汉化器） | GitHub Release，tag 即游戏版本号 |
| 内容 | `manifest.json`、`rules.json`、`tables.json`、`rawexd.zip`、`font.zip` | 6 个 sqpack 文件的 zip + `hashes.json` |
| 生成者 | `.github/workflows/sync_assets.yml`：`resource/` 相关文件变动时跑 `scripts/bundle.ts` 并 force push | `.github/workflows/ffxiv_download.yml`：cron `17,47 * * * *` 比对官方版本，未发布则新建 Release |
| 消费方 | `updater.Provision` | `verifier.resolveCloud`（仅国际服）、手动还原的云端下载 |
| 作用 | 提供中文 CSV、字体与规则 | 保证汉化前能拿到当前版本的官方原文件 |

`updater.DefaultNodes` 为 `raw.githubusercontent.com` 直连 + 6 个加速镜像。`Provision` 并发请求 `manifest.json`，首个成功者作为首选节点，后续下载失败按节点顺序回退。

`verifier.GitHubClient` 对直连与全部镜像并发发请求，首个返回合法内容的胜出，避免直连被阻断时等超时。覆盖三类地址：`api.github.com` REST、Release 网页、附件下载；API 阶段限时 6 秒，超时回退网页解析；`ReleaseByTag` 也 API 优先（一次请求带完整附件列表），被限流或阻断才解析网页。只有直连 404 判资源不存在，镜像 404 只代表该节点不支持该路径。

下载偏好与元数据偏好分开记录：`preferredNode` 只存镜像地址；`directFast` 仅在实测直连 ≥1 MiB 且 ≥512 KiB/s 时置位（几十 KB 的 `hashes.json` 不算）。未确认直连够快时，下载候选把直连排最后，`MirrorDownloadURL` 恒返回镜像地址供自更新弹窗使用。

两个 HTTP 客户端（`updater.New`、`verifier.NewGitHubClient`）的 `Transport.Proxy` 取 `netproxy.Func()`，解析顺序：`HTTPS_PROXY`/`HTTP_PROXY`/`ALL_PROXY` → Windows Internet 设置的 `ProxyEnable`/`ProxyServer`/`ProxyOverride` → 直连。注册表 30 秒内最多读一次；`ProxyOverride` 支持 `*`、`<local>`、`*.domain`、`192.168.*`；回环恒直连。仅配 PAC 而无静态代理时按直连（需 `WinHttpGetProxyForUrl`，标准库不具备）。

## 3. 本地数据布局

`resolveResourceDir`（`app.go`）优先使用可写的工作目录 `resource/`、`../resource`、exe 同级 `resource/`；都不存在时依次尝试 exe 目录、用户缓存目录、`LOCALAPPDATA` 下新建。

```text
resource/                 # 可写优先，缺失时从 assets-v2 分支同步
├── rawexd/               # 内置译文 CSV，7114 个文件，由同步流程维护
├── font/                 # 预编译字体包（按需下载；仅「内置字体包」来源使用）
├── rules.json            # specialWord（藏宝图别名）、materiaMap（魔晶石属性）
├── tables.json           # folders（14 个文件夹名）、tables（397 张表名）、files（7114 个表键）
├── manifest.json         # 资源构建清单，用于判断是否需要重新同步
└── patched_records.json  # 汉化成功后的索引哈希与版本，用于识别上次是否本工具汉化

backup/{global|chs|tc|ko}/ # 按区服存放，目录名取 region.Of(lang)
├── 0a0000.win32.{dat0,index,index2}
├── 000000.win32.{dat0,index,index2}
└── meta.json              # 版本号 + 每个文件 SHA256 + 更新时间

config.json               # 游戏目录、语言、目标语言、开关、规则、预设、词条目录
```

`backup/` 与 `config.json` 和 `resource/` 同级，`patched_records.json` 在 `resource/` 内。macOS 下若 `resource/` 位于缓存目录，`dataRootFor` 把 `backup/` 与 `config.json` 改到 `~/Library/Application Support/FFXIVChnTextPatch`。

6 个受管文件（`verifier.TextFiles` / `FontFiles`）：

- 文本：`0a0000.win32.{dat0,index,index2}`
- 字体：`000000.win32.{dat0,index,index2}`

改写只发生在 `.index`，`index2` 始终为原版，但备份与还原同样覆盖它。游戏目录 `game/sqpack/ffxiv/` 能否写入取决于目录 ACL 与文件状态，不预先探测可写性或文件锁，实际写入失败时直接返回底层系统错误并中止。

## 4. 启动

`GetBootstrap`（`app.go`）**只读本地文件**，不联网、不解压、不校验，因此界面秒开。启动阶段唯一的网络请求是前端的 `CheckAppUpdate`（GitHub Releases）；`GetGameInfo`、`CheckClientStatus`、`DetectGamePaths` 全是本地操作。

内置资源默认每次运行只同步一次，与当前选中的词条目录无关：

1. **启动即同步**：表清单补全后，前端直接调用 `App.SyncBuiltinResource`，进度压在「内置词条」选择器上，不额外占用布局；当前用的是自定义目录时同步在后台进行，不占用选择器文字。
2. **汉化前补查**：距上次检查满 30 分钟（`resourceRefreshInterval`）时，开始汉化前再确认一次云端词条版本，避免长期不关程序的用户一直用旧词条；这次补查失败只告警，继续使用启动时已校验过的本地资源。同步从未通过或资源缺失时仍按原逻辑重试一次，仍失败即中止（见第 10 节）。

使用自定义词条目录时，汉化过程完全不联网；只有本地缺少 `tables.json` 时，才会先同步一次内置资源。

**本地开发构建**：`pnpm run build` 默认带 `development` tag，`NewApp` 会把 `updater.LocalOnly` 置为 true。此时 `Provision` 只检查 `rawexd/`、`tables.json`、`rules.json` 是否存在且可读，不拉清单、不下载、不写 `manifest.json`；`EnsureFontPack` 也只认本地 `font/`。前端根据 `BootstrapInfo.devMode` 跳过云端同步与更新检查。发布构建由 `pnpm run build:release` 生成（`build_patcher.yml` 使用），不带该 tag，行为与之前完全一致。

无已保存的游戏目录时，界面渲染后在空闲回调里异步探测（`detectGameDir(true)`）。范围：注册表与常见安装路径、各盘符下的固定后缀目录、macOS 的 XIV on Mac / Bottles 候选；排除网络驱动器，避免 `os.Stat` 卡在 SMB 超时。

## 5. 一次汉化的完整链路

入口 `App.StartPatch`，核心 `verifier.Run`。

1. **前置校验**：目录非空、语言可解析、`replaceText || replaceFont` 至少一项、`beginBusy` 互斥；建立可取消的 `patchCtx`。
2. **进程互斥**：官方启动器、第三方启动器、游戏本体均在检测范围；启动器运行时可弹窗强制结束。
3. **区服核对**：`verifier.DetectClientRegion` 看 `0a0000.win32.index` 中是否存在 `exd/addon_0_{chs,ko,tc}.exd` 判国服/韩服/台服，`addon_0_{ja,en,de,fr,cht}.exd` 判国际服；与所选区服不符直接拒绝，防止覆错原文件。
4. **显卡配置**：勾选字体时 `gamecfg.EnsureHighResolutionUiAsset` 把 `FFXIV.cfg` 的 `UiAssetType` 置 1，改动前在同目录生成 `FFXIV.cfg.bak`，已是 1 则不写。
5. **确认资源**：内置词条距上次检查满 30 分钟先重新确认云端版本（失败只告警）；`resourceIncomplete` 判断当前请求缺什么，资源缺失或本次运行未同步通过时 `ensureBuiltinResource` 拉清单并校验，失败即中止。自定义目录不触发同步，除非缺表清单。
6. **计算范围**：勾选表转成 `exd/<小写路径>` 集合；`replaceText` 但未选表直接报错。
7. **生成计划**：读 `ffxivgame.ver`，对 6 个文件全量 SHA256（`hashAll`）；国际服查 Release 得 `matched` / `pending` / `localOnly`，非国际服固定 `localOnly`；再判断文本与字体是否原版，为每类文件选定动作。无官方哈希可核对时，统一以 `patched_records.json` 的索引哈希判断「上次是否本工具汉化」：命中则判定为已汉化，未命中则把当前客户端当作汉化基线。
8. **备份保障**：`ensureAllOriginalFilesBackedUp` 单趟确保 6 个文件都有哈希校验过的原版备份，缺的从游戏目录提取（国际服还可从云端下载）。复制先写临时文件、边写边算 SHA256，一致才 rename，校验失败时目标文件保持原样。
9. **执行**：`prepareTextFiles` / `prepareFontFiles` 按动作还原原文件（无需重复校验备份），再调 `applyTextData` / `applyFontData` 改写 sqpack。文本的 CSV 来源由 `resolveCSVDir` 决定（内置或当前区服选中的自定义目录），自定义目录下全部规则关闭。
10. **收尾**：写 `patched_records.json`（汉化后索引哈希 + 版本），把游戏目录、语言、目标语言、开关、规则写回 `config.json`。

### 计划动作

| 动作 | 含义 | 前提 |
|---|---|---|
| `patch` | 当前是原版，直接改写 | `determineOriginal` 判定为原版 |
| `restorePatch` | 当前已汉化，先从本地备份还原再改写 | 备份与当前版本一致且文件齐全 |
| `downloadRestorePatch` | 本地无备份，从云端取原文件还原后改写 | 仅国际服，且云端已备份当前版本 |
| `skip` | 该组文件不需要处理 | 未勾选或无需变更 |
| `none` | 未启用 | — |

判定不出是否原版时直接报错，提示先用官方启动器修复。**每次汉化都是「先回到原版，再追加」，不是叠加**，这也是重复汉化不会让 dat0 无限膨胀的原因。回到原版这一步逐文件比对哈希，已经与备份一致的文件不会被重写，日志也只报「已是官方原版，无需还原」，不会在客户端没被改动时声称正在还原。

### 用户确认

只改文本、不换字体、字体仍是原版、且不是国服时，通过 `Config.ConfirmFont` 弹窗提示「可能看不到中文」。国服不弹。

## 6. 二进制改写

### 文本

结构前提：每张表由 `exd/<表名>.exh` 描述列偏移、类型与分页，数据按「页 + 语言」拆成 `exd/<表名>_<页号>_<语言>.exd`，`root.exl` 列出全部 EXH。

`replace.ReplaceEXDF` 流程：

1. 打开 `0a0000.win32.index`，读 `root.exl` 得到全部 EXH 与表清单。
2. 每张表读 `resource/rawexd/**/*.csv`，结构为 4 行头加数据行：

   ```text
   key,0,2,8,9                          # 列偏移索引（key 固定首列）
   #,Singular,Plural,Description,Name    # 字段名，csvx 当注释跳过
   offset,0,4,8,12                      # EXD dataset 内偏移
   Int32,String,String,String,String     # 类型
   0,,,,                                # 数据
   ```

3. 逐个 dataset 取原文与 CSV 译文，按所选规则链变换（`pipeline`），再经 `text.ParseTargetString` 编码：UTF-8 直写，`<hex:...>` 解成原始字节（游戏控制码）。标签必须以完整的 `<hex:` 开头（区分大小写），`<hexabc>` 这类没有冒号的写法按普通文本保留；未闭合、嵌套或载荷不是合法十六进制时返回错误，该 EXD 记日志后跳过，不会把半截标签写进游戏。
4. 重建 EXDF，打包为 sqpack 二进制块，追加到 `0a0000.win32.dat0` 末尾。
5. 新偏移批量写回 `.index`。

两阶段提交：循环中只写 dat 并收集 `(entry, offset)`，结束后统一 `dat.Sync()` → 逐个 `WriteDataOffset` → `index.Sync()`。索引在任何时刻只指向已落盘数据，中断只留下无引用的 dat 空洞，不会产生坏索引。

读取侧对畸形数据一律返回错误，不 panic、也不按声明值大块分配：索引段或文件表偏移为负（`OpenIndex`）、dat 文件头尺寸超过 512MB 或块数远超文件长度（`ExtractFile`）、子块尺寸超过上限或越过文件末尾（`readBlockAt`）都在分配内存前拦下。

### 字体

`applyFontData` 取当前区服选中的字体方案（`getActiveFontPreset`），校验区服后交给 FFXIV-FontChanger 命令行程序（`fontengine.CompileRaw`）编译成字形文件，落到临时目录再写入。编译组件不打包进主程序，**用到时才下载**（`ensureFontCompiler`：数据目录没有就从 `fontchanger-cli` 分支取，直连 + 镜像回退，MZ 头校验）。编译组件拿不到、编译失败、或产物里没有 `.tex`，都直接报错中止本次汉化，**不会自动换成内置字体包**。

编译产物里的 `*.tex` 走 `sqpack.BuildTexBlock`（Type 4 纹理块），`*.fdt` 走 `BuildBinaryBlock`（Type 2 二进制块），追加到 `000000.win32.dat0` 并更新 `.index`。`BuildTexBlock` 只支持单 mip，多 mip 显式报错。

字体来源由界面「字体来源」下拉决定，两条路互斥：`package`（内置字体包，默认）直接使用 `resource/font/`，本地没有时才下载 `font.zip`；`compile`（自定义方案）按需下载 FFXIV-FontChanger 组件，把编辑器里的方案现场编译，失败即中止、不回退。选择存在配置文件的 `fontSource` 字段里。`compile` 在非 Windows 平台无法现场编译（组件是 Windows 程序），`StartPatch` 会在写入任何文件前（`checkFontSource`）就报错要求改用内置字体包。字体包不参与启动同步，只在需要字体替换时才下载。

**字体方案（预设）**：内置方案只有两个，随程序编译进去（`internal/fontengine/presets/{default,chnaxis}.json`）：

| id | 名称 | 可用区服 | 来源 |
|---|---|---|---|
| `default` | FFXIV-FontChanger 默认方案 | 全部 | 上游 `tools/fontchanger/Presets/Default@96.json`；靠 `exportMap{Chn,Krn,Tc}AxisToFont` 把 font 系列映射进各区字体槽 |
| `chnaxis` | ChnAXIS 中文方案 | 仅国际服 | 上游对照 `Presets/ChnAXIS.json`；只有 `font{}.tex` + `font_lobby{}.tex`，与内置字体包同一套字形 |

自定义方案落在数据目录 `presets/*.json`（`SaveCustomPreset` / `RenameCustomPreset` / `DeleteCustomPreset`，内置方案只读）。区服校验 `ValidatePresetForRegion` 同时看字体面名称与 `exportMap*` 开关：有对应区服的字体面、或者打开了该区服的导出映射，都算可用。`getActiveFontPreset` 会核对配置里记的方案是否仍然可用，失效就退回默认方案。界面上的「管理方案」弹窗负责应用 / 复制 / 重命名 / 删除 / 导入 / 保存当前，内置方案只留「应用」「复制」。

### 规则链

`pipeline` 把规则做成可组合的 `Rule`，前端开关直接对应 `ID()`：

| ID | 作用 |
|---|---|
| `treasure_map_alias` | 藏宝图等级标注，数据来自 `rules.json` 的 `specialWord` |
| `materia_stat_suffix` | 魔晶石属性后缀，数据来自 `rules.json` 的 `materiaMap` |
| `opencc_s2tw` | 简体转臺灣繁體 |
| `ktg_cheat` | 将字库改为纯数字（特殊玩法） |

规则只作用于 `<hex:...>` 之外的普通文本。

## 7. 客户端状态判定

`App.CheckClientStatus` 按优先级返回，命中即返回：

| 顺序 | 条件 | 结果 |
|---|---|---|
| 0 | 实际区服与所选区服不一致 | `mismatch`（区服不匹配） |
| 1 | 读不到 `ffxivgame.ver`，或文本、字体索引都读不到 | `unknown`（版本未知 / 文件缺失） |
| 2 | 本地有当前版本的原版备份 | 哈希一致 `original`，否则 `modified` |
| 3 | 国际服且云端 Release 有该版本的 `hashes.json` | 一致 `original`，否则 `modified`（不穿透） |
| 兜底 | 以上均不命中 | `unknown`（云端尚未收录该版本） |

`CheckClientStatus` 只回答「当前文件是否与官方原版一致」，没有「已汉化」这个状态：本工具汉化后的文件必然与官方哈希不同，因此环境栏显示 `modified`（已修改）。「已汉化」只出现在生成计划阶段，靠 `patched_records.json` 的索引哈希判断上次是否由本工具汉化。

## 8. 还原

`App.RestoreOriginal` → `verifier.Restore`：

- 非国际服：必须有版本一致的本地备份，否则报错并建议用官方启动器修复，不尝试下载。手动还原还要求 `patched_records.json` 的索引哈希与当前文件一致——版本号相同不足以证明备份未过期，否则旧备份会覆盖热修后的新内容。
- 国际服：备份无效时可从 Release 下载对应版本原文件。
- 逐文件比对哈希，只覆盖真正被改动的文件，`index2` 同样参与比对。没有文件被改动时返回空列表，日志只说「客户端已是官方原版，无需还原」，不会声称正在还原。
- 成功后清除该游戏目录在 `patched_records.json` 中的记录，其余目录保留。

### 汉化失败/中止时的回滚

汉化写入自带撤销记录（`replace.Undo`），失败或中止时**只撤回本次写入**，不做「用原版备份把文件整体覆盖回官方原版」：

- 开始写某一组文件前记录 dat0 的当前长度、以及每个将被改写的索引偏移原值（`NewUndo` / `Record`）。
- 撤回时按相反顺序执行 `Revert`：先把索引偏移改回原值并刷盘，再把 dat0 截断回原长度。顺序不能颠倒——先截断会让索引短暂指向已被删除的数据，掉电时就是坏索引。
- 结果与「本次写入之前」逐字节一致，因此**不依赖官方原版备份**：云端未收录的未知版本、根本没有本地备份的情况同样能回滚。
- 只撤回本次真正开始写入的文件组，不会把本次没碰的组（例如只勾了文本时的字体）连带还原掉。
- 只有 `RestoredText`/`RestoredFont` 或已开始的写入组才谈得上回滚；写入前的错误（资源缺失、备份校验失败等）直接返回，日志按实际发生的事说明客户端有没有被改过（含「先回原版」这一步是否真的写过文件）。

「先回原版」这一步不在回滚范围内：它在撤销记录建立之前执行，所以开始汉化时若客户端已经是上次的汉化产物，失败后会停在官方原版，而不是上次的汉化。要保住上次汉化，就得改成「只回写原版索引、不截断 dat0」，代价是 dat0 每次汉化都会累积上一次的数据块。

进程被强杀时来不及撤回，但两阶段提交保证索引不会指向未落盘的数据，游戏仍能读到写入前的内容，残留的 dat 尾部空洞会在下一次汉化前的还原中被覆盖掉。

## 9. 区服与语言

`internal/region` 是所有区服逻辑的分发点：

- `ParseLanguage`：接受 `GLOBAL/GL`、`JA/EN/DE/FR`、`CHS/CN/ZH/ZH-CN`、`TC/CHT`、`KO`，大小写与空白不敏感。
- `Of`：`CHS→chs`、`TC→tc`、`KO→ko`，其余归 `global`。
- `TargetLanguagesFor`：国际服一次处理 `JA, EN, DE, FR`，前端可勾选子集；国服/台服/韩服固定单一语言。

备份目录按区服而非语言划分，国际服四种语言共用同一份原版备份。两类区服在读版本号、全量 SHA256、汉化、本地备份与还原上完全一致，差异只在云端是否提供该版本的权威原文件：

| 能力 | 国际服 | 国服 / 韩服 / 繁中服 |
|---|---|---|
| 云端原文件下载 | 是（Release tag 即版本号） | 不提供 |
| 原版状态检测 | 云端 `hashes.json` 权威比对 | 不提供（无官方哈希） |
| 还原来源 | 本地备份，无效时云端下载 | 仅本地备份，且要求当前仍是本工具汉化产物 |
| 基线来源 | 云端哈希核对过的原版 | 当前客户端文件（靠 `patched_records.json` 识别已汉化） |

非国际服汉化前会在日志提示「将以当前客户端文件作为官方原版基线建立备份」；若检测到仍是上次汉化状态但备份已丢失，拒绝继续，不会把汉化结果再备份一遍。

## 10. 词条目录

内置词条是 `resource/rawexd/`，只由资源同步维护（`updater.Provision`），用户不能编辑。`rawexd.zip` 在校验通过后保留，作为校验解压内容的可信来源。本地开发构建例外：`Provision` 走 `LocalOnly` 分支，把工作区 `resource/` 当成唯一事实来源，不联网、不覆盖，改完 CSV 重新构建即可生效。

**内置词条的校验链**（`internal/updater/bundle.go`）：

1. `ensureBuiltinResource` 拉取云端清单 `manifest.json`；CSV 模块必须同时满足：本地清单记录的 `rawexd.sha256` 与云端一致、`rawexd.zip` 的 SHA256 与清单一致、解压目录中每个文件的大小与 CRC32 与包内条目一致。任一不符即判定需要修复。
2. 修复优先复用保留的 `rawexd.zip` 直接重新解压（不重新下载）；只有包本身缺失或哈希不符时才重新下载 28MB。
3. 同步默认只在启动时发生一次，与当前选中的词条目录无关；开始汉化时若距上次检查满 30 分钟会再确认一次云端版本（失败只告警，继续用本地资源）。只有在「本次运行未同步通过」或资源确实缺失（`resourceIncomplete`）时，同步失败才会中止汉化。另有 `EnsureTableDoc` 负责首次补 255KB 的表清单，它与资源同步共用同一份清单缓存（进程内 30 秒）。
4. 校验阶段全部上报进度（`资源清单 → 校验快照包 → 核对 CSV 文件`），前端把「阶段名 + 百分比」压在选择器文字上，不再出现无说明的「同步中」。
5. 完整校验实测约 0.2 秒：包哈希 28MB 约 60ms、7114 个 CSV 8 并发约 160ms。
6. 上一步的结果记录在 `resource/.verify.json`：清单未变，且 `rawexd`/`font` 的文件数、总大小、修改时间之和与记录一致时，直接判定内容没被改动，跳过全部逐文件哈希（本地约 15ms）；只有文件被改、被删或云端清单变化时才回到完整校验。指纹依赖 mtime，不读文件内容，因此首次运行和每次更新后需要重建一次。剩下的大头是拉取云端清单的网络耗时（0.7～1.2 秒）。

**离线语义**：内置词条不做降级——启动时的同步失败只记一条警告，内置词条在开始汉化前会重试一次，仍不通过即中止并提示联网重试或改用自定义词条目录；`tables.json` 缺失时单独补一份清单（255KB），补不到也会中止，因为没有表清单无法确定汉化范围。

**两条互斥路径**：内置词条只读、始终由云端维护，用户改不动；想改 CSV 用下拉底部「制作副本」——程序自动把内置词条整份复制到 `resource/custom-csv/`（与内置资源同目录，macOS 资源位于系统缓存时改放持久目录）并切换过去，无需选择位置。目录已存在时直接复用，不覆盖已有修改。这样「点汉化=最新原版+最新 CSV」与「用户自定义 CSV」不会互相干扰。

**校验与本地资源**：判定只看云端清单。本地资源与清单不一致时优先复用保留的 `rawexd.zip` 重解压，包本身也不符才重新下载；下载或解压过程失败不会破坏现有文件（先写 `.part`，校验后 rename）。发布构建里直接改 `resource/rawexd/` 会被下一次同步覆盖；本地开发构建不联网，改动直接生效。

词条目录的交互（`csvdirs.go`）：

- **按区服保存**：`AppConfig.CSVDirs` 以区服代码为键，保存该区服的目录列表、当前选中项与显示信息，`selected` 为空表示内置词条。当前值只由所选区服决定，切区服即切目录。
- **目录来源只有两条**：内置词条，以及「制作副本」生成的内置词条副本；不再支持手动挑选任意目录。
- **名称与描述**：`CSVDirsByRegion[区服].labels[路径]` 保存用户自定义的名称与描述（各 40/120 字上限），仅影响界面展示；名称为空时回落到文件夹名。移除目录时同步清理。
- **打开目录**：`OpenCSVDirectory` 只允许打开内置词条目录或当前区服的目录列表项，按平台调用 `explorer`/`open`/`xdg-open`。
- **副本复制**：整文件读写 + 8 并发，7114 个文件约 2.7 秒。瓶颈是逐个创建文件（含杀软扫描），实测从 `rawexd.zip` 直接解压同样要创建 7114 个文件、并无收益，因此仍从已校验的 `rawexd/` 复制。复制先落到 `custom-csv.building`，整份写完再改名为 `custom-csv`：进程被强杀只会留下临时目录（下次制作副本前清掉），正式目录要么不存在、要么完整。旧版本直接往正式目录里复制，可能留下半成品；这类副本被复用时会按内置词条比对数一遍 CSV，缺文件就在日志里提示。
- **快照标识**：`CSVContext.resourceHash` 下发内置词条包（`rawexd.zip`）的 SHA256，内置词条行显示 8 位短哈希（tooltip 给完整值）。制作副本时描述预置「基于内置词条 <短哈希> 制作」，界面上直接看得出副本基于哪份快照。
- **简繁转换副本**：副本面板可勾选「先套用简繁转换」，制作时用内置 OpenCC `s2tw` 字典逐文件改写内容（`<hex:...>` 占位保持不变，约 7 秒），描述追加「（已简繁转换）」。已存在副本时拒绝转换请求（会覆盖用户修改），需先删除现有副本。
- **移除目录**：对程序生成的副本，前端气泡默认勾选「同时删除目录文件」并传 `deleteFiles=true`；内置目录与用户自选目录无此选项，只把目录移出列表。后端只对路径恰为程序生成的 `custom-csv` 放行 `RemoveAll`，符号链接/联接点、与资源目录重叠、文件系统根目录一律拒绝；删除失败时保留列表项与配置，便于重试。
- **校验口径**：目录（含子目录）里至少有 1 个 `.csv`，且去扩展名后的相对路径能在 `tables.json` 的 `files`/`tables` 键里找到，才算有效；失效目录在下拉里灰显「已失效」，汉化前会直接中止。
- **表清单**：选中自定义目录时按目录内实际 CSV ∩ `tables.json` 生成 `CSVContext.TableList`，中文名与目录分组仍复用 `tables.json`；目录里缺的表不参与汉化，`replace.Options.QuietMissingCSV` 让这类缺文件不逐表刷日志。
- **扩展功能停用**：自定义目录无法保证与 `rules.json` 配套，`StartPatch` 忽略前端传来的规则开关，把注册表里全部规则置为关闭，前端同时隐藏整个「扩展功能」区块。
- **字体与规则数据**仍来自内置资源，自定义目录只替换文本 CSV。
- 切换目录或制作副本时，选择器立刻进入「正在切换…」/「正在制作副本…」并带脉冲进度条；制作副本全程保持列表展开，完成后副本即为当前项并直接进入名称与描述的内联编辑。

离线资源包导入已移除，`updater/offline.go` 一并删除。

资源完整性由 `modulesComplete`（完整校验）与 `verifyStamp`（快速通道）判定：`rawexd` 校验包哈希与解压目录逐文件 CRC32；`rules`、`tables` 按清单哈希比对。上次校验通过后若清单与文件指纹都没变，则跳过这些读取；外部改动或写坏时指纹变化，仍会触发完整校验与重新下载。

## 11. 构建与发布

- 本地构建：根目录 `pnpm run build`（`scripts/build.ts`）先构建前端产物，再 `go build` 产出根目录 exe；默认带 `development` tag，只使用工作区 `resource/`，不会下载云端 CSV/字体。改完代码必须跑一次。
- 发布构建：`pnpm run build:release`（`scripts/build.ts --release`），不带 `development` tag，保持内置词条向云端确认最新版本；CI 的两个构建 job 都使用它。
- `build_patcher.yml`：打 `patcher-v*` tag 或手动触发，跑 `go test ./...` 后构建 Windows exe 与 macOS 包。
- `sync_assets.yml`：`resource/rawexd`、`resource/font`、`rules.json`、`tables.json`、`scripts/bundle.ts` 变动时重新打包并更新 `assets-v2` 分支；旧的 `assets` 分支保持原样，旧版汉化器继续从那里取带字体包的清单。
- `ffxiv_download.yml`：每半小时检查官方版本，未发布则新建 Release 并附 6 个原文件 zip 与 `hashes.json`。

## 12. 已知行为与限制

- `preset` 为 `custom` 时不持久化：`selected` 是前端内存中的 `Set`，重载后回退 `recommended`。
- 资源构建时间与哈希不在界面展示，但 `BootstrapInfo`/`GameInfo` 仍下发 `resourceTime`/`resourceHash`，汉化日志开头照常输出内置资源版本。
- 表列表 7114 项，勾选与搜索都整表重建 DOM，无防抖，全展开时明显卡顿。
- 自定义词条目录按文件路径精确匹配（macOS 下大小写敏感），文件名与 EXH 表名大小写不一致时会读不到该表。
- `resource/rawexd.zip` 会常驻（约 28MB），用于校验解压后的 CSV；删除它会在下次同步时重新下载。
- `resource/.verify.json` 是校验指纹，不参与功能；删除它只会让下次启动做一次完整校验（约 0.4 秒），不影响资源一致性。
- 内置词条默认在启动时同步一次（清单未变时本地校验约 15ms，变化时约 0.4 秒，外加拉清单的网络耗时）；开始汉化时距上次检查满 30 分钟会再确认一次云端版本，因此运行中发布的词条更新最迟在下一次汉化时生效。补查失败只告警并继续用本地资源；同步从未通过时内置词条不能汉化，开始前会重试一次。本地开发构建完全不参与这套同步。
- 主程序自更新：`CheckAppUpdate` 只返回版本信息，前端弹窗提供「立即更新」（Windows）与「手动下载」。点立即更新后 `App.InstallAppUpdate` 复用 `verifier.GitHubClient.Download`（自带镜像回退）把新 exe 下到程序同目录临时文件，按发布方 SHA256 与 PE 头校验，再由 `updater.SwapExecutable` 把旧程序改名 `.old`、新程序放到原位并 `LaunchExecutable` 启动，随后 `runtime.Quit` 退出旧进程；下次启动时 `CleanupStaleAppFiles` 清理 `.old` 与中断的临时文件。启动失败会 `RollbackExecutable` 恢复旧程序。macOS 发布包是 `.app` 压缩包，改为标记「不支持自动更新」并回落到手动下载。
- 汉化、还原等占用客户端的任务期间（`beginBusy` 生效）`CheckAppUpdate` 与 `InstallAppUpdate` 均直接拒绝；前端同时隐藏「新版本」入口并禁用版本按钮，任务结束再恢复。
- 只有 `patcher-v*` 参与版本比较——仓库里还有 `v2.20.0`、`v2.18.x` 等旧版 Java 汉化器的 tag，按「形如 x.y.z」采信会误判为更新版本。降级到网页列表时按 `releases/download/<tag>/FFXIVChnTextPatch-SM-v2.exe`（macOS 为 `-macOS.zip`）直接拼地址，此时拿不到 SHA256，只做体积与 PE 头校验。

## 13. 代码索引

| 想改什么 | 去哪 |
|---|---|
| 界面、交互、事件订阅 | `frontend/src/main.ts`、`frontend/index.html`、`frontend/src/style.css` |
| Wails 绑定、配置、启动流程 | `app.go` |
| 汉化流程与状态判定 | `internal/verifier/verifier.go` |
| 词条目录配置与表清单扫描 | `csvdirs.go` |
| 二进制改写 | `internal/replace/replace.go` |
| sqpack 索引与块打包 | `internal/sqpack/index.go`、`dat.go`、`block.go` |
| EXD / EXH 解析 | `internal/exd/exdf.go`、`exhf.go` |
| CSV 读写 | `internal/csvx/csv.go` |
| 译文规则 | `internal/pipeline/rules.go`、`pipeline.go` |
| `<hex:...>` 处理 | `internal/text/hex.go` |
| 资源同步、资源包校验、更新检查与自更新 | `internal/updater/` |
| 区服语言 | `internal/region/region.go` |
| 进程互斥、目录探测 | `internal/gameproc/`、`internal/gamescan/scan.go` |
| 字体方案、系统字体、TTMP 导出、字体预览渲染 | `internal/fontengine/` |
| 字体编辑器界面与命令 | `frontend/src/main.ts`（`runFontCommand`、`wireFontMenuInteractions`） |
| 字体预览渲染 | 后端 `RenderFontPreview`（`fontchanger-cli --render-text`，与游戏内同一套栅格化） |
| 游戏字体数据 | `frontend/src/font_data.ts`、`frontend/src/game_font_metrics.json`、`tools/fontchanger/FontChanger.FixedSizeFont/data/{game_fonts,glyph_merge_shapes}.json` |

## 14. 字体编辑器（复刻 FFXIV-FontChanger）

工作台勾选「替换字体」后点「自定义字体」进入 `#view-font-editor`，布局对齐 `tools/fontchanger/FontChanger.FontEditor`：菜单栏 + 左侧字体列表框 + 右侧「元素列表 / 预览文本框 / 渲染视口」三段。HTML 保留了原对话框的控件 id（`IDD_FONTELEMENTEDITOR` 四页签），数据模型与 `tools/fontchanger` 的 `Presets/*.json` 同构（`MultiFontSet → FontSet → Faces[].Elements[]`）。

- **命令入口**：菜单栏 51 个 `data-menu` 与两个右键菜单的 13 个条目全部收敛到 `runFontCommand(cmd)`；快捷键（Ctrl+S/O/Shift+S/Q、Ins/Del/Enter、WASDQE、`[`/`]`、1~5、P 等）也调同一函数，菜单、右键、快捷键三条路径行为一致。
- **选择模型**：`selectedElementIndices` 支持 Ctrl/Shift 多选，`activeElementIndex` 只决定详情面板与预览高亮；复制/剪切写内存剪贴板并尽力同步系统剪贴板。
- **预览**：预览图由字体编译组件渲染（`RenderFontPreview` → `fontchanger-cli --render-text`），与写进客户端的字形走同一套栅格化与排版，所以看到的和游戏内一致；组件不可用时预览区直接显示错误，不用浏览器近似渲染顶替。`--max-width` 对应「自动换行」；缩放 1~5 档、左键拖动平移、滚轮滚动、Ctrl+滚轮换档、双击复位都只是对这张位图做变换，不改变排版。原版的「字距调整」「显示行高信息」依赖它自己的绘制路径，引擎渲染没有对应开关，随本次改造一并移除。三条分割条分别调左栏宽度、元素列表高度、预览文本框高度。
- **字体数据**：`font_data.ts` 不内联数据——字形合并形状直接 import 上游 `tools/fontchanger/.../data/glyph_merge_shapes.json`（随 `scripts/update_fontchanger.ts` 同步，仓库内不再另存副本）；字体面的家族、子家族、字号取上游 `game_fonts.json`（`GAME_FONT_FACES`），不再按名字里的数字反推（`TrumpGothic_184` 是 18.4px）。`game_font_metrics.json` 仍是近似快照，只用于「新图层默认字号」的兜底与自动适配的基线测量；写进客户端的字体数据由后端 `fontengine` 生成。内置方案的 `previewText` 与上游 `Presets/Default@96.json` 对齐。
- **保存与导出**：`SaveFontPreset` 同名覆盖（内置方案自动走「另存为」），`RenameFontPreset` / `DeleteFontPreset` 只管自定义方案（内置方案只读），`SelectFontJsonSavePath` + `WriteTextFile` 导出 JSON，`ExportFontTTMP` 导出 Penumbra 模组；`exportMapLobbyToFont` 等四个映射开关直接写进方案 JSON，由后端编译时使用。右上角「管理方案」打开 `#preset-modal`，列出当前区服可用方案（`GetFontPresets`），逐条提供应用 / 复制 / 重命名 / 删除，底部是导入 JSON 与「保存为方案」；复制走 `GetFontPreset` + `SaveFontPreset`，不改动原方案。
- **方案格式**：内置方案与上游 `Presets/*.json` 同构——顶层是 `MultiFontSet`（四个 `exportMap*` 开关 + `fontSets` 数组），每个 `FontSet` 带 `discardStep`/`sideLength`/`texFilenameFormat`/`faces`。上游 `MultiFontSet::from_json` 同时接受裸 `FontSet`（旧格式），两种都能打开；`fontSets[].texFilenameFormat` 决定这组字面写进哪个贴图。
- **新建**：复刻 `Menu_File_New`，按 `tools/fontchanger/.../data/game_fonts.json` 的 `fontTypes` 展开所选类型（font / font_lobby / font_chn / font_tc / font_krn）的全部字体面，每面一个游戏字体图层（字号取表里的值、码点全区间，数字字体用 `123,456,789.000!!!`），文档不落盘，Ctrl+S 才另存为方案。
- **创建空白副本（已移除）**：原版会在字号最前面插入一个 Empty 图层，用当前字号真实合并字体的 `ascent()/line_height()` 决定该字号的行高与基线；浏览器端没有这些度量，只能写近似值，会直接影响游戏内排版，因此暂不提供。
- **自动适配（已移除）**：浏览器端没有游戏字体的真实度量，按近似表算出来的字号/基线会写进方案、直接影响游戏内效果，因此暂不提供。原版 `FontsFromSystemFont::AutoFit` 用的是游戏字体自身的 line height / ascent / 码点度量 + 系统字体的真实墨迹（DirectWrite/FreeType），要恢复必须由组件提供该能力（新增 `--autofit` 模式调用现成实现），不能在前端重算。
- **同系列操作**：字体列表右键的「用当前系列替换其他字号」按原版做法清空目标字号、用当前字号全部图层的缩放副本重建并沿用垂直对齐；「从字体添加到当前系列所有字号」按原版做法先选系统字体，再给该系列每个字号各加一个新图层并适配。
- **编译组件**：FFXIV-FontChanger 编译出的命令行程序不随主程序分发，也不参与构建；`ensureFontCompiler` 在第一次替换字体时按需下载到数据目录（`fontchanger-cli` 分支，直连 + 镜像，校验 MZ 头），拿不到就报错中止，不回退预编译字体包。**macOS 无法执行这个 Windows 程序**，选「自定义方案」会在开始汉化前直接报错，需要改用「内置字体包」。
- **限制**：`verticalAlignment` 只存在方案 JSON 里并影响预览基线位置，Go 侧字体数据生成尚未使用该字段；预览字体来自系统已安装字体，游戏内实际栅格化仍由 `fontengine` 完成。

## 15. 测试

`go test ./...` 覆盖各包单测与端到端对拍。`internal/golden` 是最有价值的保障：`internal/sqpack/testdata/golden_exdf_hashes.json` 记录了旧版 Java 汉化器对国际服原版执行「替换字体 + 替换文本（JA, EN）」后导出的 29036 个 EXD 逐文件 SHA256；测试重跑同样流程并逐字节抽样比对，任何影响输出字节的改动都会被拦住。

该测试处理约 400MB 数据，`-short` 会跳过。原版样本查找顺序：`GOLDEN_SOURCE_DIR` → `testdata/golden_source` → 仓库外层 `backup/global`；都没有时自动从 GitHub Release 下载到 `testdata/golden_source`，首次运行较慢，之后走缓存。
