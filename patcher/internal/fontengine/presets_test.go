package fontengine

import (
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/region"
)

// 内置方案：default 四服通用，chnaxis 只给国际服。
func TestBuiltinPresetsPerRegion(t *testing.T) {
	cases := []struct {
		reg         region.Region
		want        []string
		wantMissing []string
	}{
		{reg: region.Global, want: []string{BuiltinDefaultPresetID, BuiltinChnAXISPresetID}},
		{reg: region.China, want: []string{BuiltinDefaultPresetID}, wantMissing: []string{BuiltinChnAXISPresetID}},
		{reg: region.Korea, want: []string{BuiltinDefaultPresetID}, wantMissing: []string{BuiltinChnAXISPresetID}},
		{reg: region.Taiwan, want: []string{BuiltinDefaultPresetID}, wantMissing: []string{BuiltinChnAXISPresetID}},
	}

	for _, tc := range cases {
		presets := GetPresetsForRegion(tc.reg, "")
		ids := make(map[string]bool, len(presets))
		for _, p := range presets {
			ids[p.ID] = true
		}
		for _, id := range tc.want {
			if !ids[id] {
				t.Errorf("区服 %s 缺少内置方案 %s（实际: %v）", tc.reg, id, ids)
			}
		}
		for _, id := range tc.wantMissing {
			if ids[id] {
				t.Errorf("区服 %s 不应出现国际服专用方案 %s", tc.reg, id)
			}
		}
		if got := DefaultPresetForRegion(tc.reg); got != BuiltinDefaultPresetID {
			t.Errorf("区服 %s 的默认方案应为 %s，实际 %s", tc.reg, BuiltinDefaultPresetID, got)
		}
	}
}

func TestBuiltinPresetContents(t *testing.T) {
	defaultJSON, err := GetPresetContent(BuiltinDefaultPresetID, "")
	if err != nil {
		t.Fatalf("读取默认方案失败: %v", err)
	}
	chnaxisJSON, err := GetPresetContent(BuiltinChnAXISPresetID, "")
	if err != nil {
		t.Fatalf("读取 ChnAXIS 方案失败: %v", err)
	}
	if len(defaultJSON) == 0 || len(chnaxisJSON) == 0 {
		t.Fatal("内置方案内容不应为空")
	}

	var defaultSet, chnaxisSet MultiFontSet
	if err := json.Unmarshal([]byte(defaultJSON), &defaultSet); err != nil {
		t.Fatalf("默认方案不是合法 JSON: %v", err)
	}
	if err := json.Unmarshal([]byte(chnaxisJSON), &chnaxisSet); err != nil {
		t.Fatalf("ChnAXIS 方案不是合法 JSON: %v", err)
	}
	if len(defaultSet.FontSets) == 0 || len(chnaxisSet.FontSets) != 2 {
		t.Fatalf("内置方案结构不符合预期: defaultSets=%d chnaxisSets=%d", len(defaultSet.FontSets), len(chnaxisSet.FontSets))
	}
	// 默认方案的导出映射必须覆盖各区字体槽，否则它不配称为「四服通用」。
	if !defaultSet.ExportMapChnAxisToFont || !defaultSet.ExportMapKrnAxisToFont || !defaultSet.ExportMapTcAxisToFont {
		t.Errorf("默认方案应启用各区导出映射: %+v", defaultSet)
	}
	// ChnAXIS 方案只写 font/font_lobby。
	if chnaxisSet.ExportMapChnAxisToFont || chnaxisSet.ExportMapKrnAxisToFont || chnaxisSet.ExportMapTcAxisToFont {
		t.Errorf("ChnAXIS 方案不应启用跨区导出映射: %+v", chnaxisSet)
	}
}

// 默认方案靠导出映射覆盖四服；ChnAXIS 方案只有 AXIS 字体面，只能给国际服用。
func TestValidatePresetForRegion(t *testing.T) {
	defaultJSON, _ := GetPresetContent(BuiltinDefaultPresetID, "")
	chnaxisJSON, _ := GetPresetContent(BuiltinChnAXISPresetID, "")

	for _, reg := range []region.Region{region.Global, region.China, region.Korea, region.Taiwan} {
		if err := ValidatePresetForRegion(defaultJSON, reg); err != nil {
			t.Errorf("默认方案应可用于 %s: %v", reg, err)
		}
	}
	if err := ValidatePresetForRegion(chnaxisJSON, region.Global); err != nil {
		t.Errorf("ChnAXIS 方案应可用于国际服: %v", err)
	}
	for _, reg := range []region.Region{region.China, region.Korea, region.Taiwan} {
		if err := ValidatePresetForRegion(chnaxisJSON, reg); err == nil {
			t.Errorf("ChnAXIS 方案不应用于 %s", reg)
		}
	}

	// 区服专用字体面：没有 AXIS 的国服方案不能用于国际服。
	chnOnly := `{"fontSets":[{"faces":[{"name":"ChnAXIS_180","elements":[]}]}]}`
	if err := ValidatePresetForRegion(chnOnly, region.Global); err == nil {
		t.Error("只有 ChnAXIS 字体面的方案不应用于国际服")
	}
	if err := ValidatePresetForRegion(chnOnly, region.China); err != nil {
		t.Errorf("只有 ChnAXIS 字体面的方案应可用于国服: %v", err)
	}
	// 空方案要被拦截。
	if err := ValidatePresetForRegion(`{"fontSets":[]}`, region.Global); err == nil {
		t.Error("没有任何字体面的方案应校验失败")
	}
}

