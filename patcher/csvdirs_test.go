package main

import (
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func writeTestTables(t *testing.T, resourceDir string) {
	t.Helper()
	doc := TablesDoc{
		Folders: map[string]string{"quest": "任务对话"},
		Tables:  map[string]string{"Item": "道具", "BNpcName": "敌人名称"},
		Files:   []string{"BNpcName", "Item", "quest/000/Test001"},
	}
	data, err := json.Marshal(doc)
	if err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(resourceDir, "tables.json"), data, 0o644); err != nil {
		t.Fatal(err)
	}
}

func writeTestCSV(t *testing.T, dir, name string) {
	t.Helper()
	path := filepath.Join(dir, filepath.FromSlash(name)+".csv")
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(path, []byte("key,0\n1,说明\n"), 0o644); err != nil {
		t.Fatal(err)
	}
}

func newCSVTestApp(t *testing.T) (*App, string) {
	t.Helper()
	resDir := filepath.Join(t.TempDir(), "resource")
	if err := os.MkdirAll(resDir, 0o755); err != nil {
		t.Fatal(err)
	}
	writeTestTables(t, resDir)
	writeTestCSV(t, filepath.Join(resDir, "rawexd"), "Item")
	app := NewApp()
	app.resourceDirOverride = resDir
	return app, resDir
}

func TestCSVContextScansCustomDir(t *testing.T) {
	app, resDir := newCSVTestApp(t)
	customDir := filepath.Join(t.TempDir(), "my_rawexd")
	writeTestCSV(t, customDir, "Item")
	writeTestCSV(t, customDir, "NotInTables")

	ctx, err := app.GetCSVContext(string("GLOBAL"))
	if err != nil {
		t.Fatalf("读取内置词条上下文失败: %v", err)
	}
	if ctx.Custom || len(ctx.TableList) != 3 || ctx.DefaultDir != filepath.Join(resDir, "rawexd") {
		t.Fatalf("内置词条上下文不正确: %+v", ctx)
	}

	cfg := defaultConfig()
	cfg.Language = "GLOBAL"
	cfg.CSVDirs["GLOBAL"] = CSVConfig{Dirs: []string{customDir}, Selected: customDir}
	if err := app.SaveConfig(cfg); err != nil {
		t.Fatalf("保存配置失败: %v", err)
	}

	ctx, err = app.GetCSVContext("GLOBAL")
	if err != nil {
		t.Fatalf("读取自定义词条上下文失败: %v", err)
	}
	if !ctx.Custom || ctx.Selected != customDir {
		t.Fatalf("自定义词条未生效: %+v", ctx)
	}
	if len(ctx.Dirs) != 1 || !ctx.Dirs[0].Valid {
		t.Fatalf("词条目录列表不正确: %+v", ctx.Dirs)
	}
	if len(ctx.TableList) != 1 || ctx.TableList[0].Key != "Item" {
		t.Fatalf("自定义词条表清单应只包含 tables.json 收录的文件: %+v", ctx.TableList)
	}

	csvDir, custom, err := app.resolveCSVDir(resDir, "GLOBAL")
	if err != nil || !custom || csvDir != customDir {
		t.Fatalf("解析汉化词条目录失败: dir=%s custom=%v err=%v", csvDir, custom, err)
	}

	ctx, err = app.SetCSVSelection("GLOBAL", "")
	if err != nil {
		t.Fatalf("切回内置词条失败: %v", err)
	}
	if ctx.Custom || len(ctx.TableList) != 3 {
		t.Fatalf("切回内置词条后表清单不正确: %+v", ctx)
	}
	if dir, custom, err := app.resolveCSVDir(resDir, "GLOBAL"); err != nil || custom || dir != filepath.Join(resDir, "rawexd") {
		t.Fatalf("切回内置后仍使用自定义目录: dir=%s custom=%v err=%v", dir, custom, err)
	}
}

