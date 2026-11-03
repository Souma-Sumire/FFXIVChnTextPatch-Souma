# 汉化器架构说明

本文描述 `patcher/` 的实际实现：数据从哪来、一次汉化经过哪些步骤、二进制被改成什么样、失败时如何回到原版。所有结论都对应仓库中的代码，不含推测；文中以「文件 + 函数名」定位，避免行号漂移。

技术栈：Wails v2（Go 1.24 + 原生 TypeScript，无前端框架），产物为单文件 `FFXIVChnTextPatch-SM-v2.exe`。

## 1. 分层

| 层 | 包 | 职责 |
|---|---|---|
| 编排 | `app.go` | Wails 绑定、配置读写、启动探测、并发与取消、日志与进度事件 |
| 流程 | `internal/verifier` | 校验版本与备份状态、生成处理计划、执行与还原 |
| 改写 | `internal/replace` | 把资源写进 sqpack（追加 dat、改 index） |
| 格式 | `internal/sqpack`、`internal/exd`、`internal/csvx` | sqpack 索引/数据包、EXD/EXH、CSV |
| 文本 | `internal/pipeline`、`internal/text` | 译文规则链、`<hex:...>` 标签解析 |
| 资源 | `internal/updater` | 资源下载同步、离线包导入、程序更新检查 |
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

两者用途完全不同，不可混用。

| | 汉化资源 | 官方原版备份 |
|---|---|---|
| 位置 | 仓库 `assets` 分支（孤儿分支，只保留最新一次构建） | GitHub Release，tag 即游戏版本号 |
| 内容 | `manifest.json`、`rules.json`、`tables.json`、`rawexd.zip`、`font.zip` | 6 个 sqpack 文件的 zip + `hashes.json` |
| 生成者 | `.github/workflows/sync_assets.yml`：`resource/` 相关文件变动时跑 `scripts/bundle.ts` 并 force push | `.github/workflows/ffxiv_download.yml`：cron `17,47 * * * *` 比对官方版本，未发布则新建 Release |
| 消费方 | `updater.Provision` | `verifier.resolveCloud`（仅国际服）、手动还原的云端下载 |
| 作用 | 提供中文 CSV、字体与规则 | 保证「汉化前能拿到当前版本的官方原文件」 |

资源下载节点由 `updater.DefaultNodes` 给出：`raw.githubusercontent.com` 直连加 6 个加速镜像。`Provision` 并发向所有节点请求 `manifest.json`，取最先成功的节点作为首选，后续下载优先使用它、失败再按节点顺序回退。因此资源更新对用户透明，不需要换程序。

所有访问 GitHub 的链路都按同一套策略处理（`verifier.GitHubClient`）：直连与全部镜像**并发**发出请求，首个返回合法内容的地址胜出，因此直连被阻断时不必等待它超时。涉及三类地址——`api.github.com` 的 REST 接口、`github.com` 的 Release 网页、`github.com` 的附件下载。API 阶段单独限时 6 秒，超时即回退到网页端解析。`ReleaseByTag` 也是 API 优先（一次请求就带完整附件列表），只有被限流或阻断时才解析网页。只有直连返回 404 才判定资源不存在，镜像返回 404 只代表该节点不支持这个路径。

「元数据能取到」不等于「附件能直连下得快」——国内常见 `api.github.com` 可连但 Release 附件直连极慢。因此两类偏好分开记录：`preferredNode` 只存镜像地址，`directFast` 只在**实测**直连下载 ≥1 MiB 且速率 ≥512 KiB/s 时置位（几十 KB 的 `hashes.json` 下载成功不算数）。未确认直连够快时，下载候选把直连排在最后，`MirrorDownloadURL` 也始终返回镜像地址，供「自更新」弹窗展示镜像入口。

