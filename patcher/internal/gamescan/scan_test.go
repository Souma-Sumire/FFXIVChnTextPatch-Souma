package gamescan

import (
	"context"
	"os"
	"path/filepath"
	"testing"
)

const testName = "FINAL FANTASY XIV - A Realm Reborn"

func mkdirs(t *testing.T, root string, rels ...string) {
	t.Helper()
	for _, rel := range rels {
		if err := os.MkdirAll(filepath.Join(root, filepath.FromSlash(rel)), 0o755); err != nil {
			t.Fatalf("创建目录 %s 失败: %v", rel, err)
		}
	}
}

func TestFindByDepth(t *testing.T) {
	root := t.TempDir()
	mkdirs(t, root,
		testName,
		"Games/"+testName,
		"Games/SquareEnix/"+testName,
		"Deep/A/B/"+testName,
	)

	got := Find(context.Background(), []string{root}, testName, 3, nil)
	want := []string{
		filepath.Join(root, testName),
		filepath.Join(root, "Games", testName),
		filepath.Join(root, "Games", "SquareEnix", testName),
	}
	if len(got) != len(want) {
		t.Fatalf("命中数量不符: got=%v want=%v", got, want)
	}
	for i := range want {
		if got[i] != want[i] {
			t.Errorf("第 %d 项不符: got=%s want=%s", i, got[i], want[i])
		}
	}
}

func TestFindIsCaseInsensitive(t *testing.T) {
	root := t.TempDir()
	mkdirs(t, root, "Games/final fantasy xiv - a realm reborn")

	got := Find(context.Background(), []string{root}, testName, 3, nil)
	if len(got) != 1 {
		t.Fatalf("大小写不敏感匹配失败: %v", got)
	}
}

func TestFindSkipsSystemDirs(t *testing.T) {
	root := t.TempDir()
	mkdirs(t, root,
		"Windows/"+testName,
		"$Recycle.Bin/"+testName,
		"ProgramData/"+testName,
	)

	if got := Find(context.Background(), []string{root}, testName, 3, nil); len(got) != 0 {
		t.Fatalf("不应展开系统目录，命中: %v", got)
	}
}

func TestFindAppliesAccept(t *testing.T) {
	root := t.TempDir()
	mkdirs(t, root, "Games/"+testName, "Steam/"+testName)

	got := Find(context.Background(), []string{root}, testName, 3, func(dir string) bool {
		return filepath.Base(filepath.Dir(dir)) == "Steam"
	})
	if len(got) != 1 || filepath.Base(filepath.Dir(got[0])) != "Steam" {
		t.Fatalf("accept 过滤结果不符: %v", got)
	}
}

func TestFindRejectsInvalidArgs(t *testing.T) {
	root := t.TempDir()
	mkdirs(t, root, testName)

	cases := []struct {
		desc     string
		roots    []string
		folder   string
		maxDepth int
	}{
		{"空目录名", []string{root}, "", 3},
		{"深度为零", []string{root}, testName, 0},
		{"无根目录", nil, testName, 3},
	}
	for _, tc := range cases {
		if got := Find(context.Background(), tc.roots, tc.folder, tc.maxDepth, nil); got != nil {
			t.Errorf("%s 应返回空结果，实际: %v", tc.desc, got)
		}
	}
}

func TestFindStopsOnCanceledContext(t *testing.T) {
	root := t.TempDir()
	mkdirs(t, root, testName, "Games/"+testName)

	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	if got := Find(ctx, []string{root}, testName, 3, nil); got != nil {
		t.Fatalf("上下文已取消时不应继续扫描: %v", got)
	}
}

func TestFindAnyMatchesMultipleFolderNames(t *testing.T) {
	root := t.TempDir()
	mkdirs(t, root,
		"Games/FFXIV",
		"WeGameApps/rail_apps/最终幻想14(2000340)",
		"Other/Unrelated",
	)

	got := FindAny(context.Background(), []string{root}, []string{testName, "FFXIV", "最终幻想14(2000340)"}, 3, nil)
	want := []string{
		filepath.Join(root, "Games", "FFXIV"),
		filepath.Join(root, "WeGameApps", "rail_apps", "最终幻想14(2000340)"),
	}
	if len(got) != len(want) {
		t.Fatalf("FindAny 命中数量不符: got=%v want=%v", got, want)
	}
	for i := range want {
		if got[i] != want[i] {
			t.Errorf("第 %d 项不符: got=%s want=%s", i, got[i], want[i])
		}
	}
}

func TestFindClientsByStructureUpTo4Levels(t *testing.T) {
	root := t.TempDir()
	clientDirs := []string{
		"CustomFF14Root",
		"Games/FFXIV_CN",
		"Games/SquareEnix/MyFFXIV",
		"A/B/C/AnyCustomName",
	}
	for _, rel := range clientDirs {
		mkdirs(t, root, rel+"/boot", rel+"/game")
		exePath := filepath.Join(root, filepath.FromSlash(rel), "game", "ffxiv_dx11.exe")
		if err := os.WriteFile(exePath, []byte("MZ"), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	// 第 5 层及系统目录不应被扫到
	for _, ignored := range []string{"A/B/C/D/TooDeep", "Users/Someone/FFXIV", ".git/FFXIV"} {
		mkdirs(t, root, ignored+"/boot", ignored+"/game")
		exePath := filepath.Join(root, filepath.FromSlash(ignored), "game", "ffxiv_dx11.exe")
		if err := os.WriteFile(exePath, []byte("MZ"), 0o644); err != nil {
			t.Fatal(err)
		}
	}

	accept := func(dir string) bool {
		st, err := os.Stat(filepath.Join(dir, "game", "ffxiv_dx11.exe"))
		return err == nil && !st.IsDir()
	}
	got := FindClients(context.Background(), []string{root}, DefaultMaxDepth, accept)
	want := []string{
		filepath.Join(root, "A", "B", "C", "AnyCustomName"),
		filepath.Join(root, "CustomFF14Root"),
		filepath.Join(root, "Games", "FFXIV_CN"),
		filepath.Join(root, "Games", "SquareEnix", "MyFFXIV"),
	}
	if len(got) != len(want) {
		t.Fatalf("FindClients 命中数量不符: got=%v want=%v", got, want)
	}
	for i := range want {
		if got[i] != want[i] {
			t.Errorf("第 %d 项不符: got=%s want=%s", i, got[i], want[i])
		}
	}
}
