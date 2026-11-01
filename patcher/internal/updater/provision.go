package updater

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"time"
)

// ResourceStatus 描述资源自举结果。
type ResourceStatus struct {
	Updated         bool  `json:"updated"`
	Offline         bool  `json:"offline"`
	DownloadedFiles int   `json:"downloadedFiles"`
	DownloadedBytes int64 `json:"downloadedBytes"`
}

// ProgressFunc 接收资源下载进度。
type ProgressFunc func(stage, detail string, done, total int64)

// Provision 保证 resourceDir 拥有与云端清单一致的最新资源。
//
// 首次启动或资源缺失时：下载 rawexd.zip、font.zip、rules.json、tables.json；
// 资源已是最新时只请求一次 manifest.json；网络不可用但本地资源完整时进入离线模式。
func (u *Updater) Provision(ctx context.Context, resourceDir string, progress ProgressFunc) (ResourceStatus, error) {
	var st ResourceStatus
	if progress == nil {
		progress = func(string, string, int64, int64) {}
	}
	if err := os.MkdirAll(resourceDir, 0o755); err != nil {
		return st, fmt.Errorf("创建资源目录失败: %w", err)
	}

	progress("manifest", "正在获取资源清单", 0, 0)
	remote, err := u.FetchManifest(ctx)
	if err != nil {
		if hasLocalResource(resourceDir) {
			st.Offline = true
			return st, nil
		}
		return st, fmt.Errorf("获取云端资源清单失败: %w", err)
	}

	local, _ := readLocalManifest(resourceDir)
	if local != nil && local.RootHash == remote.RootHash && resourceComplete(resourceDir, remote) {
		return st, nil
	}
	// 本地资源比云端更新时保留本地，避免开发中的改动被旧快照覆盖。
	if local != nil && local.GeneratedAt != "" && remote.GeneratedAt != "" &&
		local.GeneratedAt > remote.GeneratedAt {
		return st, nil
	}
	st.Updated = true

	if local == nil || local.RawEXD.SHA256 != remote.RawEXD.SHA256 || !dirExists(filepath.Join(resourceDir, "rawexd")) {
		progress("rawexd", "正在下载 CSV 快照包", 0, 0)
		if err := u.downloadZip(ctx, resourceDir, "rawexd.zip", remote.RawEXD, func(done, total int64) {
			progress("rawexd", "正在下载 CSV 快照包", done, total)
		}); err != nil {
			return st, err
		}
		st.DownloadedFiles++
		st.DownloadedBytes += remote.RawEXD.Size
	}

	metas := []struct {
		name string
		want BundleInfo
		got  BundleInfo
	}{
		{"rules.json", remote.Rules, bundleOf(local, func(m *Manifest) BundleInfo { return m.Rules })},
		{"tables.json", remote.Tables, bundleOf(local, func(m *Manifest) BundleInfo { return m.Tables })},
	}
	for _, meta := range metas {
		dst := filepath.Join(resourceDir, meta.name)
		if meta.got.SHA256 == meta.want.SHA256 && fileSizeMatches(dst, meta.want.Size) {
			continue
		}
		progress("meta", "正在下载 "+meta.name, 0, 0)
		if err := u.downloadVerified(ctx, meta.name, dst, FileInfo{SHA256: meta.want.SHA256, Size: meta.want.Size}, nil); err != nil {
			return st, err
		}
		st.DownloadedFiles++
		st.DownloadedBytes += meta.want.Size
	}

	// 字体统一下载：优先使用整包 font.zip；缺失时回退为逐个下载。
	if remote.Font.SHA256 != "" {
		needFont := false
		if local == nil || local.Font.SHA256 != remote.Font.SHA256 || !dirExists(filepath.Join(resourceDir, "font")) {
			needFont = true
		} else {
			for key, want := range remote.Fonts {
				if !fontMatches(resourceDir, key, want, local) {
					needFont = true
					break
				}
			}
		}
		if needFont {
			progress("font", "正在下载字体包", 0, 0)
			if err := u.downloadZip(ctx, resourceDir, "font.zip", remote.Font, func(done, total int64) {
				progress("font", "正在下载字体包", done, total)
			}); err != nil {
				return st, err
			}
			st.DownloadedFiles++
			st.DownloadedBytes += remote.Font.Size
		}
	} else {
		fontKeys := sortedFontKeys(remote)
		var fontTotal int64
		for _, key := range fontKeys {
			if fontMatches(resourceDir, key, remote.Fonts[key], local) {
				continue
			}
			fontTotal += remote.Fonts[key].Size
		}
		var fontDone int64
		for _, key := range fontKeys {
			want := remote.Fonts[key]
			if fontMatches(resourceDir, key, want, local) {
				continue
			}
			dst := filepath.Join(resourceDir, filepath.FromSlash(key))
			progress("font", key, fontDone, fontTotal)
			if err := u.downloadVerified(ctx, key, dst, want, func(done, _ int64) {
				progress("font", key, fontDone+done, fontTotal)
			}); err != nil {
				return st, err
			}
			fontDone += want.Size
			st.DownloadedFiles++
			st.DownloadedBytes += want.Size
		}
	}

	if err := writeManifest(resourceDir, remote); err != nil {
		return st, err
	}
	progress("done", "资源已更新", 1, 1)
	return st, nil
}

func bundleOf(local *Manifest, pick func(*Manifest) BundleInfo) BundleInfo {
	if local == nil {
		return BundleInfo{}
	}
	return pick(local)
}