两处 HTTP 客户端（`updater.New`、`verifier.NewGitHubClient`）的 `Transport.Proxy` 都取 `netproxy.Func()`，解析顺序为：环境变量 `HTTPS_PROXY`/`HTTP_PROXY`/`ALL_PROXY`（显式指定优先，与标准库一致）→ Windows「Internet 设置」里的 `ProxyEnable` / `ProxyServer` / `ProxyOverride` → 直连。注册表每 30 秒最多读一次，用户中途开关代理软件也能生效；`ProxyOverride` 支持 `*`、`<local>`、`*.domain`、`192.168.*` 形式，回环地址恒定直连。只配了 PAC（`AutoConfigURL`）而没有静态代理时无法解析，此时按直连处理——执行 PAC 需要 WinHTTP 的 `WinHttpGetProxyForUrl`，标准库不具备。

## 3. 本地数据布局

目录由 `resolveResourceDir`（`app.go`）决定：优先使用可写的工作目录 `resource/`、`../resource`、exe 同级的 `resource/`；都不存在时依次尝试 exe 目录、用户缓存目录、`LOCALAPPDATA` 下新建。

```text
resource/                 # 可写优先，缺失时从 assets 分支同步
├── rawexd/               # 汉化译文 CSV，7114 个文件；397 张有独立中文名，其余按所在文件夹归组显示
├── font/                 # 字体 tex 与 fdt
├── rules.json            # 规则数据：specialWord（藏宝图别名）、materiaMap（魔晶石属性）
├── tables.json           # 表清单：folders（14 个文件夹中文名）、tables（397 张表的中文名）、files（7114 个表键，含 content/xxx 这类带路径的表）
├── manifest.json         # 资源构建清单，用于判断是否需要重新同步
└── patched_records.json  # 本工具汉化成功后的索引哈希与版本，用于状态判定

backup/{global|chs|tc|ko}/ # 按区服存放，目录名取 region.Of(lang)
├── 0a0000.win32.{dat0,index,index2}
├── 000000.win32.{dat0,index,index2}
└── meta.json              # 版本号 + 每个文件的 SHA256 + 更新时间

config.json               # 游戏目录、语言、目标语言、开关、规则、预设
```

`backup/` 与 `config.json` 和 `resource/` 同级，`patched_records.json` 位于 `resource/` 内。macOS 下若 `resource/` 位于缓存目录，`dataRootFor` 会把 `backup/` 与 `config.json` 改到 `~/Library/Application Support/FFXIVChnTextPatch`，避免被系统清理。

6 个受管文件固定为（`verifier.TextFiles` / `FontFiles`）：

- 文本：`0a0000.win32.dat0`、`0a0000.win32.index`、`0a0000.win32.index2`
- 字体：`000000.win32.dat0`、`000000.win32.index`、`000000.win32.index2`

改写只发生在 `.index`，`index2` 始终保持原版，但备份与还原同样覆盖它。

## 4. 启动

`GetBootstrap`（`app.go`）分两条路径：

1. `updater.HasLocalResource` 为真（`manifest.json`、`tables.json`、`rawexd/` 三者齐全）：立即返回界面数据，后台 goroutine 跑 `Provision` 比对资源（`resourceSyncOnce` 保证只跑一次），有更新时通知前端但不阻塞使用。
2. 否则：同步执行 `Provision` 并进入全屏准备页，靠 `resource-progress` 事件显示进度。失败时置 `offline = true`，前端显示「已进入离线状态」并给出导入离线资源包的按钮，不直接报错退出。

没有已保存的游戏目录时，界面渲染完成后在空闲回调里异步探测（`detectGameDir(true)`），不阻塞首屏。探测范围：注册表与常见安装路径、各盘符下的固定后缀目录、macOS 的 XIV on Mac / Bottles 候选；网络驱动器被排除，避免 `os.Stat` 卡在 SMB 超时上。

## 5. 一次汉化的完整链路

入口 `App.StartPatch`，核心 `verifier.Run`。

