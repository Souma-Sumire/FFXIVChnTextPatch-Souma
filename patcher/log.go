package main

import (
	"fmt"
	"strings"
	"time"

	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/verifier"
)

const (
	logLevelInfo  = "info"
	logLevelOK    = "ok"
	logLevelWarn  = "warn"
	logLevelError = "error"
	logLevelDiag  = "diag"
)

const (
	logTagResource = "资源"
	logTagCSV      = "词条"
	logTagConfig   = "配置"
	logTagVerify   = "校验"
	logTagPatch    = "汉化"
	logTagFont     = "字体"
	logTagUpdate   = "更新"
	logTagSystem   = "系统"
)

// PatchLogEvent 是后端推送给前端的结构化日志载荷。
type PatchLogEvent struct {
	Level   string `json:"level"`
	Tag     string `json:"tag,omitempty"`
	Message string `json:"message"`
}

// emitLog 是后端日志的唯一出口。诊断行与路径会随反馈贴到公开 issue，
// 统一在这里隐去路径中的用户名，避免某处漏改就把本机账户带出去。
func (a *App) emitLog(level, tag, format string, args ...any) {
	msg := format
	if len(args) > 0 {
		msg = fmt.Sprintf(format, args...)
	}
	msg = strings.TrimSpace(redactPaths(msg))
	if msg == "" {
		return
	}
	a.emitEvent("patch-log", PatchLogEvent{
		Level:   level,
		Tag:     tag,
		Message: msg,
	})
}

func (a *App) emitInfo(tag, format string, args ...any) {
	a.emitLog(logLevelInfo, tag, format, args...)
}

func (a *App) emitOK(tag, format string, args ...any) {
	a.emitLog(logLevelOK, tag, format, args...)
}

func (a *App) emitWarn(tag, format string, args ...any) {
	a.emitLog(logLevelWarn, tag, format, args...)
}

func (a *App) emitError(tag, format string, args ...any) {
	a.emitLog(logLevelError, tag, format, args...)
}

// emitLegacyLog 将内部包传入的纯文本日志（含 [诊断]/[警告]/[校验] 等前缀）
// 规范化为结构化 PatchLogEvent，避免前端再靠字符串前缀猜测级别。
func (a *App) emitLegacyLog(defaultTag, format string, args ...any) {
	msg := format
	if len(args) > 0 {
		msg = fmt.Sprintf(format, args...)
	}
	if strings.HasPrefix(msg, verifier.DiagPrefix) {
		a.emitLog(logLevelDiag, "", "%s", strings.TrimPrefix(msg, verifier.DiagPrefix))
		return
	}

	level := logLevelInfo
	tag := defaultTag

	switch {
	case strings.HasPrefix(msg, "[错误] "):
		level = logLevelError
		msg = strings.TrimPrefix(msg, "[错误] ")
	case strings.HasPrefix(msg, "[警告] "):
		level = logLevelWarn
		msg = strings.TrimPrefix(msg, "[警告] ")
	case strings.HasPrefix(msg, "[提示] "):
		level = logLevelInfo
		msg = strings.TrimPrefix(msg, "[提示] ")
	case strings.HasPrefix(msg, "[完成] "):
		level = logLevelOK
		msg = strings.TrimPrefix(msg, "[完成] ")
	}

	for _, p := range []struct {
		prefix string
		tag    string
	}{
		{"[校验] ", logTagVerify},
		{"[备份] ", logTagVerify},
		{"[还原] ", logTagVerify},
		{"[文本] ", logTagPatch},
		{"[字体] ", logTagFont},
	} {
		if strings.HasPrefix(msg, p.prefix) {
			tag = p.tag
			msg = strings.TrimPrefix(msg, p.prefix)
			break
		}
	}

	a.emitLog(level, tag, "%s", msg)
}

func formatElapsed(d time.Duration) string {
	rounded := d.Round(time.Second)
	if rounded < time.Second {
		return "<1s"
	}
	return rounded.String()
}
