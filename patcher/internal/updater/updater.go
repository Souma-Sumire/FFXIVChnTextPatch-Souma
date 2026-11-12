// Package updater 负责 resource 资源（CSV 包、元数据与字体）的云端同步。
package updater

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
	"strings"
	"sync"
	"time"

	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/netproxy"
)

// FileInfo 是单个文件的哈希与大小。
type FileInfo struct {
	SHA256 string `json:"sha256"`
	Size   int64  `json:"size"`
}

// BundleInfo 是聚合包（rawexd.zip / rules.json / tables.json）的信息。
type BundleInfo struct {
	SHA256 string `json:"sha256"`
	Size   int64  `json:"size"`
	Count  int    `json:"count,omitempty"`
}

// Manifest 对应 resource/manifest.json。
type Manifest struct {
	GeneratedAt string     `json:"generatedAt"`
	RootHash    string     `json:"rootHash,omitempty"`
	RawEXD      BundleInfo `json:"rawexd"`
	// Font 是预编译字体包：macOS 或编译组件不可用时按需下载，平时不下载。
	Font   BundleInfo `json:"font"`
	Rules  BundleInfo `json:"rules"`
	Tables BundleInfo `json:"tables"`
}

// DefaultNodes 返回默认的下载节点（直连 + 国内加速镜像，已实测可用）。
//
// 资源统一发布在 assets 分支：新版与旧版汉化器共用同一份快照，字体包也已确认向下兼容，
// 因此不再单独维护第二条资源分支。
func DefaultNodes() []string {
	const raw = "https://raw.githubusercontent.com/Souma-Sumire/FFXIVChnTextPatch-Souma/assets/resource/"
	return []string{
		raw,
		"https://gh-proxy.com/" + raw,
		"https://cdn.gh-proxy.org/" + raw,
		"https://hk.gh-proxy.org/" + raw,
		"https://gh.monlor.com/" + raw,
		"https://ghfast.top/" + raw,
		"https://wget.la/" + raw,
	}
}

// FontCompilerURLs 返回字体编译组件的下载地址（直连 + 镜像）。
// 组件单独发布在 fontchanger-cli 分支，不随主程序打包，用到时才下载。
func FontCompilerURLs() []string {
	const raw = "https://raw.githubusercontent.com/Souma-Sumire/FFXIVChnTextPatch-Souma/fontchanger-cli/fontchanger-cli.exe"
	return []string{
		raw,
		"https://gh-proxy.com/" + raw,
		"https://cdn.gh-proxy.org/" + raw,
		"https://hk.gh-proxy.org/" + raw,
		"https://gh.monlor.com/" + raw,
		"https://ghfast.top/" + raw,
		"https://wget.la/" + raw,
	}
}

// DownloadFile 把任意 URL 下载到 dst，用于字体编译组件这类不在清单里的文件。
func (u *Updater) DownloadFile(ctx context.Context, url, dst string, progress func(done, total int64)) error {
	return u.fetchToFile(ctx, u.httpClient(), url, dst, progress)
}

// DownloadFromURLs 按顺序尝试多个节点下载文件到 dst，并在全部节点走代理失败时自动尝试无代理直连。
func (u *Updater) DownloadFromURLs(ctx context.Context, subject string, urls []string, dst string, verify func(path string) error, progress func(done, total int64)) error {
	if len(urls) == 0 {
		return errors.New("没有可用的下载节点")
	}
	failures, ok, err := u.tryDownloadFromURLs(ctx, u.httpClient(), false, urls, dst, verify, progress)
	if ok {
		return nil
	}
	if err != nil {
		return err
	}
	if u.usesProxyForAny(urls) {
		directFailures, directOK, directErr := u.tryDownloadFromURLs(ctx, u.DirectHTTP, true, urls, dst, verify, progress)
		if directOK {
			return nil
		}
		if directErr != nil {
			return directErr
		}
		failures = append(failures, directFailures...)
	}
	return DescribeNodeFailures(subject, failures)
}