1. **前置校验**：目录非空、语言可解析、`replaceText || replaceFont` 至少一项、`beginBusy` 互斥；建立可取消的 `patchCtx`。
2. **进程互斥**：官方启动器、第三方启动器、游戏本体均在检测范围；启动器运行时可弹窗强制结束。
3. **区服核对**：`verifier.DetectClientRegion` 按 `0a0000.win32.index` 中是否存在 `exd/addon_0_chs.exd`、`addon_0_ko.exd`、`addon_0_tc.exd` 判定国服 / 韩服 / 台服，存在 `addon_0_{ja,en,de,fr,cht}.exd` 判为国际服；与所选区服不符直接拒绝，防止覆错原文件。
4. **显卡配置**：勾选替换字体时，`gamecfg.EnsureHighResolutionUiAsset` 把 `FFXIV.cfg` 的 `UiAssetType` 置 1，改动前先在同目录生成 `FFXIV.cfg.bak`，已是 1 则不写入（代码未说明该字段的语义，此处不作解释）。
5. **等资源就绪**：`waitResourceReady(patchCtx)`，最多等 30 秒；超时后不再放行，而是提示并继续等同步结束（解压会覆盖 CSV，提前开始可能读到半写文件），期间可被「停止」打断。
6. **计算范围**：勾选表转成 `exd/<小写路径>` 集合；`replaceText` 但未选表直接报错。
7. **生成计划**：读 `ffxivgame.ver`，对 6 个文件全量 SHA256（`hashAll`）；国际服查 Release 得到 `matched` / `pending` / `localOnly`，非国际服固定 `localOnly`；再判断当前文本与字体是否原版，为每类文件选定动作。非国际服无法判断「是否官方原版」，只把 `patched_records.json` 的索引哈希当作「上次是本工具汉化的」的证据：命中则拒绝继续，未命中则把当前客户端当作汉化基线。不使用本地备份比对，也不使用 dat0 体积等启发式——版本号相同不代表内容相同，阈值又来自国际服原版，对自带中文字库的客户端会误判。
8. **备份保障**：`ensureAllOriginalFilesBackedUp` 确保 6 个文件都有哈希校验过的原版备份，缺的从游戏目录提取（国际服还可从云端下载）。所有复制都先写临时文件、边写边算 SHA256，与期望值一致才 rename，校验失败时目标文件保持原样。
9. **执行**：`executeText` / `executeFont` 按动作先备份或还原，再调用 `ApplyText` / `ApplyFont` 改写 sqpack。
10. **收尾**：写 `patched_records.json`（汉化后索引哈希 + 版本），把游戏目录、语言、目标语言、开关、规则写回 `config.json`。

### 计划动作

`verifier.Action` 取值与含义：

| 动作 | 含义 | 前提 |
|---|---|---|
| `patch` | 当前是原版，直接改写 | `determineOriginal` 判定为原版 |
| `restorePatch` | 当前已汉化，先从本地备份还原再改写 | 备份与当前版本一致且文件齐全 |
| `downloadRestorePatch` | 本地无备份，从云端 Release 取原文件，还原后改写 | 仅国际服，且云端已备份当前版本 |
| `skip` | 该组文件不需要处理 | 未勾选或无需变更 |
| `none` | 未启用 | — |

判定不出「是不是原版」时直接报错，提示先用官方启动器修复，不做猜测。

**核心语义：每次汉化都是「先回到原版，再追加」，不是叠加。** 这也是重复汉化不会让 dat0 无限膨胀的原因。

### 用户确认

只改文本、不换字体、字体仍是原版、且不是国服时，会通过 `Config.ConfirmFont` 弹窗提示「可能看不到中文」。国服固定不弹此确认（`verifier_test.go` 有对应用例）。

## 6. 二进制改写

### 文本

`replace.ReplaceEXDF` 的流程。先说明结构：每张表由 `exd/<表名>.exh` 描述结构（列偏移、类型、分页），数据按「页 + 语言」拆成 `exd/<表名>_<页号>_<语言>.exd`，`root.exl` 列出全部 EXH。

1. 打开 `0a0000.win32.index`，读 `root.exl` 得到全部 EXH 与表清单。
2. 每张表读 `resource/rawexd/**/*.csv`。CSV 结构固定为 4 行头加数据行：

   ```text
   key,0,2,8,9                     第 1 行：列偏移索引（key 列固定首列）
   #,Singular,Plural,Description,Name   第 2 行：字段名，csvx 当注释跳过
   offset,0,4,8,12                 第 3 行：EXD dataset 内偏移
   Int32,String,String,String,String    第 4 行：类型
   0,,,,                           第 5 行起：数据
   ```

