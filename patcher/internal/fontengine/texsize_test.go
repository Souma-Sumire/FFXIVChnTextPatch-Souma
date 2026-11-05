package fontengine

import (
	"encoding/json"
	"strings"
	"testing"
)

func decodePreset(t *testing.T, s string) map[string]any {
	t.Helper()
	var m map[string]any
	if err := json.Unmarshal([]byte(s), &m); err != nil {
		t.Fatalf("解析改写结果失败: %v", err)
	}
	return m
}

func sideLengthOf(t *testing.T, m map[string]any) int {
	t.Helper()
	v, ok := m["sideLength"].(float64)
	if !ok {
		t.Fatalf("缺少 sideLength: %v", m)
	}
	return int(v)
}

// 2048 的 lobby 子集与 4096 的主集都要被拉到 4096，其余字段原样保留。
func TestConstrainSideLengthRewritesAllSets(t *testing.T) {
	in := `{"fontSets":[{"sideLength":2048,"texFilenameFormat":"font_lobby{}.tex","faces":[{"name":"AXIS_12"}]},{"sideLength":4096,"texFilenameFormat":"font{}.tex"}],"exportMapChnAxisToFont":true}`

	out, changed, err := ConstrainSideLength(in)
	if err != nil {
		t.Fatalf("ConstrainSideLength 失败: %v", err)
	}
	if changed != 1 {
		t.Fatalf("应只有 lobby 子集被改写，实际 changed=%d", changed)
	}

	m := decodePreset(t, out)
	sets, ok := m["fontSets"].([]any)
	if !ok || len(sets) != 2 {
		t.Fatalf("fontSets 结构被破坏: %v", m["fontSets"])
	}
	for i, item := range sets {
		if got := sideLengthOf(t, item.(map[string]any)); got != SideLength {
			t.Fatalf("fontSets[%d] 边长应为 %d，实际 %d", i, SideLength, got)
		}
	}
	if m["exportMapChnAxisToFont"] != true {
		t.Fatal("改写不应丢掉其它顶层字段")
	}
	faces, ok := sets[0].(map[string]any)["faces"].([]any)
	if !ok || len(faces) != 1 {
		t.Fatal("改写不应丢掉 fontSet 内的字段")
	}
}

// 已经全是 4096 时不该重新序列化：返回的字符串与输入逐字节相同。
func TestConstrainSideLengthIsIdempotent(t *testing.T) {
	in := `{"fontSets":[{"sideLength":4096,"texFilenameFormat":"font{}.tex"}]}`

	out, changed, err := ConstrainSideLength(in)
	if err != nil {
		t.Fatalf("ConstrainSideLength 失败: %v", err)
	}
	if changed != 0 {
		t.Fatalf("无需改写时 changed 应为 0，实际 %d", changed)
	}
	if out != in {
		t.Fatalf("无需改写时不应改动方案内容:\n输入 %s\n输出 %s", in, out)
	}
}

// 顶层形态（没有 fontSets）同样要处理。
func TestConstrainSideLengthTopLevel(t *testing.T) {
	in := `{"sideLength":1024,"faces":[{"name":"AXIS_12"}]}`

	out, changed, err := ConstrainSideLength(in)
	if err != nil {
		t.Fatalf("ConstrainSideLength 失败: %v", err)
	}
	if changed != 1 {
		t.Fatalf("顶层边长应被改写，实际 changed=%d", changed)
	}
	if got := sideLengthOf(t, decodePreset(t, out)); got != SideLength {
		t.Fatalf("顶层边长应为 %d，实际 %d", SideLength, got)
	}
}

// 方案没写 sideLength 时不动：C++ 侧缺省值本身就是 4096。
func TestConstrainSideLengthLeavesMissingField(t *testing.T) {
	in := `{"fontSets":[{"texFilenameFormat":"font{}.tex"}]}`

	out, changed, err := ConstrainSideLength(in)
	if err != nil {
		t.Fatalf("ConstrainSideLength 失败: %v", err)
	}
	if changed != 0 || out != in {
		t.Fatalf("缺少 sideLength 时不应改写，changed=%d out=%s", changed, out)
	}
}

func TestConstrainSideLengthRejectsBadInput(t *testing.T) {
	if _, _, err := ConstrainSideLength(""); err == nil {
		t.Fatal("空方案应报错")
	}
	if _, _, err := ConstrainSideLength("{not json"); err == nil {
		t.Fatal("非法 JSON 应报错")
	} else if !strings.Contains(err.Error(), "JSON") {
		t.Fatalf("错误信息应说明 JSON 格式问题，实际: %v", err)
	}
}
