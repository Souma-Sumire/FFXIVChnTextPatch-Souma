package updater

import (
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	goruntime "runtime"
	"strings"
	"testing"

	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/verifier"
)

type mockUpdateCloud struct {
	releases   []*verifier.Release
	byTag      map[string]*verifier.Release
	err        error
	mirrorURL  string
	byTagCalls int
}

func (m *mockUpdateCloud) ReleaseByTag(ctx context.Context, repo, tag string) (*verifier.Release, error) {
	m.byTagCalls++
	if r, ok := m.byTag[tag]; ok {
		return r, nil
	}
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

func (m *mockUpdateCloud) MirrorDownloadURL(raw string) string {
	if m.mirrorURL == "" {
		return ""
	}
	return m.mirrorURL
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
		{"Patcher-V2.1.3", true},
		// 旧版 Java 汉化器的 tag 形如 v2.20.0，不能当成主程序发布。
		{"v2.20.0", false},
		{"v2.1.0", false},
		{"v2.0.2", false},
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

// 用本地 HTTP 服务模拟 GitHub API，端到端验证「请求 → 解析 → 版本比较 → 选中下载包」。
func TestCheckAppUpdateAgainstGitHubAPI(t *testing.T) {
	var gotQuery string
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/repos/test/repo/releases" {
			http.NotFound(w, r)
			return
		}
		gotQuery = r.URL.RawQuery
		releases := []map[string]any{
			{
				"tag_name": "patcher-v2.1.4",
				"name":     "汉化器 v2.1.4",
				"body":     "修复若干问题",
				"html_url": "https://example.com/releases/tag/patcher-v2.1.4",
				"assets": []map[string]string{
					{"name": "updater-helper.exe", "browser_download_url": "https://example.com/helper.exe"},
					{"name": "FFXIVChnTextPatch-SM-v2.exe", "browser_download_url": "https://example.com/patcher.exe"},
					{"name": "updater-helper.zip", "browser_download_url": "https://example.com/helper.zip"},
					{"name": "FFXIVChnTextPatch-SM-v2-macOS.zip", "browser_download_url": "https://example.com/patcher-macos.zip"},
				},
			},
			{"tag_name": "2026.11.04.0000.0000", "assets": []map[string]string{}},
			{"tag_name": "2026.10.16.0000.0000", "assets": []map[string]string{}},
		}
		w.Header().Set("Content-Type", "application/json")
		_ = json.NewEncoder(w).Encode(releases)
	}))
	defer srv.Close()

	client := verifier.NewGitHubClient()
	client.APIBase = srv.URL
	client.HTTP = srv.Client()

	info, err := CheckAppUpdate(context.Background(), client, "test/repo", "2.1.3")
	if err != nil {
		t.Fatalf("CheckAppUpdate 失败: %v", err)
	}
	if !info.HasUpdate || info.LatestVersion != "2.1.4" {
		t.Fatalf("期望检测到 2.1.4 更新，实际 hasUpdate=%v latest=%q", info.HasUpdate, info.LatestVersion)
	}
	if !strings.Contains(gotQuery, "per_page=100") {
		t.Errorf("应一次拉取足够多的 Release（per_page=100），实际查询为 %q", gotQuery)
	}
	wantDownloadURL := "https://example.com/patcher.exe"
	if goruntime.GOOS == "darwin" {
		wantDownloadURL = "https://example.com/patcher-macos.zip"
	}
	if info.DownloadURL != wantDownloadURL {
		t.Errorf("应优先选中主程序附件，实际为 %q", info.DownloadURL)
	}
}

