package verifier

import (
	"archive/zip"
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"sync"
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
	Tag        string
	Assets     map[string]ReleaseAsset
	FileHashes map[string]string
}

func (r *Release) linkZipAliases() {
	if r.Assets == nil {
		return
	}
	for name, asset := range r.Assets {
		if strings.HasSuffix(name, ".zip") {
			rawName := strings.TrimSuffix(name, ".zip")
			if _, exists := r.Assets[rawName]; !exists {
				r.Assets[rawName] = asset
			}
		}
	}
}

// AssetSHA256 解析原始未压缩文件的 SHA256 哈希。
func (r *Release) AssetSHA256(name string) (string, bool) {
	if r == nil {
		return "", false
	}
	if r.FileHashes != nil {
		if hash, ok := r.FileHashes[name]; ok && hash != "" {
			return strings.ToLower(hash), true
		}
	}
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

// DefaultAssetMirrors 返回默认的 Release 附件加速镜像节点（已实测连通性与哈希完整性可用）。
func DefaultAssetMirrors() []string {
	return []string{
		"https://gh-proxy.com/",
		"https://cdn.gh-proxy.org/",
		"https://hk.gh-proxy.org/",
		"https://gh.monlor.com/",
		"https://ghfast.top/",
		"https://wget.la/",
	}
}

// GitHubClient 是 Cloud 的 GitHub 实现。
type GitHubClient struct {
	HTTP          *http.Client
	APIBase       string
	Token         string
	AssetMirrors  []string
	preferredNode string
	mu            sync.RWMutex
}

// NewGitHubClient 创建默认 GitHub API 客户端。
func NewGitHubClient() *GitHubClient {
	transport := &http.Transport{
		Proxy: http.ProxyFromEnvironment,
		DialContext: (&net.Dialer{
			Timeout:   8 * time.Second,
			KeepAlive: 30 * time.Second,
		}).DialContext,
		ForceAttemptHTTP2:     true,
		MaxIdleConns:          100,
		IdleConnTimeout:       90 * time.Second,
		TLSHandshakeTimeout:   8 * time.Second,
		ResponseHeaderTimeout: 10 * time.Second,
	}
	return &GitHubClient{
		HTTP:         &http.Client{Timeout: 10 * time.Minute, Transport: transport},
		APIBase:      "https://api.github.com",
		AssetMirrors: DefaultAssetMirrors(),
	}
}

// SetPreferredNode 设置优先使用的下载镜像节点。
func (c *GitHubClient) SetPreferredNode(node string) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.preferredNode = node
}

func (c *GitHubClient) preferred() string {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.preferredNode
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
	out.linkZipAliases()
	return out
}

var (
	reAssetDownload = regexp.MustCompile(`href="(/[^"]+/releases/download/[^"]+/([^"]+))"`)
	reAssetDigest   = regexp.MustCompile(`sha256:([0-9a-fA-F]{64})`)
	reReleaseTag    = regexp.MustCompile(`/releases/tag/([^"/?#\s]+)`)
)

// ReleaseByTag 获取指定 tag 的 Release。优先通过免认证公开网页端解析获取，避免 API 频控 403 阻断；失败时回退至 REST API。
func (c *GitHubClient) ReleaseByTag(ctx context.Context, repo, tag string) (*Release, error) {
	rel, err := c.fetchReleaseInternal(ctx, repo, tag)
	if err != nil {
		return nil, err
	}
	c.loadHashesJSON(ctx, rel)
	return rel, nil
}

func (c *GitHubClient) fetchReleaseInternal(ctx context.Context, repo, tag string) (*Release, error) {
	// 若配置了自定义 APIBase（如单元测试 mockServer）或 Token，优先走 REST API
	if c.Token != "" || (c.APIBase != "" && c.APIBase != "https://api.github.com") {
		var raw githubRelease
		path := fmt.Sprintf("/repos/%s/releases/tags/%s", repo, url.PathEscape(tag))
		err := c.getJSON(ctx, path, &raw)
		if err == nil {
			return toRelease(raw), nil
		}
		if errors.Is(err, ErrNotFound) {
			return nil, ErrNotFound
		}
		if c.APIBase != "" && c.APIBase != "https://api.github.com" {
			return nil, err
		}
	}

	rel, err := c.fetchExpandedAssets(ctx, repo, tag)
	if err == nil {
		return rel, nil
	}
	if errors.Is(err, ErrNotFound) {
		return nil, ErrNotFound
	}

	var raw githubRelease
	path := fmt.Sprintf("/repos/%s/releases/tags/%s", repo, url.PathEscape(tag))
	if apiErr := c.getJSON(ctx, path, &raw); apiErr == nil {
		return toRelease(raw), nil
	} else if errors.Is(apiErr, ErrNotFound) {
		return nil, ErrNotFound
	}

	return nil, fmt.Errorf("获取 Release %s 失败: %w", tag, err)
}

