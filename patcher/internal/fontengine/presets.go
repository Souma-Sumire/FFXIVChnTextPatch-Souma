package fontengine

import (
	_ "embed"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"

	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/region"
)

// 内置方案：
//   - default：上游 FFXIV-FontChanger 的默认方案（Presets/Default@96.json），
//     四个区服的字体槽都覆盖，所以每个区服都提供。
//   - chnaxis：只写 font/font_lobby 的国际服中文方案（与内置字体包同一套字形），
//     只在国际服提供。
//
// 方案文件必须在包目录内才能 embed；源文件分别是
// tools/fontchanger/Presets/Default@96.json 与 preset/ChnAXIS.json。
//
//go:embed presets/default.json
var defaultPresetJSON string

//go:embed presets/chnaxis.json
var chnaxisPresetJSON string

const (
	BuiltinDefaultPresetID = "default"
	BuiltinChnAXISPresetID = "chnaxis"
)

var BuiltinPresetMap = map[string]PresetSummary{
	BuiltinDefaultPresetID: {
		ID:          BuiltinDefaultPresetID,
		Name:        "FFXIV-FontChanger 默认方案",
		Description: "上游默认方案，导出映射覆盖各区字体槽，四个区服通用",
		Builtin:     true,
	},
	BuiltinChnAXISPresetID: {
		ID:          BuiltinChnAXISPresetID,
		Name:        "ChnAXIS 中文方案",
		Description: "国际服 font/font_lobby 字形，与内置字体包同一套",
		Builtin:     true,
	},
}

// builtinPresetIDsForRegion 返回该区服可用的内置方案 id。
// ChnAXIS 方案不含 ChnAXIS/KrnAXIS/tcaxis 字体面，只有国际服能用。
func builtinPresetIDsForRegion(r region.Region) []string {
	if r == region.Global {
		return []string{BuiltinDefaultPresetID, BuiltinChnAXISPresetID}
	}
	return []string{BuiltinDefaultPresetID}
}

// DefaultPresetForRegion 返回新建配置时各区服默认选中的方案。
func DefaultPresetForRegion(r region.Region) string {
	return BuiltinDefaultPresetID
}

// GetPresetsForRegion 返回该区服可用的方案：内置方案在前，自定义方案按名称排序在后。
// 自定义方案同样要过区服校验，避免把不适用于该客户端的方案列出来。
func GetPresetsForRegion(r region.Region, customPresetsDir string) []PresetSummary {
	ids := builtinPresetIDsForRegion(r)
	list := make([]PresetSummary, 0, len(ids)+4)
	for _, id := range ids {
		list = append(list, BuiltinPresetMap[id])
	}
	return append(list, customPresetSummaries(r, customPresetsDir)...)
}

func customPresetSummaries(r region.Region, customPresetsDir string) []PresetSummary {
	if customPresetsDir == "" {
		return nil
	}
	entries, err := os.ReadDir(customPresetsDir)
	if err != nil {
		return nil
	}
	var list []PresetSummary
	for _, entry := range entries {
		if entry.IsDir() || !strings.HasSuffix(strings.ToLower(entry.Name()), ".json") {
			continue
		}
		name := strings.TrimSuffix(entry.Name(), filepath.Ext(entry.Name()))
		data, err := os.ReadFile(filepath.Join(customPresetsDir, entry.Name()))
		if err != nil {
			continue
		}
		if err := ValidatePresetForRegion(string(data), r); err != nil {
			continue
		}
		summary := PresetSummary{
			ID:          customPresetID(name),
			Name:        name,
			Description: "用户保存的自定义方案",
			Builtin:     false,
		}
		if info, err := entry.Info(); err == nil {
			summary.Size = info.Size()
			summary.UpdatedAt = info.ModTime().Format(time.RFC3339)
		}
		list = append(list, summary)
	}
	sort.Slice(list, func(i, j int) bool {
		return strings.ToLower(list[i].Name) < strings.ToLower(list[j].Name)
	})
	return list
}

func customPresetID(name string) string {
	return "custom:" + name
}

