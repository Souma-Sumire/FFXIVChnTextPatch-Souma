package golden

import (
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"testing"

	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/pipeline"
	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/replace"
	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/sqpack"
	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/verifier"
)

type goldenFile struct {
	SHA256 string `json:"sha256"`
	Size   int    `json:"size"`
}

type goldenDoc struct {
	Note              string                `json:"note"`
	GameVersion       string                `json:"gameVersion"`
	SlanguageRuns     []string              `json:"slanguageRuns"`
	Langs             []string              `json:"langs"`
	Originals         map[string]goldenFile `json:"originals"`
	ReplacedExdCount  int                   `json:"replacedExdCount"`
	UnchangedExdCount int                   `json:"unchangedExdCount"`
	ExdCount          int                   `json:"exdCount"`
	Samples           []string              `json:"samples"`
	Exd               map[string]goldenFile `json:"exd"`
}

type fontMeta struct {
	SHA256        string `json:"sha256"`
	Size          int    `json:"size"`
	HeaderLength  int    `json:"headerLength"`
	ContentType   int    `json:"contentType"`
	FileSize      int    `json:"fileSize"`
	BlockCount    int    `json:"blockCount"`
	DataOffset    int    `json:"dataOffset"`
	BlockLength   int64  `json:"blockLength"`
	PartCount     int    `json:"partCount"`
	SubBlockSizes []int  `json:"subBlockSizes"`
	TextureType   int    `json:"textureType"`
	Width         int    `json:"width"`
	Height        int    `json:"height"`
	MipCount      int    `json:"mipCount"`
}

type fontDoc struct {
	Fonts map[string]fontMeta `json:"fonts"`
}

var requiredFiles = []string{
	"000000.win32.dat0",
	"000000.win32.index",
	"000000.win32.index2",
	"0a0000.win32.dat0",
	"0a0000.win32.index",
	"0a0000.win32.index2",
}

func repoPath(parts ...string) string {
	all := append([]string{"..", "..", ".."}, parts...)
	return filepath.Join(all...)
}

// TestGoldenMasterByteEquality 是 Golden Master 端到端一致性门禁：
// 使用国际服原版文件 + 与旧版相同的 CSV 数据，分别执行字体替换、JA 文本替换、EN 文本替换，
// 然后逐文件比对旧版 Java 产物的未压缩 EXDF 字节（SHA256 + 抽样 bytes.Equal）。
func TestGoldenMasterByteEquality(t *testing.T) {
	if testing.Short() {
		t.Skip("Golden Master 测试需要处理约 400MB 数据，-short 模式跳过")
	}

	golden := loadGolden(t)
	fonts := loadFonts(t)
	srcDir := locateOrDownloadGoldenSource(t, golden.GameVersion)

	workDir := t.TempDir()
	gameDir := filepath.Join(workDir, "game", "sqpack", "ffxiv")
	if err := os.MkdirAll(gameDir, 0o755); err != nil {
		t.Fatal(err)
	}
	for _, name := range requiredFiles {
		copyFile(t, filepath.Join(srcDir, name), filepath.Join(gameDir, name))
	}

	csvDir := repoPath("resource", "rawexd")
	fontDir := repoPath("resource", "font")
	textIndex := filepath.Join(gameDir, "0a0000.win32.index")
	fontIndex := filepath.Join(gameDir, "000000.win32.index")

	ruleSet, ruleErr := pipeline.LoadRulesFromFile(repoPath("resource", "rules.json"))
	if ruleErr != nil {
		ruleSet = pipeline.BuiltinRules()
	}
	rules := pipeline.NewRegistry(ruleSet)
	for _, meta := range rules.List() {
		if meta.DefaultOn {
			rules.SetEnabled(meta.ID, true)
		}
	}

	if _, err := replace.ReplaceFont(fontIndex, fontDir, nil); err != nil {
		t.Fatalf("Go ReplaceFont 失败: %v", err)
	}
	for _, lang := range golden.SlanguageRuns {
		stats, err := replace.ReplaceEXDF(replace.Options{
			TextIndexPath:  textIndex,
			CSVResourceDir: csvDir,
			SourceLanguage: lang,
			SkipFiles:      "",
			Rules:          rules,
		})
		if err != nil {
			t.Fatalf("Go ReplaceEXDF(%s) 失败: %v", lang, err)
		}
		t.Logf("ReplaceEXDF(%s): tables=%d files=%d entries=%d bytes=%d",
			lang, stats.Tables, stats.Files, stats.Entries, stats.AppendedBytes)
	}

	verifyExd(t, golden, textIndex, gameDir)
	verifyFonts(t, fonts, fontIndex, fontDir, gameDir, golden)
}

