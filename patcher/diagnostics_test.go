package main

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestDiagnosticsSnapshotContainsSessionState(t *testing.T) {
	app := NewApp()
	app.resourceDirOverride = t.TempDir()

	lines := app.GetDiagnostics()
	// 快照按小节合并成十几行；上限放宽到 24，既不会因为多一行小节就失败，
	// 也拦得住重新膨胀回每项一行的写法。
	if len(lines) > 24 {
		t.Fatalf("诊断快照应合并成十几行，实际 %d 行: %v", len(lines), lines)
	}
	for _, line := range lines {
		if !strings.HasPrefix(line, diagPrefix) {
			t.Fatalf("诊断行缺少统一前缀: %q", line)
		}
	}
	joined := strings.Join(lines, "\n")
	for _, want := range []string{
		"会话快照", "环境：", "路径：", "配置：", "资源：", "清单：", "字体：",
		"客户端：未设置游戏目录", "本地备份：无", "网络：", "镜像=未选", "权限=", "环境变量=", "出口=",
	} {
		if !strings.Contains(joined, want) {
			t.Fatalf("诊断信息缺少 %q\n%s", want, joined)
		}
	}
	for _, unwanted := range []string{"<nil>", "Go go", "16 核", "PID "} {
		if strings.Contains(joined, unwanted) {
			t.Fatalf("诊断信息不应包含冗余内容 %q\n%s", unwanted, joined)
		}
	}
}

// 全新安装时资源还没下载，缺文件必须报"未生成/目录缺失"，
// 否则用户和看日志的人会把正常状态当成故障。
func TestDiagnosticsReportsMissingResourceAsAbsent(t *testing.T) {
	app := NewApp()
	app.resourceDirOverride = t.TempDir()

	joined := strings.Join(app.GetDiagnostics(), "\n")
	for _, want := range []string{"清单：未生成", "表清单=未生成", "CSV=目录缺失"} {
		if !strings.Contains(joined, want) {
			t.Fatalf("诊断信息缺少 %q\n%s", want, joined)
		}
	}
	for _, unwanted := range []string{"统计失败", "读取失败: "} {
		if strings.Contains(joined, unwanted) {
			t.Fatalf("缺失的资源不应报成 %q\n%s", unwanted, joined)
		}
	}
}

// 实际 CSV 与清单记录对不上时要说出来：这通常意味着上次同步没写完。
func TestDiagnosticsFlagsCSVCountMismatch(t *testing.T) {
	dir := t.TempDir()
	if err := os.MkdirAll(filepath.Join(dir, "rawexd", "sub"), 0o755); err != nil {
		t.Fatal(err)
	}
	for _, name := range []string{"a.csv", filepath.Join("sub", "b.csv")} {
		if err := os.WriteFile(filepath.Join(dir, "rawexd", name), []byte("x"), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	manifest := `{"generatedAt":"2026-11-10 12:00","rawexd":{"sha256":"aa","size":2,"count":7114}}`
	if err := os.WriteFile(filepath.Join(dir, "manifest.json"), []byte(manifest), 0o644); err != nil {
		t.Fatal(err)
	}

	app := NewApp()
	app.resourceDirOverride = dir
	joined := strings.Join(app.GetDiagnostics(), "\n")
	if !strings.Contains(joined, "CSV=2（清单 7114，不一致）") {
		t.Fatalf("未检出 CSV 数量与清单不一致\n%s", joined)
	}
}

func TestPatchRequestSummaryKeepsLargeTableListCompact(t *testing.T) {
	tables := make([]string, 7114)
	for i := range tables {
		tables[i] = fmt.Sprintf("Table%d", i)
	}
	summary := patchRequestSummary(PatchRequest{Language: "GLOBAL", SelectedTables: tables})
	if len(summary) > 600 {
		t.Fatalf("表清单不应整表写入日志，实际长度 %d", len(summary))
	}
	for _, want := range []string{"selectedTables=7114", "指纹", "示例 Table0、Table1"} {
		if !strings.Contains(summary, want) {
			t.Fatalf("请求摘要缺少 %q: %s", want, summary)
		}
	}
	// 置 nil 只为避免整表序列化，输出里不该再留一个 null 字段。
	if strings.Contains(summary, `"selectedTables"`) {
		t.Fatalf("请求摘要不应保留 selectedTables 原字段: %s", summary)
	}
}

// 调试信息会随反馈贴到公开 issue，路径里的账户名必须换成占位符。
// 除用户目录外还要覆盖 JSON 里的转义写法，配置里存的就是那种形式。
func TestRedactPathsHidesUserProfile(t *testing.T) {
	home, err := os.UserHomeDir()
	if err != nil || home == "" {
		t.Skip("无法确定用户目录")
	}
	user := filepath.Base(home)
	escaped := strings.ReplaceAll(filepath.Join(home, "Games", "FFXIV"), `\`, `\\`)
	got := redactPaths(filepath.Join(home, "Documents", "My Games") + "|" + escaped + "|" + filepath.ToSlash(filepath.Join(home, "x")))
	if strings.Contains(strings.ToLower(got), strings.ToLower(user)) {
		t.Fatalf("用户名未被隐去: %s", got)
	}
	if !strings.Contains(got, "%USERPROFILE%") {
		t.Fatalf("缺少占位符: %s", got)
	}

	app := NewApp()
	app.resourceDirOverride = t.TempDir()
	joined := strings.Join(app.GetDiagnostics(), "\n")
	if strings.Contains(strings.ToLower(joined), strings.ToLower(user)) {
		t.Fatalf("诊断快照仍含用户名\n%s", joined)
	}
}
