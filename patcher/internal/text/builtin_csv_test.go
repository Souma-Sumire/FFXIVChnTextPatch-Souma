package text

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/csvx"
)

// TestBuiltinCSVAllCellsParse 确保内置词条的每个单元格都能被 ParseTargetString 接受。
// 标签规则一旦收紧，这条测试能立刻发现与现有 CSV 的冲突；本地没有
// resource/rawexd（内置资源由运行期同步）时自动跳过。
func TestBuiltinCSVAllCellsParse(t *testing.T) {
	root := filepath.Join("..", "..", "..", "resource", "rawexd")
	if _, err := os.Stat(root); err != nil {
		t.Skip("no resource dir")
	}
	checked, bad := 0, 0
	err := filepath.WalkDir(root, func(path string, d os.DirEntry, walkErr error) error {
		if walkErr != nil || d.IsDir() || !strings.HasSuffix(strings.ToLower(d.Name()), ".csv") {
			return nil
		}
		rows, err := csvx.ParseFile(path)
		if err != nil {
			t.Errorf("CSV 解析失败 %s: %v", path, err)
			return nil
		}
		checked++
		for r, row := range rows {
			for c, cell := range row {
				if _, err := ParseTargetString(cell); err != nil {
					bad++
					if bad <= 20 {
						t.Errorf("%s 第 %d 行第 %d 列: %v", path, r+1, c+1, err)
					}
				}
			}
		}
		return nil
	})
	if err != nil {
		t.Fatal(err)
	}
	t.Logf("checked=%d bad=%d", checked, bad)
}