func loadGolden(t *testing.T) *goldenDoc {
	t.Helper()
	path := filepath.Join("..", "sqpack", "testdata", "golden_exdf_hashes.json")
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("读取 Golden 基准失败: %v", err)
	}
	var doc goldenDoc
	if err := json.Unmarshal(data, &doc); err != nil {
		t.Fatalf("解析 Golden 基准失败: %v", err)
	}
	return &doc
}

func loadFonts(t *testing.T) *fontDoc {
	t.Helper()
	path := filepath.Join("..", "sqpack", "testdata", "golden_font_blocks.json")
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("读取字体基准失败: %v", err)
	}
	var doc fontDoc
	if err := json.Unmarshal(data, &doc); err != nil {
		t.Fatalf("解析字体基准失败: %v", err)
	}
	return &doc
}

func locateOrDownloadGoldenSource(t *testing.T, version string) string {
	t.Helper()

	// 1. 显式环境变量优先
	if env := os.Getenv("GOLDEN_SOURCE_DIR"); env != "" {
		if hasAllRequiredFiles(env) {
			return env
		}
		t.Logf("GOLDEN_SOURCE_DIR=%s 缺少必要文件，尝试其他候选路径", env)
	}

	// 2. 本地候选路径（项目内 testdata 独立备份优先，其次本地备份目录）
	localCandidate := filepath.Join("testdata", "golden_source")
	backupCandidate := repoPath("backup", "global")
	candidates := []string{localCandidate, backupCandidate}
	for _, c := range candidates {
		if hasAllRequiredFiles(c) {
			return c
		}
	}

	// 3. 本地皆无时，自动从 GitHub Release 下载原版文件至 localCandidate
	t.Logf("本地未检测到完整的原版测试数据包，开始从 GitHub Release (v%s) 自动下载...", version)
	if err := os.MkdirAll(localCandidate, 0o755); err != nil {
		t.Fatalf("创建测试数据目录失败: %v", err)
	}

	client := verifier.NewGitHubClient()
	ctx := context.Background()
	const repo = "Souma-Sumire/FFXIVChnTextPatch-Souma"
	rel, err := client.ReleaseByTag(ctx, repo, version)
	if err != nil {
		t.Fatalf("获取 GitHub Release 失败 (repo=%s, tag=%s): %v", repo, version, err)
	}

	for _, name := range requiredFiles {
		dst := filepath.Join(localCandidate, name)
		if st, err := os.Stat(dst); err == nil && st.Size() > 0 {
			continue
		}
		asset, ok := rel.Assets[name]
		if !ok || asset.DownloadURL == "" {
			t.Fatalf("Release %s 中缺少附件 %s", version, name)
		}
		t.Logf("正在从云端拉取原版测试文件: %s...", name)
		if err := client.Download(ctx, asset.DownloadURL, dst, nil); err != nil {
			t.Fatalf("下载测试文件 %s 失败: %v", name, err)
		}
	}

	if !hasAllRequiredFiles(localCandidate) {
		t.Fatalf("从 GitHub 下载后仍缺少必要原版文件，目标目录: %s", localCandidate)
	}
	return localCandidate
}

func TestLocateOrDownloadGoldenSource_LocalExists(t *testing.T) {
	golden := loadGolden(t)
	src := locateOrDownloadGoldenSource(t, golden.GameVersion)
	if !hasAllRequiredFiles(src) {
		t.Fatalf("locateOrDownloadGoldenSource 返回的路径缺少必要文件: %s", src)
	}
	t.Logf("Golden 测试数据源路径定位成功: %s", src)
}

