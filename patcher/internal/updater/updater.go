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
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"
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
	GeneratedAt string              `json:"generatedAt"`
	RootHash    string              `json:"rootHash"`
	RawEXD      BundleInfo          `json:"rawexd"`
	Font        BundleInfo          `json:"font"`
	Rules       BundleInfo          `json:"rules"`
	Tables      BundleInfo          `json:"tables"`
	Fonts       map[string]FileInfo `json:"fonts"`
}

// DefaultNodes 返回默认的下载节点（直连 + 国内加速镜像）。
func DefaultNodes() []string {
	const raw = "https://raw.githubusercontent.com/Souma-Sumire/FFXIVChnTextPatch-Souma/main/resource/"
	return []string{
		raw,
		"https://ghfast.top/" + raw,
		"https://gh-proxy.com/" + raw,
	}
}

// Updater 负责清单拉取与资源下载。
type Updater struct {
	HTTP          *http.Client
	Nodes         []string
	preferredNode string
	mu            sync.RWMutex
}

// New 创建默认同步器。
func New() *Updater {
	return &Updater{HTTP: &http.Client{Timeout: 5 * time.Minute}, Nodes: DefaultNodes()}
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
func (u *Updater) FetchManifest(ctx context.Context) (*Manifest, error) {
	nodes := u.nodes()
	if len(nodes) == 0 {
		return nil, errors.New("没有可用的下载节点")
	}

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
			url := strings.TrimRight(n, "/") + "/manifest.json"
			data, err := u.get(raceCtx, url)
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
			if m.RootHash == "" {
				select {
				case resCh <- response{node: n, err: errors.New("manifest.json 缺少 rootHash")}:
				case <-raceCtx.Done():
				}
				return
			}
			if m.Fonts == nil {
				m.Fonts = map[string]FileInfo{}
			}
			select {
			case resCh <- response{manifest: &m, node: n}:
			case <-raceCtx.Done():
			}
		}(node)
	}

	var lastErr error
	for i := 0; i < len(nodes); i++ {
		select {
		case <-ctx.Done():
			return nil, ctx.Err()
		case res := <-resCh:
			if res.manifest != nil {
				cancel()
				u.SetPreferredNode(res.node)
				return res.manifest, nil
			}
			lastErr = res.err
		}
	}

	if lastErr == nil {
		lastErr = errors.New("没有可用的下载节点")
	}
	return nil, lastErr
}

func (u *Updater) get(ctx context.Context, url string) ([]byte, error) {
	ctx, cancel := context.WithTimeout(ctx, 10*time.Second)
	defer cancel()
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, url, nil)
	if err != nil {
		return nil, err
	}
	resp, err := u.httpClient().Do(req)
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
	reader, err := zip.OpenReader(zipPath)
	if err != nil {
		return fmt.Errorf("打开快照包失败: %w", err)
	}
	defer reader.Close()
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
				return err
			}
			continue
		}
		if err := os.MkdirAll(filepath.Dir(target), 0o755); err != nil {
			return err
		}
		src, err := f.Open()
		if err != nil {
			return err
		}
		dst, err := os.Create(target)
		if err != nil {
			_ = src.Close()
			return err
		}
		if _, err := io.Copy(dst, src); err != nil {
			_ = src.Close()
			_ = dst.Close()
			return err
		}
		_ = src.Close()
		if err := dst.Close(); err != nil {
			return err
		}
	}
	return nil
}

func writeManifest(resourceDir string, manifest *Manifest) error {
	data, err := json.MarshalIndent(manifest, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(filepath.Join(resourceDir, "manifest.json"), append(data, '\n'), 0o644)
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
