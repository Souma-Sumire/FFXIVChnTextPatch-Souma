package updater

import (
	"context"
	"fmt"
	"net/url"
	goruntime "runtime"
	"sort"
	"strings"

	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/verifier"
)

// AppUpdateInfo 描述汉化器主程序的最新版本与更新信息。
type AppUpdateInfo struct {
	HasUpdate      bool   `json:"hasUpdate"`
	CurrentVersion string `json:"currentVersion"`
	LatestVersion  string `json:"latestVersion"`
	ReleaseNotes   string `json:"releaseNotes"`
	ReleaseURL     string `json:"releaseUrl"`
	DownloadURL    string `json:"downloadUrl"`
	// DownloadSHA256 是发布方给出的附件哈希，自更新下载后据此校验完整性。
	DownloadSHA256 string `json:"downloadSha256,omitempty"`
	// MirrorURL 是同一附件经加速镜像包装后的地址，供无法直连 GitHub 的用户使用。
	MirrorURL string `json:"mirrorUrl,omitempty"`
	// SelfUpdateSupported 表示当前平台能否下载后就地替换并重启自身。
	SelfUpdateSupported bool `json:"selfUpdateSupported"`
}

// CleanAppVersion 清理版本字符串前缀与后缀元数据（如 patcher-v2.1.1 -> 2.1.1，2.1.1-8-g28272842 -> 2.1.1）。
func CleanAppVersion(v string) string {
	v = strings.TrimSpace(v)
	v = strings.TrimPrefix(v, "refs/tags/")
	v = strings.TrimPrefix(v, "patcher-")
	v = strings.TrimPrefix(v, "v")
	if idx := strings.IndexAny(v, "-+_"); idx != -1 {
		v = v[:idx]
	}
	return strings.TrimSpace(v)
}

// DisplayAppVersion 返回界面展示的版本号。预览版保留 beta.1 这类后缀，
// 其余构建后缀（git describe 的提交距离）仍按基线版本显示。
func DisplayAppVersion(v string) string {
	clean := CleanAppVersion(v)
	if !isPreviewTag(v) {
		return clean
	}
	raw := strings.TrimSpace(v)
	raw = strings.TrimPrefix(raw, "refs/tags/")
	raw = strings.TrimPrefix(raw, "patcher-")
	return strings.TrimPrefix(raw, "v")
}

// isPreviewTag 判断 tag 是否为预览版。后缀关键字须与 build_patcher.yml 的判定一致，
// 否则 CI 建了 Pre-release，客户端却仍会把它推给所有用户。
func isPreviewTag(tag string) bool {
	t := strings.ToLower(strings.TrimSpace(tag))
	t = strings.TrimPrefix(t, "refs/tags/")
	t = strings.TrimPrefix(t, "patcher-")
	t = strings.TrimPrefix(t, "v")
	suffix := strings.TrimPrefix(t, CleanAppVersion(t))
	suffix = strings.TrimPrefix(suffix, "-")
	for _, keyword := range []string{"alpha", "beta", "rc", "preview"} {
		if strings.HasPrefix(suffix, keyword) {
			return true
		}
	}
	return false
}

// IsPatcherReleaseTag 判断该 tag 是否属于汉化器主程序 Release。
// 只有 build_patcher.yml 的 patcher-v* 才是主程序发布。仓库里同时存在
// v2.20.0、v2.18.x 这类旧版 Java 汉化器的 tag，只要「形如 x.y.z」就采信，
// 2.20.0 会被当成比当前 2.1.x 更新的版本，指向旧版的 Release 页面。
func IsPatcherReleaseTag(tag string) bool {
	return strings.HasPrefix(strings.ToLower(strings.TrimSpace(tag)), "patcher-")
}

// pickReleaseAsset 在 Release 附件中挑选主程序包。
// 附件存放在 map 里，直接遍历会因 Go 的随机顺序在不同版本间选出不同文件，
// 因此优先匹配产品名，其次按文件名排序取第一个，保证结果稳定。
func pickReleaseAsset(goos string, assets map[string]verifier.ReleaseAsset) string {
	name := pickReleaseAssetName(goos, assets)
	if name == "" {
		return ""
	}
	return assets[name].DownloadURL
}

// pickReleaseAssetName 返回被选中的主程序附件名，用于同时取出发布方记录的哈希。
func pickReleaseAssetName(goos string, assets map[string]verifier.ReleaseAsset) string {
	names := make([]string, 0, len(assets))
	for name := range assets {
		names = append(names, name)
	}
	sort.Strings(names)

	var fallback string
	for _, name := range names {
		lower := strings.ToLower(name)
		asset := assets[name]
		if asset.DownloadURL == "" {
			continue
		}
		switch goos {
		case "windows":
			if !strings.HasSuffix(lower, ".exe") {
				continue
			}
		case "darwin":
			if !strings.HasSuffix(lower, ".zip") {
				continue
			}
		default:
			return ""
		}
		if strings.Contains(lower, "ffxivchntextpatch") {
			return name
		}
		if fallback == "" {
			fallback = name
		}
	}
	return fallback
}

