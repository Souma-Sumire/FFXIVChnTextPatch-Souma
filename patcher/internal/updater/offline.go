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
// 包先展开到目标同卷的临时目录并校验，全部通过后才整体切换，损坏的包不会污染现有资源。
func ImportOffline(srcPath, destResourceDir string) (*ImportSummary, error) {
	if srcPath == "" {
		return nil, errors.New("离线包路径不能为空")
	}
	st, err := os.Stat(srcPath)
	if err != nil {
		return nil, fmt.Errorf("无法访问离线包: %w", err)
	}

	destResourceDir = filepath.Clean(destResourceDir)
	parent := filepath.Dir(destResourceDir)
	if err := os.MkdirAll(parent, 0o755); err != nil {
		return nil, fmt.Errorf("创建目标资源目录失败: %w", err)
	}

	staging, err := os.MkdirTemp(parent, ".resource-import-")
	if err != nil {
		return nil, fmt.Errorf("创建临时目录失败: %w", err)
	}
	defer func() {
		if staging != "" {
			_ = os.RemoveAll(staging)
		}
	}()

	if st.IsDir() {
		err = copyOfflineDir(srcPath, staging)
	} else {
		err = extractOfflineZip(srcPath, staging)
	}
	if err != nil {
		return nil, err
	}

	if err := extractNestedZips(staging); err != nil {
		return nil, err
	}
	if err := validateStagedResource(staging); err != nil {
		return nil, err
	}

	rawexdDir := filepath.Join(staging, "rawexd")
	csvCount := countFilesWithExt(rawexdDir, ".csv")

	tablesPath := filepath.Join(staging, "tables.json")
	if !fileExists(tablesPath) {
		if err := generateFallbackTablesJSON(rawexdDir, tablesPath); err != nil {
			return nil, fmt.Errorf("生成默认 tables.json 失败: %w", err)
		}
	}

	if !fileExists(filepath.Join(staging, "manifest.json")) {
		if err := generateFallbackManifest(staging); err != nil {
			return nil, fmt.Errorf("生成本地 manifest.json 失败: %w", err)
		}
	}

	if err := replaceResourceDir(staging, destResourceDir); err != nil {
		return nil, err
	}
	staging = ""

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
		FontCount:    countFiles(filepath.Join(destResourceDir, "font")),
		ResourceTime: resTime,
		ResourceHash: resHash,
	}, nil
}

// extractNestedZips 展开离线包自带的 rawexd.zip / font.zip。
func extractNestedZips(dir string) error {
	for _, name := range []string{"rawexd.zip", "font.zip"} {
		zipPath := filepath.Join(dir, name)
		if !fileExists(zipPath) {
			continue
		}
		if err := ExtractZip(zipPath, dir); err != nil {
			return fmt.Errorf("解压 %s 失败: %w", name, err)
		}
	}
	return nil
}

// validateStagedResource 在切换前确认暂存目录是一套可用资源：
// 至少有一个词条 CSV，且包内自带的 zip 与清单声明的哈希一致。
func validateStagedResource(dir string) error {
	if countFilesWithExt(filepath.Join(dir, "rawexd"), ".csv") == 0 {
		return errors.New("离线资源包未包含有效的游戏词条数据（rawexd 目录下缺少 .csv 文件）")
	}

	mf, err := ReadLocalManifest(dir)
	if err != nil {
		if os.IsNotExist(err) {
			return nil
		}
		return fmt.Errorf("离线包内的 manifest.json 无效: %w", err)
	}

	checks := []struct {
		name string
		want BundleInfo
	}{
		{"rawexd.zip", mf.RawEXD},
		{"font.zip", mf.Font},
	}
	for _, c := range checks {
		path := filepath.Join(dir, c.name)
		if c.want.SHA256 == "" || !fileExists(path) {
			continue
		}
		got, err := HashFile(path)
		if err != nil {
			return fmt.Errorf("校验 %s 失败: %w", c.name, err)
		}
		if got.SHA256 != c.want.SHA256 || (c.want.Size > 0 && got.Size != c.want.Size) {
			return fmt.Errorf("离线包内 %s 与清单声明的哈希不一致，包可能已损坏", c.name)
		}
	}
	return nil
}

