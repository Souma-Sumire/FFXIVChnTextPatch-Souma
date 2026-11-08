package verifier

import (
	"archive/zip"
	"bytes"
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

	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/netproxy"
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
	Title      string
	Body       string
	URL        string
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
	// linkZipAliases 造出的别名指向 .zip 附件：其 digest 是压缩包自身的哈希，
	// 不等于解压后原文件的哈希，不能拿来当权威哈希比对。
	if asset.Name != name {
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
	// MirrorDownloadURL 返回经过加速节点包装的下载地址；地址无需加速或不适用时返回空串。
	MirrorDownloadURL(raw string) string
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

const (
	// defaultAPIBase 是 GitHub 官方 REST API 地址。
	defaultAPIBase = "https://api.github.com"
	// probeTimeout 是单个候选地址的请求上限，超时即视为该节点不可用。
	probeTimeout = 8 * time.Second
	// apiStageTimeout 限定 API 阶段的耗时：API 不通时要尽快回退到网页端解析，
	// 不能把调用方（如启动时的静默检查）的整个预算耗在 API 上。
	apiStageTimeout = 6 * time.Second
	// apiBodyLimit 是 API 响应体上限；网页端页面体积更小，单独限制。
	apiBodyLimit  = 8 << 20
	htmlBodyLimit = 4 << 20
	userAgent     = "FFXIVChnTextPatch-Souma"
	browserAgent  = "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"
	// 直连速度判定门限。只在足够大的下载上采信：hashes.json 这类几十 KB 的
	// 请求即使在慢速链路上也会很快完成，不能据此认为直连可用。
	fastDownloadMinBytes       = 1 << 20
	fastDownloadBytesPerSecond = 512 << 10
	apiAcceptValue             = "application/vnd.github+json"
)

// mirrorURL 把 GitHub 原始地址套到加速节点上：镜像节点接受完整 URL 作为路径。
func mirrorURL(mirror, raw string) string {
	return strings.TrimRight(mirror, "/") + "/" + raw
}

func browserHeaders() map[string]string {
	return map[string]string{"User-Agent": browserAgent}
}

// GitHubClient 是 Cloud 的 GitHub 实现。
type GitHubClient struct {
	HTTP         *http.Client
	APIBase      string
	Token        string
	AssetMirrors []string
	// preferredNode 是已确认可用的附件镜像节点，只会存镜像地址。
	// 元数据请求成功（尤其是 api.github.com 直连）不能证明附件能直连快下载，
	// 所以这里不收 "direct"，避免把慢速直连固化成后续大文件的首选节点。
	preferredNode string
	// directFast 表示实测过直连下载速度可接受，用于决定是否还需要提供镜像入口。
	directFast bool
	// nodeHook 在每次尝试下载节点前回调，用于让界面显示当前正在使用哪个镜像。
	nodeHook func(node string)
	mu       sync.RWMutex
}

// NewGitHubClient 创建默认 GitHub API 客户端。
func NewGitHubClient() *GitHubClient {
	transport := &http.Transport{
		Proxy: netproxy.Func(),
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
		APIBase:      defaultAPIBase,
		AssetMirrors: DefaultAssetMirrors(),
	}
}

func (c *GitHubClient) httpClient() *http.Client {
	if c.HTTP != nil {
		return c.HTTP
	}
	return http.DefaultClient
}

// mirrors 返回可用的镜像节点，未显式配置时回落到默认节点。
func (c *GitHubClient) mirrors() []string {
	if len(c.AssetMirrors) == 0 {
		return DefaultAssetMirrors()
	}
	return c.AssetMirrors
}

// mirrorCandidates 把原始 GitHub 地址展开为各镜像节点上的地址。
func (c *GitHubClient) mirrorCandidates(rawURL string) []string {
	mirrors := c.mirrors()
	out := make([]string, 0, len(mirrors))
	for _, m := range mirrors {
		out = append(out, mirrorURL(m, rawURL))
	}
	return out
}

// rememberNode 记住响应来自哪个镜像，后续大文件下载优先使用它。
func (c *GitHubClient) rememberNode(source string) {
	for _, m := range c.mirrors() {
		if strings.HasPrefix(source, m) {
			c.SetPreferredNode(m)
			return
		}
	}
}

// MirrorDownloadURL 返回把 GitHub 下载地址套到加速节点后的地址。
// 只有 GitHub 附件地址才需要加速；实测直连速度足够快时才返回空串。
func (c *GitHubClient) MirrorDownloadURL(raw string) string {
	if !strings.HasPrefix(raw, "https://github.com/") {
		return ""
	}
	if c.isDirectFast() {
		return ""
	}
	node := c.preferred()
	if node == "" {
		mirrors := c.mirrors()
		if len(mirrors) == 0 {
			return ""
		}
		node = mirrors[0]
	}
	return mirrorURL(node, raw)
}

func (c *GitHubClient) apiHeaders() map[string]string {
	headers := map[string]string{
		"Accept":     apiAcceptValue,
		"User-Agent": userAgent,
	}
	if c.Token != "" {
		headers["Authorization"] = "Bearer " + c.Token
	}
	return headers
}

// SetPreferredNode 设置优先使用的附件镜像节点。传 "direct" 等价于标记「直连可用」。
func (c *GitHubClient) SetPreferredNode(node string) {
	c.mu.Lock()
	defer c.mu.Unlock()
	if node == "direct" {
		c.directFast = true
		return
	}
	c.preferredNode = node
}

func (c *GitHubClient) preferred() string {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.preferredNode
}

// SetNodeHook 设置下载节点回调，每次切换候选节点（镜像或直连）时触发。
// 传 nil 取消回调。
func (c *GitHubClient) SetNodeHook(hook func(node string)) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.nodeHook = hook
}

func (c *GitHubClient) notifyNode(node string) {
	c.mu.RLock()
	hook := c.nodeHook
	c.mu.RUnlock()
	if hook != nil {
		hook(nodeLabel(node))
	}
}

// nodeLabel 把节点标识转成界面可读的名称：镜像取域名，直连显式说明。
func nodeLabel(node string) string {
	if node == "" || node == "direct" {
		return "GitHub 直连"
	}
	if u, err := url.Parse(node); err == nil && u.Host != "" {
		return u.Host
	}
	return strings.TrimRight(node, "/")
}

func (c *GitHubClient) isDirectFast() bool {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.directFast
}

func (c *GitHubClient) setDirectFast(v bool) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.directFast = v
}