func TestSaveRenameDeleteCustomPreset(t *testing.T) {
	dir := t.TempDir()
	content, err := GetPresetContent(BuiltinDefaultPresetID, "")
	if err != nil {
		t.Fatal(err)
	}

	saved, err := SaveCustomPreset("我的方案", content, dir, region.Global)
	if err != nil {
		t.Fatalf("保存自定义方案失败: %v", err)
	}
	if saved.ID != "custom:我的方案" || saved.Name != "我的方案" || saved.Builtin {
		t.Fatalf("保存结果不符合预期: %+v", saved)
	}
	if saved.Size == 0 {
		t.Error("自定义方案应带文件大小信息")
	}

	presets := GetPresetsForRegion(region.Global, dir)
	found := false
	for _, p := range presets {
		if p.ID == saved.ID {
			found = true
		}
	}
	if !found {
		t.Fatalf("保存后的方案未出现在列表里: %+v", presets)
	}

	renamed, err := RenameCustomPreset(saved.ID, "改过的方案", dir)
	if err != nil {
		t.Fatalf("重命名失败: %v", err)
	}
	if renamed.ID != "custom:改过的方案" {
		t.Fatalf("重命名后的 id 不对: %+v", renamed)
	}
	if _, err := os.Stat(filepath.Join(dir, "我的方案.json")); !os.IsNotExist(err) {
		t.Error("旧方案文件应已删除")
	}
	if _, err := os.Stat(filepath.Join(dir, "改过的方案.json")); err != nil {
		t.Errorf("新方案文件应存在: %v", err)
	}

	if _, err := RenameCustomPreset(renamed.ID, "改过的方案", dir); err != nil {
		t.Errorf("重命名为同名应直接返回成功: %v", err)
	}
	if _, err := RenameCustomPreset(BuiltinDefaultPresetID, "x", dir); err == nil {
		t.Error("内置方案不允许重命名")
	}
	if err := DeleteCustomPreset(BuiltinDefaultPresetID, dir); err == nil {
		t.Error("内置方案不允许删除")
	}
	if err := DeleteCustomPreset(renamed.ID, dir); err != nil {
		t.Fatalf("删除自定义方案失败: %v", err)
	}
	if err := DeleteCustomPreset(renamed.ID, dir); err == nil {
		t.Error("重复删除应报方案不存在")
	}
}

func TestCustomPresetNameValidation(t *testing.T) {
	dir := t.TempDir()
	content, _ := GetPresetContent(BuiltinDefaultPresetID, "")
	for _, bad := range []string{"", "   ", "../escape", `a\b`, "a/b", "a:b", "a*b"} {
		if _, err := SaveCustomPreset(bad, content, dir, region.Global); err == nil {
			t.Errorf("非法方案名 %q 应被拒绝", bad)
		}
	}
	// 允许带 .json 后缀，落盘时去掉。
	saved, err := SaveCustomPreset("带后缀.json", content, dir, region.Global)
	if err != nil {
		t.Fatalf("带 .json 后缀的名称应可用: %v", err)
	}
	if saved.Name != "带后缀" {
		t.Errorf("落盘名称应去掉后缀，实际 %q", saved.Name)
	}
	if !strings.HasSuffix(saved.ID, "带后缀") {
		t.Errorf("id 不符合预期: %s", saved.ID)
	}
}

func TestSaveCustomPresetRejectsWrongRegion(t *testing.T) {
	dir := t.TempDir()
	chnaxis, err := GetPresetContent(BuiltinChnAXISPresetID, "")
	if err != nil {
		t.Fatal(err)
	}
	if _, err := SaveCustomPreset("国际服方案", chnaxis, dir, region.China); err == nil {
		t.Error("国际服专用方案保存到国服调用应被拦截")
	}
}
