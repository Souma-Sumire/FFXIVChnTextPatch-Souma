package region

import (
	"reflect"
	"testing"
)

func TestParseLanguage(t *testing.T) {
	tests := []struct {
		input   string
		want    Language
		wantErr bool
	}{
		{"GLOBAL", GLOBAL, false},
		{"global", GLOBAL, false},
		{" gl ", GLOBAL, false},
		{"JA", JA, false},
		{"ja", JA, false},
		{"EN", EN, false},
		{"DE", DE, false},
		{"FR", FR, false},
		{"CHS", CHS, false},
		{"cn", CHS, false},
		{"zh", CHS, false},
		{"zh-cn", CHS, false},
		{"TC", TC, false},
		{"cht", TC, false},
		{"KO", KO, false},
		{"ko", KO, false},
		{"UNKNOWN", "", true},
		{"", "", true},
	}

	for _, tt := range tests {
		got, err := ParseLanguage(tt.input)
		if (err != nil) != tt.wantErr {
			t.Errorf("ParseLanguage(%q) error = %v, wantErr %v", tt.input, err, tt.wantErr)
			continue
		}
		if got != tt.want {
			t.Errorf("ParseLanguage(%q) = %v, want %v", tt.input, got, tt.want)
		}
	}
}

func TestRegionMapping(t *testing.T) {
	if Of(CHS) != China || !IsGlobal(GLOBAL) || IsGlobal(CHS) || IsGlobal(TC) || IsGlobal(KO) {
		t.Errorf("Of() 或 IsGlobal() 逻辑不符合预期")
	}

	if BackupFolder(GLOBAL) != "global" || BackupFolder(CHS) != "chs" {
		t.Errorf("BackupFolder() 路径不符")
	}

	globalTargets := TargetLanguagesFor(GLOBAL)
	wantGlobal := []string{"JA", "EN", "DE", "FR"}
	if !reflect.DeepEqual(globalTargets, wantGlobal) {
		t.Errorf("TargetLanguagesFor(GLOBAL) = %v, want %v", globalTargets, wantGlobal)
	}

	chsTargets := TargetLanguagesFor(CHS)
	if !reflect.DeepEqual(chsTargets, []string{"CHS"}) {
		t.Errorf("TargetLanguagesFor(CHS) = %v, want %v", chsTargets, []string{"CHS"})
	}

	if DisplayName(China) != "国服" || DisplayName(Global) != "国际服" {
		t.Errorf("DisplayName() 不符合预期")
	}

	if DisplayNameFor(CHS) != "国服" || DisplayNameFor(GLOBAL) != "国际服" {
		t.Errorf("DisplayNameFor() 不符合预期")
	}
}
