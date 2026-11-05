package e2e

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/pipeline"
	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/replace"
	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/sqpack"
)

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

// TestReplacePipelineSelfConsistent 在真实原版 sqpack 上跑一遍替换，
// 校验写出的数据块与源数据自洽：解包长度等于块头声明、块头按 128 字节对齐、
// 追加块内容类型为 EXDF、字体解包结果逐字节等于源字体文件。
//
// 它不依赖任何历史产物基线，因此游戏版本更新、译文更新都不需要维护本测试。
// 与旧版实现逐字节对拍只在迁移期有意义，已随迁移完成移除。
func TestReplacePipelineSelfConsistent(t *testing.T) {
	if testing.Short() {
		t.Skip("需要本地原版 sqpack 数据（约 400MB），-short 模式跳过")
	}
	srcDir := locateSource(t)

	workDir := t.TempDir()
	gameDir := filepath.Join(workDir, "game", "sqpack", "ffxiv")
	if err := os.MkdirAll(gameDir, 0o755); err != nil {
		t.Fatal(err)
	}
	for _, name := range requiredFiles {
		copyFile(t, filepath.Join(srcDir, name), filepath.Join(gameDir, name))
	}

	textIndexPath := filepath.Join(gameDir, "0a0000.win32.index")
	fontIndexPath := filepath.Join(gameDir, "000000.win32.index")
	origTextDatSize := mustSize(t, filepath.Join(gameDir, "0a0000.win32.dat0"))
	origFontDatSize := mustSize(t, filepath.Join(gameDir, "000000.win32.dat0"))

	csvDir := repoPath("resource", "rawexd")
	fontDir := repoPath("resource", "font")

	// 关闭全部附加选项：本测试只校验写入与编码链路，规则行为由 pipeline 单测覆盖。
	rules := pipeline.NewRegistry(nil)
	for _, meta := range rules.List() {
		rules.SetEnabled(meta.ID, false)
	}

	fontStats, err := replace.ReplaceFont(fontIndexPath, fontDir, nil)
	if err != nil {
		t.Fatalf("ReplaceFont 失败: %v", err)
	}
	if fontStats.Files == 0 {
		t.Fatal("ReplaceFont 未处理任何字体")
	}

	for _, lang := range []string{"ja", "en"} {
		stats, err := replace.ReplaceEXDF(replace.Options{
			TextIndexPath:  textIndexPath,
			CSVResourceDir: csvDir,
			SourceLanguage: lang,
			Rules:          rules,
		})
		if err != nil {
			t.Fatalf("ReplaceEXDF(%s) 失败: %v", lang, err)
		}
		if stats.Files == 0 || stats.Entries == 0 {
			t.Fatalf("ReplaceEXDF(%s) 统计异常: %+v", lang, stats)
		}
		t.Logf("ReplaceEXDF(%s): tables=%d files=%d entries=%d appended=%d bytes",
			lang, stats.Tables, stats.Files, stats.Entries, stats.AppendedBytes)
	}

	verifyTextBlocks(t, textIndexPath, origTextDatSize)
	verifyFontBlocks(t, fontIndexPath, fontDir, origFontDatSize)

	if size := mustSize(t, filepath.Join(gameDir, "000000.win32.dat0")); size%128 != 0 {
		t.Fatalf("字体 dat0 长度未按 128 字节对齐: %d", size)
	}
}

func verifyTextBlocks(t *testing.T, indexPath string, origDatSize int64) {
	t.Helper()
	index, err := sqpack.OpenIndex(indexPath)
	if err != nil {
		t.Fatalf("打开文本索引失败: %v", err)
	}
	cache := &datCache{files: map[string]*sqpack.DatFile{}}
	defer cache.Close()

	var total, appended, kept int
	for folderID, folder := range index.Folders {
		for fileID, entry := range folder.Files {
			aligned, dat, err := cache.resolve(indexPath, entry.DataOffset)
			if err != nil {
				t.Fatalf("folder=%d file=%d 定位数据块失败: %v", folderID, fileID, err)
			}
			data, err := dat.ExtractFile(aligned)
			if err != nil {
				t.Fatalf("folder=%d file=%d 解包失败: %v", folderID, fileID, err)
			}
			total++

			if aligned < origDatSize {
				kept++
				continue
			}
			appended++

			if aligned%128 != 0 {
				t.Fatalf("folder=%d file=%d 追加块起始未按 128 对齐: %d", folderID, fileID, aligned)
			}
			header, err := dat.ReadAt(aligned, 24)
			if err != nil {
				t.Fatalf("folder=%d file=%d 读取块头失败: %v", folderID, fileID, err)
			}
			headerLength := int(binary.LittleEndian.Uint32(header[0:]))
			contentType := int(binary.LittleEndian.Uint32(header[4:]))
			fileSize := int(binary.LittleEndian.Uint32(header[8:]))
			if headerLength <= 0 || headerLength%128 != 0 {
				t.Fatalf("folder=%d file=%d 块头长度非法: %d", folderID, fileID, headerLength)
			}
			if contentType != 2 {
				t.Fatalf("folder=%d file=%d 追加块内容类型非 EXDF: %d", folderID, fileID, contentType)
			}
			if fileSize != len(data) {
				t.Fatalf("folder=%d file=%d 块头声明 %d 字节，实际解包 %d 字节",
					folderID, fileID, fileSize, len(data))
			}
		}
	}

	if total == 0 {
		t.Fatal("文本索引为空，无法验证替换结果")
	}
	if appended == 0 {
		t.Fatal("文本索引没有任何追加块，替换未生效")
	}
	t.Logf("文本块校验通过: %d 个文件（追加 %d / 保留原版 %d）", total, appended, kept)
}

