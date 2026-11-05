package replace

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/sqpack"
)

func newFontCheckFixture(t *testing.T, indexFiles, dirFiles []string) (indexPath, fontDir string) {
	t.Helper()
	dir := t.TempDir()
	indexPath = filepath.Join(dir, "000000.win32.index")
	if err := os.WriteFile(indexPath, sqpack.CreateMockIndex("common/font", indexFiles...), 0o644); err != nil {
		t.Fatalf("写测试 index 失败: %v", err)
	}
	fontDir = filepath.Join(dir, "font")
	if err := os.MkdirAll(fontDir, 0o755); err != nil {
		t.Fatalf("建字体目录失败: %v", err)
	}
	for _, name := range dirFiles {
		if err := os.WriteFile(filepath.Join(fontDir, name), []byte{1, 2, 3, 4}, 0o644); err != nil {
			t.Fatalf("写测试字体失败: %v", err)
		}
	}
	return indexPath, fontDir
}

func seriesNames(prefix, suffix string, n int) []string {
	names := make([]string, 0, n)
	for i := 1; i <= n; i++ {
		names = append(names, fmt.Sprintf("%s%d%s", prefix, i, suffix))
	}
	return names
}

// 产出与索引条目一一对应时应放行。
func TestCheckFontFilesPasses(t *testing.T) {
	indexFiles := seriesNames("font", ".tex", 7)
	indexPath, fontDir := newFontCheckFixture(t, indexFiles, indexFiles)

	if err := CheckFontFiles(indexPath, fontDir); err != nil {
		t.Fatalf("合法字体目录不应报错: %v", err)
	}
}

// 超出该系列上限时要报出「需要几张、游戏有几张」和具体多出的文件。
func TestCheckFontFilesReportsOverflow(t *testing.T) {
	indexPath, fontDir := newFontCheckFixture(t,
		seriesNames("font", ".tex", 7),
		seriesNames("font", ".tex", 9),
	)

	err := CheckFontFiles(indexPath, fontDir)
	if err == nil {
		t.Fatal("超出上限应报错")
	}
	msg := err.Error()
	for _, want := range []string{"需要 9 张", "游戏只有 7 张", "font8.tex", "font9.tex", "未写入任何数据"} {
		if !strings.Contains(msg, want) {
			t.Fatalf("错误信息应包含 %q，实际: %s", want, msg)
		}
	}
}

// 每个系列各自算上限：主集够用、lobby 超出时只该报 lobby。
func TestCheckFontFilesChecksSeriesSeparately(t *testing.T) {
	indexPath, fontDir := newFontCheckFixture(t,
		append(seriesNames("font", ".tex", 7), seriesNames("font_lobby", ".tex", 4)...),
		append(seriesNames("font", ".tex", 7), seriesNames("font_lobby", ".tex", 5)...),
	)

	err := CheckFontFiles(indexPath, fontDir)
	if err == nil {
		t.Fatal("lobby 超出上限应报错")
	}
	msg := err.Error()
	if !strings.Contains(msg, "font_lobby{}") {
		t.Fatalf("应指出超限的是 lobby 系列，实际: %s", msg)
	}
	if strings.Contains(msg, "font{}") {
		t.Fatalf("主集没有超限，不该出现在错误里: %s", msg)
	}
}

// 索引里一张都没有的系列（写错文件名格式）要单独说清楚，而不是笼统报超限。
func TestCheckFontFilesReportsUnknownSeries(t *testing.T) {
	indexPath, fontDir := newFontCheckFixture(t,
		[]string{"font1.tex"},
		[]string{"font_tc9.tex"},
	)

	err := CheckFontFiles(indexPath, fontDir)
	if err == nil {
		t.Fatal("索引中不存在的系列应报错")
	}
	if !strings.Contains(err.Error(), "没有该系列贴图") {
		t.Fatalf("应说明该系列在索引里不存在，实际: %v", err)
	}
}

// 编号不连续但都在索引里时应放行：替换只按名字查条目，不要求从 1 开始连续。
func TestCheckFontFilesAllowsSparseSeries(t *testing.T) {
	indexPath, fontDir := newFontCheckFixture(t,
		seriesNames("font", ".tex", 7),
		[]string{"font1.tex", "font3.tex", "font7.tex"},
	)

	if err := CheckFontFiles(indexPath, fontDir); err != nil {
		t.Fatalf("全部条目都存在的稀疏编号不应报错: %v", err)
	}
}
