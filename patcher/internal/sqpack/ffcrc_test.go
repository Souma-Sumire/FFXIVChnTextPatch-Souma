package sqpack

import "testing"

// 期望值由旧版 FFCRC.ComputeCRC（Java int32）生成，确保有符号结果完全一致。
var crcCases = []struct {
	input string
	want  int32
}{
	{"", -1},
	{"exd", -476350055},
	{"common/font", 1713442675},
	{"root.exl", 1370848956},
	{"exd/achievement_0_ja.exd", -141637814},
	{"axis_12.fdt", -1447579230},
	{"ab", 1635563410},
	{"abc", -891568579},
	{"abcd", 310194926},
	{"abcde", 2054694810},
	{"font1.tex", -1115802020},
}

func TestComputeCRCMatchesJava(t *testing.T) {
	for _, c := range crcCases {
		if got := ComputeCRC([]byte(c.input)); got != c.want {
			t.Errorf("ComputeCRC(%q) = %d, want %d", c.input, got, c.want)
		}
	}
}

func TestComputeCRCStringLowercases(t *testing.T) {
	if got, want := ComputeCRCString("Common/Font"), int32(1713442675); got != want {
		t.Fatalf("ComputeCRCString 应转小写后计算: got %d want %d", got, want)
	}
	if got, want := ComputeCRCString("AXIS_12.FDT"), int32(-1447579230); got != want {
		t.Fatalf("ComputeCRCString 应转小写后计算: got %d want %d", got, want)
	}
}