func verifyFontBlocks(t *testing.T, indexPath, fontDir string, origDatSize int64) {
	t.Helper()
	index, err := sqpack.OpenIndex(indexPath)
	if err != nil {
		t.Fatalf("打开字体索引失败: %v", err)
	}
	cache := &datCache{files: map[string]*sqpack.DatFile{}}
	defer cache.Close()

	entries, err := os.ReadDir(fontDir)
	if err != nil {
		t.Fatalf("读取字体目录失败: %v", err)
	}

	checked := 0
	for _, e := range entries {
		if e.IsDir() {
			continue
		}
		name := e.Name()
		entry := index.Lookup("common/font", name)
		if entry == nil {
			t.Fatalf("字体索引缺失: %s", name)
		}
		aligned, dat, err := cache.resolve(indexPath, entry.DataOffset)
		if err != nil {
			t.Fatalf("%s: 定位数据块失败: %v", name, err)
		}
		if aligned < origDatSize {
			t.Fatalf("%s: 字体块未追加到 dat0 末尾 (offset=%d, 原长度=%d)", name, aligned, origDatSize)
		}
		if aligned%128 != 0 {
			t.Fatalf("%s: 字体块起始未按 128 对齐: %d", name, aligned)
		}
		header, err := dat.ReadAt(aligned, 24)
		if err != nil {
			t.Fatalf("%s: 读取块头失败: %v", name, err)
		}
		if headerLength := int(binary.LittleEndian.Uint32(header[0:])); headerLength <= 0 || headerLength%128 != 0 {
			t.Fatalf("%s: 块头长度非法: %d", name, headerLength)
		}

		data, err := dat.ExtractFile(aligned)
		if err != nil {
			t.Fatalf("%s: 解包失败: %v", name, err)
		}
		want, err := os.ReadFile(filepath.Join(fontDir, name))
		if err != nil {
			t.Fatalf("%s: 读取源字体失败: %v", name, err)
		}
		if !bytes.Equal(data, want) {
			t.Fatalf("%s: 解包内容与源字体不一致 (%d != %d 字节)", name, len(data), len(want))
		}
		checked++
	}

	if checked == 0 {
		t.Fatal("字体目录为空，无法验证替换结果")
	}
	t.Logf("字体块校验通过: %d 个文件解包内容与源字体一致", checked)
}

func locateSource(t *testing.T) string {
	t.Helper()
	if env := os.Getenv("GOLDEN_SOURCE_DIR"); env != "" && hasAllRequiredFiles(env) {
		return env
	}
	if c := repoPath("backup", "global"); hasAllRequiredFiles(c) {
		return c
	}
	t.Skip("未找到原版 sqpack 数据（backup/global 或 GOLDEN_SOURCE_DIR），跳过端到端测试")
	return ""
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

type datCache struct {
	files map[string]*sqpack.DatFile
}

func (c *datCache) resolve(indexPath string, dataOffset int32) (int64, *sqpack.DatFile, error) {
	datNum, aligned := sqpack.DatNumAndAlignedOffset(dataOffset)
	base := strings.TrimSuffix(strings.TrimSuffix(indexPath, ".index2"), ".index")
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

func (c *datCache) Close() {
	for _, f := range c.files {
		_ = f.Close()
	}
}

func mustSize(t *testing.T, path string) int64 {
	t.Helper()
	st, err := os.Stat(path)
	if err != nil {
		t.Fatal(err)
	}
	return st.Size()
}

func copyFile(t *testing.T, src, dst string) {
	t.Helper()
	data, err := os.ReadFile(src)
	if err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(dst, data, 0o644); err != nil {
		t.Fatal(err)
	}
}
