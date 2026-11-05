package updater

import (
	"archive/zip"
	"fmt"
	"hash/crc32"
	"io"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"sync"
	"sync/atomic"
)

// VerifyZipBundle 校验资源包自身哈希，并核对解压目录与包内每个文件的大小和 CRC32。
// 信任链是 云端清单 -> 包 SHA256 -> 包内 CRC32 -> 解压文件，因此解压目录被改动也能发现。
func VerifyZipBundle(resourceDir, zipName string, want BundleInfo) error {
	if want.SHA256 == "" {
		return fmt.Errorf("%s 缺少清单哈希，无法校验", zipName)
	}
	zipPath := filepath.Join(resourceDir, zipName)
	got, err := HashFile(zipPath)
	if err != nil {
		return fmt.Errorf("读取 %s 失败: %w", zipName, err)
	}
	if got.SHA256 != want.SHA256 || (want.Size > 0 && got.Size != want.Size) {
		return fmt.Errorf("%s 哈希不符（期望 %s，实际 %s）", zipName, shortHash(want.SHA256), shortHash(got.SHA256))
	}
	return verifyExtractedDir(resourceDir, zipPath)
}

// zipMatches 只校验包文件自身哈希，解压内容是否一致由 dirMatchesZip 判断。
func zipMatches(resourceDir, zipName string, want BundleInfo) bool {
	if want.SHA256 == "" {
		return false
	}
	got, err := HashFile(filepath.Join(resourceDir, zipName))
	return err == nil && got.SHA256 == want.SHA256 && (want.Size <= 0 || got.Size == want.Size)
}

// bundleDirMatches 校验包哈希以及解压目录与包内条目是否完全一致。
func bundleDirMatches(resourceDir, zipName string, want BundleInfo) bool {
	return bundleDirMatchesWithProgress(resourceDir, zipName, want, nil)
}

// bundleDirMatchesWithProgress 同上，逐文件核对时回调已完成数量。
func bundleDirMatchesWithProgress(resourceDir, zipName string, want BundleInfo, progress FileProgress) bool {
	if !zipMatches(resourceDir, zipName, want) {
		return false
	}
	return verifyExtractedDirWithProgress(resourceDir, filepath.Join(resourceDir, zipName), progress) == nil
}

// dirMatchesZip 校验解压目录与包内条目一一对应，返回首个不一致项。
func dirMatchesZip(resourceDir, zipName string) error {
	zipPath := filepath.Join(resourceDir, zipName)
	if _, err := os.Stat(zipPath); err != nil {
		return fmt.Errorf("缺少 %s: %w", zipName, err)
	}
	return verifyExtractedDir(resourceDir, zipPath)
}

func verifyExtractedDir(resourceDir, zipPath string) error {
	return verifyExtractedDirWithProgress(resourceDir, zipPath, nil)
}

func verifyExtractedDirWithProgress(resourceDir, zipPath string, progress FileProgress) error {
	reader, err := zip.OpenReader(zipPath)
	if err != nil {
		return fmt.Errorf("打开 %s 失败: %w", filepath.Base(zipPath), err)
	}
	defer reader.Close()
	cleanDir := filepath.Clean(resourceDir)
	files := make([]*zip.File, 0, len(reader.File))
	for _, f := range reader.File {
		if f.FileInfo().IsDir() {
			continue
		}
		target := filepath.Join(cleanDir, filepath.FromSlash(f.Name))
		if !strings.HasPrefix(filepath.Clean(target), cleanDir+string(os.PathSeparator)) {
			return fmt.Errorf("%s 包含非法路径: %s", filepath.Base(zipPath), f.Name)
		}
		files = append(files, f)
	}
	return verifyFilesConcurrently(cleanDir, files, progress)
}

// verifyFilesConcurrently 并发核对解压文件，7114 个 CSV 的小文件读取开销靠并发摊掉。
func verifyFilesConcurrently(cleanDir string, files []*zip.File, progress FileProgress) error {
	if len(files) == 0 {
		return nil
	}
	total := int64(len(files))
	if progress != nil {
		progress(0, total)
	}
	workers := runtime.GOMAXPROCS(0)
	if workers > 8 {
		workers = 8
	}
	if workers > len(files) {
		workers = len(files)
	}
	var (
		wg       sync.WaitGroup
		mu       sync.Mutex
		firstErr error
		idx      atomic.Int64
		done     atomic.Int64
	)
	for i := 0; i < workers; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for {
				n := int(idx.Add(1)) - 1
				if n >= len(files) {
					return
				}
				mu.Lock()
				failed := firstErr != nil
				mu.Unlock()
				if failed {
					return
				}
				target := filepath.Join(cleanDir, filepath.FromSlash(files[n].Name))
				if err := verifyExtractedFile(target, files[n]); err != nil {
					mu.Lock()
					if firstErr == nil {
						firstErr = err
					}
					mu.Unlock()
					return
				}
				if progress != nil {
					progress(done.Add(1), total)
				}
			}
		}()
	}
	wg.Wait()
	return firstErr
}

func verifyExtractedFile(target string, f *zip.File) error {
	st, err := os.Stat(target)
	if err != nil {
		return fmt.Errorf("解压目录缺少 %s", f.Name)
	}
	if st.IsDir() || uint64(st.Size()) != f.UncompressedSize64 {
		return fmt.Errorf("%s 大小不符（期望 %d，实际 %d）", f.Name, f.UncompressedSize64, st.Size())
	}
	fh, err := os.Open(target)
	if err != nil {
		return fmt.Errorf("读取 %s 失败: %w", target, err)
	}
	defer fh.Close()
	h := crc32.NewIEEE()
	n, err := io.Copy(h, fh)
	if err != nil {
		return fmt.Errorf("校验 %s 失败: %w", target, err)
	}
	if uint64(n) != f.UncompressedSize64 || h.Sum32() != f.CRC32 {
		return fmt.Errorf("%s 内容与资源包不符", f.Name)
	}
	return nil
}