func (u *Updater) tryDownloadFromURLs(ctx context.Context, client *http.Client, direct bool, urls []string, dst string, verify func(path string) error, progress func(done, total int64)) ([]NodeFailure, bool, error) {
	failures := make([]NodeFailure, 0, len(urls))
	for _, rawURL := range urls {
		if ctx.Err() != nil {
			return failures, false, ctx.Err()
		}
		tmp := dst + ".part"
		_ = os.Remove(tmp)
		if err := u.fetchToFile(ctx, client, rawURL, tmp, progress); err != nil {
			failures = append(failures, NodeFailure{Node: rawURL, Err: err, Direct: direct})
			_ = os.Remove(tmp)
			continue
		}
		if verify != nil {
			if err := verify(tmp); err != nil {
				failures = append(failures, NodeFailure{Node: rawURL, Err: err, Direct: direct})
				_ = os.Remove(tmp)
				continue
			}
		}
		if err := os.MkdirAll(filepath.Dir(dst), 0o755); err != nil {
			_ = os.Remove(tmp)
			return failures, false, err
		}
		if err := os.Rename(tmp, dst); err != nil {
			failures = append(failures, NodeFailure{Node: rawURL, Err: err, Direct: direct})
			_ = os.Remove(tmp)
			continue
		}
		return failures, true, nil
	}
	return failures, false, nil
}

// Updater 负责清单拉取与资源下载。
type Updater struct {
	HTTP       *http.Client
	DirectHTTP *http.Client
	Nodes      []string
	// LocalOnly 为 true 时只校验本地资源，不向云端发任何请求。
	// 本地开发构建用它保住工作区里的 resource/；发布构建恒为 false。
	LocalOnly     bool
	preferredNode string
	mu            sync.RWMutex

	// 同一次启动里表清单与资源同步各请求一次，缓存几十秒可省掉重复的网络往返。
	manifestMu     sync.Mutex
	manifestCache  *Manifest
	manifestCached time.Time
}

// manifestCacheTTL 是清单缓存的有效期：只用于合并同一次启动内的重复请求，
// 不做跨启动复用，保证每次运行都会向云端确认真实版本。
const manifestCacheTTL = 30 * time.Second

