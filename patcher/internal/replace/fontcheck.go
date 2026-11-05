package replace

import (
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"

	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/sqpack"
)

// CheckFontFiles 在写入任何数据之前，确认 fontDir 里的文件都能在 common/font
// 找到对应条目。
//
// ReplaceFontWithUndo 是边写边查的：第 8 张 font8.tex 查不到时，前 7 张已经追加
// 进 dat0，只能靠 undo 撤回。这里把同一套查询提前到只读阶段，失败时客户端文件
// 一个字节都没动。
//
// 张数上限不写成常量：各系列的贴图张数不同（font 7 张、font_lobby 6 张、
// font_tc 13 张），官方加图集后还会变。索引只存 CRC 无法枚举，所以按候选名
// 反查，数出某个系列实际允许几张。
func CheckFontFiles(indexPath, fontDir string) error {
	index, err := sqpack.OpenIndex(indexPath)
	if err != nil {
		return err
	}
	folder := index.Folders[sqpack.ComputeCRCString("common/font")]
	if folder == nil {
		return fmt.Errorf("index 中不存在 common/font 目录")
	}

	entries, err := os.ReadDir(fontDir)
	if err != nil {
		return fmt.Errorf("读取字体目录失败: %w", err)
	}
	if len(entries) == 0 {
		return fmt.Errorf("字体目录为空: %s", fontDir)
	}

	// 前缀 + 数字 + 后缀相同的文件属于同一个贴图系列（font{} 与 font_lobby{}
	// 是两套独立贴图，各自受自己的上限约束）。
	series := map[string][]string{}
	for _, e := range entries {
		if e.IsDir() {
			continue
		}
		name := e.Name()
		// 只看字体文件：目录里混进说明文件一类的东西不该被当成字体去索引里查。
		switch strings.ToLower(filepath.Ext(name)) {
		case ".tex", ".fdt":
		default:
			continue
		}
		key := name
		if prefix, _, suffix, ok := splitFontName(name); ok {
			key = prefix + "{}" + suffix
		}
		series[key] = append(series[key], name)
	}

	patterns := make([]string, 0, len(series))
	for pattern := range series {
		patterns = append(patterns, pattern)
	}
	sort.Strings(patterns)

	var problems []string
	for _, pattern := range patterns {
		names := series[pattern]
		sort.Strings(names)

		var missing []string
		for _, name := range names {
			if folder.Files[sqpack.ComputeCRCString(name)] == nil {
				missing = append(missing, name)
			}
		}
		if len(missing) == 0 {
			continue
		}

		prefix, _, suffix, ok := splitFontName(names[0])
		if !ok {
			problems = append(problems, fmt.Sprintf("  %s：游戏 common/font 中不存在（%s）",
				pattern, strings.Join(missing, "、")))
			continue
		}
		// 探测上界取产出里最大的编号，足以判断「连续存在到第几张」。
		maxNum := 0
		for _, name := range names {
			if _, n, _, ok := splitFontName(name); ok && n > maxNum {
				maxNum = n
			}
		}
		limit := countFontSeries(folder, prefix, suffix, maxNum)
		if limit == 0 {
			problems = append(problems, fmt.Sprintf("  %s：游戏 common/font 中没有该系列贴图（%s）",
				pattern, strings.Join(missing, "、")))
			continue
		}
		problems = append(problems, fmt.Sprintf("  %s：需要 %d 张，游戏只有 %d 张（%s 无对应条目）",
			pattern, len(names), limit, strings.Join(missing, "、")))
	}

	if len(problems) == 0 {
		return nil
	}
	return fmt.Errorf("字体贴图超出游戏上限，未写入任何数据：\n%s\n请减少方案里的字号档或字符范围后重新编译", strings.Join(problems, "\n"))
}

// countFontSeries 数出 prefix+数字+suffix 从 1 开始连续存在多少张。
// 游戏按连续编号加载，缺一张就到此为止。
func countFontSeries(folder *sqpack.Folder, prefix, suffix string, maxIndex int) int {
	limit := 0
	for i := 1; i <= maxIndex+1; i++ {
		name := prefix + strconv.Itoa(i) + suffix
		if folder.Files[sqpack.ComputeCRCString(name)] == nil {
			break
		}
		limit = i
	}
	return limit
}

// splitFontName 把 font8.tex 拆成 ("font", 8, ".tex")。
// 名称里没有数字编号时返回 ok=false。
func splitFontName(name string) (prefix string, index int, suffix string, ok bool) {
	dot := strings.LastIndex(name, ".")
	if dot <= 0 {
		return "", 0, "", false
	}
	suffix = name[dot:]
	base := name[:dot]

	end := len(base)
	start := end
	for start > 0 && base[start-1] >= '0' && base[start-1] <= '9' {
		start--
	}
	if start == end || start == 0 {
		return "", 0, "", false
	}
	n, err := strconv.Atoi(base[start:])
	if err != nil {
		return "", 0, "", false
	}
	return base[:start], n, suffix, true
}
