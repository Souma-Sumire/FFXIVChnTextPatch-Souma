//go:build development

package main

import (
	"os"
	"path/filepath"
	"testing"
)

func TestDevelopmentBuildUsesLocalResource(t *testing.T) {
	if !localResourceOnly {
		t.Fatal("development 构建应启用本地资源模式")
	}
	app := NewApp()
	if !app.updater.LocalOnly {
		t.Fatal("NewApp 应把本地资源模式传给 updater")
	}
	info, err := app.CheckAppUpdate()
	if err != nil {
		t.Fatalf("本地开发构建的更新检查不应失败: %v", err)
	}
	if info.HasUpdate {
		t.Fatal("本地开发构建不应提示主程序更新")
	}
}

// 启动同步在本地开发构建里只校验工作区文件，不下载、不写 manifest.json。
func TestDevelopmentSyncBuiltinResourceUsesLocalFiles(t *testing.T) {
	dir := t.TempDir()
	if err := os.MkdirAll(filepath.Join(dir, "rawexd"), 0o755); err != nil {
		t.Fatal(err)
	}
	for name, content := range map[string]string{
		"rawexd/A.csv": "a",
		"tables.json":  `{"files":["A"]}`,
		"rules.json":   `{}`,
	} {
		if err := os.WriteFile(filepath.Join(dir, filepath.FromSlash(name)), []byte(content), 0o644); err != nil {
			t.Fatal(err)
		}
	}

	app := NewApp()
	app.resourceDirOverride = dir
	if err := app.SyncBuiltinResource(); err != nil {
		t.Fatalf("本地资源完整时同步不应失败: %v", err)
	}
	if !app.builtinSynced.Load() {
		t.Fatal("同步成功后应标记内置词条已就绪")
	}
	if _, err := os.Stat(filepath.Join(dir, "manifest.json")); !os.IsNotExist(err) {
		t.Fatalf("本地开发构建不应写 manifest.json: %v", err)
	}
}
