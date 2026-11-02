package updater

import (
	"context"
	"fmt"
	goruntime "runtime"
	"strings"

	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/verifier"
)

// AppUpdateInfo 描述汉化器主程序的最新版本与更新信息。
type AppUpdateInfo struct {
	HasUpdate      bool   `json:"hasUpdate"`
	CurrentVersion string `json:"currentVersion"`
	LatestVersion  string `json:"latestVersion"`
	Title          string `json:"title"`
	ReleaseNotes   string `json:"releaseNotes"`
	ReleaseURL     string `json:"releaseUrl"`
	DownloadURL    string `json:"downloadUrl"`
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

// IsPatcherReleaseTag 判断该 tag 是否属于汉化器主程序 Release。
func IsPatcherReleaseTag(tag string) bool {
	lower := strings.ToLower(strings.TrimSpace(tag))
	if strings.HasPrefix(lower, "patcher-") {
		return true
	}
	if strings.HasPrefix(lower, "202") || strings.HasPrefix(lower, "7.") || strings.HasPrefix(lower, "6.") {
		return false
	}
	clean := CleanAppVersion(tag)
	parts := strings.Split(clean, ".")
	return len(parts) >= 2 && len(parts) <= 4
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
	info := &AppUpdateInfo{
		CurrentVersion: cleanCurrent,
		LatestVersion:  cleanCurrent,
		ReleaseURL:     fmt.Sprintf("https://github.com/%s/releases/latest", repo),
	}

	releases, err := cloud.RecentReleases(ctx, repo, 15)
	if err != nil {
		return info, fmt.Errorf("检查更新失败: %w", err)
	}

	var latestPatcherRelease *verifier.Release
	var highestVersion string

	for _, rel := range releases {
		if rel == nil || !IsPatcherReleaseTag(rel.Tag) {
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
	if latestPatcherRelease.Title != "" {
		info.Title = latestPatcherRelease.Title
	} else {
		info.Title = fmt.Sprintf("汉化器 v%s", highestVersion)
	}
	info.ReleaseNotes = latestPatcherRelease.Body
	if latestPatcherRelease.URL != "" {
		info.ReleaseURL = latestPatcherRelease.URL
	} else {
		info.ReleaseURL = fmt.Sprintf("https://github.com/%s/releases/tag/%s", repo, latestPatcherRelease.Tag)
	}

	if goruntime.GOOS == "windows" {
		for name, asset := range latestPatcherRelease.Assets {
			if strings.HasSuffix(strings.ToLower(name), ".exe") && asset.DownloadURL != "" {
				info.DownloadURL = asset.DownloadURL
				break
			}
		}
	} else if goruntime.GOOS == "darwin" {
		for name, asset := range latestPatcherRelease.Assets {
			if strings.HasSuffix(strings.ToLower(name), ".zip") && asset.DownloadURL != "" {
				info.DownloadURL = asset.DownloadURL
				break
			}
		}
	}

	if info.DownloadURL == "" {
		info.DownloadURL = info.ReleaseURL
	}

	if cleanCurrent != "dev" && cleanCurrent != "" {
		if verifier.CompareVersion(highestVersion, cleanCurrent) > 0 {
			info.HasUpdate = true
		}
	}

	return info, nil
}
