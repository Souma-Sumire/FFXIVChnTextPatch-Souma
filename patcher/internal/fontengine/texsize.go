package fontengine

import (
	"bytes"
	"encoding/json"
	"fmt"
	"strconv"
	"strings"
)

// SideLength 是用户自带方案唯一允许的贴图边长。
//
// 一张贴图只有 4 个平面（RGBA 各一张 sideLength² 的图集），边长折半容量即降到
// 四分之一；而游戏对某个字体系列可用的贴图张数是固定的（例如 font 系列只有
// 7 张），所以小边长装不下中文字形，编译白跑一趟后才在替换阶段失败。
// 只有 4096 规格能装下全量字形，也正好对应游戏「高分辨率界面贴图」档位。
//
// 内置方案不受这里约束：主集 4096、大厅 2048 是上游验证过的规格。
const SideLength = 4096

// ConstrainSideLength 把方案里所有贴图边长改写为 SideLength，返回改写后的 JSON
// 与被改写的方案数。只用于用户自带方案，内置方案由调用方跳过。
//
// 方案没写 sideLength 时不改写：C++ 侧反序列化的缺省值本身就是 4096。
func ConstrainSideLength(presetJson string) (string, int, error) {
	if strings.TrimSpace(presetJson) == "" {
		return "", 0, fmt.Errorf("字体方案为空")
	}

	dec := json.NewDecoder(strings.NewReader(presetJson))
	dec.UseNumber()
	var root map[string]any
	if err := dec.Decode(&root); err != nil {
		return "", 0, fmt.Errorf("JSON 格式错误: %w", err)
	}

	// 顶层与 fontSets[] 两种形态都改：C++ 侧两者都能反序列化成方案。
	changed := constrainFontSet(root)
	if sets, ok := root["fontSets"].([]any); ok {
		for _, item := range sets {
			if m, ok := item.(map[string]any); ok {
				changed += constrainFontSet(m)
			}
		}
	}
	if changed == 0 {
		return presetJson, 0, nil
	}

	var buf bytes.Buffer
	enc := json.NewEncoder(&buf)
	enc.SetEscapeHTML(false)
	if err := enc.Encode(root); err != nil {
		return "", 0, fmt.Errorf("序列化字体方案失败: %w", err)
	}
	return strings.TrimRight(buf.String(), "\n"), changed, nil
}

func constrainFontSet(m map[string]any) int {
	raw, ok := m["sideLength"]
	if !ok {
		return 0
	}
	if n, ok := raw.(json.Number); ok {
		if v, err := n.Int64(); err == nil && v == int64(SideLength) {
			return 0
		}
	}
	m["sideLength"] = json.Number(strconv.Itoa(SideLength))
	return 1
}
