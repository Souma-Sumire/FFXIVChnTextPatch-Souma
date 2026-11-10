package main

import (
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// newBuildFixture 造一个数据根目录与其中的 resource 目录，返回 (resourceDir, buildDir)。
func newBuildFixture(t *testing.T) (string, string) {
	t.Helper()
	root := t.TempDir()
	resourceDir := filepath.Join(root, "resource")
	if err := os.MkdirAll(resourceDir, 0o755); err != nil {
		t.Fatalf("建 resource 目录失败: %v", err)
	}
	return resourceDir, fontBuildDir(resourceDir)
}

func writeBuildMeta(t *testing.T, app *App, resourceDir, buildDir, presetID, content string, use16Bit bool) {
	t.Helper()
	if err := os.MkdirAll(buildDir, 0o755); err != nil {
		t.Fatalf("建编译目录失败: %v", err)
	}
	// 产物里至少要有一张 .tex，否则会被判成不完整。
	if err := os.WriteFile(filepath.Join(buildDir, "font1.tex"), make([]byte, 16), 0o644); err != nil {
		t.Fatalf("写测试贴图失败: %v", err)
	}
	meta := fontBuildMeta{
		Version:    fontBuildVersion,
		PresetID:   presetID,
		Content:    hashText(content),
		Use16Bit:   use16Bit,
		Compiler:   app.compilerStamp(),
		CompiledAt: "2026-11-10T18:00:00+08:00",
		TexCount:   1,
		SideLength: 4096,
	}
	raw, err := json.Marshal(meta)
	if err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(fontBuildMetaPath(resourceDir), raw, 0o644); err != nil {
		t.Fatal(err)
	}
}

// 没编译过时不能放行，且要说清楚原因。
func TestFontBuildInfoWithoutArtifact(t *testing.T) {
	app := NewApp()
	resourceDir, _ := newBuildFixture(t)

	info := app.fontBuildInfoFor(resourceDir, "custom:mine", `{"fontSets":[]}`, true)
	if info.Ready {
		t.Fatal("没有产物时不应就绪")
	}
	if !strings.Contains(info.Reason, "尚未编译") {
		t.Fatalf("应说明尚未编译，实际: %s", info.Reason)
	}
}

// 方案、位数、编译器都对得上时放行，并把张数与边长带给界面。
func TestFontBuildInfoMatchesArtifact(t *testing.T) {
	app := NewApp()
	resourceDir, buildDir := newBuildFixture(t)
	content := `{"fontSets":[{"sideLength":4096}]}`
	writeBuildMeta(t, app, resourceDir, buildDir, "custom:mine", content, true)

	info := app.fontBuildInfoFor(resourceDir, "custom:mine", content, true)
	if !info.Ready {
		t.Fatalf("应判定为已编译，实际原因: %s", info.Reason)
	}
	if info.TexCount != 1 || info.SideLength != 4096 {
		t.Fatalf("张数/边长应来自编译记录，实际 %d 张 %dpx", info.TexCount, info.SideLength)
	}
}

// 方案改了但没重新编译，是最容易漏掉的一种：必须判成不可用。
func TestFontBuildInfoDetectsStaleContent(t *testing.T) {
	app := NewApp()
	resourceDir, buildDir := newBuildFixture(t)
	writeBuildMeta(t, app, resourceDir, buildDir, "custom:mine", `{"fontSets":[{"sideLength":4096}]}`, true)

	info := app.fontBuildInfoFor(resourceDir, "custom:mine", `{"fontSets":[{"sideLength":4096},{"sideLength":4096}]}`, true)
	if info.Ready {
		t.Fatal("方案内容变了，旧产物不应继续放行")
	}
	if !strings.Contains(info.Reason, "内容已变更") {
		t.Fatalf("应说明方案内容已变更，实际: %s", info.Reason)
	}
}

// 16 位贴图开关变了会让产物不兼容，同样要重编。
func TestFontBuildInfoDetectsTextureBitChange(t *testing.T) {
	app := NewApp()
	resourceDir, buildDir := newBuildFixture(t)
	content := `{"fontSets":[{"sideLength":4096}]}`
	writeBuildMeta(t, app, resourceDir, buildDir, "custom:mine", content, true)

	info := app.fontBuildInfoFor(resourceDir, "custom:mine", content, false)
	if info.Ready {
		t.Fatal("贴图位数变了，旧产物不应继续放行")
	}
	if !strings.Contains(info.Reason, "位数") {
		t.Fatalf("应说明贴图位数变化，实际: %s", info.Reason)
	}
}

// 换方案后旧产物不能顶用，哪怕内容碰巧一样也不该被当成同一份。
func TestFontBuildInfoDetectsPresetSwitch(t *testing.T) {
	app := NewApp()
	resourceDir, buildDir := newBuildFixture(t)
	content := `{"fontSets":[{"sideLength":4096}]}`
	writeBuildMeta(t, app, resourceDir, buildDir, "custom:other", content, true)

	info := app.fontBuildInfoFor(resourceDir, "custom:mine", content, true)
	if info.Ready {
		t.Fatal("方案 ID 变了，旧产物不应继续放行")
	}
	if !strings.Contains(info.Reason, "方案已变更") {
		t.Fatalf("应说明方案已变更，实际: %s", info.Reason)
	}
}