func TestCSVContextInvalidDir(t *testing.T) {
	app, resDir := newCSVTestApp(t)
	emptyDir := t.TempDir()

	cfg := defaultConfig()
	cfg.Language = "GLOBAL"
	cfg.CSVDirs["GLOBAL"] = CSVConfig{Dirs: []string{emptyDir}, Selected: emptyDir}
	if err := app.SaveConfig(cfg); err != nil {
		t.Fatalf("保存配置失败: %v", err)
	}

	ctx, err := app.GetCSVContext("GLOBAL")
	if err != nil {
		t.Fatalf("读取上下文失败: %v", err)
	}
	if len(ctx.Dirs) != 1 || ctx.Dirs[0].Valid {
		t.Fatalf("空目录应标记为失效: %+v", ctx.Dirs)
	}
	if len(ctx.TableList) != 0 {
		t.Fatalf("失效目录不应产出表清单: %d", len(ctx.TableList))
	}
	if _, _, err := app.resolveCSVDir(resDir, "GLOBAL"); err == nil {
		t.Fatal("失效的自定义词条目录应阻止汉化")
	}

	if _, err := app.SetCSVSelection("GLOBAL", emptyDir); err == nil {
		t.Fatal("不应允许切换到失效目录")
	}

	ctx, err = app.RemoveCSVDirectory("GLOBAL", emptyDir, false)
	if err != nil {
		t.Fatalf("删除词条目录失败: %v", err)
	}
	if len(ctx.Dirs) != 0 || ctx.Selected != "" {
		t.Fatalf("删除后应回到内置词条: %+v", ctx)
	}
}

func TestCSVConfigIsolatedByRegion(t *testing.T) {
	app, _ := newCSVTestApp(t)
	customDir := filepath.Join(t.TempDir(), "my_rawexd")
	writeTestCSV(t, customDir, "Item")

	cfg := defaultConfig()
	cfg.Language = "GLOBAL"
	cfg.CSVDirs["GLOBAL"] = CSVConfig{Dirs: []string{customDir}, Selected: customDir}
	if err := app.SaveConfig(cfg); err != nil {
		t.Fatalf("保存配置失败: %v", err)
	}

	chinaCtx, err := app.GetCSVContext("CHS")
	if err != nil {
		t.Fatalf("读取国服上下文失败: %v", err)
	}
	if chinaCtx.Custom || len(chinaCtx.Dirs) != 0 {
		t.Fatalf("国服不应继承国际服的词条目录: %+v", chinaCtx)
	}

	loaded := app.loadConfig(app.resourceDir())
	if conf := loaded.CSVDirs["GLOBAL"]; conf.Selected != customDir {
		t.Fatalf("配置未正确落盘: %+v", conf)
	}
}

