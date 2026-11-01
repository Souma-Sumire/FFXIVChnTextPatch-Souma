package pipeline

import (
	"crypto/sha256"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func newTestRegistry(rules ...Rule) *Registry {
	reg := NewRegistry(rules)
	for _, r := range rules {
		reg.SetEnabled(r.ID(), true)
	}
	return reg
}

func TestBuiltinRuleDefaults(t *testing.T) {
	reg := NewRegistry(BuiltinRules())
	if !reg.Enabled("treasure_map_alias") {
		t.Fatal("藏宝图别名规则应默认开启")
	}
	if !reg.Enabled("materia_stat_suffix") {
		t.Fatal("魔晶石属性后缀规则应默认开启")
	}
	if reg.Enabled("opencc_s2tw") {
		t.Fatal("台湾正体转换规则应默认关闭")
	}
	if reg.Enabled("ktg_cheat") {
		t.Fatal("魔光键影作弊规则应默认关闭")
	}
	if len(reg.List()) != 4 {
		t.Fatalf("内置规则数量应为 4，实际 %d", len(reg.List()))
	}
}

func TestTreasureMapAlias(t *testing.T) {
	reg := newTestRegistry(NewTreasureMapAliasRule())
	cases := []struct{ target, source, want string }{
		{"陈旧的鞣革地图", "", "陈旧的鞣革地图（G1）"},
		{"陈旧的金毗罗鳄革地图", "", "陈旧的金毗罗鳄革地图（G14）"},
		{"", "古ぼけた地図G18", "陈旧的卡冈图亚革地图（G18）"},
		{"普通物品", "普通アイテム", "普通物品"},
	}
	for _, c := range cases {
		ctx := &CellContext{TableName: "Item", RowKey: 1, ColOffset: 4, SourceStr: c.source, TargetStr: c.target}
		if got := reg.Apply(ctx); got != c.want {
			t.Fatalf("Apply(%q, %q) = %q, want %q", c.target, c.source, got, c.want)
		}
	}
}

func TestMateriaStatSuffix(t *testing.T) {
	reg := newTestRegistry(NewMateriaStatSuffixRule())
	cases := []struct {
		table  string
		target string
		want   string
	}{
		{"Item", "信力魔晶石壹型", "信力魔晶石壹型（信仰）"},
		{"Item", "武略魔晶石伍型", "武略魔晶石伍型（暴击）"},
		{"Item", "咏唱魔晶石拾贰型", "咏唱魔晶石拾贰型（咏唱速度）"},
		{"Item", "信力魔晶石壹型（信仰）", "信力魔晶石壹型（信仰）"}, // 已带后缀不应重复追加
		{"Action", "信力魔晶石壹型", "信力魔晶石壹型"},       // 非 Item 表不处理
		{"Item", "普通道具", "普通道具"},
	}
	for _, c := range cases {
		ctx := &CellContext{TableName: c.table, RowKey: 1, ColOffset: 0, TargetStr: c.target}
		if got := reg.Apply(ctx); got != c.want {
			t.Fatalf("Apply(%s, %q) = %q, want %q", c.table, c.target, got, c.want)
		}
	}
}

func TestRuleToggleIsolation(t *testing.T) {
	reg := NewRegistry(BuiltinRules())
	ctx := &CellContext{TableName: "Item", TargetStr: "陈旧的鞣革地图"}
	if got := reg.Apply(ctx); got != "陈旧的鞣革地图（G1）" {
		t.Fatalf("默认开启时结果错误: %q", got)
	}
	reg.SetEnabled("treasure_map_alias", false)
	ctx = &CellContext{TableName: "Item", TargetStr: "陈旧的鞣革地图"}
	if got := reg.Apply(ctx); got != "陈旧的鞣革地图" {
		t.Fatalf("关闭规则后不应转换: %q", got)
	}
}

func TestOpenCCProtectsHexTags(t *testing.T) {
	rule := NewOpenCCS2TWRule()
	if err := rule.InitError(); err != nil {
		t.Fatalf("OpenCC 初始化失败: %v", err)
	}
	reg := newTestRegistry(rule)
	input := "汉字<hex:02100103>测试<hex:02280CFF05>EObj<hex:F61E9D210103>"
	got := reg.Apply(&CellContext{TableName: "Test", TargetStr: input})
	if got == input {
		t.Fatal("开启台湾正体转换后文本应发生变化")
	}
	if !strings.Contains(got, "<hex:02100103>") ||
		!strings.Contains(got, "<hex:02280CFF05>EObj<hex:F61E9D210103>") {
		t.Fatalf("OpenCC 转换破坏了 <hex:...> 标签: %q", got)
	}
	if !strings.Contains(got, "漢") || !strings.Contains(got, "測") {
		t.Fatalf("OpenCC 未转换纯文本部分: %q", got)
	}
}

// TestNoDiskPollution 验证规则只在内存中生效，绝不改写 resource/rawexd。
func TestNoDiskPollution(t *testing.T) {
	itemPath := filepath.Join("..", "..", "..", "resource", "rawexd", "Item.csv")
	before, err := os.ReadFile(itemPath)
	if err != nil {
		t.Skipf("本地缺少 %s，跳过磁盘污染测试", itemPath)
	}
	beforeSum := sha256.Sum256(before)

	reg := NewRegistry(BuiltinRules())
	for i := 0; i < 100; i++ {
		reg.Apply(&CellContext{TableName: "Item", RowKey: i, ColOffset: 0, TargetStr: "信力魔晶石壹型"})
	}
	after, err := os.ReadFile(itemPath)
	if err != nil {
		t.Fatal(err)
	}
	if sha256.Sum256(after) != beforeSum {
		t.Fatal("运行期规则污染了磁盘上的 resource/rawexd/Item.csv")
	}
}

// TestRulesJSONDrivesRules 校验运行期规则确实由 resource/rules.json 驱动。
func TestRulesJSONDrivesRules(t *testing.T) {
	path := filepath.Join("..", "..", "..", "resource", "rules.json")
	rules, err := LoadRulesFromFile(path)
	if err != nil {
		t.Skipf("缺少或无法解析 %s: %v", path, err)
	}
	reg := NewRegistry(rules)

	cases := []struct {
		table  string
		target string
		source string
		want   string
	}{
		{"Item", "陈旧的鞣革地图", "", "陈旧的鞣革地图（G1）"},
		{"Item", "", "古ぼけた地図G18", "陈旧的卡冈图亚革地图（G18）"},
		{"Item", "信力魔晶石壹型", "", "信力魔晶石壹型（信仰）"},
		{"Action", "陈旧的鞣革地图", "", "陈旧的鞣革地图（G1）"},
	}
	for _, c := range cases {
		ctx := &CellContext{TableName: c.table, RowKey: 1, ColOffset: 0, SourceStr: c.source, TargetStr: c.target}
		if got := reg.Apply(ctx); got != c.want {
			t.Fatalf("rules.json 规则结果错误: Apply(%q, %q) = %q, want %q", c.target, c.source, got, c.want)
		}
	}
	if len(rules) != 4 {
		t.Fatalf("规则数量应为 4，实际 %d", len(rules))
	}
}

func TestKTGCheatRule(t *testing.T) {
	rule := NewKTGCheatRule()
	reg := newTestRegistry(rule)

	cases := []struct {
		table    string
		rowKey   int
		colIndex int
		input    string
		want     string
	}{
		{"KTGTypeWordTextData", 0, 1, "", "1"},
		{"KTGTypeWordTextData", 1, 1, "ShenLuo", "2"},
		{"KTGTypeWordTextData", 2, 1, "Test", "3"},
		{"KTGTypeWordTextData", 8, 1, "", "9"},
		{"KTGTypeWordTextData", 9, 1, "foo", "1"},
		{"KTGTypeWordTextData", 10, 1, "", "2"},
		{"KTGTypeWordTextData", 1, 0, "神罗", "神罗"},
		{"Item", 1, 1, "ShenLuo", "ShenLuo"},
	}

	for _, c := range cases {
		ctx := &CellContext{
			TableName: c.table,
			RowKey:    c.rowKey,
			ColIndex:  c.colIndex,
			TargetStr: c.input,
		}
		if got := reg.Apply(ctx); got != c.want {
			t.Fatalf("Apply(%s, row=%d, col=%d, input=%q) = %q, want %q",
				c.table, c.rowKey, c.colIndex, c.input, got, c.want)
		}
	}
}
