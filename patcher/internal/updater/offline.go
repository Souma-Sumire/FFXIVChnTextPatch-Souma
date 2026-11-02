package updater

import (
	"archive/zip"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"
)

// ImportSummary 记录离线包导入结果。
type ImportSummary struct {
	Success      bool   `json:"success"`
	TableCount   int    `json:"tableCount"`
	CSVCount     int    `json:"csvCount"`
	FontCount    int    `json:"fontCount"`
	ResourceTime string `json:"resourceTime"`
	ResourceHash string `json:"resourceHash"`
}

// ImportOffline 将本地离线资源包（.zip 文件或已解压目录）导入到目标资源目录中。
func ImportOffline(srcPath, destResourceDir string) (*ImportSummary, error) {
	if srcPath == "" {
		return nil, errors.New("离线包路径不能为空")
	}
	st, err := os.Stat(srcPath)
	if err != nil {
		return nil, fmt.Errorf("无法访问离线包: %w", err)
	}

	if err := os.MkdirAll(destResourceDir, 0o755); err != nil {
		return nil, fmt.Errorf("创建目标资源目录失败: %w", err)
	}

	if st.IsDir() {
		if err := copyOfflineDir(srcPath, destResourceDir); err != nil {
			return nil, err
		}
	} else {
		if err := extractOfflineZip(srcPath, destResourceDir); err != nil {
			return nil, err
		}
	}

	// 保证嵌套 zip（rawexd.zip / font.zip）解压完成
	rawexdZip := filepath.Join(destResourceDir, "rawexd.zip")
	rawexdDir := filepath.Join(destResourceDir, "rawexd")
	if fileExists(rawexdZip) && (!dirExists(rawexdDir) || countFilesWithExt(rawexdDir, ".csv") == 0) {
		if err := ExtractZip(rawexdZip, destResourceDir); err != nil {
			return nil, fmt.Errorf("解压 rawexd.zip 失败: %w", err)
		}
	}

	fontZip := filepath.Join(destResourceDir, "font.zip")
	fontDir := filepath.Join(destResourceDir, "font")
	if fileExists(fontZip) && (!dirExists(fontDir) || countFiles(fontDir) == 0) {
		if err := ExtractZip(fontZip, destResourceDir); err != nil {
			return nil, fmt.Errorf("解压 font.zip 失败: %w", err)
		}
	}

	csvCount := countFilesWithExt(rawexdDir, ".csv")
	if csvCount == 0 {
		return nil, errors.New("离线资源包未包含有效的游戏词条数据（rawexd 目录下缺少 .csv 文件）")
	}

	tablesPath := filepath.Join(destResourceDir, "tables.json")
	if !fileExists(tablesPath) {
		if err := generateFallbackTablesJSON(rawexdDir, tablesPath); err != nil {
			return nil, fmt.Errorf("生成默认 tables.json 失败: %w", err)
		}
	}

	manifestPath := filepath.Join(destResourceDir, "manifest.json")
	if !fileExists(manifestPath) {
		if err := generateFallbackManifest(destResourceDir); err != nil {
			return nil, fmt.Errorf("生成本地 manifest.json 失败: %w", err)
		}
	}

	tableCount := countTables(tablesPath)
	if tableCount == 0 {
		tableCount = csvCount
	}

	var resTime, resHash string
	if mf, err := ReadLocalManifest(destResourceDir); err == nil && mf != nil {
		resTime = mf.GeneratedAt
		resHash = mf.RawEXD.SHA256
	}

	return &ImportSummary{
		Success:      true,
		TableCount:   tableCount,
		CSVCount:     csvCount,
		FontCount:    countFiles(fontDir),
		ResourceTime: resTime,
		ResourceHash: resHash,
	}, nil
}

