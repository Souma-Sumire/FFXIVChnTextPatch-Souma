// Package pipeline 提供汉化器运行期的 CSV 单元格转换管道。
//
// 设计目标：所有可选优化规则都在内存中执行，绝不回写 resource/rawexd/*.csv，
// 从而保证磁盘上的汉化数据零污染，并允许前端按 Checkbox 动态启停。
package pipeline

// CellContext 描述一个待转换的 CSV 单元格。
type CellContext struct {
	TableName string // 如 "Item" 或 "quest/000/ClsHrv001_00003"
	RowKey    int
	ColIndex  int // CSV 中的列索引（0 对应 key 后面的第一列，1 对应第二列，依此类推）
	ColOffset int
	SourceStr string // 原文（EXD 中当前语言列的内容）
	TargetStr string // CSV 中的初始中文
}

// 规则可用状态。数据由 rules.json 下发，缺失时规则本身仍会列出，但不参与转换。
const (
	RuleStatusOK          = "ok"
	RuleStatusUnavailable = "unavailable"
)

// RuleMeta 是规则的可序列化元数据，供前端动态渲染 Checkbox。
type RuleMeta struct {
	ID           string `json:"id"`
	Name         string `json:"name"`
	Description  string `json:"description"`
	Category     string `json:"category"`
	DefaultOn    bool   `json:"defaultOn"`
	Status       string `json:"status"`
	StatusDetail string `json:"statusDetail"`
}

// Rule 是单元格转换规则。
type Rule interface {
	ID() string
	Meta() RuleMeta
	Apply(ctx *CellContext) string
}

// Registry 管理规则集合与启停状态。
type Registry struct {
	rules   []Rule
	enabled map[string]bool
}

// NewRegistry 以规则默认开关状态创建注册表。不可用的规则一律保持关闭。
func NewRegistry(rules []Rule) *Registry {
	r := &Registry{
		rules:   make([]Rule, 0, len(rules)),
		enabled: make(map[string]bool, len(rules)),
	}
	for _, rule := range rules {
		meta := rule.Meta()
		r.rules = append(r.rules, rule)
		r.enabled[rule.ID()] = meta.DefaultOn && meta.Status == RuleStatusOK
	}
	return r
}

// SetEnabled 开启或关闭某个规则。不可用的规则无法开启。
func (r *Registry) SetEnabled(id string, on bool) {
	if _, ok := r.enabled[id]; !ok {
		return
	}
	if on && !r.available(id) {
		return
	}
	r.enabled[id] = on
}

func (r *Registry) available(id string) bool {
	for _, rule := range r.rules {
		if rule.ID() == id {
			return rule.Meta().Status == RuleStatusOK
		}
	}
	return false
}

// Enabled 查询规则是否开启。
func (r *Registry) Enabled(id string) bool { return r.enabled[id] }

// List 返回全部规则的元数据（按注册顺序）。
func (r *Registry) List() []RuleMeta {
	out := make([]RuleMeta, 0, len(r.rules))
	for _, rule := range r.rules {
		out = append(out, rule.Meta())
	}
	return out
}

// Apply 依次执行已开启的规则，返回最终字符串。
func (r *Registry) Apply(ctx *CellContext) string {
	cur := ctx.TargetStr
	for _, rule := range r.rules {
		if !r.enabled[rule.ID()] {
			continue
		}
		ctx.TargetStr = cur
		cur = rule.Apply(ctx)
	}
	ctx.TargetStr = cur
	return cur
}