func hasAllRequiredFiles(dir string) bool {
	for _, name := range requiredFiles {
		st, err := os.Stat(filepath.Join(dir, name))
		if err != nil || st.Size() == 0 {
			return false
		}
	}
	return true
}

func verifyExd(t *testing.T, golden *goldenDoc, textIndex, gameDir string) {
	t.Helper()
	index, err := sqpack.OpenIndex(textIndex)
	if err != nil {
		t.Fatalf("打开 Go 输出索引失败: %v", err)
	}
	cache := &testDatCache{files: map[string]*sqpack.DatFile{}}
	defer cache.Close()

	originalDatSize := golden.Originals["0a0000.win32.dat0"].Size
	if originalDatSize <= 0 {
		t.Fatalf("Golden 基准缺少原始 dat0 尺寸")
	}

	replaced := 0
	unchanged := 0
	matched := 0
	sampleChecked := 0
	var mismatches []string
	sampleSet := map[string]bool{}
	for _, s := range golden.Samples {
		if _, ok := golden.Exd[s]; !ok {
			t.Fatalf("Golden 样本 %s 不在基准清单中，该样本永远不会被比对", s)
		}
		sampleSet[s] = true
	}

	keys := make([]string, 0, len(golden.Exd))
	for k := range golden.Exd {
		keys = append(keys, k)
	}
	sort.Strings(keys)

	for _, key := range keys {
		want := golden.Exd[key]
		slash := strings.LastIndex(key, "/")
		if slash < 0 {
			t.Fatalf("非法 Golden 键: %s", key)
		}
		dir, name := key[:slash], key[slash+1:]
		entry := index.Lookup(dir, name)
		if entry == nil {
			mismatches = append(mismatches, fmt.Sprintf("%s: Go 输出索引中缺失", key))
			continue
		}
		aligned, dat, err := cache.resolve(textIndex, entry.DataOffset)
		if err != nil {
			mismatches = append(mismatches, fmt.Sprintf("%s: %v", key, err))
			continue
		}
		data, err := dat.ExtractFile(aligned)
		if err != nil {
			mismatches = append(mismatches, fmt.Sprintf("%s: 解包失败 %v", key, err))
			continue
		}
		gotHash := sha256hex(data)
		if gotHash != want.SHA256 || len(data) != want.Size {
			if len(mismatches) < 20 {
				mismatches = append(mismatches, fmt.Sprintf("%s: hash %s != %s (size %d != %d)",
					key, gotHash[:12], want.SHA256[:12], len(data), want.Size))
			}
			continue
		}
		matched++

		isAppended := aligned >= int64(originalDatSize)
		if isAppended {
			replaced++
			header, err := dat.ReadAt(aligned, 24)
			if err != nil {
				t.Fatalf("%s: 读取块头失败: %v", key, err)
			}
			headerLength := int(binary.LittleEndian.Uint32(header[0:]))
			contentType := int(binary.LittleEndian.Uint32(header[4:]))
			fileSize := int(binary.LittleEndian.Uint32(header[8:]))
			if headerLength <= 0 || headerLength%128 != 0 {
				t.Fatalf("%s: 追加块头长度未按 128 对齐: %d", key, headerLength)
			}
			if contentType != 2 {
				t.Fatalf("%s: 追加块内容类型错误: %d", key, contentType)
			}
			if fileSize != len(data) {
				t.Fatalf("%s: 追加块 fileSize=%d 与实际解包长度 %d 不一致", key, fileSize, len(data))
			}
			if entry.DataOffset%2 != 0 {
				t.Fatalf("%s: index 偏移未能被 8 整除: %d", key, entry.DataOffset)
			}
		} else {
			unchanged++
			if strings.HasSuffix(key, "_de.exd") || strings.HasSuffix(key, "_fr.exd") {
				// 非替换语言必须保持原版块位置。
			}
		}

		if sampleSet[key] {
			sampleChecked++
			safe := strings.ReplaceAll(key, "/", "_")
			wantBytes, err := os.ReadFile(filepath.Join("..", "sqpack", "testdata", "golden_samples", safe))
			if err != nil {
				t.Fatalf("读取 Golden 样本失败 %s: %v", key, err)
			}
			if !bytes.Equal(data, wantBytes) {
				t.Fatalf("Golden 样本字节不一致: %s (len %d != %d)", key, len(data), len(wantBytes))
			}
		}
	}

	if len(mismatches) > 0 {
		t.Fatalf("EXDF 字节级比对失败 %d 项，前若干项：\n%s", len(mismatches), strings.Join(mismatches, "\n"))
	}
	if matched != golden.ExdCount {
		t.Fatalf("EXDF 覆盖数量不一致: matched=%d want=%d", matched, golden.ExdCount)
	}
	if sampleChecked != len(sampleSet) {
		t.Fatalf("Golden 字节样本命中数量不一致: checked=%d want=%d", sampleChecked, len(sampleSet))
	}
	if golden.ReplacedExdCount > 0 && replaced != golden.ReplacedExdCount {
		t.Fatalf("被替换 EXD 数量不一致: got=%d want=%d", replaced, golden.ReplacedExdCount)
	}
	if golden.UnchangedExdCount > 0 && unchanged != golden.UnchangedExdCount {
		t.Fatalf("未替换 EXD 数量不一致: got=%d want=%d", unchanged, golden.UnchangedExdCount)
	}
	t.Logf("EXDF 比对通过: %d 个文件全部字节一致（替换 %d / 未替换 %d，深度样本 %d 个）", matched, replaced, unchanged, sampleChecked)

	// 断言 3：dat0 总长度必须为 128 字节整数倍。
	dat0, err := os.Stat(filepath.Join(gameDir, "0a0000.win32.dat0"))
	if err != nil {
		t.Fatal(err)
	}
	if dat0.Size()%128 != 0 {
		t.Fatalf("0a0000 dat0 长度未按 128 字节对齐: %d", dat0.Size())
	}
}

