package updater

import (
	"context"
	"testing"

	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/verifier"
)

type mockUpdateCloud struct {
	releases []*verifier.Release
	err      error
}

func (m *mockUpdateCloud) ReleaseByTag(ctx context.Context, repo, tag string) (*verifier.Release, error) {
	for _, r := range m.releases {
		if r.Tag == tag {
			return r, nil
		}
	}
	return nil, verifier.ErrNotFound
}

func (m *mockUpdateCloud) RecentReleases(ctx context.Context, repo string, limit int) ([]*verifier.Release, error) {
	if m.err != nil {
		return nil, m.err
	}
	return m.releases, nil
}

func (m *mockUpdateCloud) Download(ctx context.Context, url, dst string, progress func(done, total int64)) error {
	return nil
}

func TestCheckAppUpdate(t *testing.T) {
	cloud := &mockUpdateCloud{
		releases: []*verifier.Release{
			{
				Tag:   "patcher-v2.1.2",
				Title: "汉化器 v2.1.2",
				Body:  "修复自更新与离线包导入",
				URL:   "https://github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/releases/tag/patcher-v2.1.2",
				Assets: map[string]verifier.ReleaseAsset{
					"FFXIVChnTextPatch-SM-v2.exe": {
						Name:        "FFXIVChnTextPatch-SM-v2.exe",
						DownloadURL: "https://example.com/FFXIVChnTextPatch-SM-v2.exe",
					},
				},
			},
			{
				Tag: "2026.10.16.0000.0000",
			},
			{
				Tag: "patcher-v2.1.0",
			},
		},
	}

	// 1. 本地为旧版本，应当检测出有新版本
	info, err := CheckAppUpdate(context.Background(), cloud, "test/repo", "2.1.0")
	if err != nil {
		t.Fatalf("CheckAppUpdate 失败: %v", err)
	}
	if !info.HasUpdate {
		t.Errorf("期望检测到更新，实际未检测到")
	}
	if info.LatestVersion != "2.1.2" {
		t.Errorf("期望最新版本 2.1.2，实际得到 %s", info.LatestVersion)
	}
	if info.DownloadURL == "" {
		t.Errorf("期望 DownloadURL 非空")
	}

	// 2. 本地已是最新版本
	infoLatest, err := CheckAppUpdate(context.Background(), cloud, "test/repo", "v2.1.2")
	if err != nil {
		t.Fatalf("CheckAppUpdate 失败: %v", err)
	}
	if infoLatest.HasUpdate {
		t.Errorf("期望已是最新版本，实际提示有更新")
	}

	// 3. 本地为 dev 开发版本，不提示有更新，但能解析出最新版本号
	infoDev, err := CheckAppUpdate(context.Background(), cloud, "test/repo", "dev")
	if err != nil {
		t.Fatalf("CheckAppUpdate 失败: %v", err)
	}
	if infoDev.HasUpdate {
		t.Errorf("dev 版本不应强制提示有更新")
	}
	if infoDev.LatestVersion != "2.1.2" {
		t.Errorf("期望识别最新版本 2.1.2，实际得到 %s", infoDev.LatestVersion)
	}

	// 4. 本地为带有 git describe 提交距离或哈希后缀的构建版本，不应误报更新
	infoAhead, err := CheckAppUpdate(context.Background(), cloud, "test/repo", "2.1.2-8-g28272842")
	if err != nil {
		t.Fatalf("CheckAppUpdate 失败: %v", err)
	}
	if infoAhead.HasUpdate {
		t.Errorf("当前版本已基于 2.1.2 开发，不应误报更新")
	}
	if infoAhead.CurrentVersion != "2.1.2" {
		t.Errorf("期望 CurrentVersion 被清洗为 2.1.2，实际得到 %s", infoAhead.CurrentVersion)
	}
}

func TestCleanAppVersion(t *testing.T) {
	cases := []struct {
		input string
		want  string
	}{
		{"patcher-v2.1.1", "2.1.1"},
		{"v2.1.1", "2.1.1"},
		{"2.1.1-8-g28272842", "2.1.1"},
		{"patcher-v2.1.1-preview", "2.1.1"},
		{"refs/tags/patcher-v2.1.2", "2.1.2"},
		{"dev", "dev"},
	}
	for _, c := range cases {
		if got := CleanAppVersion(c.input); got != c.want {
			t.Errorf("CleanAppVersion(%q) = %q, want %q", c.input, got, c.want)
		}
	}
}

func TestIsPatcherReleaseTag(t *testing.T) {
	cases := []struct {
		tag  string
		want bool
	}{
		{"patcher-v2.1.1", true},
		{"patcher-v2.0.0", true},
		{"v2.1.0", true},
		{"2026.10.16.0000.0000", false},
		{"7.25hf2v1", false},
		{"backup-ui-exploration", false},
	}

	for _, c := range cases {
		if got := IsPatcherReleaseTag(c.tag); got != c.want {
			t.Errorf("IsPatcherReleaseTag(%q) = %v, want %v", c.tag, got, c.want)
		}
	}
}