// 附件顺序不应影响选择结果。
func TestPickReleaseAssetIsDeterministic(t *testing.T) {
	assets := map[string]verifier.ReleaseAsset{
		"zzz-tool.exe":      {DownloadURL: "https://example.com/tool.exe"},
		"patcher-setup.exe": {DownloadURL: "https://example.com/setup.exe"},
	}
	for i := 0; i < 20; i++ {
		if got := pickReleaseAsset("windows", assets); got != "https://example.com/setup.exe" {
			t.Fatalf("第 %d 次选择结果不稳定: %q", i, got)
		}
	}

	withProduct := map[string]verifier.ReleaseAsset{
		"aaa.exe":                           {DownloadURL: "https://example.com/aaa.exe"},
		"FFXIVChnTextPatch-SM-v2.exe":       {DownloadURL: "https://example.com/patcher.exe"},
		"aaa.zip":                           {DownloadURL: "https://example.com/aaa.zip"},
		"FFXIVChnTextPatch-SM-v2-macOS.zip": {DownloadURL: "https://example.com/patcher-macos.zip"},
	}
	if got := pickReleaseAsset("windows", withProduct); got != "https://example.com/patcher.exe" {
		t.Errorf("应优先选中 Windows 产品主程序，实际为 %q", got)
	}
	if got := pickReleaseAsset("darwin", withProduct); got != "https://example.com/patcher-macos.zip" {
		t.Errorf("应优先选中 macOS 产品主程序，实际为 %q", got)
	}
	if got := pickReleaseAsset("linux", withProduct); got != "" {
		t.Errorf("非 Windows/macOS 平台不应返回下载地址，实际为 %q", got)
	}
}

// 降级到网页端列表时只有 tag、没有附件信息。patcher-v* 由 CI 按固定命名发布，
// 直接按约定拼地址即可，不应再为附件列表多请求一次。
func TestCheckAppUpdateBuildsAssetURLWithoutExtraRequest(t *testing.T) {
	const tag = "patcher-v2.2.0"
	want := "https://github.com/o/r/releases/download/" + tag + "/" + releaseAssetName(goruntime.GOOS)
	cloud := &mockUpdateCloud{
		// 网页端解析只会给出 tag 与 Release 链接。
		releases: []*verifier.Release{
			{Tag: tag, URL: "https://github.com/o/r/releases/tag/" + tag},
		},
		mirrorURL: "https://gh-proxy.com/whatever",
	}

	info, err := CheckAppUpdate(context.Background(), cloud, "o/r", "2.1.3")
	if err != nil {
		t.Fatalf("CheckAppUpdate 失败: %v", err)
	}
	if cloud.byTagCalls != 0 {
		t.Errorf("patcher tag 不需要回查附件列表，实际调用了 %d 次", cloud.byTagCalls)
	}
	if info.DownloadURL != want {
		t.Errorf("应按固定命名拼出下载地址，实际 %q", info.DownloadURL)
	}
	if info.MirrorURL == "" {
		t.Error("应给出镜像下载地址")
	}
}

// 旧版 Java 汉化器的 tag（v2.20.0 等）不能参与主程序版本比较，
// 否则会指向旧版 Release 页并把当前版本判成「有更新」。
func TestCheckAppUpdateIgnoresLegacyJavaRelease(t *testing.T) {
	cloud := &mockUpdateCloud{
		releases: []*verifier.Release{
			{Tag: "v2.20.0", URL: "https://github.com/o/r/releases/tag/v2.20.0"},
			{Tag: "2026.10.16.0000.0000"},
		},
		mirrorURL: "https://gh-proxy.com/x",
	}

	info, err := CheckAppUpdate(context.Background(), cloud, "o/r", "2.1.3")
	if err != nil {
		t.Fatalf("CheckAppUpdate 失败: %v", err)
	}
	if info.HasUpdate {
		t.Errorf("旧版 Java 汉化器的 tag 不应触发更新，latest=%q url=%q", info.LatestVersion, info.ReleaseURL)
	}
	if info.LatestVersion != "2.1.3" {
		t.Errorf("无主程序发布时应保持当前版本，实际 %q", info.LatestVersion)
	}
	if strings.Contains(info.ReleaseURL, "v2.20.0") {
		t.Errorf("不应指向旧版 Release 页，实际 %q", info.ReleaseURL)
	}
	if cloud.byTagCalls != 0 {
		t.Errorf("不应为旧版 tag 回查附件，实际 %d 次", cloud.byTagCalls)
	}
}

