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
	"strings"
	"sync"
	"time"
)

// ResourceStatus 描述资源自举结果。
type ResourceStatus struct {
	Updated         bool   `json:"updated"`
	DownloadedFiles int    `json:"downloadedFiles"`
	DownloadedBytes int64  `json:"downloadedBytes"`
	CSVHash         string `json:"csvHash,omitempty"`
	CSVUpdated      bool   `json:"csvUpdated,omitempty"`
	GeneratedAt     string `json:"generatedAt,omitempty"`
}

// ProgressFunc 接收资源下载进度。
type ProgressFunc func(stage, detail string, done, total int64)

// FileProgress 接收逐文件/逐字节校验进度。
type FileProgress func(done, total int64)

// throttleProgress 给高频进度回调限流：阶段或文案变化立即放行，
// 同一阶段内最多每 60ms 发一次，全部完成时立即放行。
func throttleProgress(p ProgressFunc) ProgressFunc {
	if p == nil {
		return func(string, string, int64, int64) {}
	}
	var (
		mu       sync.Mutex
		last     time.Time
		lastKey  string
		lastDone int64
	)
	return func(stage, detail string, done, total int64) {
		key := stage + "|" + detail
		mu.Lock()
		now := time.Now()
		finished := total > 0 && done >= total
		if key == lastKey && done == lastDone && lastKey != "" {
			mu.Unlock()
			return
		}
		if !finished && now.Sub(last) < 60*time.Millisecond {
			mu.Unlock()
			return
		}
		last = now
		lastKey = key
		lastDone = done
		mu.Unlock()
		p(stage, detail, done, total)
	}
}

// Provision 保证 resourceDir 拥有与云端清单一致的最新资源。
// 各模块（CSV、字体、规则、表格）独立比对哈希与更新，互不捆绑。
func (u *Updater) Provision(ctx context.Context, resourceDir string, progress ProgressFunc) (ResourceStatus, error) {
	var st ResourceStatus
	if progress == nil {
		progress = func(string, string, int64, int64) {}
	}
	progress = throttleProgress(progress)
	if u.LocalOnly {
		return u.provisionLocal(resourceDir, progress)
	}
	if err := os.MkdirAll(resourceDir, 0o755); err != nil {
		return st, fmt.Errorf("创建资源目录失败: %w", err)
	}

	progress("manifest", "获取资源清单", 0, 0)
	remote, err := u.FetchManifest(ctx)
	if err != nil {
		return st, fmt.Errorf("获取云端资源清单失败: %w", err)
	}

	st.CSVHash = remote.RawEXD.SHA256
	st.GeneratedAt = remote.GeneratedAt

	local, _ := ReadLocalManifest(resourceDir)
	// 快速通道：清单与上次校验一致，且本地文件的数量/大小/修改时间都没变，
	// 直接认定内容未被改动，省掉逐文件哈希。
	if stamp := readVerifyStamp(resourceDir); stamp != nil &&
		local != nil && local.RawEXD.SHA256 == remote.RawEXD.SHA256 &&
		metaModulesComplete(resourceDir, local, remote, true) && stamp.matches(resourceDir, remote) {
		progress("done", "资源已是最新", 1, 1)
		return st, nil
	}
	progress("verify", "校验 CSV 快照包", 0, 0)
	cloudCSVOK := local != nil && local.RawEXD.SHA256 == remote.RawEXD.SHA256 &&
		bundleDirMatchesWithProgress(resourceDir, "rawexd.zip", remote.RawEXD, func(done, total int64) {
			progress("verify", "核对 CSV 文件", done, total)
		})
	if metaModulesComplete(resourceDir, local, remote, cloudCSVOK) {
		writeVerifyStamp(resourceDir, buildVerifyStamp(resourceDir, remote))
		progress("done", "资源已是最新", 1, 1)
		return st, nil
	}
	st.Updated = true

	if !cloudCSVOK {
		zipPath := filepath.Join(resourceDir, "rawexd.zip")
		if zipMatches(resourceDir, "rawexd.zip", remote.RawEXD) {
			// 快照包本身可信，只是解压目录被改动或缺失：直接重新解压，无需重新下载。
			progress("rawexd", "修复 CSV 快照", 0, 0)
			if err := ExtractZip(zipPath, resourceDir); err != nil {
				return st, fmt.Errorf("解压 rawexd.zip 失败: %w", err)
			}
		} else {
			progress("rawexd", "下载 CSV 快照包", 0, 0)
			if err := u.downloadZip(ctx, resourceDir, "rawexd.zip", remote.RawEXD, true, func(done, total int64) {
				progress("rawexd", "下载 CSV 快照包", done, total)
			}); err != nil {
				return st, err
			}
			st.DownloadedFiles++
			st.DownloadedBytes += remote.RawEXD.Size
		}
		st.CSVUpdated = true
	}

	metas := []struct {
		name string
		want BundleInfo
	}{
		{"rules.json", remote.Rules},
		{"tables.json", remote.Tables},
	}
	for _, meta := range metas {
		dst := filepath.Join(resourceDir, meta.name)
		// 对本地文件重算哈希再与云端比对：本地 manifest 只是下载时的历史记录，
		// 拿记录比记录发现不了文件本身已被改动（同尺寸篡改尤其如此）。
		if got, err := HashFile(dst); err == nil &&
			got.SHA256 == meta.want.SHA256 && (meta.want.Size <= 0 || got.Size == meta.want.Size) {
			continue
		}
		progress("meta", "下载 "+meta.name, 0, 0)
		if err := u.downloadVerified(ctx, meta.name, dst, FileInfo{SHA256: meta.want.SHA256, Size: meta.want.Size}, nil); err != nil {
			return st, err
		}
		st.DownloadedFiles++
		st.DownloadedBytes += meta.want.Size
	}

	if err := writeManifest(resourceDir, remote); err != nil {
		return st, err
	}
	writeVerifyStamp(resourceDir, buildVerifyStamp(resourceDir, remote))
	progress("done", "资源已更新", 1, 1)
	return st, nil
}