func extractOfflineZip(zipPath, destDir string) error {
	reader, err := zip.OpenReader(zipPath)
	if err != nil {
		return fmt.Errorf("读取压缩包失败: %w", err)
	}
	defer reader.Close()

	basePrefix := detectZipBasePrefix(reader.File)
	cleanDest := filepath.Clean(destDir)

	for _, f := range reader.File {
		name := filepath.ToSlash(f.Name)
		if !strings.HasPrefix(name, basePrefix) {
			continue
		}
		relPath := strings.TrimPrefix(name, basePrefix)
		relPath = strings.TrimPrefix(relPath, "/")
		if relPath == "" {
			continue
		}

		target := filepath.Join(cleanDest, filepath.FromSlash(relPath))
		if !strings.HasPrefix(filepath.Clean(target), cleanDest+string(os.PathSeparator)) && filepath.Clean(target) != cleanDest {
			return fmt.Errorf("压缩包包含非法路径: %s", f.Name)
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

		rc, err := f.Open()
		if err != nil {
			return err
		}

		dstFile, err := os.OpenFile(target, os.O_CREATE|os.O_TRUNC|os.O_WRONLY, 0o644)
		if err != nil {
			rc.Close()
			return err
		}
		_, copyErr := io.Copy(dstFile, rc)
		dstFile.Close()
		rc.Close()
		if copyErr != nil {
			return copyErr
		}

		if relPath == "rawexd.zip" {
			_ = ExtractZip(target, cleanDest)
		} else if relPath == "font.zip" {
			_ = ExtractZip(target, cleanDest)
		}
	}

	return nil
}

func detectZipBasePrefix(files []*zip.File) string {
	for _, f := range files {
		name := filepath.ToSlash(f.Name)
		if strings.HasSuffix(name, "manifest.json") {
			return name[:strings.LastIndex(name, "manifest.json")]
		}
	}
	for _, f := range files {
		name := filepath.ToSlash(f.Name)
		if strings.HasSuffix(name, "tables.json") {
			return name[:strings.LastIndex(name, "tables.json")]
		}
	}
	for _, f := range files {
		name := filepath.ToSlash(f.Name)
		if strings.HasSuffix(name, "rawexd.zip") {
			return name[:strings.LastIndex(name, "rawexd.zip")]
		}
	}
	for _, f := range files {
		name := filepath.ToSlash(f.Name)
		if strings.Contains(name, "rawexd/") {
			idx := strings.Index(name, "rawexd/")
			return name[:idx]
		}
	}
	return ""
}

func copyOfflineDir(srcDir, destDir string) error {
	baseDir := srcDir
	if sub := filepath.Join(srcDir, "resource"); dirExists(sub) {
		baseDir = sub
	}

	return filepath.Walk(baseDir, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		rel, err := filepath.Rel(baseDir, path)
		if err != nil || rel == "." {
			return nil
		}
		target := filepath.Join(destDir, rel)
		if info.IsDir() {
			return os.MkdirAll(target, 0o755)
		}
		return copyFile(path, target)
	})
}

func copyFile(src, dst string) error {
	if err := os.MkdirAll(filepath.Dir(dst), 0o755); err != nil {
		return err
	}
	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()

	out, err := os.Create(dst)
	if err != nil {
		return err
	}
	defer out.Close()

	_, err = io.Copy(out, in)
	return err
}

func fileExists(p string) bool {
	st, err := os.Stat(p)
	return err == nil && !st.IsDir()
}

func countFilesWithExt(dir, ext string) int {
	if !dirExists(dir) {
		return 0
	}
	count := 0
	_ = filepath.Walk(dir, func(path string, info os.FileInfo, err error) error {
		if err == nil && !info.IsDir() && strings.HasSuffix(strings.ToLower(info.Name()), ext) {
			count++
		}
		return nil
	})
	return count
}

func countFiles(dir string) int {
	if !dirExists(dir) {
		return 0
	}
	count := 0
	_ = filepath.Walk(dir, func(path string, info os.FileInfo, err error) error {
		if err == nil && !info.IsDir() {
			count++
		}
		return nil
	})
	return count
}

func countTables(tablesPath string) int {
	data, err := os.ReadFile(tablesPath)
	if err != nil {
		return 0
	}
	var doc struct {
		Files  []string          `json:"files"`
		Tables map[string]string `json:"tables"`
	}
	if err := json.Unmarshal(data, &doc); err != nil {
		return 0
	}
	if len(doc.Files) > 0 {
		return len(doc.Files)
	}
	return len(doc.Tables)
}

func generateFallbackTablesJSON(rawexdDir, targetPath string) error {
	files := make([]string, 0, 100)
	folders := make(map[string]string)

	_ = filepath.Walk(rawexdDir, func(path string, info os.FileInfo, err error) error {
		if err != nil || info.IsDir() || !strings.HasSuffix(strings.ToLower(info.Name()), ".csv") {
			return nil
		}
		rel, err := filepath.Rel(rawexdDir, path)
		if err != nil {
			return nil
		}
		rel = filepath.ToSlash(rel)
		key := strings.TrimSuffix(rel, ".csv")
		files = append(files, key)
		if i := strings.Index(key, "/"); i > 0 {
			folder := key[:i]
			folders[folder] = folder
		}
		return nil
	})

	sort.Strings(files)
	doc := map[string]any{
		"folders": folders,
		"tables":  map[string]string{},
		"files":   files,
	}

	data, err := json.MarshalIndent(doc, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(targetPath, append(data, '\n'), 0o644)
}

func generateFallbackManifest(resourceDir string) error {
	now := time.Now().UTC().Format(time.RFC3339)
	rawexdZip := filepath.Join(resourceDir, "rawexd.zip")

	var rawHash string
	var rawSize int64
	if fileExists(rawexdZip) {
		if fh, err := HashFile(rawexdZip); err == nil {
			rawHash = fh.SHA256
			rawSize = fh.Size
		}
	}
	if rawHash == "" {
		h := sha256.New()
		h.Write([]byte(now))
		rawHash = hex.EncodeToString(h.Sum(nil))
	}

	mf := Manifest{
		GeneratedAt: now,
		RootHash:    rawHash,
		RawEXD: BundleInfo{
			SHA256: rawHash,
			Size:   rawSize,
		},
		Fonts: map[string]FileInfo{},
	}

	data, err := json.MarshalIndent(mf, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(filepath.Join(resourceDir, "manifest.json"), append(data, '\n'), 0o644)
}
