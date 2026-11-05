package main

import (
	"context"
	"os"
	"path/filepath"
	goruntime "runtime"
	"testing"

	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/fontengine"
)

// writeFontSourceConfig 在数据根目录写配置，模拟界面选择的字体来源。
func writeFontSourceConfig(t *testing.T, root, fontSource string) {
	t.Helper()
	cfg := `{"fontSource":"` + fontSource + `"}`
	if err := os.WriteFile(filepath.Join(root, "config.json"), []byte(cfg), 0o644); err != nil {
		t.Fatal(err)
	}
}

// 内置字体包来源不需要编译组件：预检必须直接放行。
func TestCheckFontSourcePackageNeedsNoCompiler(t *testing.T) {
	resDir := filepath.Join(t.TempDir(), "resource")
	if err := os.MkdirAll(resDir, 0o755); err != nil {
		t.Fatal(err)
	}
	writeFontSourceConfig(t, filepath.Dir(resDir), "package")

	app := NewApp()
	app.resourceDirOverride = resDir
	if err := app.checkFontSource(context.Background(), resDir, t.TempDir()); err != nil {
		t.Fatalf("内置字体包来源不应依赖编译组件: %v", err)
	}
}

// 自定义方案来源：数据根目录里已有编译组件时直接可用，不再下载。
func TestCheckFontSourceCompileUsesInstalledCompiler(t *testing.T) {
	if goruntime.GOOS != "windows" {
		t.Skip("现场编译只在 Windows 上可用")
	}
	root := t.TempDir()
	resDir := filepath.Join(root, "resource")
	if err := os.MkdirAll(resDir, 0o755); err != nil {
		t.Fatal(err)
	}
	writeFontSourceConfig(t, root, "compile")
	if err := os.WriteFile(filepath.Join(root, fontengine.CLIBinaryName), make([]byte, 1<<20+1), 0o755); err != nil {
		t.Fatal(err)
	}

	app := NewApp()
	app.resourceDirOverride = resDir
	if err := app.checkFontSource(context.Background(), resDir, t.TempDir()); err != nil {
		t.Fatalf("已有编译组件时不应失败: %v", err)
	}
}

// 自定义方案来源拿不到编译组件时必须报错中止，不再回落到内置字体包。
func TestCheckFontSourceCompileWithoutCompilerFails(t *testing.T) {
	root := t.TempDir()
	resDir := filepath.Join(root, "resource")
	if err := os.MkdirAll(resDir, 0o755); err != nil {
		t.Fatal(err)
	}
	writeFontSourceConfig(t, root, "compile")
	// 清空 PATH，避免本机恰好装了同名程序影响断言。
	t.Setenv("PATH", "")

	app := NewApp()
	app.resourceDirOverride = resDir
	// 预取消的 ctx 让下载循环立刻退出，测试不触网。
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	if err := app.checkFontSource(ctx, resDir, t.TempDir()); err == nil {
		t.Fatal("选自定义方案却拿不到编译组件时必须报错，不能静默改用内置字体包")
	}
}