func (c *GitHubClient) loadHashesJSON(ctx context.Context, rel *Release) {
	if rel == nil || rel.Assets == nil {
		return
	}
	asset, ok := rel.Assets["hashes.json"]
	if !ok || asset.DownloadURL == "" {
		return
	}
	tmp := filepath.Join(os.TempDir(), fmt.Sprintf("ffxiv_hashes_%d.tmp", time.Now().UnixNano()))
	defer os.Remove(tmp)
	if err := c.Download(ctx, asset.DownloadURL, tmp, nil); err != nil {
		return
	}
	data, err := os.ReadFile(tmp)
	if err != nil {
		return
	}
	var hashes map[string]string
	if err := json.Unmarshal(data, &hashes); err == nil {
		rel.FileHashes = hashes
	}
}

func (c *GitHubClient) fetchExpandedAssets(ctx context.Context, repo, tag string) (*Release, error) {
	pagePath := fmt.Sprintf("https://github.com/%s/releases/expanded_assets/%s", repo, url.PathEscape(tag))
	candidates := []string{pagePath}
	for _, m := range c.AssetMirrors {
		if strings.Contains(m, "ghfast.top") || strings.Contains(m, "gh.monlor.com") {
			candidates = append(candidates, strings.TrimRight(m, "/")+"/"+pagePath)
		}
	}
	pref := c.preferred()
	if pref != "" && len(candidates) > 1 {
		for i, cand := range candidates {
			if strings.HasPrefix(cand, pref) {
				candidates[0], candidates[i] = candidates[i], candidates[0]
				break
			}
		}
	}

	var lastErr error
	for _, u := range candidates {
		rel, err := c.requestAndParseExpandedAssets(ctx, u, tag)
		if err == nil {
			for _, m := range c.AssetMirrors {
				if strings.HasPrefix(u, m) {
					c.SetPreferredNode(m)
					break
				}
			}
			return rel, nil
		}
		if errors.Is(err, ErrNotFound) {
			return nil, ErrNotFound
		}
		lastErr = err
	}
	return nil, lastErr
}

func (c *GitHubClient) requestAndParseExpandedAssets(ctx context.Context, pageURL, tag string) (*Release, error) {
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, pageURL, nil)
	if err != nil {
		return nil, err
	}
	req.Header.Set("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64)")
	resp, err := c.HTTP.Do(req)
	if err != nil {
		return nil, fmt.Errorf("请求 Release 网页失败: %w", err)
	}
	defer resp.Body.Close()
	if resp.StatusCode == http.StatusNotFound {
		return nil, ErrNotFound
	}
	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("Release 网页返回状态码 %d", resp.StatusCode)
	}
	body, err := io.ReadAll(io.LimitReader(resp.Body, 2*1024*1024))
	if err != nil {
		return nil, err
	}
	return parseExpandedAssets(string(body), tag)
}

func parseExpandedAssets(html, tag string) (*Release, error) {
	rel := &Release{
		Tag:    tag,
		Assets: make(map[string]ReleaseAsset),
	}
	items := strings.Split(html, "<li")
	for _, item := range items {
		m := reAssetDownload.FindStringSubmatch(item)
		if len(m) < 3 {
			continue
		}
		href := m[1]
		name := m[2]
		downloadURL := href
		if strings.HasPrefix(downloadURL, "/") {
			downloadURL = "https://github.com" + downloadURL
		}
		digest := ""
		if dMatch := reAssetDigest.FindStringSubmatch(item); len(dMatch) >= 2 {
			digest = "sha256:" + strings.ToLower(dMatch[1])
		}
		rel.Assets[name] = ReleaseAsset{
			Name:        name,
			Digest:      digest,
			DownloadURL: downloadURL,
		}
	}
	if len(rel.Assets) == 0 {
		return nil, fmt.Errorf("Release %s 中未解析到任何附件", tag)
	}
	rel.linkZipAliases()
	return rel, nil
}