func TestCSVDirectoryMeta(t *testing.T) {
	app, _ := newCSVTestApp(t)
	customDir := filepath.Join(t.TempDir(), "my_rawexd")
	writeTestCSV(t, customDir, "Item")

	cfg := defaultConfig()
	cfg.Language = "GLOBAL"
	cfg.CSVDirs["GLOBAL"] = CSVConfig{Dirs: []string{customDir}, Selected: customDir}
	if err := app.SaveConfig(cfg); err != nil {
		t.Fatalf("保存配置失败: %v", err)
	}

	ctx, err := app.SetCSVDirectoryMeta("GLOBAL", customDir, "  我的修改版  ", " 只改技能名 ")
	if err != nil {
		t.Fatalf("设置词条目录信息失败: %v", err)
	}
	if len(ctx.Dirs) != 1 || ctx.Dirs[0].Title != "我的修改版" || ctx.Dirs[0].Desc != "只改技能名" {
		t.Fatalf("名称与描述未生效: %+v", ctx.Dirs)
	}

	loaded := app.loadConfig(app.resourceDir())
	if label := loaded.CSVDirs["GLOBAL"].Labels[customDir]; label.Title != "我的修改版" {
		t.Fatalf("名称未持久化: %+v", label)
	}

	if _, err := app.SetCSVDirectoryMeta("GLOBAL", customDir, " ", ""); err != nil {
		t.Fatalf("清空词条目录信息失败: %v", err)
	}
	loaded = app.loadConfig(app.resourceDir())
	if len(loaded.CSVDirs["GLOBAL"].Labels) != 0 {
		t.Fatalf("清空后不应保留标签: %+v", loaded.CSVDirs["GLOBAL"].Labels)
	}

	if _, err := app.SetCSVDirectoryMeta("GLOBAL", customDir, "临时名", ""); err != nil {
		t.Fatalf("恢复名称失败: %v", err)
	}
	if _, err := app.RemoveCSVDirectory("GLOBAL", customDir, false); err != nil {
		t.Fatalf("移除词条目录失败: %v", err)
	}
	loaded = app.loadConfig(app.resourceDir())
	if len(loaded.CSVDirs["GLOBAL"].Labels) != 0 {
		t.Fatalf("移除目录后应清掉标签: %+v", loaded.CSVDirs["GLOBAL"].Labels)
	}
}