// 混合列表里应以 patcher-v* 的最高版本为准，不能被 2.20.0 这种更大的旧版号盖过。
func TestCheckAppUpdatePicksHighestPatcherTag(t *testing.T) {
	cloud := &mockUpdateCloud{
		releases: []*verifier.Release{
			{Tag: "v2.20.0", URL: "https://github.com/o/r/releases/tag/v2.20.0"},
			{Tag: "patcher-v2.1.3", URL: "https://github.com/o/r/releases/tag/patcher-v2.1.3"},
			{Tag: "patcher-v2.1.2", URL: "https://github.com/o/r/releases/tag/patcher-v2.1.2"},
		},
		mirrorURL: "https://gh-proxy.com/x",
	}

	info, err := CheckAppUpdate(context.Background(), cloud, "o/r", "2.1.2")
	if err != nil {
		t.Fatalf("CheckAppUpdate 失败: %v", err)
	}
	if info.LatestVersion != "2.1.3" {
		t.Fatalf("应取 patcher tag 的最高版本，实际 %q", info.LatestVersion)
	}
	if !info.HasUpdate {
		t.Error("2.1.2 -> 2.1.3 应判定为有更新")
	}
	if !strings.HasSuffix(info.ReleaseURL, "/releases/tag/patcher-v2.1.3") {
		t.Errorf("Release 链接应指向对应 patcher tag，实际 %q", info.ReleaseURL)
	}
	wantSuffix := "/releases/download/patcher-v2.1.3/" + releaseAssetName(goruntime.GOOS)
	if !strings.HasSuffix(info.DownloadURL, wantSuffix) {
		t.Errorf("下载地址应基于 patcher tag 拼装，实际 %q", info.DownloadURL)
	}
}

func TestIsPreviewTag(t *testing.T) {
	cases := []struct {
		tag  string
		want bool
	}{
		{"patcher-v2.2.0-beta.1", true},
		{"patcher-v2.2.0-rc1", true},
		{"patcher-v2.2.0-preview.2", true},
		{"patcher-v2.2.0-alpha.3", true},
		{"PATCHER-V2.2.0-BETA.1", true},
		{"refs/tags/patcher-v2.2.0-beta.1", true},
		{"patcher-v2.2.0", false},
		{"patcher-v2.1.4", false},
		{"2.1.2-8-g28272842", false},
		{"dev", false},
		{"v2.20.0", false},
	}
	for _, c := range cases {
		if got := isPreviewTag(c.tag); got != c.want {
			t.Errorf("isPreviewTag(%q) = %v, want %v", c.tag, got, c.want)
		}
	}
}

func TestDisplayAppVersion(t *testing.T) {
	cases := []struct {
		input string
		want  string
	}{
		{"patcher-v2.2.0-beta.1", "2.2.0-beta.1"},
		{"2.2.0-rc1", "2.2.0-rc1"},
		{"refs/tags/patcher-v2.2.0-preview.2", "2.2.0-preview.2"},
		{"patcher-v2.2.0", "2.2.0"},
		{"2.1.2-8-g28272842", "2.1.2"},
		{"dev", "dev"},
	}
	for _, c := range cases {
		if got := DisplayAppVersion(c.input); got != c.want {
			t.Errorf("DisplayAppVersion(%q) = %q, want %q", c.input, got, c.want)
		}
	}
}

// 预览版的 tag 后缀与 GitHub 的 prerelease 标记都要挡住，否则普通用户会被推测试包。
func TestCheckAppUpdateIgnoresPreviewRelease(t *testing.T) {
	cloud := &mockUpdateCloud{
		releases: []*verifier.Release{
			{Tag: "patcher-v2.2.0-beta.1", Prerelease: true, URL: "https://example.com/beta"},
			{Tag: "patcher-v2.2.0-rc.1", URL: "https://example.com/rc"},
			{Tag: "patcher-v2.1.4", URL: "https://example.com/2.1.4"},
		},
	}

	info, err := CheckAppUpdate(context.Background(), cloud, "o/r", "2.1.3")
	if err != nil {
		t.Fatalf("CheckAppUpdate 失败: %v", err)
	}
	if info.LatestVersion != "2.1.4" {
		t.Errorf("预览版不应参与版本比较，实际 latest=%q", info.LatestVersion)
	}
	if !info.HasUpdate {
		t.Error("应提示 2.1.3 -> 2.1.4 的正式版更新")
	}
	if !strings.HasSuffix(info.ReleaseURL, "/2.1.4") {
		t.Errorf("Release 链接应指向正式版，实际 %q", info.ReleaseURL)
	}
}

// 仓库里只有预览版时，普通用户应保持「已是最新版本」。
func TestCheckAppUpdateOnlyPreviewReleases(t *testing.T) {
	cloud := &mockUpdateCloud{
		releases: []*verifier.Release{
			{Tag: "patcher-v2.2.0-beta.1", Prerelease: true},
		},
	}
	info, err := CheckAppUpdate(context.Background(), cloud, "o/r", "2.1.4")
	if err != nil {
		t.Fatalf("CheckAppUpdate 失败: %v", err)
	}
	if info.HasUpdate || info.LatestVersion != "2.1.4" {
		t.Errorf("只有预览版时不应提示更新，hasUpdate=%v latest=%q", info.HasUpdate, info.LatestVersion)
	}
}

