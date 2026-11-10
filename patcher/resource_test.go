package main

import (
	"os"
	"path/filepath"
	"testing"
	"time"
)

func TestResourceIncomplete(t *testing.T) {
	dir := t.TempDir()
	if resourceIncomplete(dir, true) {
		t.Fatal("自定义词条目录不要求内置 rawexd")
	}
	if !resourceIncomplete(dir, false) {
		t.Fatal("内置词条需要 rawexd 目录")
	}
	if err := os.MkdirAll(filepath.Join(dir, "rawexd"), 0o755); err != nil {
		t.Fatal(err)
	}
	if resourceIncomplete(dir, false) {
		t.Fatal("表清单由轻量同步单独补齐，不应阻止内置词条")
	}
}

// TestGetBootstrapLocalOnly 确认启动只读本地文件：空资源目录也能立即返回，不报错。
func TestGetBootstrapLocalOnly(t *testing.T) {
	app := NewApp()
	app.resourceDirOverride = t.TempDir()
	start := time.Now()
	info, err := app.GetBootstrap()
	if err != nil {
		t.Fatalf("启动不应因缺少资源而失败: %v", err)
	}
	if info.ResourceReady {
		t.Fatal("空资源目录不应标记为就绪")
	}
	if info.ResourceDir == "" {
		t.Fatal("应返回资源目录")
	}
	if elapsed := time.Since(start); elapsed > 3*time.Second {
		t.Fatalf("启动过于耗时（疑似联网）: %v", elapsed)
	}
}
