// Package replace 移植旧版 ReplaceEXDF / ReplaceFont 的二进制替换主流程。
package replace

import (
	"context"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"

	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/csvx"
	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/exd"
	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/pipeline"
	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/sqpack"
	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/text"
)

// Options 控制文本替换流程。
type Options struct {
	Context         context.Context
	TextIndexPath   string
	CSVResourceDir  string
	SourceLanguage  string   // 单语言兼容 (如 JA / EN / TC / KO / DE / FR)
	TargetLanguages []string // 批量目标语言 (若指定，将依次处理所有指定语言，如国际服处理 JA, EN, DE, FR)
	SkipFiles       string   // 与旧版 conf 一致，使用 | 分隔
	// IncludeFiles 非空时，只替换其中列出的表（键为小写的 "exd/..." 路径，不含扩展名）。
	IncludeFiles map[string]bool
	// QuietMissingCSV 为真时，CSV 文件缺失不再逐表记录日志，用于用户自选词条目录的场景。
	QuietMissingCSV bool
	Rules           *pipeline.Registry
	Progress        func(done, total int, file string)
	Log             func(format string, args ...any)
}

// Stats 记录替换统计。
type Stats struct {
	Tables        int
	Files         int
	Entries       int
	AppendedBytes int64
}

// ReplaceEXDF 执行 CSV 文本替换：把新的 EXDF 二进制块追加到 0a0000 dat0，
// 并把新偏移回写到 index（与旧版 ReplaceEXDF 完全一致）。
func ReplaceEXDF(opts Options) (Stats, error) {
	var stats Stats
	if opts.Log == nil {
		opts.Log = func(string, ...any) {}
	}
	if opts.Progress == nil {
		opts.Progress = func(int, int, string) {}
	}
	if opts.Rules == nil {
		opts.Rules = pipeline.NewRegistry(nil)
	}

	index, err := sqpack.OpenIndex(opts.TextIndexPath)
	if err != nil {
		return stats, err
	}
	dat0Path := datPathFor(opts.TextIndexPath, 0)

	datFile, err := os.OpenFile(dat0Path, os.O_RDWR, 0)
	if err != nil {
		return stats, fmt.Errorf("打开 dat0 (%s) 失败: %w", dat0Path, err)
	}
	defer datFile.Close()
	indexFile, err := os.OpenFile(opts.TextIndexPath, os.O_RDWR, 0)
	if err != nil {
		return stats, fmt.Errorf("打开 index (%s) 失败: %w", opts.TextIndexPath, err)
	}
	defer indexFile.Close()

	datLength, err := datFile.Seek(0, io.SeekEnd)
	if err != nil {
		return stats, fmt.Errorf("定位 dat0 (%s) 末尾失败: %w", dat0Path, err)
	}
	if datLength%8 != 0 {
		return stats, fmt.Errorf("dat0 (%s) 长度未按 8 字节对齐: %d", dat0Path, datLength)
	}

	fileList, err := readRootExl(opts.TextIndexPath, index)
	if err != nil {
		return stats, err
	}
	skipFiles := strings.Split(strings.ToLower(opts.SkipFiles), "|")
	cache := newDatCache()
	defer cache.Close()
	pending := make([]pendingOffset, 0, 256)

	targetLangs := opts.TargetLanguages
	if len(targetLangs) == 0 {
		if opts.SourceLanguage != "" {
			targetLangs = []string{opts.SourceLanguage}
		} else {
			targetLangs = []string{"JA"}
		}
	}
	for idx, exhPath := range fileList {
		if opts.Context != nil && opts.Context.Err() != nil {
			return stats, opts.Context.Err()
		}
		if !strings.HasSuffix(strings.ToUpper(exhPath), ".EXH") {
			continue
		}
		replaceFileName := strings.ToLower(exhPath[:strings.LastIndex(exhPath, ".")])
		opts.Progress(idx+1, len(fileList), exhPath)
		if shouldSkip(replaceFileName, skipFiles) {
			continue
		}
		if len(opts.IncludeFiles) > 0 && !opts.IncludeFiles[replaceFileName] {
			continue
		}

		filePatch := exhPath[:strings.LastIndex(exhPath, "/")]
		fileName := exhPath[strings.LastIndex(exhPath, "/")+1:]

		folderCRC := sqpack.ComputeCRCString(filePatch)
		folder := index.Folders[folderCRC]
		if folder == nil {
			continue
		}
		exhEntry := folder.Files[sqpack.ComputeCRCString(fileName)]
		if exhEntry == nil {
			continue
		}
		exhData, err := cache.extract(opts.TextIndexPath, exhEntry.DataOffset)
		if err != nil {
			opts.Log("读取 EXH 失败 %s: %v", exhPath, err)
			continue
		}
		exh, err := exd.ParseEXHF(exhData)
		if err != nil {
			opts.Log("解析 EXH 失败 %s: %v", exhPath, err)
			continue
		}
		if len(exh.Langs) == 0 || len(exh.Pages) == 0 {
			continue
		}

		csvRel := exhPath[4:strings.Index(exhPath, ".")]
		csvPath := filepath.Join(opts.CSVResourceDir, filepath.FromSlash(csvRel)+".csv")
		offsetMap, dataMap, err := loadCSV(csvPath)
		if err != nil {
			// 自定义词条目录允许只包含部分表，缺文件属于预期情况，不逐表刷日志。
			if !(opts.QuietMissingCSV && errors.Is(err, os.ErrNotExist)) {
				opts.Log("跳过 %s: %v", exhPath, err)
			}
			continue
		}

		for _, page := range exh.Pages {
			for _, langItem := range targetLangs {
				langSuffix := strings.ToUpper(strings.TrimSpace(langItem))
				exdFileName := strings.Replace(fileName, ".EXH",
					fmt.Sprintf("_%d_%s.EXD", page.PageNum, langSuffix), 1)
				exdEntry := folder.Files[sqpack.ComputeCRCString(exdFileName)]
				if exdEntry == nil {
					continue
				}
				exdData, err := cache.extract(opts.TextIndexPath, exdEntry.DataOffset)
				if err != nil {
					if !errors.Is(err, sqpack.ErrEmptyDatBlock) {
						opts.Log("读取 EXD 失败 %s: %v", exdFileName, err)
					}
					continue
				}
				exdFile, err := exd.ParseEXDF(exdData)
				if err != nil {
					opts.Log("解析 EXD 失败 %s: %v", exdFileName, err)
					continue
				}

				newEntries, count, err := buildEntries(exh, exdFile, csvRel, offsetMap, dataMap, opts.Rules)
				if err != nil {
					opts.Log("构建 %s 失败: %v", exdFileName, err)
					continue
				}
				exdfBytes, err := exd.BuildEXDF(newEntries)
				if err != nil {
					return stats, fmt.Errorf("封装 EXDF (%s) 失败: %w", exdFileName, err)
				}
				block, err := sqpack.BuildBinaryBlock(exdfBytes)
				if err != nil {
					return stats, fmt.Errorf("压缩二进制块 (%s) 失败: %w", exdFileName, err)
				}

				if _, err := datFile.WriteAt(block, datLength); err != nil {
					return stats, fmt.Errorf("写入 dat0 (%s, %s, off=%d) 失败: %w", dat0Path, exdFileName, datLength, err)
				}
				pending = append(pending, pendingOffset{entry: exdEntry, offset: datLength / 8})
				datLength += int64(len(block))
				stats.AppendedBytes += int64(len(block))
				stats.Files++
				stats.Entries += count
			}
		}
		stats.Tables++
	}
	if err := commitIndexOffsets(datFile, indexFile, pending); err != nil {
		return stats, err
	}
	return stats, nil
}