3. 逐个 dataset 取原文与 CSV 译文，按所选规则链变换（`pipeline`），再经 `text.ParseTargetString` 编码：UTF-8 直写，`<hex:...>` 解成原始字节，用于游戏控制码。
4. 重建 EXDF，打包为 sqpack 二进制块，追加到 `0a0000.win32.dat0` 末尾。
5. 新偏移批量写回 `.index`。

写入采用两阶段提交：循环中只写 dat 并收集 `(entry, offset)`，结束后统一 `dat.Sync()` → 逐个 `WriteDataOffset` → `index.Sync()`。任何时刻索引都只指向已落盘的数据，中断只会留下无引用的 dat 空洞，不会产生坏索引。

### 字体

`resource/font/*.tex` 走 `sqpack.BuildTexBlock`（Type 4 纹理块），`*.fdt` 走 `BuildBinaryBlock`（Type 2 二进制块），同样追加到 `000000.win32.dat0` 并更新 `.index`。`BuildTexBlock` 目前只支持单 mip 纹理，遇到多 mip 会显式报错而不是产出坏数据。

### 规则链

`pipeline` 把规则做成可组合的 `Rule`，前端开关直接对应 `ID()`：

| ID | 作用 |
|---|---|
| `treasure_map_alias` | 藏宝图等级标注，数据来自 `rules.json` 的 `specialWord` |
| `materia_stat_suffix` | 魔晶石属性后缀，数据来自 `rules.json` 的 `materiaMap` |
| `opencc_s2tw` | 简体转臺灣繁體 |
| `ktg_cheat` | 将字库改为纯数字（特殊玩法） |

规则只作用于 `<hex:...>` 之外的普通文本，标签与载荷保持原样。

## 7. 客户端状态判定

`App.CheckClientStatus` 按固定优先级返回，命中即返回：

| 顺序 | 条件 | 结果 |
|---|---|---|
| 0 | 实际区服与所选区服不一致 | `mismatch` 区服不匹配 |
| 1 | `patched_records.json` 中目录、区服一致，且记录的索引哈希与当前文件全中（版本号仅用于展示） | `patched` 已汉化 |
| 2 | 非国际服（未被第 1 级命中） | `unsupported` 不支持检测 |
| 3 | 本地备份版本与当前一致 | 哈希一致 `original`；不一致 `modified` |
| 4 | 索引无法解析 | `unknown` 文件损坏 |
| 5 | 国际服且云端 Release 存在，4 秒超时内取到 `hashes.json` | 一致 `original`；不一致 `modified`（不穿透） |
| 6 | 启发式：`000000.win32.dat0 > 90MB`；国际服另查 `addon_0_en.exd` 内容是否含 CJK | `modified` |
| 兜底 | 以上均不命中 | `original`，描述为「未检测到已知修改」 |

第 2 级是硬性约束：非国际服没有官方原版哈希，且**版本号相同不代表文件内容相同**（区服热修、发行渠道差异都会让同版本客户端产生不同内容），所以本地备份也不能当作官方基准，只能如实报告「不支持检测」。

第 6 级与兜底属于 fail-open：启发式漏检时会把被第三方改过的文件当作原版。因此文案刻意写「未检测到已知修改」而非「官方原版」。

## 8. 还原

`App.RestoreOriginal` → `verifier.Restore`：

- 非国际服：必须存在版本一致的本地备份，否则报错并建议用官方启动器检查修复；不会尝试下载。手动还原还要求 `patched_records.json` 的索引哈希与当前文件一致，确认当前仍是本工具汉化的产物——版本号相同不足以证明备份未过期，否则旧备份会覆盖游戏热修后的新内容，此时直接拒绝并建议官方修复。
- 国际服：备份无效时可从 Release 资产下载对应版本原文件。
- 逐文件比对哈希，**只覆盖真正被改动的文件**，`index2` 也参与比对。
- 成功后清除该游戏目录在 `patched_records.json` 中的记录，其余目录的记录保留。