// RecentReleases 获取最近的若干个 Release。优先通过 REST API 获取，遭遇限流或网络异常时自动降级从公开 Releases 页面提取。
func (c *GitHubClient) RecentReleases(ctx context.Context, repo string, limit int) ([]*Release, error) {
	var raw []githubRelease
	path := fmt.Sprintf("/repos/%s/releases?per_page=%d", repo, limit)
	if err := c.getJSON(ctx, path, &raw); err == nil {
		out := make([]*Release, 0, len(raw))
		for _, r := range raw {
			out = append(out, toRelease(r))
		}
		return out, nil
	} else {
		if c.APIBase != "" && c.APIBase != "https://api.github.com" {
			return nil, err
		}
		if rels, fbErr := c.fallbackRecentReleases(ctx, repo, limit); fbErr == nil && len(rels) > 0 {
			return rels, nil
		}
		return nil, err
	}
}

func (c *GitHubClient) fallbackRecentReleases(ctx context.Context, repo string, limit int) ([]*Release, error) {
	target := fmt.Sprintf("https://github.com/%s/releases", repo)
	candidates := []string{target}
	for _, m := range c.AssetMirrors {
		if strings.Contains(m, "ghfast.top") || strings.Contains(m, "gh.monlor.com") {
			candidates = append(candidates, strings.TrimRight(m, "/")+"/"+target)
		}
	}
	pref := c.preferred()
	if pref != "" && len(candidates) > 1 {
		for i, cand := range candidates {
			if strings.HasPrefix(cand, pref) {
				candidates[0], candidates[i] = candidates[i], candidates[0]
				break
			}
		}
	}

	for _, u := range candidates {
		req, err := http.NewRequestWithContext(ctx, http.MethodGet, u, nil)
		if err != nil {
			continue
		}
		req.Header.Set("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64)")
		resp, err := c.HTTP.Do(req)
		if err != nil {
			continue
		}
		if resp.StatusCode != http.StatusOK {
			resp.Body.Close()
			continue
		}
		body, err := io.ReadAll(io.LimitReader(resp.Body, 1024*1024))
		resp.Body.Close()
		if err != nil {
			continue
		}
		rels := parseRecentReleasesHTML(string(body), limit)
		if len(rels) > 0 {
			return rels, nil
		}
	}
	return nil, errors.New("无法从公开网页获取 Release 列表")
}

func parseRecentReleasesHTML(html string, limit int) []*Release {
	matches := reReleaseTag.FindAllStringSubmatch(html, -1)
	seen := make(map[string]bool)
	var out []*Release
	for _, m := range matches {
		if len(m) < 2 {
			continue
		}
		tag := m[1]
		if seen[tag] {
			continue
		}
		seen[tag] = true
		out = append(out, &Release{Tag: tag, Assets: make(map[string]ReleaseAsset)})
		if limit > 0 && len(out) >= limit {
			break
		}
	}
	return out
}

type downloadCandidate struct {
	url    string
	mirror string
}

func (c *GitHubClient) candidateURLs(rawURL string) []downloadCandidate {
	var res []downloadCandidate
	pref := c.preferred()

	if strings.HasPrefix(rawURL, "https://github.com/") {
		mirrors := c.AssetMirrors
		if len(mirrors) == 0 {
			mirrors = DefaultAssetMirrors()
		}

		if pref != "" && pref != "direct" {
			res = append(res, downloadCandidate{
				url:    strings.TrimRight(pref, "/") + "/" + rawURL,
				mirror: pref,
			})
		}

		for _, m := range mirrors {
			if m != pref {
				res = append(res, downloadCandidate{
					url:    strings.TrimRight(m, "/") + "/" + rawURL,
					mirror: m,
				})
			}
		}
	}

	res = append(res, downloadCandidate{url: rawURL, mirror: "direct"})
	return res
}

const ioBufferSize = 4 * 1024 * 1024 // 4 MB 黄金 I/O 缓冲区大小

var ioBufferPool = sync.Pool{
	New: func() any {
		b := make([]byte, ioBufferSize)
		return &b
	},
}

func getIOBuffer() *[]byte {
	return ioBufferPool.Get().(*[]byte)
}

func putIOBuffer(b *[]byte) {
	if b != nil && len(*b) == ioBufferSize {
		ioBufferPool.Put(b)
	}
}

