package pipeline

import (
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"regexp"
	"strconv"
	"strings"
	"sync"

	"github.com/longbridgeapp/opencc"

	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/text"
)

// RuleData 对应 resource/rules.json，规则数据只随资源下发，不编入可执行文件。
type RuleData struct {
	SpecialWord map[string]string `json:"specialWord"`
	MateriaMap  map[string]string `json:"materiaMap"`
}

// ParseRules 从 rules.json 字节流创建规则集合。
// 缺少某一段数据时该规则被标注为不可用；两段都缺失说明文件无意义，额外返回错误。
func ParseRules(data []byte) ([]Rule, error) {
	var doc RuleData
	if err := json.Unmarshal(data, &doc); err != nil {
		return nil, fmt.Errorf("解析 rules.json 失败: %w", err)
	}
	rules := RulesFromData(doc)
	if len(doc.SpecialWord) == 0 && len(doc.MateriaMap) == 0 {
		return rules, errors.New("rules.json 未包含任何规则数据")
	}
	return rules, nil
}

// LoadRulesFromFile 读取并创建规则集合。
func LoadRulesFromFile(path string) ([]Rule, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	return ParseRules(data)
}

// RulesFromData 用给定的映射表创建规则集合。
// 缺少数据的规则仍保留在集合中，由 Meta 标注为不可用，前端据此禁用开关。
func RulesFromData(data RuleData) []Rule {
	return []Rule{
		NewTreasureMapAliasRuleFrom(data.SpecialWord),
		NewMateriaStatSuffixRuleFrom(data.MateriaMap),
		NewKTGCheatRule(),
		NewOpenCCS2TWRule(),
	}
}

// DataLessRules 返回不含规则数据的集合，用于 rules.json 缺失时显式标注不可用。
func DataLessRules() []Rule { return RulesFromData(RuleData{}) }

// DefaultRuleStates 返回各规则的默认开关状态，与规则数据是否存在无关。
func DefaultRuleStates() map[string]bool {
	states := make(map[string]bool, 4)
	for _, rule := range RulesFromData(RuleData{}) {
		states[rule.ID()] = rule.Meta().DefaultOn
	}
	return states
}

func buildTreasureAliases(src map[string]string) map[string]string {
	out := make(map[string]string, len(src)*2)
	for key, value := range src {
		out[key] = value
		if i := strings.Index(value, "（G"); i > 0 {
			out[value[:i]] = value
		}
	}
	return out
}

// TreasureMapAliasRule 为藏宝图名称追加 G1~G18 等级标注。
type TreasureMapAliasRule struct {
	aliases map[string]string
}

// NewTreasureMapAliasRuleFrom 使用给定映射表创建藏宝图别名规则。
func NewTreasureMapAliasRuleFrom(src map[string]string) *TreasureMapAliasRule {
	return &TreasureMapAliasRule{aliases: buildTreasureAliases(src)}
}

// ID 返回规则 ID。
func (r *TreasureMapAliasRule) ID() string { return "treasure_map_alias" }

// Meta 返回规则元数据。缺少 rules.json 数据时标记为不可用。
func (r *TreasureMapAliasRule) Meta() RuleMeta {
	meta := RuleMeta{
		ID:          r.ID(),
		Name:        "藏宝图等级标注",
		Description: "在藏宝图名称后追加（G1）~（G18）等级后缀",
		Category:    "文本优化",
		DefaultOn:   true,
		Status:      RuleStatusOK,
	}
	if len(r.aliases) == 0 {
		meta.Status = RuleStatusUnavailable
		meta.StatusDetail = "缺少规则数据 rules.json"
	}
	return meta
}

// Apply 执行转换。
func (r *TreasureMapAliasRule) Apply(ctx *CellContext) string {
	if v, ok := r.aliases[ctx.TargetStr]; ok {
		return v
	}
	if v, ok := r.aliases[ctx.SourceStr]; ok {
		return v
	}
	return ctx.TargetStr
}

var materiaPattern = regexp.MustCompile(`^(..)魔晶石.+?型$`)

// MateriaStatSuffixRule 为 Item 表中的魔晶石名称追加属性后缀。
type MateriaStatSuffixRule struct {
	stats map[string]string
}

// NewMateriaStatSuffixRuleFrom 使用给定映射表创建魔晶石属性后缀规则。
func NewMateriaStatSuffixRuleFrom(src map[string]string) *MateriaStatSuffixRule {
	stats := make(map[string]string, len(src))
	for k, v := range src {
		stats[k] = v
	}
	return &MateriaStatSuffixRule{stats: stats}
}