func loadCSV(path string) (map[int]int, map[int][]string, error) {
	rows, err := csvx.ParseFile(path)
	if err != nil {
		return nil, nil, err
	}
	if len(rows) < 2 {
		return nil, nil, fmt.Errorf("CSV (%s) 行数不足 (%d 行)", path, len(rows))
	}
	offsetMap := make(map[int]int, len(rows[1]))
	for i := 1; i < len(rows[1]); i++ {
		offset, err := strconv.Atoi(strings.TrimSpace(rows[1][i]))
		if err != nil {
			return nil, nil, fmt.Errorf("CSV (%s) 第 2 行第 %d 列不是列 Offset 映射表: %w", path, i+1, err)
		}
		offsetMap[offset] = i - 1
	}
	dataMap := make(map[int][]string, len(rows))
	for i := 3; i < len(rows); i++ {
		row := rows[i]
		if len(row) == 0 {
			continue
		}
		key, err := strconv.Atoi(strings.TrimSpace(row[0]))
		if err != nil {
			return nil, nil, fmt.Errorf("CSV (%s) 第 %d 行主键非法 (%q): %w", path, i+1, row[0], err)
		}
		dataMap[key] = row[1:]
	}
	return offsetMap, dataMap, nil
}

func buildEntries(exh *exd.EXHF, file *exd.File, tableName string,
	offsetMap map[int]int, dataMap map[int][]string, rules *pipeline.Registry) (map[int32][]byte, int, error) {

	keys := make([]int32, 0, len(file.Entries))
	for k := range file.Entries {
		keys = append(keys, k)
	}
	sort.Slice(keys, func(i, j int) bool { return keys[i] < keys[j] })

	out := make(map[int32][]byte, len(file.Entries))
	processed := 0
	for _, rowIndex := range keys {
		entry := exd.NewEntry(file.Entries[rowIndex], int(exh.DatasetChunkSize))
		if len(entry.Data) == 0 {
			out[rowIndex] = file.Entries[rowIndex]
			continue
		}
		chunk := make([]byte, len(entry.Chunk))
		copy(chunk, entry.Chunk)

		var newString []byte
		rowStrings := dataMap[int(rowIndex)]
		for _, dataset := range exh.Datasets {
			if dataset.Type != 0 {
				continue
			}
			jaBytes := entry.GetString(int(dataset.Offset))
			if int(dataset.Offset)+4 > len(chunk) {
				return nil, 0, fmt.Errorf("表 %s 行 %d 字符串列偏移越界: offset=%d chunkLen=%d", tableName, rowIndex, dataset.Offset, len(chunk))
			}
			binary.BigEndian.PutUint32(chunk[dataset.Offset:], uint32(len(newString)))

			replaced := false
			if rowStrings != nil {
				if col, ok := offsetMap[int(dataset.Offset)]; ok && col >= 0 && col < len(rowStrings) {
					readString := rowStrings[col]
					ctx := &pipeline.CellContext{
						TableName: tableName,
						RowKey:    int(rowIndex),
						ColIndex:  col,
						ColOffset: int(dataset.Offset),
						SourceStr: string(jaBytes),
						TargetStr: readString,
					}
					target := rules.Apply(ctx)
					if target != "" {
						encoded, err := text.ParseTargetString(target)
						if err != nil {
							return nil, 0, fmt.Errorf("表 %s 行 %d 列 %d 编码文本失败: %w", tableName, rowIndex, col, err)
						}
						newString = append(newString, encoded...)
						replaced = true
					}
				}
			}
			if !replaced {
				newString = append(newString, jaBytes...)
			}
			newString = append(newString, 0)
		}

		body := make([]byte, 0, len(chunk)+len(newString)+4)
		body = append(body, chunk...)
		body = append(body, newString...)
		paddingSize := 4 - len(body)%4
		if paddingSize == 0 {
			paddingSize = 4
		}
		body = append(body, make([]byte, paddingSize)...)
		out[rowIndex] = body
		processed++
	}
	return out, processed, nil
}