// GetPresetContent 返回方案 JSON 内容；内置方案直接返回内嵌内容。
func GetPresetContent(id string, customPresetsDir string) (string, error) {
	switch id {
	case BuiltinDefaultPresetID:
		return defaultPresetJSON, nil
	case BuiltinChnAXISPresetID:
		return chnaxisPresetJSON, nil
	}

	if name, ok := customPresetName(id); ok && customPresetsDir != "" {
		data, err := os.ReadFile(customPresetPath(customPresetsDir, name))
		if err != nil {
			return "", fmt.Errorf("读取自定义方案失败: %w", err)
		}
		return string(data), nil
	}

	return "", fmt.Errorf("未找到字体方案: %s", id)
}

// SaveCustomPreset 把方案内容写入自定义方案目录；同名方案会被覆盖。
// 返回的 ID 形如 custom:<名称>，可直接交给 SelectFontPreset。
func SaveCustomPreset(name, content, customPresetsDir string, r region.Region) (PresetSummary, error) {
	if customPresetsDir == "" {
		return PresetSummary{}, fmt.Errorf("自定义方案目录未指定")
	}
	if err := ValidatePresetForRegion(content, r); err != nil {
		return PresetSummary{}, fmt.Errorf("方案格式不合法或与当前区服不匹配: %w", err)
	}
	safeName, err := sanitizePresetName(name)
	if err != nil {
		return PresetSummary{}, err
	}
	if err := os.MkdirAll(customPresetsDir, 0o755); err != nil {
		return PresetSummary{}, fmt.Errorf("创建方案目录失败: %w", err)
	}
	if err := os.WriteFile(customPresetPath(customPresetsDir, safeName), []byte(content), 0o644); err != nil {
		return PresetSummary{}, err
	}
	return PresetSummary{
		ID:          customPresetID(safeName),
		Name:        safeName,
		Description: "用户保存的自定义方案",
		Builtin:     false,
		Size:        int64(len(content)),
		UpdatedAt:   time.Now().Format(time.RFC3339),
	}, nil
}

// DeleteCustomPreset 删除自定义方案；内置方案不允许删除。
func DeleteCustomPreset(id, customPresetsDir string) error {
	name, ok := customPresetName(id)
	if !ok {
		return fmt.Errorf("内置方案不能删除: %s", id)
	}
	if customPresetsDir == "" {
		return fmt.Errorf("自定义方案目录未指定")
	}
	path := customPresetPath(customPresetsDir, name)
	if _, err := os.Stat(path); err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return fmt.Errorf("方案不存在: %s", name)
		}
		return err
	}
	if err := os.Remove(path); err != nil {
		return fmt.Errorf("删除方案失败: %w", err)
	}
	return nil
}

// RenameCustomPreset 重命名自定义方案，返回新的方案摘要；内置方案不允许重命名。
func RenameCustomPreset(id, newName, customPresetsDir string) (PresetSummary, error) {
	name, ok := customPresetName(id)
	if !ok {
		return PresetSummary{}, fmt.Errorf("内置方案不能重命名: %s", id)
	}
	if customPresetsDir == "" {
		return PresetSummary{}, fmt.Errorf("自定义方案目录未指定")
	}
	safeName, err := sanitizePresetName(newName)
	if err != nil {
		return PresetSummary{}, err
	}
	if safeName == name {
		return PresetSummary{ID: customPresetID(name), Name: name, Description: "用户保存的自定义方案"}, nil
	}
	src := customPresetPath(customPresetsDir, name)
	data, err := os.ReadFile(src)
	if err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return PresetSummary{}, fmt.Errorf("方案不存在: %s", name)
		}
		return PresetSummary{}, fmt.Errorf("读取方案失败: %w", err)
	}
	dst := customPresetPath(customPresetsDir, safeName)
	if _, err := os.Stat(dst); err == nil {
		return PresetSummary{}, fmt.Errorf("已存在同名方案: %s", safeName)
	}
	if err := os.WriteFile(dst, data, 0o644); err != nil {
		return PresetSummary{}, fmt.Errorf("写入方案失败: %w", err)
	}
	if err := os.Remove(src); err != nil {
		return PresetSummary{}, fmt.Errorf("删除原方案失败: %w", err)
	}
	info, _ := os.Stat(dst)
	summary := PresetSummary{
		ID:          customPresetID(safeName),
		Name:        safeName,
		Description: "用户保存的自定义方案",
		Builtin:     false,
		Size:        int64(len(data)),
	}
	if info != nil {
		summary.Size = info.Size()
		summary.UpdatedAt = info.ModTime().Format(time.RFC3339)
	}
	return summary, nil
}

