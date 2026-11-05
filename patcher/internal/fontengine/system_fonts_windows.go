//go:build windows

package fontengine

import (
	"sort"
	"strings"

	"golang.org/x/sys/windows/registry"
)

// ListSystemFonts 枚举系统已安装字体的 Family 名称
func ListSystemFonts() ([]string, error) {
	key, err := registry.OpenKey(registry.LOCAL_MACHINE, `SOFTWARE\Microsoft\Windows NT\CurrentVersion\Fonts`, registry.READ)
	if err != nil {
		return defaultFontList(), nil
	}
	defer key.Close()

	valNames, err := key.ReadValueNames(-1)
	if err != nil {
		return defaultFontList(), nil
	}

	seen := make(map[string]bool)
	var list []string

	for _, name := range valNames {
		// 注册表中字体名称格式常为 "Microsoft YaHei & Microsoft YaHei UI (TrueType)" 或 "SimSun (TrueType)"
		cleaned := name
		if idx := strings.Index(cleaned, " ("); idx != -1 {
			cleaned = cleaned[:idx]
		}
		for _, part := range strings.Split(cleaned, "&") {
			trimmed := strings.TrimSpace(part)
			if trimmed != "" && !seen[trimmed] {
				seen[trimmed] = true
				list = append(list, trimmed)
			}
		}
	}

	sort.Strings(list)
	return list, nil
}

func defaultFontList() []string {
	return []string{
		"Microsoft YaHei",
		"SimSun",
		"SimHei",
		"Source Han Sans SC",
		"WenQuanYi Micro Hei",
		"Arial",
		"Segoe UI",
		"Tahoma",
	}
}