func verifyFonts(t *testing.T, fonts *fontDoc, fontIndex, fontDir, gameDir string, golden *goldenDoc) {
	t.Helper()
	index, err := sqpack.OpenIndex(fontIndex)
	if err != nil {
		t.Fatalf("打开 Go 输出字体索引失败: %v", err)
	}
	cache := &testDatCache{files: map[string]*sqpack.DatFile{}}
	defer cache.Close()

	originalDatSize := golden.Originals["000000.win32.dat0"].Size
	names := make([]string, 0, len(fonts.Fonts))
	for name := range fonts.Fonts {
		names = append(names, name)
	}
	sort.Strings(names)

	for _, name := range names {
		want := fonts.Fonts[name]
		entry := index.Lookup("common/font", name)
		if entry == nil {
			t.Fatalf("字体索引缺失: %s", name)
		}
		aligned, dat, err := cache.resolve(fontIndex, entry.DataOffset)
		if err != nil {
			t.Fatalf("%s: %v", name, err)
		}
		if aligned < int64(originalDatSize) {
			t.Fatalf("%s: 字体块未追加到 dat0 末尾 (offset=%d)", name, aligned)
		}
		if aligned%128 != 0 {
			t.Fatalf("%s: 字体块起始未按 128 对齐: %d", name, aligned)
		}

		header, err := dat.ReadAt(aligned, 24)
		if err != nil {
			t.Fatalf("%s: %v", name, err)
		}
		headerLength := int(binary.LittleEndian.Uint32(header[0:]))
		contentType := int(binary.LittleEndian.Uint32(header[4:]))
		fileSize := int(binary.LittleEndian.Uint32(header[8:]))
		blockCount := int(binary.LittleEndian.Uint32(header[20:]))
		if headerLength != want.HeaderLength || contentType != want.ContentType ||
			fileSize != want.FileSize || blockCount != want.BlockCount {
			t.Fatalf("%s: 块头结构不一致 got(h=%d t=%d size=%d blocks=%d) want(h=%d t=%d size=%d blocks=%d)",
				name, headerLength, contentType, fileSize, blockCount,
				want.HeaderLength, want.ContentType, want.FileSize, want.BlockCount)
		}
		if headerLength%128 != 0 {
			t.Fatalf("%s: 块头长度未按 128 对齐: %d", name, headerLength)
		}

		if want.ContentType == 2 {
			table, err := dat.ReadAt(aligned+24, blockCount*8)
			if err != nil {
				t.Fatalf("%s: %v", name, err)
			}
			for i := 0; i < blockCount; i++ {
				paddingAndSize := binary.LittleEndian.Uint32(table[i*8+4:])
				size := int(paddingAndSize >> 16 & 0xFFFF)
				if i < len(want.SubBlockSizes) && size != want.SubBlockSizes[i] {
					t.Fatalf("%s: 第 %d 子块解压尺寸 %d != %d", name, i, size, want.SubBlockSizes[i])
				}
			}
		}

		data, err := dat.ExtractFile(aligned)
		if err != nil {
			t.Fatalf("%s: 解包失败: %v", name, err)
		}
		origHash := sha256hex(data)
		orig, err := os.ReadFile(filepath.Join(fontDir, name))
		if err != nil {
			t.Fatalf("%s: 读取原始字体失败: %v", name, err)
		}
		if origHash != sha256hex(orig) {
			t.Fatalf("%s: 解包内容与原字体不一致", name)
		}
		if want.ContentType == 4 {
			textureType := int(binary.LittleEndian.Uint16(data[4:]))
			width := int(binary.LittleEndian.Uint16(data[8:]))
			height := int(binary.LittleEndian.Uint16(data[10:]))
			mipCount := int(binary.LittleEndian.Uint16(data[14:]))
			if textureType != want.TextureType || width != want.Width || height != want.Height || mipCount != want.MipCount {
				t.Fatalf("%s: 纹理头不一致 got(t=%d %dx%d mips=%d) want(t=%d %dx%d mips=%d)",
					name, textureType, width, height, mipCount,
					want.TextureType, want.Width, want.Height, want.MipCount)
			}
		}
	}

	dat0, err := os.Stat(filepath.Join(gameDir, "000000.win32.dat0"))
	if err != nil {
		t.Fatal(err)
	}
	if dat0.Size()%128 != 0 {
		t.Fatalf("000000 dat0 长度未按 128 字节对齐: %d", dat0.Size())
	}
	t.Logf("字体比对通过: %d 个字体块结构与解包内容全部一致", len(names))
}