// 手动在 GitHub 标为 Pre-release、tag 却没有预览后缀的发布同样不能推给用户。
func TestCheckAppUpdateIgnoresGitHubPrerelease(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/repos/test/repo/releases" {
			http.NotFound(w, r)
			return
		}
		releases := []map[string]any{
			{"tag_name": "patcher-v2.2.0", "prerelease": true, "assets": []map[string]string{}},
			{"tag_name": "patcher-v2.1.4", "prerelease": false, "assets": []map[string]string{}},
		}
		w.Header().Set("Content-Type", "application/json")
		_ = json.NewEncoder(w).Encode(releases)
	}))
	defer srv.Close()

	client := verifier.NewGitHubClient()
	client.APIBase = srv.URL
	client.HTTP = srv.Client()

	info, err := CheckAppUpdate(context.Background(), client, "test/repo", "2.1.3")
	if err != nil {
		t.Fatalf("CheckAppUpdate 失败: %v", err)
	}
	if info.LatestVersion != "2.1.4" || !info.HasUpdate {
		t.Errorf("应取正式版 2.1.4，实际 hasUpdate=%v latest=%q", info.HasUpdate, info.LatestVersion)
	}
}

// 同号正式版发布后，预览版用户仍应收到升级提示；正式版用户则不应误报。
func TestCheckAppUpdatePreviewUserUpgradesToFinal(t *testing.T) {
	cloud := &mockUpdateCloud{
		releases: []*verifier.Release{
			{Tag: "patcher-v2.2.0", URL: "https://example.com/2.2.0"},
		},
	}

	info, err := CheckAppUpdate(context.Background(), cloud, "o/r", "2.2.0-beta.1")
	if err != nil {
		t.Fatalf("CheckAppUpdate 失败: %v", err)
	}
	if !info.HasUpdate {
		t.Error("同号正式版发布后，预览版用户应收到升级提示")
	}
	if info.CurrentVersion != "2.2.0-beta.1" || info.LatestVersion != "2.2.0" {
		t.Errorf("版本展示应区分预览版与正式版，current=%q latest=%q", info.CurrentVersion, info.LatestVersion)
	}

	infoFinal, err := CheckAppUpdate(context.Background(), cloud, "o/r", "2.2.0")
	if err != nil {
		t.Fatalf("CheckAppUpdate 失败: %v", err)
	}
	if infoFinal.HasUpdate {
		t.Error("正式版用户不应收到同版本的更新提示")
	}
}

func TestCheckAppUpdateFetchesHashFromReleaseByTag(t *testing.T) {
	assetName := "FFXIVChnTextPatch-SM-v2.exe"
	if goruntime.GOOS == "darwin" {
		assetName = "FFXIVChnTextPatch-SM-v2-macOS.zip"
	}
	cloud := &mockUpdateCloud{
		releases: []*verifier.Release{
			{
				Tag: "patcher-v2.2.0",
				Assets: map[string]verifier.ReleaseAsset{
					assetName: {Name: assetName, DownloadURL: "https://example.com/app"},
				},
			},
		},
		byTag: map[string]*verifier.Release{
			"patcher-v2.2.0": {
				Tag: "patcher-v2.2.0",
				Assets: map[string]verifier.ReleaseAsset{
					assetName: {Name: assetName, DownloadURL: "https://example.com/app"},
				},
				FileHashes: map[string]string{
					assetName: "deadbeef12345678deadbeef12345678deadbeef12345678deadbeef12345678",
				},
			},
		},
	}

	info, err := CheckAppUpdate(context.Background(), cloud, "o/r", "2.1.0")
	if err != nil {
		t.Fatalf("CheckAppUpdate 失败: %v", err)
	}
	if !info.HasUpdate {
		t.Fatal("应检测到更新")
	}
	if info.DownloadSHA256 != "deadbeef12345678deadbeef12345678deadbeef12345678deadbeef12345678" {
		t.Fatalf("应从 ReleaseByTag 补充拉取到哈希，实际: %q", info.DownloadSHA256)
	}
}