// releaseAssetName 返回 CI 为「汉化器主程序」固定发布的附件名，
// 与 .github/workflows/build_patcher.yml 的 files 列表保持一致。
func releaseAssetName(goos string) string {
	switch goos {
	case "windows":
		return "FFXIVChnTextPatch-SM-v2.exe"
	case "darwin":
		return "FFXIVChnTextPatch-SM-v2-macOS.zip"
	}
	return ""
}

// assetURLFromTag 按发布约定直接拼出附件下载地址。
// patcher-v* 的 Release 由 CI 用固定文件名发布，拼地址比再抓一次网页附件列表更省事。
func assetURLFromTag(repo, tag, goos string) string {
	name := releaseAssetName(goos)
	if name == "" || tag == "" {
		return ""
	}
	return fmt.Sprintf("https://github.com/%s/releases/download/%s/%s", repo, url.PathEscape(tag), name)
}

// CheckAppUpdate 查询云端获取汉化器主程序的最新版本并比对当前版本。
func CheckAppUpdate(ctx context.Context, cloud verifier.Cloud, repo, currentVersion string) (*AppUpdateInfo, error) {
	if repo == "" {
		repo = "Souma-Sumire/FFXIVChnTextPatch-Souma"
	}
	if cloud == nil {
		cloud = verifier.NewGitHubClient()
	}

	cleanCurrent := CleanAppVersion(currentVersion)
	displayCurrent := DisplayAppVersion(currentVersion)
	info := &AppUpdateInfo{
		CurrentVersion:      displayCurrent,
		LatestVersion:       displayCurrent,
		ReleaseURL:          fmt.Sprintf("https://github.com/%s/releases/latest", repo),
		SelfUpdateSupported: SelfUpdateSupported(),
	}

	releases, err := cloud.RecentReleases(ctx, repo, 100)
	if err != nil {
		return info, fmt.Errorf("检查更新失败: %w", err)
	}

	var latestPatcherRelease *verifier.Release
	var highestVersion string

	for _, rel := range releases {
		if rel == nil || !IsPatcherReleaseTag(rel.Tag) {
			continue
		}
		// 网页端兜底列表不带 prerelease 字段，只能靠 tag 后缀识别预览版。
		if rel.Prerelease || isPreviewTag(rel.Tag) {
			continue
		}
		ver := CleanAppVersion(rel.Tag)
		if ver == "" {
			continue
		}
		if highestVersion == "" || verifier.CompareVersion(ver, highestVersion) > 0 {
			highestVersion = ver
			latestPatcherRelease = rel
		}
	}

	if latestPatcherRelease == nil || highestVersion == "" {
		return info, nil
	}

	info.LatestVersion = highestVersion
	info.ReleaseNotes = latestPatcherRelease.Body
	if latestPatcherRelease.URL != "" {
		info.ReleaseURL = latestPatcherRelease.URL
	} else {
		info.ReleaseURL = fmt.Sprintf("https://github.com/%s/releases/tag/%s", repo, latestPatcherRelease.Tag)
	}

	if assetName := pickReleaseAssetName(goruntime.GOOS, latestPatcherRelease.Assets); assetName != "" {
		info.DownloadURL = latestPatcherRelease.Assets[assetName].DownloadURL
		if digest, ok := latestPatcherRelease.AssetSHA256(assetName); ok {
			info.DownloadSHA256 = digest
		}
	}
	if info.DownloadURL == "" {
		// 降级解析网页端列表时拿不到附件信息。patcher-v* 由 CI 按固定命名发布，
		// 直接拼地址即可，不必为附件列表多请求一次。
		info.DownloadURL = assetURLFromTag(repo, latestPatcherRelease.Tag, goruntime.GOOS)
	}

	if info.DownloadURL == "" {
		info.DownloadURL = info.ReleaseURL
	} else {
		info.MirrorURL = cloud.MirrorDownloadURL(info.DownloadURL)
	}

	if cleanCurrent != "dev" && cleanCurrent != "" {
		cmp := verifier.CompareVersion(highestVersion, cleanCurrent)
		// 预览版与同号正式版比较结果相等，仍需让预览用户升级到正式版。
		if cmp > 0 || (cmp == 0 && isPreviewTag(currentVersion)) {
			info.HasUpdate = true
		}
	}

	return info, nil
}
