# 架构

Wails v3，Go 加原生 TypeScript，打包成一个 `FFXIVChnTextPatch-SM-v2.exe`（macOS 为 `FFXIVChnTextPatch-SM-v2-macOS.zip`）。

只写代码里看不出来、改错会出事的。大部分理由已经在注释里了，这里不复述。

## 代码地图

`internal/` 下都是包，下表省略这个前缀。

| 位置 | 管什么 |
|---|---|
| `frontend/`：`src/main.ts`、`index.html`、`src/style.css` | 界面、交互、字体编辑器（`runFontCommand`） |
| `app.go` | 绑定、配置、启动探测、并发与取消、资源同步编排 |
| `csvdirs.go` | 词条目录 |
| `diagnostics.go` | 会话诊断快照：环境、配置、资源、客户端与备份状态 |
| `verifier/verifier.go` | 版本与备份校验、回原版、跑改写、还原 |
| `verifier/github.go` | Release 访问、镜像竞速 |
| `replace/` | 追加 dat、改 index、撤销 |
| `sqpack/`、`exd/`、`csvx/` | sqpack 索引与块、EXD/EXH、CSV |
| `pipeline/`、`text/` | 规则链、`<hex:...>` |
| `fontengine/` | 字体方案、系统字体、TTMP、预览 |
| `updater/` | 词条同步、自更新 |
| `region/`、`netproxy/` | 区服映射、代理探测 |
| `gamecfg/`、`gameproc/`、`gamescan/` | 游戏配置、进程互斥、目录探测 |
| `frontend/src/font_data.ts`、`../tools/fontchanger/` | 游戏字体数据 |
| `buildmode_development.go`、`buildmode_release.go`、`../scripts/build.ts` | 开发/发布构建 |

调用只往下走：app → verifier → replace → sqpack / exd / pipeline。底层包不反向引用编排层。

## 数据

云端三份，别混用。

| 在哪 | 是什么 | 谁用 |
|---|---|---|
| `assets` 分支，孤儿分支只留最新一次构建 | 清单、`rawexd.zip`、`font.zip` | 内置词条同步 |
| Release，tag 就是游戏版本 | 6 个 sqpack 加 `hashes.json` | 国际服查原版、还原 |
| `fontchanger-cli` 分支 | 编译组件 exe | 第一次换自定义字体时下载 |

本地是这样：

```text
resource/                   rawexd/（7114 个 CSV）、font/、rules.json、tables.json、manifest.json
backup/global|chs|tc|ko/    6 个 sqpack 加 meta.json（版本号、每个文件的 SHA256）
fontchanger/                编译组件，带自己的 config.json
config.json                 主程序配置
```

管 6 个文件：文本 `0a0000.win32.*`、字体 `000000.win32.*`，各有 dat0、index、index2。改写只动 index，index2 一直是原版，但备份和还原也覆盖它。

`resolveResourceDir` 优先用可写的 `resource/`，`backup/` 和 `config.json` 跟着它走。macOS 上资源落在缓存目录时，这两个改放 `Application Support`。

## 改写时的硬约束

1. 每次都先回官方原版，再追加这次的数据。直接叠着写，汉化几次 dat0 就撑爆了。回原版前逐文件比哈希，已经一致的不写。
2. dat 只往后追加，index 只改 4 字节偏移。所以记下「dat0 原长度 + 每个被改偏移的原值」就能精确退回，不用原版备份，未知版本也救得回来。
3. 提交分两步：`dat.Sync` → 改偏移 → `index.Sync`。反了的话索引可能指向还没落盘的地方。
4. 撤销是反着来：先把索引改回去刷盘，再截断 dat。先截断就坏索引了。「回原版」在记录撤销之前做，所以失败会停在官方原版，不是上次汉化。
5. 只撤这次真写过的文件组。只勾了文本，字体就别动。
6. 字体来源 `package` 和 `compile` 二选一，失败就中止，不自动换另一种。这个判断得在写任何客户端文件之前做完。
7. 读到畸形的 sqpack 直接报错，不 panic，也不按头里声明的长度去分配内存。

## 区服差异

备份按区服存，国际服四种语言共用一份。差别只在云端有没有这个版本的权威原文件。

| | 国际服 | 国服、韩服、繁中服 |
|---|---|---|
| 云端原文件 | 有 | 没有 |
| 查原版 | 跟 `hashes.json` 比 | 只能跟自己的基线比 |
| 基线哪来 | 云端核对过的原版 | 汉化前从当前客户端提 |

## 已知陷阱

- 表列表 7114 项，勾选和搜索都是整表重建 DOM，没有防抖，全展开会卡。
- 自定义词条目录按路径精确匹配，macOS 分大小写，文件名和表名大小写对不上就读不到。
- `rawexd.zip`（28MB）和 `.verify.json` 会一直躺着。后者删了只是下次启动多做一次完整校验。
- `preset` 是 `custom` 时不会存，重载后回落 `recommended`。
- 版本号只认 `patcher-v*`。仓库里还有旧 Java 版汉化器的 `v2.x` tag，按「x.y.z 就算新版本」判断会误报升级。
- macOS 上没有自定义字体方案，也没有自动更新，只能用内置字体包加手动下载。
- 进程被强杀就来不及撤销，但两步提交保证索引不会指向没落盘的数据，剩下的 dat 空洞下次汉化前的还原会盖掉。
- 汉化或还原期间 `beginBusy` 生效，自更新的调用一律拒掉。

## 测试

端到端在 `internal/e2e`，要真实原版 sqpack，从 `GOLDEN_SOURCE_DIR` 或外层 `backup/global` 找，找不到就跳过。断言只看输入数据，译文更新不用改测试。