// provisionLocal 只检查工作区里的资源是否齐全，绝不联网、也不改动任何文件。
// 本地开发时 resource/ 是唯一事实来源：改 CSV、换字体后重新构建即可生效。
func (u *Updater) provisionLocal(resourceDir string, progress ProgressFunc) (ResourceStatus, error) {
	var st ResourceStatus
	progress("verify", "校验本地资源", 0, 0)
	csvDir := filepath.Join(resourceDir, "rawexd")
	if !dirExists(csvDir) {
		return st, fmt.Errorf("本地开发构建不下载云端资源，但 %s 不存在", csvDir)
	}
	if entries, err := os.ReadDir(csvDir); err != nil || len(entries) == 0 {
		return st, fmt.Errorf("本地开发构建不下载云端资源，但 %s 为空或不可读", csvDir)
	}
	tablesPath := filepath.Join(resourceDir, "tables.json")
	data, err := os.ReadFile(tablesPath)
	if err != nil {
		return st, fmt.Errorf("本地开发构建不下载云端资源，但缺少 %s: %w", tablesPath, err)
	}
	if !json.Valid(data) {
		return st, fmt.Errorf("本地开发构建不下载云端资源，但 %s 不是合法 JSON", tablesPath)
	}
	if _, err := os.Stat(filepath.Join(resourceDir, "rules.json")); err != nil {
		return st, fmt.Errorf("本地开发构建不下载云端资源，但缺少 rules.json: %w", err)
	}
	progress("done", "使用本地资源", 1, 1)
	return st, nil
}

// EnsureFontPack 按需准备预编译字体包，只在字体来源选「内置字体包」时调用。
// 本地已有且校验通过时直接返回，不联网。
func (u *Updater) EnsureFontPack(ctx context.Context, resourceDir string, progress ProgressFunc) error {
	if progress == nil {
		progress = func(string, string, int64, int64) {}
	}
	progress = throttleProgress(progress)

	if u.LocalOnly {
		fontDir := filepath.Join(resourceDir, "font")
		if !dirExists(fontDir) {
			return fmt.Errorf("本地开发构建不下载云端字体包，但 %s 不存在", fontDir)
		}
		return nil
	}

	remote, err := u.FetchManifest(ctx)
	if err != nil {
		return fmt.Errorf("获取云端资源清单失败: %w", err)
	}
	if remote.Font.SHA256 == "" {
		return errors.New("云端清单里没有字体包")
	}
	if bundleDirMatches(resourceDir, "font.zip", remote.Font) {
		return nil
	}
	if zipMatches(resourceDir, "font.zip", remote.Font) {
		// 包本身可信，只是解压目录缺失或被改动：重新解压即可。
		progress("font", "解压字体包", 0, 0)
		if err := ExtractZip(filepath.Join(resourceDir, "font.zip"), resourceDir); err != nil {
			return fmt.Errorf("解压 font.zip 失败: %w", err)
		}
		return nil
	}
	progress("font", "下载字体包", 0, 0)
	if err := u.downloadZip(ctx, resourceDir, "font.zip", remote.Font, true, func(done, total int64) {
		progress("font", "下载字体包", done, total)
	}); err != nil {
		return err
	}
	return nil
}

