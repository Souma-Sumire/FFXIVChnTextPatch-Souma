package csvx

import (
	"os"
	"path/filepath"
	"reflect"
	"testing"
)

func TestParse(t *testing.T) {
	input := []byte("\xEF\xBB\xBF# 这是注释\r\n" +
		"  # 带缩进的注释\n" +
		"\n" +
		"key, 0 , 1 \r\n" +
		"#,int,str\r\n" +
		"100, \"hello, world\", \"quote \"\"inside\"\"\"\n" +
		"101,  val  ,  \n")

	got := Parse(input)
	want := [][]string{
		{"key", "0", "1"},
		{"100", "hello, world", "quote \"inside\""},
		{"101", "val", ""},
	}

	if !reflect.DeepEqual(got, want) {
		t.Fatalf("Parse() = %#v, want %#v", got, want)
	}
}

func TestParseFile(t *testing.T) {
	tmpDir := t.TempDir()
	csvPath := filepath.Join(tmpDir, "test.csv")
	content := "col1,col2\nval1,val2\n"
	if err := os.WriteFile(csvPath, []byte(content), 0o644); err != nil {
		t.Fatal(err)
	}

	rows, err := ParseFile(csvPath)
	if err != nil {
		t.Fatalf("ParseFile 失败: %v", err)
	}
	want := [][]string{
		{"col1", "col2"},
		{"val1", "val2"},
	}
	if !reflect.DeepEqual(rows, want) {
		t.Fatalf("ParseFile() = %#v, want %#v", rows, want)
	}

	unclosedInput := []byte("1, \"unclosed\n2, val2\n3, val3\n")
	unclosedRows := Parse(unclosedInput)
	if len(unclosedRows) < 2 {
		t.Fatalf("未闭合引号不应吞掉后续行，实际解析出 %d 行", len(unclosedRows))
	}

	unclosedPath := filepath.Join(tmpDir, "unclosed.csv")
	if err := os.WriteFile(unclosedPath, unclosedInput, 0o644); err != nil {
		t.Fatal(err)
	}
	if _, err := ParseFile(unclosedPath); err == nil {
		t.Fatal("未闭合引号应返回 error，实际为 nil")
	}

	_, err = ParseFile(filepath.Join(tmpDir, "non_existent.csv"))
	if err == nil {
		t.Fatal("期望读取不存在文件返回 error，实际为 nil")
	}
}