自动还原只发生在汉化确实写入过数据之后（`patchWroteData`）；资源缺失、备份校验失败等在写入前的错误直接返回，不去动客户端，避免把上一次成功的汉化静默回滚。还原过程会上报进度与日志，可被取消（用户主动中止的分支除外，该分支保证恢复跑完）。

## 9. 区服与语言

`internal/region` 是所有区服逻辑的分发点：

- `ParseLanguage`：接受 `GLOBAL/GL`、`JA/EN/DE/FR`、`CHS/CN/ZH/ZH-CN`、`TC/CHT`、`KO`，大小写与空白不敏感。
- `Of`：`CHS→chs`、`TC→tc`、`KO→ko`，其余归 `global`。
- `TargetLanguagesFor`：国际服一次处理 `JA, EN, DE, FR` 四种语言后缀，前端可勾选子集；国服 / 台服 / 韩服固定单一语言。

备份目录按区服而非语言划分，所以国际服四种语言共用同一份原版备份。

### 能力矩阵

| 能力 | 国际服 | 国服 / 韩服 / 繁中服 |
|---|---|---|
| 检测游戏版本 | 是 | 是 |
| 全量 SHA256 校验 | 是（6 个受管文件） | 是（6 个受管文件） |
| 执行汉化 | 是 | 是 |
| 本地备份与还原 | 是 | 是 |
| 云端原文件下载 | 是（Release tag 即游戏版本号） | 不提供 |
| 原版状态检测 | 云端 `hashes.json` 权威比对 | 不提供（没有官方哈希，同版本号内容也可能不同） |
| 还原来源 | 本地备份，无效时可从云端下载 | 仅本地备份，且要求当前仍是本工具汉化产物 |
| 基线来源 | 通过云端哈希核对的原版 | 当前客户端文件（无权威基准，靠 `patched_records.json` 识别已汉化） |

两类区服的差异只在于**云端是否提供该版本的权威原文件**：非国际服同样会读版本号、对 6 个受管文件做全量哈希、建立本地备份、校验备份完整性后再改写、按哈希还原；所有文件操作都校验 SHA256。但非国际服无法判断「当前文件是否官方原版」，这一点不因为本地存在备份而改变。

非国际服没有权威基线，因此汉化前会在日志中明确提示「将以当前客户端文件作为官方原版基线建立备份」；若检测到客户端仍处于上次汉化状态但备份已丢失，会拒绝继续而不是把汉化结果再备份一遍。

## 10. 离线包

`updater.ImportOffline` 支持 zip 或目录，自动识别公共前缀，内层 `rawexd.zip` / `font.zip` 会自动解压：

- **先暂存再切换**：包展开到资源目录同卷的 `.resource-import-*` 临时目录，校验通过后才替换 `resource/`。包损坏、解压失败或校验不通过时现有资源保持原样。
- **校验**：暂存目录至少要有 `rawexd/*.csv`；包内自带 `manifest.json` 时，`rawexd.zip` / `font.zip` 的实际 SHA256 必须与清单声明一致。
- **保留未提供文件**：切换后把旧目录中存在、包内没有的文件（如 `rules.json`）补回，等价于「包提供什么就覆盖什么」。
- 缺 `tables.json`：从 CSV 目录生成。
- 缺 `manifest.json`：调用 `generateFallbackManifest`。有 zip 时直接对 `rawexd.zip`、`font.zip` 取 SHA256（与发布清单同一值域，可用于比对）；只有解压目录时计算 `LocalFingerprint`（按 `相对路径:文件sha256` 排序拼接后再取 SHA256），仅作本地内容标识，不写入 `rawexd.sha256`，因此不会干扰云端同步判断。
- 导入后清除离线标记并标记资源就绪。

需要注意：CI 只发布 exe 与 macOS 包（`build_patcher.yml`），**仓库不发布离线资源包**。离线包由用户自行打包或第三方分享，所以 fallback 分支在实际使用中很常见。

资源完整性判定在 `modulesComplete`：`rawexd`、`rules`、`tables` 以清单哈希比对；字体不复用清单里的历史记录，而是对每个 `.fdt`/`.tex` 重新计算 SHA256（49 个文件约 17MB），文件被外部改动或写坏时能触发重新下载。