func readRootExl(indexPath string, index *sqpack.Index) ([]string, error) {
	folder := index.Folders[sqpack.ComputeCRCString("exd")]
	if folder == nil {
		return nil, fmt.Errorf("index 中不存在 exd 目录")
	}
	root := folder.Files[sqpack.ComputeCRCString("root.exl")]
	if root == nil {
		return nil, fmt.Errorf("index 中不存在 root.exl")
	}
	cache := newDatCache()
	defer cache.Close()
	data, err := cache.extract(indexPath, root.DataOffset)
	if err != nil {
		return nil, fmt.Errorf("读取 root.exl 失败: %w", err)
	}
	lines := strings.Split(string(data), "\n")
	out := make([]string, 0, len(lines))
	for _, line := range lines {
		line = strings.TrimSuffix(line, "\r")
		if line == "" {
			continue
		}
		base := line
		if i := strings.Index(base, ","); i >= 0 {
			base = base[:i]
		}
		out = append(out, "EXD/"+base+".EXH")
	}
	return out, nil
}

func shouldSkip(name string, skipFiles []string) bool {
	for _, key := range skipFiles {
		if key == "" {
			continue
		}
		if name == key || strings.HasPrefix(name, key+"/") {
			return true
		}
	}
	return false
}

// ReplaceFont 把 resource/font 下的字体逐个打包为 Type2 / Type4 块并追加到 000000 dat0。
func ReplaceFont(indexPath, fontDir string, progress func(done, total int, file string)) (Stats, error) {
	return ReplaceFontWithContext(context.Background(), indexPath, fontDir, progress)
}

