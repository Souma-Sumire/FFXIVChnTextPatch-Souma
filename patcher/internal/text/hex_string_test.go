package text

import (
	"bytes"
	"errors"
	"strings"
	"testing"
)

func TestParseTargetString(t *testing.T) {
	cases := []struct {
		name  string
		input string
		want  []byte
	}{
		{"仅控制码", "<hex:02100103>", []byte{0x02, 0x10, 0x01, 0x03}},
		{"尾部控制码", "abc<hex:02100103>", []byte{'a', 'b', 'c', 0x02, 0x10, 0x01, 0x03}},
		{"首部控制码", "<hex:02100103>abc", []byte{0x02, 0x10, 0x01, 0x03, 'a', 'b', 'c'}},
		{"短控制码", "a<hex:02>b", []byte{'a', 0x02, 'b'}},
		{"连续控制码", "<hex:01><hex:02>", []byte{0x01, 0x02}},
		{"换行符", "第一行<hex:02100103>第二行",
			append(append([]byte("第一行"), 0x02, 0x10, 0x01, 0x03), []byte("第二行")...)},
		{"多字节与标签交替", "<hex:02280CFF05>EObj<hex:F61E9D210103>",
			append(append([]byte{0x02, 0x28, 0x0C, 0xFF, 0x05}, []byte("EObj")...), 0xF6, 0x1E, 0x9D, 0x21, 0x01, 0x03)},
		{"无标签", "普通文本", []byte("普通文本")},
		{"空字符串", "", []byte{}},
		// 没有冒号的 "<hex" 不是标签，必须原样保留，不能被截成十六进制字节。
		{"缺冒号", "<hexabc>", []byte("<hexabc>")},
		{"缺冒号混排", "a<hexdead>b", []byte("a<hexdead>b")},
		{"大写标签", "<HEX:02>", []byte("<HEX:02>")},
		{"半截标签前缀", "<hex", []byte("<hex")},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			got, err := ParseTargetString(c.input)
			if err != nil {
				t.Fatalf("ParseTargetString 返回错误: %v", err)
			}
			if !bytes.Equal(got, c.want) {
				t.Fatalf("ParseTargetString(%q) = % x, want % x", c.input, got, c.want)
			}
		})
	}
}

func TestParseTargetStringRejectsNestedTag(t *testing.T) {
	_, err := ParseTargetString("<hex:02<hex:03>>")
	if err == nil {
		t.Fatal("非法嵌套标签应返回错误")
	}
	if !errors.Is(err, ErrNestedHexTag) {
		t.Fatalf("错误类型应为 ErrNestedHexTag，实际 %v", err)
	}
}

func TestParseTargetStringRejectsBadHex(t *testing.T) {
	if _, err := ParseTargetString("<hex:>"); err == nil {
		t.Fatal("空载荷十六进制标签应返回错误")
	}
	if _, err := ParseTargetString("<hex:   >"); err == nil {
		t.Fatal("空白十六进制标签应返回错误")
	}
	if _, err := ParseTargetString("<hex:zz>"); err == nil {
		t.Fatal("非法十六进制内容应返回错误")
	}
	if _, err := ParseTargetString("<hex:abc>"); err == nil {
		t.Fatal("奇数长度十六进制内容应返回错误")
	}
}

// 未闭合的 <hex:...> 必须报错：否则半截标签会作为普通文本写进游戏。
func TestParseTargetStringRejectsUnclosedTag(t *testing.T) {
	for _, input := range []string{"<hex:0210", "abc<hex:02", "<hex:", "a<hex:02>b<hex:03"} {
		_, err := ParseTargetString(input)
		if err == nil {
			t.Fatalf("%q 的标签未闭合，应返回错误", input)
		}
		if !errors.Is(err, ErrUnclosedHexTag) {
			t.Fatalf("错误类型应为 ErrUnclosedHexTag，实际 %v", err)
		}
	}
}

func TestTransformPlainProtectsHexTags(t *testing.T) {
	input := "汉<hex:02100103>字<hex:02280CFF05>EObj<hex:F61E9D210103>"
	replaced := strings.NewReplacer("汉", "漢", "字", "字").Replace
	got := TransformPlain(input, replaced)
	want := "漢<hex:02100103>字<hex:02280CFF05>EObj<hex:F61E9D210103>"
	if got != want {
		t.Fatalf("TransformPlain = %q, want %q", got, want)
	}
}