// replaceResourceDir 用暂存目录整体替换资源目录。
// 旧目录里存在、新目录里没有的文件（如离线包未携带的 rules.json）会被保留。
func replaceResourceDir(staging, dest string) error {
	if _, err := os.Stat(dest); err != nil {
		if !os.IsNotExist(err) {
			return err
		}
		return os.Rename(staging, dest)
	}

	backup := fmt.Sprintf("%s.old-%d", dest, time.Now().UnixNano())
	if err := os.Rename(dest, backup); err != nil {
		return fmt.Errorf("切换资源目录失败: %w", err)
	}
	if err := os.Rename(staging, dest); err != nil {
		_ = os.Rename(backup, dest)
		return fmt.Errorf("切换资源目录失败: %w", err)
	}
	if err := copyMissingFiles(backup, dest); err != nil {
		_ = os.RemoveAll(dest)
		_ = os.Rename(backup, dest)
		return fmt.Errorf("合并旧资源失败: %w", err)
	}
	_ = os.RemoveAll(backup)
	return nil
}

// copyMissingFiles 把 src 中 dst 缺失的文件补到 dst，dst 已有的文件不覆盖。
func copyMissingFiles(src, dst string) error {
	return filepath.Walk(src, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		rel, relErr := filepath.Rel(src, path)
		if relErr != nil || rel == "." {
			return nil
		}
		target := filepath.Join(dst, rel)
		if info.IsDir() {
			if !dirExists(target) {
				return os.MkdirAll(target, 0o755)
			}
			return nil
		}
		if fileExists(target) {
			return nil
		}
		return copyFile(path, target)
	})
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

	tmp := dst + ".part"
	out, err := os.Create(tmp)
	if err != nil {
		return err
	}
	if _, err := io.Copy(out, in); err != nil {
		_ = out.Close()
		_ = os.Remove(tmp)
		return err
	}
	if err := out.Close(); err != nil {
		_ = os.Remove(tmp)
		return err
	}
	if err := os.Rename(tmp, dst); err != nil {
		_ = os.Remove(tmp)
		return err
	}
	return nil
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
	return writeFileAtomic(targetPath, append(data, '\n'))
}

func generateFallbackManifest(resourceDir string) error {
	rawexdZip := filepath.Join(resourceDir, "rawexd.zip")
	fontZip := filepath.Join(resourceDir, "font.zip")

	var rawHash, fontHash string
	var rawSize, fontSize int64
	if fileExists(rawexdZip) {
		if fh, err := HashFile(rawexdZip); err == nil {
			rawHash = fh.SHA256
			rawSize = fh.Size
		}
	}
	if fileExists(fontZip) {
		if fh, err := HashFile(fontZip); err == nil {
			fontHash = fh.SHA256
			fontSize = fh.Size
		}
	}

	// 缺少 .zip 包文件（只放了已解压目录）时无法还原发布清单里的 bundle 哈希，
	// 退化为按文件内容计算的本地指纹，仅用于展示与区分，不参与云端比对。
	var fingerprint string
	if rawHash == "" || fontHash == "" {
		fingerprint = resourceFingerprint(resourceDir)
	}

	mf := Manifest{
		GeneratedAt: "",
		RootHash:    rawHash,
		RawEXD: BundleInfo{
			SHA256: rawHash,
			Size:   rawSize,
		},
		Font: BundleInfo{
			SHA256: fontHash,
			Size:   fontSize,
		},
		LocalFingerprint: fingerprint,
		Fonts:            map[string]FileInfo{},
	}

	data, err := json.MarshalIndent(mf, "", "  ")
	if err != nil {
		return err
	}
	return writeFileAtomic(filepath.Join(resourceDir, "manifest.json"), append(data, '\n'))
}

// resourceFingerprint 按“相对路径:文件哈希”逐行排序后计算 rawexd 与 font 的内容指纹。
func resourceFingerprint(resourceDir string) string {
	h := sha256.New()
	for _, sub := range []string{"rawexd", "font"} {
		root := filepath.Join(resourceDir, sub)
		if !dirExists(root) {
			continue
		}
		var files []string
		_ = filepath.WalkDir(root, func(p string, d os.DirEntry, err error) error {
			if err != nil || d.IsDir() {
				return nil
			}
			if rel, relErr := filepath.Rel(resourceDir, p); relErr == nil {
				files = append(files, filepath.ToSlash(rel))
			}
			return nil
		})
		sort.Strings(files)
		for _, rel := range files {
			fh, err := HashFile(filepath.Join(resourceDir, filepath.FromSlash(rel)))
			if err != nil {
				continue
			}
			fmt.Fprintf(h, "%s:%s\n", rel, fh.SHA256)
		}
	}
	return hex.EncodeToString(h.Sum(nil))
}