// ReplaceFontWithContext 支持 context 取消的字体替换。
func ReplaceFontWithContext(ctx context.Context, indexPath, fontDir string, progress func(done, total int, file string)) (Stats, error) {
	var stats Stats
	if progress == nil {
		progress = func(int, int, string) {}
	}
	index, err := sqpack.OpenIndex(indexPath)
	if err != nil {
		return stats, err
	}
	folder := index.Folders[sqpack.ComputeCRCString("common/font")]
	if folder == nil {
		return stats, fmt.Errorf("index 中不存在 common/font 目录")
	}

	entries, err := os.ReadDir(fontDir)
	if err != nil {
		return stats, fmt.Errorf("读取字体目录失败: %w", err)
	}
	fonts := make([]string, 0, len(entries))
	for _, e := range entries {
		if e.IsDir() {
			continue
		}
		fonts = append(fonts, e.Name())
	}
	sort.Strings(fonts)

	dat0Path := datPathFor(indexPath, 0)
	datFile, err := os.OpenFile(dat0Path, os.O_RDWR, 0)
	if err != nil {
		return stats, fmt.Errorf("打开字体 dat0 (%s) 失败: %w", dat0Path, err)
	}
	defer datFile.Close()
	indexFile, err := os.OpenFile(indexPath, os.O_RDWR, 0)
	if err != nil {
		return stats, fmt.Errorf("打开字体 index (%s) 失败: %w", indexPath, err)
	}
	defer indexFile.Close()

	datLength, err := datFile.Seek(0, io.SeekEnd)
	if err != nil {
		return stats, fmt.Errorf("定位字体 dat0 (%s) 末尾失败: %w", dat0Path, err)
	}
	if datLength%8 != 0 {
		return stats, fmt.Errorf("字体 dat0 (%s) 长度未按 8 字节对齐: %d", dat0Path, datLength)
	}

	pending := make([]pendingOffset, 0, len(fonts))
	for i, name := range fonts {
		if ctx != nil && ctx.Err() != nil {
			return stats, ctx.Err()
		}
		progress(i+1, len(fonts), name)
		entry := folder.Files[sqpack.ComputeCRCString(name)]
		if entry == nil {
			return stats, fmt.Errorf("index 中不存在字体文件: %s", name)
		}
		data, err := os.ReadFile(filepath.Join(fontDir, name))
		if err != nil {
			return stats, fmt.Errorf("读取字体失败 %s: %w", name, err)
		}
		var block []byte
		if strings.HasSuffix(strings.ToLower(name), ".tex") {
			block, err = sqpack.BuildTexBlock(data)
		} else {
			block, err = sqpack.BuildBinaryBlock(data)
		}
		if err != nil {
			return stats, fmt.Errorf("构建字体块失败 %s: %w", name, err)
		}
		if _, err := datFile.WriteAt(block, datLength); err != nil {
			return stats, fmt.Errorf("写入字体 dat0 (%s, %s, off=%d) 失败: %w", dat0Path, name, datLength, err)
		}
		pending = append(pending, pendingOffset{entry: entry, offset: datLength / 8})
		datLength += int64(len(block))
		stats.AppendedBytes += int64(len(block))
		stats.Files++
	}
	if err := commitIndexOffsets(datFile, indexFile, pending); err != nil {
		return stats, err
	}
	stats.Tables = 1
	return stats, nil
}

// pendingOffset 记录一条待提交的索引偏移。
type pendingOffset struct {
	entry  *sqpack.IndexFile
	offset int64
}

// commitIndexOffsets 先确保 dat 数据块全部落盘，再批量回写索引并刷盘。
// 顺序不可颠倒：任何时刻索引都只能指向已持久化的数据，否则崩溃或掉电后
// 会留下指向空洞的坏索引，游戏读取该表时会得到损坏数据。
func commitIndexOffsets(datFile, indexFile *os.File, pending []pendingOffset) error {
	if len(pending) == 0 {
		return nil
	}
	if err := datFile.Sync(); err != nil {
		return fmt.Errorf("同步 dat0 (%s) 失败: %w", datFile.Name(), err)
	}
	for _, p := range pending {
		if err := sqpack.WriteDataOffset(indexFile, p.entry, p.offset); err != nil {
			return fmt.Errorf("回写索引偏移 (%s, pt=%d) 失败: %w", indexFile.Name(), p.entry.Pt, err)
		}
	}
	if err := indexFile.Sync(); err != nil {
		return fmt.Errorf("同步 index (%s) 失败: %w", indexFile.Name(), err)
	}
	return nil
}

func datPathFor(indexPath string, datNum int) string {
	base := indexPath
	switch {
	case strings.HasSuffix(strings.ToLower(base), ".index2"):
		base = base[:len(base)-len(".index2")]
	case strings.HasSuffix(strings.ToLower(base), ".index"):
		base = base[:len(base)-len(".index")]
	}
	return base + ".dat" + strconv.Itoa(datNum)
}

type datCache struct {
	files map[string]*sqpack.DatFile
}

func newDatCache() *datCache { return &datCache{files: make(map[string]*sqpack.DatFile)} }

func (c *datCache) extract(indexPath string, dataOffset int32) ([]byte, error) {
	datNum, aligned := sqpack.DatNumAndAlignedOffset(dataOffset)
	path := datPathFor(indexPath, datNum)
	dat := c.files[path]
	if dat == nil {
		var err error
		dat, err = sqpack.OpenDat(path)
		if err != nil {
			return nil, err
		}
		c.files[path] = dat
	}
	return dat.ExtractFile(aligned)
}

// Close 释放缓存中的 dat 句柄。
func (c *datCache) Close() {
	for _, f := range c.files {
		_ = f.Close()
	}
	c.files = make(map[string]*sqpack.DatFile)
}