func newHTTPTransport(proxyFn func(*http.Request) (*url.URL, error)) *http.Transport {
	return &http.Transport{
		Proxy: proxyFn,
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
}

// New 创建默认同步器。
func New() *Updater {
	return &Updater{
		HTTP:       &http.Client{Timeout: 10 * time.Minute, Transport: newHTTPTransport(netproxy.Func())},
		DirectHTTP: &http.Client{Timeout: 10 * time.Minute, Transport: newHTTPTransport(nil)},
		Nodes:      DefaultNodes(),
	}
}

// SetPreferredNode 设置优先节点（通常为并发测速最快的镜像节点）。
func (u *Updater) SetPreferredNode(node string) {
	u.mu.Lock()
	defer u.mu.Unlock()
	u.preferredNode = node
}

func (u *Updater) httpClient() *http.Client {
	if u.HTTP != nil {
		return u.HTTP
	}
	return http.DefaultClient
}

// usesProxyForAny 判断给定 URL 列表中是否有至少一个会经过当前 HTTP 客户端的代理。
// 只有在确实走了代理且全部失败时，才有必要回退到无代理直连。
func (u *Updater) usesProxyForAny(urls []string) bool {
	if u.DirectHTTP == nil {
		return false
	}
	tr, ok := u.httpClient().Transport.(*http.Transport)
	if !ok || tr.Proxy == nil {
		return false
	}
	for _, rawURL := range urls {
		req, err := http.NewRequest(http.MethodGet, rawURL, nil)
		if err != nil {
			continue
		}
		if proxyURL, err := tr.Proxy(req); err == nil && proxyURL != nil {
			return true
		}
	}
	return false
}

func (u *Updater) cachedManifest() *Manifest {
	u.manifestMu.Lock()
	defer u.manifestMu.Unlock()
	if u.manifestCache == nil || time.Since(u.manifestCached) > manifestCacheTTL {
		return nil
	}
	return u.manifestCache
}

func (u *Updater) cacheManifest(m *Manifest) {
	u.manifestMu.Lock()
	u.manifestCache = m
	u.manifestCached = time.Now()
	u.manifestMu.Unlock()
}

func (u *Updater) nodes() []string {
	u.mu.RLock()
	pref := u.preferredNode
	nodes := u.Nodes
	u.mu.RUnlock()

	base := nodes
	if len(base) == 0 {
		base = DefaultNodes()
	}
	if pref == "" {
		return base
	}
	res := make([]string, 0, len(base))
	res = append(res, pref)
	for _, n := range base {
		if n != pref {
			res = append(res, n)
		}
	}
	return res
}

// FetchManifest 并发向所有节点发起请求，首个成功且合法的清单直接胜出并返回。
// 同一进程内 30 秒内重复调用会复用上一次的结果；若经代理全失败，自动尝试无代理直连。
func (u *Updater) FetchManifest(ctx context.Context) (*Manifest, error) {
	if u.LocalOnly {
		return nil, errors.New("本地开发构建不访问云端资源清单")
	}
	if m := u.cachedManifest(); m != nil {
		return m, nil
	}
	nodes := u.nodes()
	if len(nodes) == 0 {
		return nil, errors.New("没有可用的下载节点")
	}

	m, failures, err := u.fetchManifestWithClient(ctx, u.httpClient(), false, nodes)
	if m != nil {
		return m, nil
	}
	if err != nil {
		return nil, err
	}

	manifestURLs := make([]string, 0, len(nodes))
	for _, n := range nodes {
		manifestURLs = append(manifestURLs, strings.TrimRight(n, "/")+"/manifest.json")
	}
	if u.usesProxyForAny(manifestURLs) {
		dm, directFailures, directErr := u.fetchManifestWithClient(ctx, u.DirectHTTP, true, nodes)
		if dm != nil {
			return dm, nil
		}
		if directErr != nil {
			return nil, directErr
		}
		failures = append(failures, directFailures...)
	}

	return nil, describeManifestFailures(failures)
}

func (u *Updater) fetchManifestWithClient(ctx context.Context, client *http.Client, direct bool, nodes []string) (*Manifest, []NodeFailure, error) {
	raceCtx, cancel := context.WithCancel(ctx)
	defer cancel()

	type response struct {
		manifest *Manifest
		node     string
		err      error
	}

	resCh := make(chan response, len(nodes))

	for _, node := range nodes {
		go func(n string) {
			rawURL := strings.TrimRight(n, "/") + "/manifest.json"
			data, err := u.getWithClient(raceCtx, client, rawURL)
			if err != nil {
				select {
				case resCh <- response{node: n, err: err}:
				case <-raceCtx.Done():
				}
				return
			}
			var m Manifest
			if err := json.Unmarshal(data, &m); err != nil {
				select {
				case resCh <- response{node: n, err: fmt.Errorf("解析 manifest.json 失败: %w", err)}:
				case <-raceCtx.Done():
				}
				return
			}
			if m.RawEXD.SHA256 == "" {
				select {
				case resCh <- response{node: n, err: errors.New("manifest.json 缺少 rawexd")}:
				case <-raceCtx.Done():
				}
				return
			}
			select {
			case resCh <- response{manifest: &m, node: n}:
			case <-raceCtx.Done():
			}
		}(node)
	}

	var failures []NodeFailure
	for i := 0; i < len(nodes); i++ {
		select {
		case <-ctx.Done():
			return nil, failures, ctx.Err()
		case res := <-resCh:
			if res.manifest != nil {
				cancel()
				u.SetPreferredNode(res.node)
				u.cacheManifest(res.manifest)
				return res.manifest, nil, nil
			}
			failures = append(failures, NodeFailure{Node: res.node, Err: res.err, Direct: direct})
		}
	}

	return nil, failures, nil
}

// NodeFailure 记录某个节点这一轮的失败原因。
type NodeFailure struct {
	Node   string
	Err    error
	Direct bool
}

// 错误信息里已经带了节点前缀，末尾的完整 URL 只是噪音，统一裁掉。
var trailingURLErr = regexp.MustCompile(`:\s*https?://\S+$`)

// DescribeNodeFailures 汇总整轮请求的结果，避免只抛出最后一个节点错误让人误以为没有重试。
func DescribeNodeFailures(subject string, failures []NodeFailure) error {
	return describeNodeFailures(subject, failures, "")
}

func describeNodeFailures(subject string, failures []NodeFailure, extraNote string) error {
	if len(failures) == 0 {
		return errors.New("没有可用的下载节点")
	}
	parts := make([]string, 0, len(failures))
	for _, f := range failures {
		msg := ""
		if f.Err != nil {
			msg = trailingURLErr.ReplaceAllString(f.Err.Error(), "")
		}
		name := shortNodeName(f.Node)
		if f.Direct {
			name += "(直连)"
		}
		parts = append(parts, name+": "+msg)
	}
	var b strings.Builder
	fmt.Fprintf(&b, "已向全部 %d 个下载节点请求%s，都失败了%s：", len(failures), subject, extraNote)
	b.WriteString(strings.Join(parts, "；"))
	return errors.New(b.String())
}

// describeManifestFailures 汇总整轮清单请求的结果。
func describeManifestFailures(failures []NodeFailure) error {
	all404 := len(failures) > 0
	for _, f := range failures {
		if f.Err == nil {
			all404 = false
			break
		}
		msg := trailingURLErr.ReplaceAllString(f.Err.Error(), "")
		if !strings.Contains(msg, "404") {
			all404 = false
			break
		}
	}
	note := ""
	if all404 {
		note = "（所有节点都是 404，说明资源分支或路径不存在）"
	}
	return describeNodeFailures("资源清单", failures, note)
}

// shortNodeName 取节点的主机名，便于在错误信息里辨认是哪个加速节点。
func shortNodeName(node string) string {
	if u, err := url.Parse(node); err == nil && u.Host != "" {
		return u.Host
	}
	return strings.TrimRight(node, "/")
}

func (u *Updater) get(ctx context.Context, url string) ([]byte, error) {
	return u.getWithClient(ctx, u.httpClient(), url)
}

func (u *Updater) getWithClient(ctx context.Context, client *http.Client, url string) ([]byte, error) {
	if client == nil {
		client = u.httpClient()
	}
	ctx, cancel := context.WithTimeout(ctx, 10*time.Second)
	defer cancel()
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, url, nil)
	if err != nil {
		return nil, err
	}
	resp, err := client.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("HTTP %d: %s", resp.StatusCode, url)
	}
	return io.ReadAll(resp.Body)
}

