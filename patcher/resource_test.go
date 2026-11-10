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

// TestResourceSyncStale 确认开始汉化前的补查节奏：从未检查过或超过间隔才算过期。
func TestResourceSyncStale(t *testing.T) {
	app := NewApp()
	if !app.resourceSyncStale() {
		t.Fatal("从未检查过时应视为过期")
	}
	app.resourceSyncedAt.Store(time.Now().UnixNano())
	if app.resourceSyncStale() {
		t.Fatal("刚检查过不应视为过期")
	}
	app.resourceSyncedAt.Store(time.Now().Add(-resourceRefreshInterval - time.Minute).UnixNano())
	if !app.resourceSyncStale() {
		t.Fatal("超过间隔后应视为过期")
	}
}

func TestIsAppBundlePath(t *testing.T) {
	cases := []struct {
		path string
		want bool
	}{
		{filepath.FromSlash("/Applications/FFXIVChnTextPatch-SM-v2.app/Contents/MacOS"), true},
		{filepath.FromSlash("/Users/me/Downloads/X.app/Contents/MacOS/resource"), true},
		{filepath.FromSlash("/Applications/X.app/Contents/Resources"), false},
		{filepath.FromSlash("/Users/me/Library/Application Support/FFXIVChnTextPatch/resource"), false},
		{filepath.FromSlash("/tmp/patcher"), false},
	}
	for _, tc := range cases {
		if got := isAppBundlePath(tc.path); got != tc.want {
			t.Errorf("isAppBundlePath(%q) = %v, want %v", tc.path, got, tc.want)
		}
	}
}

func TestWithinDir(t *testing.T) {
	parent := filepath.FromSlash("/Users/me/Library/Caches")
	child := filepath.Join(parent, "FFXIVChnTextPatch", "resource")
	sibling := filepath.FromSlash("/Users/me/Library/Caches2/FFXIVChnTextPatch")

	if !withinDir(parent, parent) {
		t.Fatal("目录自身应判定为在内")
	}
	if !withinDir(child, parent) {
		t.Fatal("子目录应判定为在内")
	}
	if withinDir(sibling, parent) {
		t.Fatal("只有相同前缀、不同目录名时不应误判")
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
