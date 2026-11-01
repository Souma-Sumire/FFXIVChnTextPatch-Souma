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

// defaultSpecialWord 是内置的藏宝图别名（日文原名 -> 国服名称）。
// 国服原名由带（Gx）后缀的值反推得到。
var defaultSpecialWord = map[string]string{
	"古ぼけた地図G1":  "陈旧的鞣革地图（G1）",
	"古ぼけた地図G2":  "陈旧的山羊革地图（G2）",
	"古ぼけた地図G3":  "陈旧的巨蟾蜍革地图（G3）",
	"古ぼけた地図G4":  "陈旧的野猪革地图（G4）",
	"古ぼけた地図G5":  "陈旧的毒蜥蜴革地图（G5）",
	"古ぼけた地図G6":  "陈旧的古鸟革地图（G6）",
	"古ぼけた地図G7":  "陈旧的飞龙革地图（G7）",
	"古ぼけた地図G8":  "陈旧的巨龙革地图（G8）",
	"古ぼけた地図G9":  "陈旧的迦迦纳怪鸟革地图（G9）",
	"古ぼけた地図G10": "陈旧的瞪羚革地图（G10）",
	"古ぼけた地図G11": "陈旧的绿飘龙革地图（G11）",
	"古ぼけた地図G12": "陈旧的缠尾蛟革地图（G12）",
	"古ぼけた地図G13": "陈旧的赛加羚羊革地图（G13）",
	"古ぼけた地図G14": "陈旧的金毗罗鳄革地图（G14）",
	"古ぼけた地図G15": "陈旧的蛇牛革地图（G15）",
	"古ぼけた地図G16": "陈旧的银狼革地图（G16）",
	"古ぼけた地図G17": "陈旧的狞豹革地图（G17）",
	"古ぼけた地図G18": "陈旧的卡冈图亚革地图（G18）",
}

// defaultMateriaMap 与旧版 scripts/special.ts 中的 materiaMap 完全一致。
var defaultMateriaMap = map[string]string{
	"信力": "信仰",
	"神眼": "直击",
	"武略": "暴击",
	"雄略": "信念",
	"刚柔": "坚韧",
	"达识": "获得力",
	"博识": "鉴别力",
	"器识": "采集力",
	"名匠": "作业精度",
	"魔匠": "制作力",
	"巨匠": "加工精度",
	"战技": "技能速度",
	"咏唱": "咏唱速度",
}

// RuleData 对应 resource/rules.json。
type RuleData struct {
	SpecialWord map[string]string `json:"specialWord"`
	MateriaMap  map[string]string `json:"materiaMap"`
}

// ParseRules 从 rules.json 字节流创建规则集合。
func ParseRules(data []byte) ([]Rule, error) {
	var doc RuleData
	if err := json.Unmarshal(data, &doc); err != nil {
		return nil, fmt.Errorf("解析 rules.json 失败: %w", err)
	}
	if len(doc.SpecialWord) == 0 && len(doc.MateriaMap) == 0 {
		return nil, errors.New("rules.json 内容为空")
	}
	return RulesFromData(doc), nil
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
func RulesFromData(data RuleData) []Rule {
	specialWord := data.SpecialWord
	if len(specialWord) == 0 {
		specialWord = defaultSpecialWord
	}
	materia := data.MateriaMap
	if len(materia) == 0 {
		materia = defaultMateriaMap
	}
	return []Rule{
		NewTreasureMapAliasRuleFrom(specialWord),
		NewMateriaStatSuffixRuleFrom(materia),
		NewKTGCheatRule(),
		NewOpenCCS2TWRule(),
	}
}

// BuiltinRules 返回内置默认规则集合，用于 rules.json 缺失时的回退。
func BuiltinRules() []Rule {
	return RulesFromData(RuleData{SpecialWord: defaultSpecialWord, MateriaMap: defaultMateriaMap})
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

// NewTreasureMapAliasRule 创建使用内置数据的藏宝图别名规则。
func NewTreasureMapAliasRule() *TreasureMapAliasRule {
	return NewTreasureMapAliasRuleFrom(defaultSpecialWord)
}

// NewTreasureMapAliasRuleFrom 使用给定映射表创建藏宝图别名规则。
func NewTreasureMapAliasRuleFrom(src map[string]string) *TreasureMapAliasRule {
	if len(src) == 0 {
		src = defaultSpecialWord
	}
	return &TreasureMapAliasRule{aliases: buildTreasureAliases(src)}
}

// ID 返回规则 ID。
func (r *TreasureMapAliasRule) ID() string { return "treasure_map_alias" }

// Meta 返回规则元数据。
func (r *TreasureMapAliasRule) Meta() RuleMeta {
	return RuleMeta{
		ID:          r.ID(),
		Name:        "藏宝图等级标注",
		Description: "在藏宝图名称后追加（G1）~（G18）等级后缀",
		Category:    "文本优化",
		DefaultOn:   true,
	}
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

// NewMateriaStatSuffixRule 创建使用内置数据的魔晶石属性后缀规则。
func NewMateriaStatSuffixRule() *MateriaStatSuffixRule {
	return NewMateriaStatSuffixRuleFrom(defaultMateriaMap)
}

// NewMateriaStatSuffixRuleFrom 使用给定映射表创建魔晶石属性后缀规则。
func NewMateriaStatSuffixRuleFrom(src map[string]string) *MateriaStatSuffixRule {
	if len(src) == 0 {
		src = defaultMateriaMap
	}
	stats := make(map[string]string, len(src))
	for k, v := range src {
		stats[k] = v
	}
	return &MateriaStatSuffixRule{stats: stats}
}

// ID 返回规则 ID。
func (r *MateriaStatSuffixRule) ID() string { return "materia_stat_suffix" }

// Meta 返回规则元数据。
func (r *MateriaStatSuffixRule) Meta() RuleMeta {
	return RuleMeta{
		ID:          r.ID(),
		Name:        "魔晶石属性后缀",
		Description: "在魔晶石名称后追加属性名，如（暴击）",
		Category:    "文本优化",
		DefaultOn:   true,
	}
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