func extractZipFile(zipPath, dstPath string) error {
	r, err := zip.OpenReader(zipPath)
	if err != nil {
		return fmt.Errorf("打开压缩包失败: %w", err)
	}
	defer r.Close()

	if len(r.File) == 0 {
		return errors.New("压缩包为空")
	}

	targetBase := filepath.Base(dstPath)
	var targetFile *zip.File
	for _, f := range r.File {
		if filepath.Base(f.Name) == targetBase {
			targetFile = f
			break
		}
	}
	if targetFile == nil {
		targetFile = r.File[0]
	}

	rc, err := targetFile.Open()
	if err != nil {
		return fmt.Errorf("读取压缩包内容失败: %w", err)
	}
	defer rc.Close()

	tmpDst := dstPath + ".unzip.part"
	_ = os.Remove(tmpDst)
	out, err := os.Create(tmpDst)
	if err != nil {
		return err
	}

	bufPtr := getIOBuffer()
	defer putIOBuffer(bufPtr)
	if _, err := io.CopyBuffer(out, rc, *bufPtr); err != nil {
		_ = out.Close()
		_ = os.Remove(tmpDst)
		return fmt.Errorf("解压写入失败: %w", err)
	}
	if err := out.Close(); err != nil {
		_ = os.Remove(tmpDst)
		return err
	}
	return os.Rename(tmpDst, dstPath)
}

// Download 下载文件到目标路径，并回调进度。若下载文件为压缩包而目标为原始文件，则自动解压。支持多节点故障回退。
func (c *GitHubClient) Download(ctx context.Context, rawURL, dst string, progress func(done, total int64)) error {
	if err := os.MkdirAll(dirOf(dst), 0o755); err != nil {
		return err
	}

	isZipArchive := strings.HasSuffix(strings.ToLower(rawURL), ".zip") && !strings.HasSuffix(strings.ToLower(dst), ".zip")
	downloadDst := dst
	if isZipArchive {
		downloadDst = dst + ".download.tmp.zip"
		defer os.Remove(downloadDst)
	}

	candidates := c.candidateURLs(rawURL)
	var lastErr error
	for _, cand := range candidates {
		err := c.downloadCandidate(ctx, cand.url, downloadDst, progress)
		if err == nil {
			if cand.mirror != "" {
				c.SetPreferredNode(cand.mirror)
			}
			if isZipArchive {
				if err := extractZipFile(downloadDst, dst); err != nil {
					_ = os.Remove(dst)
					return fmt.Errorf("解压下载文件失败 (%s): %w", rawURL, err)
				}
			}
			return nil
		}
		lastErr = err
	}
	if lastErr == nil {
		lastErr = errors.New("无可用下载节点")
	}
	return fmt.Errorf("下载失败（已尝试所有镜像与直连节点）: %w", lastErr)
}

func (c *GitHubClient) downloadCandidate(ctx context.Context, u, dst string, progress func(done, total int64)) error {
	streamCtx, cancelStream := context.WithCancel(ctx)
	defer cancelStream()

	req, err := http.NewRequestWithContext(streamCtx, http.MethodGet, u, nil)
	if err != nil {
		return err
	}
	req.Header.Set("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64)")
	resp, err := c.HTTP.Do(req)
	if err != nil {
		return fmt.Errorf("请求 %s 失败: %w", u, err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return fmt.Errorf("HTTP %d: %s", resp.StatusCode, u)
	}

	tmp := dst + ".part"
	_ = os.Remove(tmp)
	out, err := os.Create(tmp)
	if err != nil {
		return err
	}

	total := resp.ContentLength
	done := int64(0)
	bufPtr := getIOBuffer()
	defer putIOBuffer(bufPtr)
	buf := *bufPtr

	// 15 秒数据流停滞看门狗：若连续 15 秒没有任何数据读入，主动中断该节点连接并切换备用节点
	stallTimer := time.AfterFunc(15*time.Second, func() {
		cancelStream()
	})
	defer stallTimer.Stop()

	for {
		n, readErr := resp.Body.Read(buf)
		if n > 0 {
			stallTimer.Reset(15 * time.Second)
			if _, err := out.Write(buf[:n]); err != nil {
				_ = out.Close()
				_ = os.Remove(tmp)
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
			_ = os.Remove(tmp)
			if errors.Is(streamCtx.Err(), context.Canceled) && !errors.Is(ctx.Err(), context.Canceled) {
				return fmt.Errorf("节点 %s 传输停滞超时（15秒无数据）", u)
			}
			return fmt.Errorf("读取网络流中断 (%s): %w", u, readErr)
		}
	}
	if err := out.Close(); err != nil {
		_ = os.Remove(tmp)
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