// ExtractZip 把 zip 解压到目标目录，并阻止 Zip Slip 路径穿越。
func ExtractZip(zipPath, destDir string) error {
	return ExtractZipWithProgress(zipPath, destDir, nil)
}

// ExtractZipWithProgress 把 zip 解压到目标目录并回调解压进度，同时阻止 Zip Slip 路径穿越。
func ExtractZipWithProgress(zipPath, destDir string, progress func(done, total int64)) error {
	reader, err := zip.OpenReader(zipPath)
	if err != nil {
		return fmt.Errorf("打开快照包 %s 失败: %w", zipPath, err)
	}
	defer reader.Close()

	var totalBytes int64
	var totalFiles int64
	for _, f := range reader.File {
		if f.FileInfo().IsDir() {
			continue
		}
		totalFiles++
		totalBytes += int64(f.UncompressedSize64)
	}
	byBytes := totalBytes > 0
	total := totalBytes
	if !byBytes {
		total = totalFiles
	}
	var done int64
	if progress != nil && total > 0 {
		progress(0, total)
	}

	buf := make([]byte, 256*1024)
	cleanDest := filepath.Clean(destDir)
	for _, f := range reader.File {
		name := filepath.FromSlash(f.Name)
		target := filepath.Join(cleanDest, name)
		if !strings.HasPrefix(filepath.Clean(target), cleanDest+string(os.PathSeparator)) &&
			filepath.Clean(target) != cleanDest {
			return fmt.Errorf("快照包包含非法路径: %s", f.Name)
		}
		if f.FileInfo().IsDir() {
			if err := os.MkdirAll(target, 0o755); err != nil {
				return fmt.Errorf("创建目录 %s 失败: %w", target, err)
			}
			continue
		}
		if err := os.MkdirAll(filepath.Dir(target), 0o755); err != nil {
			return fmt.Errorf("创建父目录 %s 失败: %w", filepath.Dir(target), err)
		}
		src, err := f.Open()
		if err != nil {
			return fmt.Errorf("打开压缩条目 %s 失败: %w", f.Name, err)
		}
		tmp := target + ".part"
		dst, err := os.Create(tmp)
		if err != nil {
			_ = src.Close()
			return fmt.Errorf("创建临时文件 %s 失败: %w", tmp, err)
		}
		for {
			nr, readErr := src.Read(buf)
			if nr > 0 {
				nw, writeErr := dst.Write(buf[:nr])
				if writeErr != nil {
					_ = src.Close()
					_ = dst.Close()
					_ = os.Remove(tmp)
					return fmt.Errorf("解压写入 %s 失败: %w", target, writeErr)
				}
				if nw != nr {
					_ = src.Close()
					_ = dst.Close()
					_ = os.Remove(tmp)
					return fmt.Errorf("解压写入 %s 失败: %w", target, io.ErrShortWrite)
				}
				if byBytes && progress != nil && total > 0 {
					done += int64(nw)
					if done > total {
						done = total
					}
					progress(done, total)
				}
			}
			if readErr == io.EOF {
				break
			}
			if readErr != nil {
				_ = src.Close()
				_ = dst.Close()
				_ = os.Remove(tmp)
				return fmt.Errorf("解压读取 %s 失败: %w", f.Name, readErr)
			}
		}
		_ = src.Close()
		if err := dst.Close(); err != nil {
			_ = os.Remove(tmp)
			return fmt.Errorf("关闭临时文件 %s 失败: %w", tmp, err)
		}
		if err := os.Rename(tmp, target); err != nil {
			_ = os.Remove(tmp)
			return fmt.Errorf("覆盖目标文件 %s 失败: %w", target, err)
		}
		if !byBytes && progress != nil && total > 0 {
			done++
			progress(done, total)
		}
	}
	if progress != nil && total > 0 {
		progress(total, total)
	}
	return nil
}

func writeManifest(resourceDir string, manifest *Manifest) error {
	data, err := json.MarshalIndent(manifest, "", "  ")
	if err != nil {
		return err
	}
	return writeFileAtomic(filepath.Join(resourceDir, "manifest.json"), append(data, '\n'))
}

// writeFileAtomic 先写同目录临时文件再改名，避免中途失败留下截断的文件。
func writeFileAtomic(path string, data []byte) error {
	tmp := path + ".tmp"
	if err := os.WriteFile(tmp, data, 0o644); err != nil {
		return err
	}
	if err := os.Rename(tmp, path); err != nil {
		_ = os.Remove(tmp)
		return err
	}
	return nil
}

// HashFile 计算文件 SHA256 与大小。
func HashFile(path string) (FileInfo, error) {
	f, err := os.Open(path)
	if err != nil {
		return FileInfo{}, err
	}
	defer f.Close()
	h := sha256.New()
	n, err := io.Copy(h, f)
	if err != nil {
		return FileInfo{}, err
	}
	return FileInfo{SHA256: hex.EncodeToString(h.Sum(nil)), Size: n}, nil
}
