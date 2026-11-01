package verifier

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"strconv"
	"strings"
	"time"
)

// ErrNotFound 表示 Release 不存在（GitHub 404）。
var ErrNotFound = errors.New("远端资源不存在")

// FileHash 记录文件哈希与大小。
type FileHash struct {
	SHA256 string `json:"sha256"`
	Size   int64  `json:"size"`
}

// ReleaseAsset 是 Release 中的一个附件。
type ReleaseAsset struct {
	Name        string
	Digest      string
	DownloadURL string
}

// Release 是精简后的 GitHub Release。
type Release struct {
	Tag    string
	Assets map[string]ReleaseAsset
}

// AssetSHA256 解析 "sha256:xxxx" 格式的 digest。
func (r *Release) AssetSHA256(name string) (string, bool) {
	asset, ok := r.Assets[name]
	if !ok {
		return "", false
	}
	const prefix = "sha256:"
	if !strings.HasPrefix(asset.Digest, prefix) {
		return "", false
	}
	return strings.ToLower(strings.TrimPrefix(asset.Digest, prefix)), true
}

// Cloud 抽象云端 Release 与下载能力，便于测试注入。
type Cloud interface {
	ReleaseByTag(ctx context.Context, repo, tag string) (*Release, error)
	RecentReleases(ctx context.Context, repo string, limit int) ([]*Release, error)
	Download(ctx context.Context, url, dst string, progress func(done, total int64)) error
}

// GitHubClient 是 Cloud 的 GitHub 实现。
type GitHubClient struct {
	HTTP    *http.Client
	APIBase string
	Token   string
}

// NewGitHubClient 创建默认 GitHub API 客户端。
func NewGitHubClient() *GitHubClient {
	return &GitHubClient{
		HTTP:    &http.Client{Timeout: 5 * time.Minute},
		APIBase: "https://api.github.com",
	}
}

type githubAsset struct {
	Name               string `json:"name"`
	Digest             string `json:"digest"`
	BrowserDownloadURL string `json:"browser_download_url"`
}

type githubRelease struct {
	TagName string        `json:"tag_name"`
	Assets  []githubAsset `json:"assets"`
}

func (c *GitHubClient) getJSON(ctx context.Context, path string, out any) error {
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, c.APIBase+path, nil)
	if err != nil {
		return err
	}
	req.Header.Set("Accept", "application/vnd.github+json")
	if c.Token != "" {
		req.Header.Set("Authorization", "Bearer "+c.Token)
	}
	resp, err := c.HTTP.Do(req)
	if err != nil {
		return fmt.Errorf("请求 GitHub API 失败: %w", err)
	}
	defer resp.Body.Close()
	if resp.StatusCode == http.StatusNotFound {
		return ErrNotFound
	}
	if resp.StatusCode != http.StatusOK {
		body, _ := io.ReadAll(io.LimitReader(resp.Body, 512))
		return fmt.Errorf("GitHub API 返回 %d: %s", resp.StatusCode, string(body))
	}
	return json.NewDecoder(resp.Body).Decode(out)
}

func toRelease(in githubRelease) *Release {
	out := &Release{Tag: in.TagName, Assets: make(map[string]ReleaseAsset, len(in.Assets))}
	for _, a := range in.Assets {
		out.Assets[a.Name] = ReleaseAsset{Name: a.Name, Digest: a.Digest, DownloadURL: a.BrowserDownloadURL}
	}
	return out
}

// ReleaseByTag 获取指定 tag 的 Release。
func (c *GitHubClient) ReleaseByTag(ctx context.Context, repo, tag string) (*Release, error) {
	var raw githubRelease
	path := fmt.Sprintf("/repos/%s/releases/tags/%s", repo, url.PathEscape(tag))
	if err := c.getJSON(ctx, path, &raw); err != nil {
		return nil, err
	}
	return toRelease(raw), nil
}

// RecentReleases 获取最近的若干个 Release。
func (c *GitHubClient) RecentReleases(ctx context.Context, repo string, limit int) ([]*Release, error) {
	var raw []githubRelease
	path := fmt.Sprintf("/repos/%s/releases?per_page=%d", repo, limit)
	if err := c.getJSON(ctx, path, &raw); err != nil {
		return nil, err
	}
	out := make([]*Release, 0, len(raw))
	for _, r := range raw {
		out = append(out, toRelease(r))
	}
	return out, nil
}

// Download 下载文件到目标路径，并回调进度。
func (c *GitHubClient) Download(ctx context.Context, rawURL, dst string, progress func(done, total int64)) error {
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, rawURL, nil)
	if err != nil {
		return err
	}
	resp, err := c.HTTP.Do(req)
	if err != nil {
		return fmt.Errorf("下载失败: %w", err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return fmt.Errorf("下载返回状态码 %d: %s", resp.StatusCode, rawURL)
	}
	if err := os.MkdirAll(dirOf(dst), 0o755); err != nil {
		return err
	}
	tmp := dst + ".part"
	out, err := os.Create(tmp)
	if err != nil {
		return err
	}
	total := resp.ContentLength
	done := int64(0)
	buf := make([]byte, 256*1024)
	for {
		n, readErr := resp.Body.Read(buf)
		if n > 0 {
			if _, err := out.Write(buf[:n]); err != nil {
				_ = out.Close()
				return err
			}
			done += int64(n)
			if progress != nil {
				progress(done, total)
			}
		}
		if readErr == io.EOF {
			break
		}
		if readErr != nil {
			_ = out.Close()
			return readErr
		}
	}
	if err := out.Close(); err != nil {
		return err
	}
	return os.Rename(tmp, dst)
}

func dirOf(p string) string {
	i := strings.LastIndexAny(p, `/\`)
	if i < 0 {
		return "."
	}
	return p[:i]
}

// HashFile 计算文件 SHA256 与大小。
func HashFile(path string) (FileHash, error) {
	f, err := os.Open(path)
	if err != nil {
		return FileHash{}, err
	}
	defer f.Close()
	h := sha256.New()
	n, err := io.Copy(h, f)
	if err != nil {
		return FileHash{}, err
	}
	return FileHash{SHA256: hex.EncodeToString(h.Sum(nil)), Size: n}, nil
}

// CompareVersion 按点分数字比较版本号（如 2026.10.16.0000.0000）。
func CompareVersion(a, b string) int {
	as := strings.Split(strings.TrimSpace(a), ".")
	bs := strings.Split(strings.TrimSpace(b), ".")
	n := len(as)
	if len(bs) > n {
		n = len(bs)
	}
	for i := 0; i < n; i++ {
		var av, bv int64
		if i < len(as) {
			av, _ = strconv.ParseInt(strings.TrimSpace(as[i]), 10, 64)
		}
		if i < len(bs) {
			bv, _ = strconv.ParseInt(strings.TrimSpace(bs[i]), 10, 64)
		}
		if av < bv {
			return -1
		}
		if av > bv {
			return 1
		}
	}
	return 0
}
