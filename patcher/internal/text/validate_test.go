package text

import (
	"errors"
	"testing"
)

func TestValidateSeStringValid(t *testing.T) {
	cases := []struct {
		name  string
		input string
	}{
		{"纯文本", "冒险者"},
		{"换行符", "第一行<hex:02100103>第二行"},
		{"ClassJob引用", "<hex:02280EFF09>ClassJob<hex:E8020103>"},
		{"HeadAll嵌套", "<hex:022D14FF12><hex:02280EFF09>ClassJob<hex:E8021F03><hex:03>"},
		{"Format指令", "<hex:022206E802FF022C03>"},
		{"If条件含二元表达式", "<hex:020703E80203><hex:020816E3DBFF04> 12<hex:FF06><hex:022002DB03><hex:FF06><hex:022002DB03><hex:03>"},
		{"颜色与关闭", "<hex:024804F201FC03>警告<hex:0248020103>"},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			b, err := ParseTargetString(c.input)
			if err != nil {
				t.Fatalf("ParseTargetString 失败: %v", err)
			}
			if err := ValidateSeString(b); err != nil {
				t.Fatalf("ValidateSeString 不应报错: %v", err)
			}
		})
	}
}

func TestValidateSeStringErrors(t *testing.T) {
	cases := []struct {
		name  string
		input string
	}{
		{"长度过长越界", "<hex:02100503>"},
		{"长度过短未落在03", "<hex:022802FF09>ClassJob<hex:E8020103>"},
		{"缺少03结束符", "<hex:021001>"},
		{"孤立的03", "文本<hex:03>"},
		{"内嵌FF长度越界", "<hex:020805E802FF0A>abc<hex:03>"},
		{"变长整型截断", "<hex:0208F20103>"},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			b, err := ParseTargetString(c.input)
			if err != nil {
				t.Fatalf("ParseTargetString 失败: %v", err)
			}
			err = ValidateSeString(b)
			if err == nil {
				t.Fatalf("%s 应被 ValidateSeString 拦截", c.name)
			}
			if !errors.Is(err, ErrInvalidSeString) {
				t.Fatalf("错误类型应包含 ErrInvalidSeString，实际: %v", err)
			}
		})
	}
}