func TestCopyBuiltinCSVDirectory(t *testing.T) {
	app, resDir := newCSVTestApp(t)
	writeTestCSV(t, filepath.Join(resDir, "rawexd"), "BNpcName")

	ctx, err := app.CopyBuiltinCSVDirectory("GLOBAL", false)
	if err != nil {
		t.Fatalf("制作副本失败: %v", err)
	}
	dstDir := filepath.Join(resDir, "custom-csv")
	if !ctx.Custom || ctx.Selected != dstDir {
		t.Fatalf("副本未成为当前目录: selected=%s custom=%v", ctx.Selected, ctx.Custom)
	}
	if _, err := os.Stat(filepath.Join(dstDir, "Item.csv")); err != nil {
		t.Fatalf("副本未复制 CSV: %v", err)
	}
	if len(ctx.Dirs) != 1 || !ctx.Dirs[0].Valid {
		t.Fatalf("副本未出现在目录列表中: %+v", ctx.Dirs)
	}
	// 名称留空，界面回落到文件夹名 custom-csv；描述记录副本基于哪份内置词条。
	if ctx.Dirs[0].Title != "" {
		t.Fatalf("副本不应预置名称: %+v", ctx.Dirs[0])
	}
	if !strings.HasPrefix(ctx.Dirs[0].Desc, "基于内置词条") {
		t.Fatalf("副本描述应记录来源词条: %+v", ctx.Dirs[0])
	}
	if ctx.Dirs[0].UpdatedAt == "" {
		t.Fatalf("副本应带最后更新时间: %+v", ctx.Dirs[0])
	}

	edited := filepath.Join(dstDir, "Item.csv")
	if err := os.WriteFile(edited, []byte("key,0\n1,自定义\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	ctx, err = app.CopyBuiltinCSVDirectory("GLOBAL", false)
	if err != nil {
		t.Fatalf("复用副本失败: %v", err)
	}
	if data, err := os.ReadFile(edited); err != nil || !strings.Contains(string(data), "自定义") {
		t.Fatalf("复用副本覆盖了用户修改: err=%v data=%q", err, string(data))
	}
	if len(ctx.Dirs) != 1 {
		t.Fatalf("重复制作副本不应重复加入列表: %+v", ctx.Dirs)
	}
}

// 复制先落到 .building 临时目录，完成后再改名；上次被强杀留下的半成品必须被清掉，
// 不能混进新副本，也不能让正式目录出现缺文件的中间状态。
func TestCopyBuiltinCSVDirectoryIsAtomic(t *testing.T) {
	app, resDir := newCSVTestApp(t)
	writeTestCSV(t, filepath.Join(resDir, "rawexd"), "BNpcName")
	dstDir := customCSVDir(resDir)
	tmpDir := dstDir + csvCopyTmpSuffix
	if err := os.MkdirAll(tmpDir, 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(tmpDir, "Stale.csv"), []byte("key,0\n1,旧\n"), 0o644); err != nil {
		t.Fatal(err)
	}

	if _, err := app.CopyBuiltinCSVDirectory("GLOBAL", false); err != nil {
		t.Fatalf("制作副本失败: %v", err)
	}
	if _, err := os.Stat(tmpDir); !os.IsNotExist(err) {
		t.Fatalf("临时目录应已清理: %v", err)
	}
	if _, err := os.Stat(filepath.Join(dstDir, "Stale.csv")); !os.IsNotExist(err) {
		t.Fatalf("半成品文件不应混入副本: %v", err)
	}
	for _, name := range []string{"Item.csv", "BNpcName.csv"} {
		if _, err := os.Stat(filepath.Join(dstDir, name)); err != nil {
			t.Fatalf("副本缺少 %s: %v", name, err)
		}
	}
}

func TestRemoveCSVDirectoryWithFiles(t *testing.T) {
	app, resDir := newCSVTestApp(t)
	ctx, err := app.CopyBuiltinCSVDirectory("GLOBAL", false)
	if err != nil {
		t.Fatalf("制作副本失败: %v", err)
	}
	if len(ctx.Dirs) != 1 || !ctx.Dirs[0].CanDeleteFiles {
		t.Fatalf("程序生成的副本应允许删除文件: %+v", ctx.Dirs)
	}
	dstDir := customCSVDir(resDir)

	ctx, err = app.RemoveCSVDirectory("GLOBAL", dstDir, true)
	if err != nil {
		t.Fatalf("删除副本目录失败: %v", err)
	}
	if len(ctx.Dirs) != 0 || ctx.Selected != "" {
		t.Fatalf("删除后应回到内置词条: %+v", ctx)
	}
	if _, err := os.Stat(dstDir); !os.IsNotExist(err) {
		t.Fatalf("副本目录应已被删除: %v", err)
	}
}

func TestRemoveCSVDirectoryKeepsForeignFiles(t *testing.T) {
	app, _ := newCSVTestApp(t)
	foreignDir := filepath.Join(t.TempDir(), "my_rawexd")
	writeTestCSV(t, foreignDir, "Item")

	cfg := defaultConfig()
	cfg.Language = "GLOBAL"
	cfg.CSVDirs["GLOBAL"] = CSVConfig{Dirs: []string{foreignDir}, Selected: foreignDir}
	if err := app.SaveConfig(cfg); err != nil {
		t.Fatalf("保存配置失败: %v", err)
	}
	ctx, err := app.GetCSVContext("GLOBAL")
	if err != nil {
		t.Fatalf("读取上下文失败: %v", err)
	}
	if ctx.Dirs[0].CanDeleteFiles {
		t.Fatalf("用户自选目录不应允许删除文件: %+v", ctx.Dirs[0])
	}

	if _, err := app.RemoveCSVDirectory("GLOBAL", foreignDir, true); err == nil {
		t.Fatal("删除非程序生成的目录应报错")
	}
	if _, err := os.Stat(filepath.Join(foreignDir, "Item.csv")); err != nil {
		t.Fatalf("非程序生成的目录文件不应被删除: %v", err)
	}
	loaded := app.loadConfig(app.resourceDir())
	if len(loaded.CSVDirs["GLOBAL"].Dirs) != 1 {
		t.Fatalf("删除失败时应保留列表项: %+v", loaded.CSVDirs["GLOBAL"])
	}
}

func TestRemoveCSVDirectoryFilesRejectsUnsafePaths(t *testing.T) {
	_, resDir := newCSVTestApp(t)
	managed := customCSVDir(resDir)
	if err := os.MkdirAll(managed, 0o755); err != nil {
		t.Fatal(err)
	}
	writeTestCSV(t, managed, "Item")

	for _, target := range []string{
		filepath.Join(resDir, "rawexd"),
		resDir,
		filepath.Dir(resDir),
		filepath.Join(managed, "sub"),
	} {
		if err := removeCSVDirectoryFiles(resDir, target); err == nil {
			t.Fatalf("不应允许删除 %s", target)
		}
	}
	if _, err := os.Stat(filepath.Join(resDir, "rawexd", "Item.csv")); err != nil {
		t.Fatalf("内置词条文件被误删: %v", err)
	}
	if err := removeCSVDirectoryFiles(resDir, managed); err != nil {
		t.Fatalf("删除规范副本目录失败: %v", err)
	}
	if _, err := os.Stat(managed); !os.IsNotExist(err) {
		t.Fatalf("副本目录应已被删除: %v", err)
	}
}

func TestCSVContextResourceHash(t *testing.T) {
	app, resDir := newCSVTestApp(t)
	// 该用例验证发布构建的清单哈希展示，与构建模式无关。
	app.updater.LocalOnly = false
	manifest := `{"generatedAt":"2026-10-16T08:30:00Z","rawexd":{"sha256":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef","size":1}}`
	if err := os.WriteFile(filepath.Join(resDir, "manifest.json"), []byte(manifest), 0o644); err != nil {
		t.Fatal(err)
	}

	ctx, err := app.GetCSVContext("GLOBAL")
	if err != nil {
		t.Fatalf("读取上下文失败: %v", err)
	}
	if ctx.ResourceHash != "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef" {
		t.Fatalf("内置词条应下发完整哈希: %q", ctx.ResourceHash)
	}

	ctx, err = app.CopyBuiltinCSVDirectory("GLOBAL", false)
	if err != nil {
		t.Fatalf("制作副本失败: %v", err)
	}
	// 描述里记录的是短哈希，便于一眼看出副本基于哪份快照。
	if len(ctx.Dirs) != 1 || !strings.Contains(ctx.Dirs[0].Desc, "01234567") {
		t.Fatalf("副本描述应带内置词条短哈希: %+v", ctx.Dirs)
	}
	if strings.Contains(ctx.Dirs[0].Desc, "简繁转换") {
		t.Fatalf("直接制作的副本不应标注简繁转换: %+v", ctx.Dirs[0])
	}
}

func TestCopyBuiltinCSVDirectoryConverts(t *testing.T) {
	app, resDir := newCSVTestApp(t)
	src := filepath.Join(resDir, "rawexd", "Item.csv")
	if err := os.WriteFile(src, []byte("key,0\n1,金币<hex:02100103>可以在\n"), 0o644); err != nil {
		t.Fatal(err)
	}

	ctx, err := app.CopyBuiltinCSVDirectory("GLOBAL", true)
	if err != nil {
		t.Fatalf("制作简繁转换副本失败: %v", err)
	}
	if len(ctx.Dirs) != 1 || !strings.Contains(ctx.Dirs[0].Desc, "已简繁转换") {
		t.Fatalf("副本描述应标注简繁转换: %+v", ctx.Dirs)
	}

	data, err := os.ReadFile(filepath.Join(customCSVDir(resDir), "Item.csv"))
	if err != nil {
		t.Fatalf("读取简体转换副本失败: %v", err)
	}
	text := string(data)
	if !strings.Contains(text, "金幣") {
		t.Fatalf("副本内容未转换为繁體: %q", text)
	}
	if !strings.Contains(text, "<hex:02100103>") {
		t.Fatalf("十六进制占位不应被改写: %q", text)
	}

	// 已有副本时再要求转换会覆盖用户修改，直接拒绝。
	if _, err := app.CopyBuiltinCSVDirectory("GLOBAL", true); err == nil {
		t.Fatal("已存在副本时不应重新转换")
	}
}