// SyncTableDoc 只同步 tables.json，用于首次运行或表清单缺失时，
// 避免为了列出可汉化数据表去下载整个快照包。云端清单必须可获取，不做离线兜底。
func (u *Updater) SyncTableDoc(ctx context.Context, resourceDir string) (bool, error) {
	if u.LocalOnly {
		dst := filepath.Join(resourceDir, "tables.json")
		data, err := os.ReadFile(dst)
		if err != nil {
			return false, fmt.Errorf("本地开发构建不下载云端资源，但缺少 %s: %w", dst, err)
		}
		if !json.Valid(data) {
			return false, fmt.Errorf("本地开发构建不下载云端资源，但 %s 不是合法 JSON", dst)
		}
		return false, nil
	}
	if err := os.MkdirAll(resourceDir, 0o755); err != nil {
		return false, fmt.Errorf("创建资源目录失败: %w", err)
	}
	remote, err := u.FetchManifest(ctx)
	if err != nil {
		return false, fmt.Errorf("获取云端资源清单失败: %w", err)
	}
	dst := filepath.Join(resourceDir, "tables.json")
	if got, err := HashFile(dst); err == nil && got.SHA256 == remote.Tables.SHA256 &&
		(remote.Tables.Size <= 0 || got.Size == remote.Tables.Size) {
		return false, nil
	}
	if err := u.downloadVerified(ctx, "tables.json", dst, FileInfo{SHA256: remote.Tables.SHA256, Size: remote.Tables.Size}, nil); err != nil {
		return false, err
	}
	return true, nil
}

func shortHash(h string) string {
	if len(h) > 8 {
		return h[:8]
	}
	return h
}

func (u *Updater) downloadZip(ctx context.Context, resourceDir, zipName string, want BundleInfo, keep bool, progress func(done, total int64)) error {
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
			lastErr = fmt.Errorf("计算 %s 哈希失败: %w", tmp, err)
			_ = os.Remove(tmp)
			continue
		}
		if got.SHA256 != want.SHA256 || (want.Size > 0 && got.Size != want.Size) {
			lastErr = fmt.Errorf("%s 校验失败（节点 %s，期望 SHA256=%s 大小=%d，实际 SHA256=%s 大小=%d）",
				zipName, node, shortHash(want.SHA256), want.Size, shortHash(got.SHA256), got.Size)
			_ = os.Remove(tmp)
			continue
		}
		zipPath := tmp
		if keep {
			// 保留快照包，后续校验解压目录时作为可信来源。
			zipPath = filepath.Join(resourceDir, zipName)
			_ = os.Remove(zipPath)
			if err := os.Rename(tmp, zipPath); err != nil {
				lastErr = fmt.Errorf("保存 %s 失败: %w", zipName, err)
				_ = os.Remove(tmp)
				continue
			}
		}
		if err := ExtractZip(zipPath, resourceDir); err != nil {
			lastErr = fmt.Errorf("解压 %s 失败: %w", zipName, err)
			_ = os.Remove(tmp)
			continue
		}
		if !keep {
			_ = os.Remove(tmp)
		}
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
			lastErr = fmt.Errorf("计算 %s 哈希失败: %w", tmp, err)
			_ = os.Remove(tmp)
			continue
		}
		if got.SHA256 != want.SHA256 || (want.Size > 0 && got.Size != want.Size) {
			lastErr = fmt.Errorf("%s 校验失败（节点 %s，期望 SHA256=%s 大小=%d，实际 SHA256=%s 大小=%d）",
				key, node, shortHash(want.SHA256), want.Size, shortHash(got.SHA256), got.Size)
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
				return fmt.Errorf("写入文件 %s 失败: %w", dst, err)
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
			return fmt.Errorf("读取下载流中断 (%s): %w", url, readErr)
		}
	}
	return out.Close()
}

func dirExists(path string) bool {
	st, err := os.Stat(path)
	return err == nil && st.IsDir()
}

func metaModulesComplete(resourceDir string, local, remote *Manifest, csvOK bool) bool {
	if local == nil || remote == nil || !csvOK {
		return false
	}
	for _, name := range []string{"manifest.json", "rules.json", "tables.json"} {
		if _, err := os.Stat(filepath.Join(resourceDir, name)); err != nil {
			return false
		}
	}
	if local.Rules.SHA256 != remote.Rules.SHA256 {
		return false
	}
	if local.Tables.SHA256 != remote.Tables.SHA256 {
		return false
	}
	return true
}

// modulesComplete 判断 CSV 快照、规则与表清单是否都与云端一致。
func modulesComplete(resourceDir string, local, remote *Manifest) bool {
	if local == nil || remote == nil {
		return false
	}
	csvOK := local.RawEXD.SHA256 == remote.RawEXD.SHA256 &&
		bundleDirMatches(resourceDir, "rawexd.zip", remote.RawEXD)
	return metaModulesComplete(resourceDir, local, remote, csvOK)
}

// ReadLocalManifest 读取本地资源目录中的 manifest.json。
func ReadLocalManifest(resourceDir string) (*Manifest, error) {
	data, err := os.ReadFile(filepath.Join(resourceDir, "manifest.json"))
	if err != nil {
		return nil, err
	}
	var m Manifest
	if err := json.Unmarshal(data, &m); err != nil {
		return nil, err
	}
	return &m, nil
}

// HasLocalResource 检查本地是否已有可运行的必要基础资源。
// manifest.json 只是同步元数据，本地开发构建本来就没有，不参与可用性判定。
func HasLocalResource(resourceDir string) bool {
	if _, err := os.Stat(filepath.Join(resourceDir, "tables.json")); err != nil {
		return false
	}
	return dirExists(filepath.Join(resourceDir, "rawexd"))
}
