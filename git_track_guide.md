# Git 本地忽略文件跟踪指南 (conf/global.properties)

针对 `conf/global.properties` 文件的本地跟踪管理命令：

## 1. 取消本地跟踪（忽略本地修改）

```bash
git update-index --assume-unchanged conf/global.properties
```

## 2. 恢复本地跟踪

```bash
git update-index --no-assume-unchanged conf/global.properties
```

## 附：查看当前状态

```bash
git ls-files -v conf/global.properties
```

* 如果输出以 `h` 开头，表示已取消跟踪（本地修改被忽略）
* 如果输出以 `H` 开头，表示正在跟踪