// ID 返回规则 ID。
func (r *MateriaStatSuffixRule) ID() string { return "materia_stat_suffix" }

// Meta 返回规则元数据。缺少 rules.json 数据时标记为不可用。
func (r *MateriaStatSuffixRule) Meta() RuleMeta {
	meta := RuleMeta{
		ID:          r.ID(),
		Name:        "魔晶石属性后缀",
		Description: "在魔晶石名称后追加属性名，如（暴击）",
		Category:    "文本优化",
		DefaultOn:   true,
		Status:      RuleStatusOK,
	}
	if len(r.stats) == 0 {
		meta.Status = RuleStatusUnavailable
		meta.StatusDetail = "缺少规则数据 rules.json"
	}
	return meta
}

// Apply 执行转换。
func (r *MateriaStatSuffixRule) Apply(ctx *CellContext) string {
	if ctx.TableName != "Item" {
		return ctx.TargetStr
	}
	m := materiaPattern.FindStringSubmatch(ctx.TargetStr)
	if m == nil {
		return ctx.TargetStr
	}
	stat, ok := r.stats[m[1]]
	if !ok {
		return ctx.TargetStr
	}
	return m[0] + "（" + stat + "）"
}

// OpenCCS2TWRule 使用纯 Go OpenCC 将简体中文转换为台湾正体。
// 字典在首次使用时才加载，避免未启用该规则时拖慢启动。
type OpenCCS2TWRule struct {
	once      sync.Once
	converter *opencc.OpenCC
	initErr   error
}

// NewOpenCCS2TWRule 创建台湾正体转换规则。
func NewOpenCCS2TWRule() *OpenCCS2TWRule { return &OpenCCS2TWRule{} }

func (r *OpenCCS2TWRule) ensureInit() {
	r.once.Do(func() {
		r.converter, r.initErr = opencc.New("s2tw")
	})
}

// ID 返回规则 ID。
func (r *OpenCCS2TWRule) ID() string { return "opencc_s2tw" }

// Meta 返回规则元数据。
func (r *OpenCCS2TWRule) Meta() RuleMeta {
	return RuleMeta{
		ID:          r.ID(),
		Name:        "臺灣繁體轉換",
		Description: "將簡體中文文本轉換為臺灣繁體字",
		Category:    "文本优化",
		DefaultOn:   false,
		Status:      RuleStatusOK,
	}
}

// Apply 执行转换，仅处理 <hex:...> 之外的普通文本。
func (r *OpenCCS2TWRule) Apply(ctx *CellContext) string {
	r.ensureInit()
	if r.initErr != nil || r.converter == nil || ctx.TargetStr == "" {
		return ctx.TargetStr
	}
	var convertErr error
	out := text.TransformPlain(ctx.TargetStr, func(plain string) string {
		if plain == "" {
			return ""
		}
		converted, err := r.converter.Convert(plain)
		if err != nil {
			convertErr = err
			return plain
		}
		return converted
	})
	if convertErr != nil {
		return ctx.TargetStr
	}
	return out
}

// InitError 返回 OpenCC 初始化错误（供启动日志诊断）。
func (r *OpenCCS2TWRule) InitError() error {
	r.ensureInit()
	if r.initErr != nil {
		return fmt.Errorf("OpenCC 初始化失败: %w", r.initErr)
	}
	return nil
}

// KTGCheatRule 将 KTGTypeWordTextData 表中拼音/按键字库列替换为 1~9 数字循环。
type KTGCheatRule struct{}

// NewKTGCheatRule 创建魔光键影作弊规则。
func NewKTGCheatRule() *KTGCheatRule { return &KTGCheatRule{} }

// ID 返回规则 ID。
func (r *KTGCheatRule) ID() string { return "ktg_cheat" }

// Meta 返回规则元数据。
func (r *KTGCheatRule) Meta() RuleMeta {
	return RuleMeta{
		ID:          r.ID(),
		Name:        "魔光键影作弊（将字库改为纯数字）",
		Description: "将魔光键影的拼音替换为 1~9 数字循环，便于快速击打",
		Category:    "文本优化",
		DefaultOn:   false,
		Status:      RuleStatusOK,
	}
}

// Apply 执行转换。针对 KTGTypeWordTextData 表第三列（ColIndex 为 1），转换为 (RowKey % 9) + 1。
func (r *KTGCheatRule) Apply(ctx *CellContext) string {
	if !strings.EqualFold(ctx.TableName, "KTGTypeWordTextData") || ctx.ColIndex != 1 {
		return ctx.TargetStr
	}
	val := (ctx.RowKey % 9) + 1
	return strconv.Itoa(val)
}