func (u *Updater) downloadZip(ctx context.Context, resourceDir, zipName string, want BundleInfo, progress func(done, total int64)) error {
	var lastErr error
	for _, node := range u.nodes() {
		url := strings.TrimRight(node, "/") + "/" + zipName
		tmp := filepath.Join(resourceDir, zipName+".part")
		if err := u.fetchToFile(ctx, url, tmp, progress); err != nil {
			lastErr = err
			continue
		}
		got, err := HashFile(tmp)
		if err != nil {
			lastErr = err
			_ = os.Remove(tmp)
			continue
		}
		if got.SHA256 != want.SHA256 || (want.Size > 0 && got.Size != want.Size) {
			lastErr = fmt.Errorf("%s 校验失败", zipName)
			_ = os.Remove(tmp)
			continue
		}
		if err := ExtractZip(tmp, resourceDir); err != nil {
			lastErr = err
			_ = os.Remove(tmp)
			continue
		}
		_ = os.Remove(tmp)
		return nil
	}
	if lastErr == nil {
		lastErr = errors.New("没有可用的下载节点")
	}
	return fmt.Errorf("下载 %s 失败: %w", zipName, lastErr)
}

func (u *Updater) downloadVerified(ctx context.Context, key, dst string, want FileInfo, progress func(done, total int64)) error {
	var lastErr error
	for _, node := range u.nodes() {
		url := strings.TrimRight(node, "/") + "/" + strings.TrimLeft(key, "/")
		tmp := dst + ".part"
		if err := u.fetchToFile(ctx, url, tmp, progress); err != nil {
			lastErr = err
			continue
		}
		got, err := HashFile(tmp)
		if err != nil {
			lastErr = err
			_ = os.Remove(tmp)
			continue
		}
		if got.SHA256 != want.SHA256 || (want.Size > 0 && got.Size != want.Size) {
			lastErr = fmt.Errorf("%s 校验失败", key)
			_ = os.Remove(tmp)
			continue
		}
		if err := os.MkdirAll(filepath.Dir(dst), 0o755); err != nil {
			return err
		}
		if err := os.Rename(tmp, dst); err != nil {
			return err
		}
		return nil
	}
	if lastErr == nil {
		lastErr = errors.New("没有可用的下载节点")
	}
	return fmt.Errorf("下载 %s 失败: %w", key, lastErr)
}

const ioBufferSize = 4 * 1024 * 1024

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

func (u *Updater) fetchToFile(ctx context.Context, url, dst string, progress func(done, total int64)) error {
	streamCtx, cancelStream := context.WithCancel(ctx)
	defer cancelStream()

	req, err := http.NewRequestWithContext(streamCtx, http.MethodGet, url, nil)
	if err != nil {
		return err
	}
	resp, err := u.httpClient().Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return fmt.Errorf("HTTP %d: %s", resp.StatusCode, url)
	}
	if err := os.MkdirAll(filepath.Dir(dst), 0o755); err != nil {
		return err
	}
	out, err := os.Create(dst)
	if err != nil {
		return err
	}
	total := resp.ContentLength
	done := int64(0)
	bufPtr := getIOBuffer()
	defer putIOBuffer(bufPtr)
	buf := *bufPtr

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
			if errors.Is(streamCtx.Err(), context.Canceled) && !errors.Is(ctx.Err(), context.Canceled) {
				return fmt.Errorf("下载节点传输停滞超时（15秒无响应）: %s", url)
			}
			return readErr
		}
	}
	return out.Close()
}

func sortedFontKeys(manifest *Manifest) []string {
	keys := make([]string, 0, len(manifest.Fonts))
	for key := range manifest.Fonts {
		keys = append(keys, key)
	}
	sort.Strings(keys)
	return keys
}

func fontMatches(resourceDir, key string, want FileInfo, local *Manifest) bool {
	if !fileSizeMatches(filepath.Join(resourceDir, filepath.FromSlash(key)), want.Size) {
		return false
	}
	if local != nil {
		if got, ok := local.Fonts[key]; ok {
			return got.SHA256 == want.SHA256 && got.Size == want.Size
		}
	}
	return false
}

func fileSizeMatches(path string, size int64) bool {
	st, err := os.Stat(path)
	return err == nil && st.Size() == size
}

func dirExists(path string) bool {
	st, err := os.Stat(path)
	return err == nil && st.IsDir()
}

func resourceComplete(resourceDir string, remote *Manifest) bool {
	for _, name := range []string{"manifest.json", "rules.json", "tables.json"} {
		if _, err := os.Stat(filepath.Join(resourceDir, name)); err != nil {
			return false
		}
	}
	if !dirExists(filepath.Join(resourceDir, "rawexd")) {
		return false
	}
	for key, want := range remote.Fonts {
		if !fileSizeMatches(filepath.Join(resourceDir, filepath.FromSlash(key)), want.Size) {
			return false
		}
	}
	return true
}

func readLocalManifest(resourceDir string) (*Manifest, error) {
	data, err := os.ReadFile(filepath.Join(resourceDir, "manifest.json"))
	if err != nil {
		return nil, err
	}
	var m Manifest
	if err := json.Unmarshal(data, &m); err != nil {
		return nil, err
	}
	if m.Fonts == nil {
		m.Fonts = map[string]FileInfo{}
	}
	return &m, nil
}

// HasLocalResource 检查本地是否已有可运行的必要基础资源。
func HasLocalResource(resourceDir string) bool {
	for _, name := range []string{"manifest.json", "tables.json"} {
		if _, err := os.Stat(filepath.Join(resourceDir, name)); err != nil {
			return false
		}
	}
	return dirExists(filepath.Join(resourceDir, "rawexd"))
}

func hasLocalResource(resourceDir string) bool {
	return HasLocalResource(resourceDir)
}