// rememberDirectSpeed 用一次实际下载的字节数与耗时判断直连是否够快。
func (c *GitHubClient) rememberDirectSpeed(bytes int64, elapsed time.Duration) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.directFast = bytes >= fastDownloadMinBytes && elapsed > 0 &&
		float64(bytes)/elapsed.Seconds() >= fastDownloadBytesPerSecond
}

type githubAsset struct {
	Name               string `json:"name"`
	Digest             string `json:"digest"`
	BrowserDownloadURL string `json:"browser_download_url"`
}

type githubRelease struct {
	TagName string        `json:"tag_name"`
	Name    string        `json:"name"`
	Body    string        `json:"body"`
	HTMLURL string        `json:"html_url"`
	Assets  []githubAsset `json:"assets"`
}

// httpResult 是单个候选地址的请求结果。
type httpResult struct {
	body   []byte
	url    string
	status int
	err    error
}

// fetchBody 发起一次 GET 请求并读取响应体（最多 maxBytes）。
func fetchBody(ctx context.Context, client *http.Client, u string, headers map[string]string, maxBytes int64) ([]byte, int, error) {
	reqCtx, cancel := context.WithTimeout(ctx, probeTimeout)
	defer cancel()
	req, err := http.NewRequestWithContext(reqCtx, http.MethodGet, u, nil)
	if err != nil {
		return nil, 0, err
	}
	for k, v := range headers {
		req.Header.Set(k, v)
	}
	resp, err := client.Do(req)
	if err != nil {
		return nil, 0, fmt.Errorf("请求 %s 失败: %w", u, err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return nil, resp.StatusCode, fmt.Errorf("%s 返回 HTTP %d", u, resp.StatusCode)
	}
	body, err := io.ReadAll(io.LimitReader(resp.Body, maxBytes))
	if err != nil {
		return nil, resp.StatusCode, fmt.Errorf("读取 %s 响应失败: %w", u, err)
	}
	return body, resp.StatusCode, nil
}

// raceGet 并发请求直连地址与全部镜像候选，返回首个通过 accept 的响应体及其来源。
// 之所以并发而不是依次重试：直连被阻断时通常要等到超时才会失败，
// 串行重试会把整个预算耗在第一个不可达的地址上。
// 全部失败时只有直连明确返回 404 才认定资源不存在——镜像对不支持的路径同样回 404。
func raceGet(ctx context.Context, client *http.Client, direct string, mirrors []string, headers map[string]string, maxBytes int64, accept func([]byte) bool) ([]byte, string, error) {
	urls := append([]string{direct}, mirrors...)
	raceCtx, cancel := context.WithCancel(ctx)
	defer cancel()

	results := make(chan httpResult, len(urls))
	for _, u := range urls {
		// 认证头与 GitHub 专用 Accept 只发给直连地址，不转发给第三方镜像。
		h := headers
		if u != direct {
			h = browserHeaders()
		}
		go func(u string, h map[string]string) {
			body, status, err := fetchBody(raceCtx, client, u, h, maxBytes)
			select {
			case results <- httpResult{body: body, url: u, status: status, err: err}:
			case <-raceCtx.Done():
			}
		}(u, h)
	}

	var lastErr error
	notFound := false
	for range urls {
		select {
		case <-ctx.Done():
			return nil, "", ctx.Err()
		case r := <-results:
			if r.err == nil && len(r.body) > 0 && accept(r.body) {
				cancel()
				return r.body, r.url, nil
			}
			if r.url == direct && r.status == http.StatusNotFound {
				notFound = true
			}
			if r.err != nil {
				lastErr = r.err
			} else {
				lastErr = fmt.Errorf("%s 返回内容无法识别", r.url)
			}
		}
	}
	if notFound {
		return nil, "", ErrNotFound
	}
	if lastErr == nil {
		lastErr = errors.New("没有可用的下载节点")
	}
	return nil, "", lastErr
}

// getJSON 拉取 GitHub API 响应。公开仓库的请求会同时发往官方 API 与各镜像节点，
// 首个返回合法 JSON 的地址胜出，直连不可用时不必等待超时。
func (c *GitHubClient) getJSON(ctx context.Context, path string, out any) error {
	base := c.APIBase
	if base == "" {
		base = defaultAPIBase
	}
	raw := base + path
	client := c.httpClient()

	// 自定义 APIBase（测试替身或自建代理）只走该地址；
	// 带 Token 的请求也不扩散到第三方镜像，避免泄漏凭据。
	if base != defaultAPIBase || c.Token != "" {
		body, status, err := fetchBody(ctx, client, raw, c.apiHeaders(), apiBodyLimit)
		if err != nil {
			if status == http.StatusNotFound {
				return ErrNotFound
			}
			return err
		}
		return json.Unmarshal(body, out)
	}

	body, source, err := raceGet(ctx, client, raw, c.mirrorCandidates(raw), c.apiHeaders(), apiBodyLimit, isValidReleaseJSON)
	if err != nil {
		return err
	}
	c.rememberNode(source)
	return json.Unmarshal(body, out)
}

// isValidReleaseJSON 判断响应是否真的是 Release 数据，而不只是语法合法的 JSON。
// 限流或镜像错误页常返回 200 + {"message":...}；空对象、null、空数组同样没有信息量。
// 这类内容一旦被当作首个成功响应，会把错误固化成「云端未收录该版本」。
// 列表与对象必须分别校验：拿对象 probe 去解析数组会直接反序列化失败，
// 从而把合法的 Release 列表全部拒掉。
func isValidReleaseJSON(b []byte) bool {
	if !json.Valid(b) {
		return false
	}
	trimmed := bytes.TrimSpace(b)
	if len(trimmed) == 0 {
		return false
	}
	switch trimmed[0] {
	case '[':
		var list []struct {
			TagName string `json:"tag_name"`
		}
		if err := json.Unmarshal(trimmed, &list); err != nil {
			return false
		}
		return len(list) > 0 && list[0].TagName != ""
	case '{':
		var obj struct {
			Message string `json:"message"`
			TagName string `json:"tag_name"`
		}
		if err := json.Unmarshal(trimmed, &obj); err != nil {
			return false
		}
		return obj.Message == "" && obj.TagName != ""
	default:
		return false
	}
}

func toRelease(in githubRelease) *Release {
	out := &Release{
		Tag:    in.TagName,
		Title:  in.Name,
		Body:   in.Body,
		URL:    in.HTMLURL,
		Assets: make(map[string]ReleaseAsset, len(in.Assets)),
	}
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

// ReleaseByTag 获取指定 tag 的 Release。优先走 REST API（一次请求即含完整附件列表），
// 被限流或阻断时才解析网页端附件列表。
func (c *GitHubClient) ReleaseByTag(ctx context.Context, repo, tag string) (*Release, error) {
	rel, err := c.fetchReleaseInternal(ctx, repo, tag)
	if err != nil {
		return nil, err
	}
	c.loadHashesJSON(ctx, rel)
	return rel, nil
}

func (c *GitHubClient) fetchReleaseInternal(ctx context.Context, repo, tag string) (*Release, error) {
	path := fmt.Sprintf("/repos/%s/releases/tags/%s", repo, url.PathEscape(tag))
	customAPI := c.APIBase != "" && c.APIBase != defaultAPIBase

	var raw githubRelease
	apiErr := c.getJSON(ctx, path, &raw)
	if apiErr == nil {
		return toRelease(raw), nil
	}
	// 自定义 APIBase（测试替身或自建代理）没有备用路径，直接返回。
	if errors.Is(apiErr, ErrNotFound) || customAPI {
		return nil, apiErr
	}

	// API 不可用时才降级解析网页端：网页体积大、需要正则提取，且部分镜像不支持该路径。
	rel, err := c.fetchExpandedAssets(ctx, repo, tag)
	if err == nil {
		return rel, nil
	}
	if errors.Is(err, ErrNotFound) {
		return nil, ErrNotFound
	}
	return nil, fmt.Errorf("获取 Release %s 失败: %w（API 错误: %v）", tag, err, apiErr)
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

// fetchExpandedAssets 从网页端解析指定 tag 的附件列表。
// 各镜像对页面路径的支持不一致，因此全部作为候选并发尝试，首个解析出附件的地址胜出。
func (c *GitHubClient) fetchExpandedAssets(ctx context.Context, repo, tag string) (*Release, error) {
	pageURL := fmt.Sprintf("https://github.com/%s/releases/expanded_assets/%s", repo, url.PathEscape(tag))
	body, source, err := raceGet(ctx, c.httpClient(), pageURL, c.mirrorCandidates(pageURL), browserHeaders(), htmlBodyLimit,
		func(b []byte) bool {
			rel, parseErr := parseExpandedAssets(string(b), tag)
			return parseErr == nil && len(rel.Assets) > 0
		})
	if err != nil {
		return nil, err
	}
	c.rememberNode(source)
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

	// API 阶段单独限时：直连与镜像都不通时要尽快回退到网页端解析，
	// 否则调用方（启动时的静默检查）会把整个预算耗在 API 上。
	apiCtx, cancelAPI := context.WithTimeout(ctx, apiStageTimeout)
	err := c.getJSON(apiCtx, path, &raw)
	cancelAPI()

	if err == nil {
		out := make([]*Release, 0, len(raw))
		for _, r := range raw {
			out = append(out, toRelease(r))
		}
		return out, nil
	}
	if c.APIBase != "" && c.APIBase != defaultAPIBase {
		return nil, err
	}
	if rels, fbErr := c.fallbackRecentReleases(ctx, repo, limit); fbErr == nil && len(rels) > 0 {
		return rels, nil
	}
	return nil, err
}

// fallbackRecentReleases 从网页端解析 Release 列表。API 不可用（限流或网络阻断）时的兜底路径，
// 同样把全部镜像作为候选并发尝试。
func (c *GitHubClient) fallbackRecentReleases(ctx context.Context, repo string, limit int) ([]*Release, error) {
	target := fmt.Sprintf("https://github.com/%s/releases", repo)
	body, source, err := raceGet(ctx, c.httpClient(), target, c.mirrorCandidates(target), browserHeaders(), htmlBodyLimit,
		func(b []byte) bool {
			return len(parseRecentReleasesHTML(string(b), repo, limit)) > 0
		})
	if err != nil {
		return nil, err
	}
	c.rememberNode(source)
	rels := parseRecentReleasesHTML(string(body), repo, limit)
	if len(rels) == 0 {
		return nil, errors.New("无法从公开网页获取 Release 列表")
	}
	return rels, nil
}

func parseRecentReleasesHTML(html, repo string, limit int) []*Release {
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
		out = append(out, &Release{
			Tag:    tag,
			URL:    fmt.Sprintf("https://github.com/%s/releases/tag/%s", repo, tag),
			Assets: make(map[string]ReleaseAsset),
		})
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
	isGitHub := strings.HasPrefix(rawURL, "https://github.com/")
	pref := c.preferred()
	directFast := c.isDirectFast()

	var res []downloadCandidate
	// 只有实测直连够快时才把它排在最前。否则大文件会先卡在慢速直连上，
	// 要等 15 秒停滞看门狗触发才切换节点。
	if directFast {
		res = append(res, downloadCandidate{url: rawURL, mirror: "direct"})
	}

	if isGitHub {
		if pref != "" {
			res = append(res, downloadCandidate{url: mirrorURL(pref, rawURL), mirror: pref})
		}
		for _, m := range c.mirrors() {
			if m != pref {
				res = append(res, downloadCandidate{url: mirrorURL(m, rawURL), mirror: m})
			}
		}
	}

	if !directFast {
		res = append(res, downloadCandidate{url: rawURL, mirror: "direct"})
	}
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

// maxExtractBytes 是单个压缩包条目解压后的上限。超出时宁可报错，
// 也不能静默截断后把不完整文件 rename 成目标文件。
const maxExtractBytes = 4 << 30

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
		if !f.FileInfo().IsDir() && filepath.Base(f.Name) == targetBase {
			targetFile = f
			break
		}
	}
	if targetFile == nil {
		for _, f := range r.File {
			if !f.FileInfo().IsDir() {
				targetFile = f
				break
			}
		}
	}
	if targetFile == nil {
		return errors.New("压缩包内无有效文件")
	}
	if targetFile.UncompressedSize64 > maxExtractBytes {
		return fmt.Errorf("压缩包内文件解压后过大: %d 字节", targetFile.UncompressedSize64)
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
	written, err := io.CopyBuffer(out, io.LimitReader(rc, maxExtractBytes+1), *bufPtr)
	if err != nil {
		_ = out.Close()
		_ = os.Remove(tmpDst)
		return fmt.Errorf("解压写入失败: %w", err)
	}
	if written > maxExtractBytes {
		_ = out.Close()
		_ = os.Remove(tmpDst)
		return fmt.Errorf("解压后文件超过上限 %d 字节", int64(maxExtractBytes))
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
		c.notifyNode(cand.mirror)
		start := time.Now()
		written, err := c.downloadCandidate(ctx, cand.url, downloadDst, progress)
		if err != nil {
			lastErr = err
			continue
		}
		if isZipArchive {
			if err := extractZipFile(downloadDst, dst); err != nil {
				_ = os.Remove(dst)
				lastErr = fmt.Errorf("解压下载文件失败 (%s): %w", rawURL, err)
				continue
			}
		}
		// 只有下载与解压都成功才把节点记为可用，避免把返回损坏数据的镜像固化成首选。
		if cand.mirror == "direct" {
			c.rememberDirectSpeed(written, time.Since(start))
		} else {
			c.SetPreferredNode(cand.mirror)
			c.setDirectFast(false)
		}
		return nil
	}
	if lastErr == nil {
		lastErr = errors.New("无可用下载节点")
	}
	return fmt.Errorf("下载失败，请检查网络后重试: %w", lastErr)
}

func (c *GitHubClient) downloadCandidate(ctx context.Context, u, dst string, progress func(done, total int64)) (int64, error) {
	streamCtx, cancelStream := context.WithCancel(ctx)
	defer cancelStream()

	req, err := http.NewRequestWithContext(streamCtx, http.MethodGet, u, nil)
	if err != nil {
		return 0, err
	}
	req.Header.Set("User-Agent", browserAgent)
	resp, err := c.httpClient().Do(req)
	if err != nil {
		return 0, fmt.Errorf("请求 %s 失败: %w", u, err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return 0, fmt.Errorf("HTTP %d: %s", resp.StatusCode, u)
	}

	tmp := dst + ".part"
	_ = os.Remove(tmp)
	out, err := os.Create(tmp)
	if err != nil {
		return 0, err
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
				return done, err
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
				return done, fmt.Errorf("节点 %s 传输停滞超时（15秒无数据）", u)
			}
			return done, fmt.Errorf("读取网络流中断 (%s): %w", u, readErr)
		}
	}
	if err := out.Close(); err != nil {
		_ = os.Remove(tmp)
		return done, err
	}
	return done, os.Rename(tmp, dst)
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

func parseSegmentNumber(s string) int64 {
	s = strings.TrimSpace(s)
	end := 0
	for end < len(s) && s[end] >= '0' && s[end] <= '9' {
		end++
	}
	if end == 0 {
		return 0
	}
	v, _ := strconv.ParseInt(s[:end], 10, 64)
	return v
}

// CompareVersion 按点分数字比较版本号（如 2026.10.16.0000.0000 或 2.1.1）。
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
			av = parseSegmentNumber(as[i])
		}
		if i < len(bs) {
			bv = parseSegmentNumber(bs[i])
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