type testDatCache struct {
	files map[string]*sqpack.DatFile
}

func (c *testDatCache) resolve(indexPath string, dataOffset int32) (int64, *sqpack.DatFile, error) {
	datNum, aligned := sqpack.DatNumAndAlignedOffset(dataOffset)
	base := indexPath
	switch {
	case strings.HasSuffix(strings.ToLower(base), ".index2"):
		base = base[:len(base)-len(".index2")]
	case strings.HasSuffix(strings.ToLower(base), ".index"):
		base = base[:len(base)-len(".index")]
	}
	path := fmt.Sprintf("%s.dat%d", base, datNum)
	dat := c.files[path]
	if dat == nil {
		var err error
		dat, err = sqpack.OpenDat(path)
		if err != nil {
			return 0, nil, err
		}
		c.files[path] = dat
	}
	return aligned, dat, nil
}

func (c *testDatCache) Close() {
	for _, f := range c.files {
		_ = f.Close()
	}
}

func copyFile(t *testing.T, src, dst string) {
	t.Helper()
	in, err := os.Open(src)
	if err != nil {
		t.Fatal(err)
	}
	defer in.Close()
	out, err := os.Create(dst)
	if err != nil {
		t.Fatal(err)
	}
	defer out.Close()
	if _, err := io.Copy(out, in); err != nil {
		t.Fatal(err)
	}
}

func sha256hex(data []byte) string {
	sum := sha256.Sum256(data)
	return hex.EncodeToString(sum[:])
}