## 11. 构建与发布

- 本地构建：根目录 `pnpm run build`（`scripts/build.ts`）先构建前端产物，再 `go build` 产出根目录 exe。改完代码后必须跑一次。
- `build_patcher.yml`：打 `patcher-v*` tag 或手动触发，跑 `go test ./...` 后构建 Windows exe 与 macOS 包。
- `sync_assets.yml`：`resource/rawexd`、`resource/font`、`rules.json`、`tables.json`、`scripts/bundle.ts` 变动时重新打包资源并更新 `assets` 分支。
- `ffxiv_download.yml`：每半小时检查官方版本，未发布则新建 Release 并附 6 个原文件 zip 与 `hashes.json`。

## 12. 已知行为与限制

以下均为当前实现的真实行为，不是缺陷清单，改动前需评估影响面：

- `preset` 为 `custom` 时不持久化：`selected` 是前端内存中的 `Set`，重载后回退为 `recommended`，自定义范围静默丢失。
- `resourceHash` 未在界面展示：`updateResourceBuildMeta` 忽略 hash 参数，仅显示构建时间。
- 表列表 7114 项，勾选与搜索都会整表重建 DOM，无防抖；全展开时明显卡顿。
- `CheckAppUpdate` 只返回版本信息，前端提示并打开 Release 页，不做自动安装；弹窗额外提供「镜像下载」，走与资源下载相同的加速节点。只有 `patcher-v*` 参与主程序版本比较——仓库里还有 `v2.20.0`、`v2.18.x` 这类旧版 Java 汉化器的 tag，按「形如 x.y.z」采信会把它当成比 2.1.x 更新的版本。`patcher-v*` 的附件名由 CI 固定，降级到网页列表时按 `releases/download/<tag>/FFXIVChnTextPatch-SM-v2.exe`（macOS 为 `-macOS.zip`）直接拼地址，不再多请求一次。
- `BuildTexBlock` 仅支持单 mip 纹理，多 mip 显式报错。
- 状态判定的启发式与兜底为 fail-open（见第 7 节）。
- 备份与还原以 `meta.json` 为依据，写入使用临时文件 + rename，避免崩溃截断导致还原永久失败。

## 13. 代码索引

| 想改什么 | 去哪 |
|---|---|
| 界面、交互、事件订阅 | `frontend/src/main.ts`、`frontend/index.html`、`frontend/src/style.css` |
| Wails 绑定、配置、启动流程 | `app.go` |
| 汉化流程与状态判定 | `internal/verifier/verifier.go` |
| 二进制改写 | `internal/replace/replace.go` |
| sqpack 索引与块打包 | `internal/sqpack/index.go`、`dat.go`、`block.go` |
| EXD / EXH 解析 | `internal/exd/exdf.go`、`exhf.go` |
| CSV 读写 | `internal/csvx/csv.go` |
| 译文规则 | `internal/pipeline/rules.go`、`pipeline.go` |
| `<hex:...>` 处理 | `internal/text/hex.go` |
| 资源同步、离线包、更新检查 | `internal/updater/` |
| 区服语言 | `internal/region/region.go` |
| 进程互斥、目录探测 | `internal/gameproc/`、`internal/gamescan/scan.go` |

## 14. 测试

`go test ./...` 覆盖各包单测与端到端对拍。其中 `internal/golden` 是最有价值的保障：基准文件 `internal/sqpack/testdata/golden_exdf_hashes.json` 记录了旧版 FFXIVChnTextPatch-SM.exe（Java 版）对国际服原版执行「替换字体 + 替换文本（JA, EN）」后，由旧版解析器导出的 29036 个 EXD 的逐文件 SHA256；测试重新执行同样流程并逐字节抽样比对，任何影响输出字节的改动都会被拦住。

该测试处理约 400MB 数据，`-short` 模式会跳过。原版样本的查找顺序为：环境变量 `GOLDEN_SOURCE_DIR` → `testdata/golden_source` → 仓库外层的 `backup/global`；三者都没有时自动从 GitHub Release 下载到 `testdata/golden_source`，因此首次运行耗时较长，后续走缓存。
