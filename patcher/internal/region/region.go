// Package region 定义客户端语言与区服的映射关系。
package region

import (
	"fmt"
	"strings"
)

// Language 表示客户端原始语言或区服标识。
type Language string

const (
	GLOBAL Language = "GLOBAL"
	JA     Language = "JA"
	EN     Language = "EN"
	DE     Language = "DE"
	FR     Language = "FR"
	CHS    Language = "CHS"
	TC     Language = "TC"
	KO     Language = "KO"
)

// Region 表示区服。
type Region string

const (
	Global Region = "global"
	China  Region = "chs"
	Taiwan Region = "tc"
	Korea  Region = "ko"
)

// ParseLanguage 解析前端传入的语言或区服代码。
func ParseLanguage(s string) (Language, error) {
	switch strings.ToUpper(strings.TrimSpace(s)) {
	case "GLOBAL", "GL":
		return GLOBAL, nil
	case "JA":
		return JA, nil
	case "EN":
		return EN, nil
	case "DE":
		return DE, nil
	case "FR":
		return FR, nil
	case "CHS", "CN", "ZH", "ZH-CN":
		return CHS, nil
	case "TC", "CHT":
		return TC, nil
	case "KO":
		return KO, nil
	default:
		return "", fmt.Errorf("不支持的客户端语言或区服: %s", s)
	}
}

// Of 返回语言所属区服。
func Of(l Language) Region {
	switch l {
	case CHS:
		return China
	case TC:
		return Taiwan
	case KO:
		return Korea
	default:
		return Global
	}
}

// IsGlobal 判断是否国际服。
func IsGlobal(l Language) bool { return Of(l) == Global }

// BackupFolder 返回该区服在 backup/ 下的子目录名。
func BackupFolder(l Language) string { return string(Of(l)) }

// TargetLanguagesFor 返回该区服在执行汉化时需要处理的全部语言后缀。
// 国际服将一次性处理日英德法所有语言（JA, EN, DE, FR），无需用户手动切换。
func TargetLanguagesFor(l Language) []string {
	if IsGlobal(l) {
		return []string{"JA", "EN", "DE", "FR"}
	}
	return []string{string(l)}
}
