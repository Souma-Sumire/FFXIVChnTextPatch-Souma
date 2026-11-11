package gamecfg

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

const sampleConfig0 = `<FINAL FANTASY XIV Config File>

<Version>
ConfigVersion	2

<Display Settings>
MainAdapter	
ScreenWidth	1920
ScreenHeight	1080
UiAssetType	0
CharaLight	0

<Cutscene Settings>
CutsceneMovieOpening	0
`

const sampleConfigWithoutEntry = `<FINAL FANTASY XIV Config File>

<Version>
ConfigVersion	2

<Display Settings>
MainAdapter	
ScreenWidth	1920
ScreenHeight	1080
CharaLight	0

<Cutscene Settings>
CutsceneMovieOpening	0
`

func TestReadUiAssetType(t *testing.T) {
	dir := t.TempDir()
	p0 := filepath.Join(dir, "FFXIV_0.cfg")
	if err := os.WriteFile(p0, []byte(strings.ReplaceAll(sampleConfig0, "\n", "\r\n")), 0o644); err != nil {
		t.Fatal(err)
	}

	val, exists, err := ReadUiAssetType(p0)
	if err != nil {
		t.Fatalf("读取失败: %v", err)
	}
	if !exists || val != 0 {
		t.Fatalf("预期 val=0, exists=true, 实际 val=%d, exists=%v", val, exists)
	}

	pNo := filepath.Join(dir, "FFXIV_no.cfg")
	if err := os.WriteFile(pNo, []byte(sampleConfigWithoutEntry), 0o644); err != nil {
		t.Fatal(err)
	}
	val, exists, err = ReadUiAssetType(pNo)
	if err != nil {
		t.Fatalf("读取失败: %v", err)
	}
	if exists || val != -1 {
		t.Fatalf("预期 val=-1, exists=false, 实际 val=%d, exists=%v", val, exists)
	}
}

func TestEnsureHighResolutionUiAsset_ModifiesAndBacksUp(t *testing.T) {
	dir := t.TempDir()
	p := filepath.Join(dir, "FFXIV.cfg")
	origContent := strings.ReplaceAll(sampleConfig0, "\n", "\r\n")
	if err := os.WriteFile(p, []byte(origContent), 0o644); err != nil {
		t.Fatal(err)
	}

	res, err := EnsureHighResolutionUiAsset(p)
	if err != nil {
		t.Fatalf("修改失败: %v", err)
	}
	if !res.Modified {
		t.Fatal("预期 Modified 为 true")
	}
	if res.Previous != 0 || res.Current != 1 {
		t.Fatalf("预期 Previous=0, Current=1, 实际 Previous=%d, Current=%d", res.Previous, res.Current)
	}

	bakPath := p + ".bak"
	bakData, err := os.ReadFile(bakPath)
	if err != nil {
		t.Fatalf("备份文件未生成: %v", err)
	}
	if string(bakData) != origContent {
		t.Fatal("备份内容与原始配置不一致")
	}

	curData, err := os.ReadFile(p)
	if err != nil {
		t.Fatalf("读取修改后的配置失败: %v", err)
	}
	if !strings.Contains(string(curData), "UiAssetType\t1") {
		t.Fatalf("配置未成功包含 UiAssetType\\t1: %s", string(curData))
	}
	if !strings.Contains(string(curData), "\r\n") {
		t.Fatal("未保留 CRLF 换行符")
	}

	res2, err := EnsureHighResolutionUiAsset(p)
	if err != nil {
		t.Fatalf("二次调用失败: %v", err)
	}
	if res2.Modified {
		t.Fatal("已为 1 时预期 Modified 为 false")
	}
}

func TestEnsureHighResolutionUiAsset_InsertsWhenMissing(t *testing.T) {
	dir := t.TempDir()
	p := filepath.Join(dir, "FFXIV.cfg")
	origContent := strings.ReplaceAll(sampleConfigWithoutEntry, "\n", "\r\n")
	if err := os.WriteFile(p, []byte(origContent), 0o644); err != nil {
		t.Fatal(err)
	}

	res, err := EnsureHighResolutionUiAsset(p)
	if err != nil {
		t.Fatalf("插入失败: %v", err)
	}
	if !res.Modified {
		t.Fatal("预期 Modified 为 true")
	}
	if res.Previous != -1 || res.Current != 1 {
		t.Fatalf("预期 Previous=-1, Current=1, 实际 Previous=%d, Current=%d", res.Previous, res.Current)
	}

	curData, err := os.ReadFile(p)
	if err != nil {
		t.Fatalf("读取修改后的配置失败: %v", err)
	}
	if !strings.Contains(string(curData), "UiAssetType\t1\r\n") {
		t.Fatalf("插入的内容缺少正确换行: %s", string(curData))
	}
}