// customPresetName 从 custom:<名称> 形式的 id 里取出方案名。
func customPresetName(id string) (string, bool) {
	trimmed, ok := strings.CutPrefix(id, "custom:")
	if !ok || strings.TrimSpace(trimmed) == "" {
		return "", false
	}
	return strings.TrimSpace(trimmed), true
}

func customPresetPath(dir, name string) string {
	return filepath.Join(dir, name+".json")
}

// sanitizePresetName 把用户输入收敛成安全的文件名（不含扩展名、不含路径分隔符）。
func sanitizePresetName(name string) (string, error) {
	safe := strings.TrimSpace(name)
	safe = strings.TrimSuffix(safe, ".json")
	safe = strings.TrimSpace(safe)
	if safe == "" {
		return "", fmt.Errorf("方案名称不能为空")
	}
	if strings.ContainsAny(safe, `/\:*?"<>|`) {
		return "", fmt.Errorf(`方案名称不能包含 / \ : * ? " < > | 等字符`)
	}
	if safe == "." || safe == ".." {
		return "", fmt.Errorf("方案名称不合法")
	}
	return safe, nil
}

// ValidatePresetForRegion 校验方案结构与该区服是否匹配。
// 判定同时看字体面名称和导出映射开关：FFXIV-FontChanger 的方案可以通过
// exportMapChnAxisToFont 等开关把 font 系列映射到各区字体槽，
// 这类方案（例如上游默认方案）虽然只有 AXIS 字体面，也能用于其它区服。
func ValidatePresetForRegion(presetJson string, r region.Region) error {
	var raw map[string]any
	if err := json.Unmarshal([]byte(presetJson), &raw); err != nil {
		return fmt.Errorf("JSON 格式错误: %w", err)
	}

	faces := map[string][]string{}
	extractFaces := func(m map[string]any) {
		for _, key := range []string{"faces", "fonts"} {
			items, ok := m[key].([]any)
			if !ok {
				continue
			}
			for _, item := range items {
				itemMap, ok := item.(map[string]any)
				if !ok {
					continue
				}
				name, _ := itemMap["name"].(string)
				if name = strings.TrimSpace(name); name != "" {
					faces[key] = append(faces[key], name)
				}
			}
		}
	}

	extractFaces(raw)
	if fontSets, ok := raw["fontSets"].([]any); ok {
		for _, fs := range fontSets {
			if fsMap, ok := fs.(map[string]any); ok {
				extractFaces(fsMap)
			}
		}
	}

	var names []string
	for _, list := range faces {
		names = append(names, list...)
	}
	if len(names) == 0 {
		return fmt.Errorf("字体方案未包含任何有效的字体面 (faces/fonts)")
	}

	var hasChnAxis, hasKrnAxis, hasTcAxis, hasGlobalAxis bool
	for _, name := range names {
		switch upper := strings.ToUpper(name); {
		case strings.HasPrefix(upper, "CHNAXIS"):
			hasChnAxis = true
		case strings.HasPrefix(upper, "KRNAXIS"):
			hasKrnAxis = true
		case strings.HasPrefix(upper, "TCAXIS"):
			hasTcAxis = true
		case strings.HasPrefix(upper, "AXIS"):
			hasGlobalAxis = true
		}
	}

	exportChn := boolField(raw, "exportMapChnAxisToFont")
	exportKrn := boolField(raw, "exportMapKrnAxisToFont")
	exportTc := boolField(raw, "exportMapTcAxisToFont")

	switch r {
	case region.China:
		if !hasChnAxis && !exportChn {
			return fmt.Errorf("该字体方案既没有 ChnAXIS 字体面，也没有启用 ChnAXIS 导出映射，不能用于国服客户端")
		}
	case region.Korea:
		if !hasKrnAxis && !exportKrn {
			return fmt.Errorf("该字体方案既没有 KrnAXIS 字体面，也没有启用 KrnAXIS 导出映射，不能用于韩服客户端")
		}
	case region.Taiwan:
		if !hasTcAxis && !exportTc {
			return fmt.Errorf("该字体方案既没有 tcaxis 字体面，也没有启用 tcaxis 导出映射，不能用于繁中服客户端")
		}
	default:
		if !hasGlobalAxis {
			return fmt.Errorf("该字体方案没有 AXIS 字体面，不能用于国际服客户端")
		}
	}
	return nil
}

func boolField(m map[string]any, key string) bool {
	v, ok := m[key].(bool)
	return ok && v
}
