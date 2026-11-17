package main

import (
	"fmt"
	"strings"
	"time"

	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/region"
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
	logTagPatch   = "汉化"
	logTagRestore = "还原"
	logTagFont    = "字体"
	logTagCSV     = "词条"
	logTagConfig  = "配置"
	logTagUpdate  = "更新"
	logTagSystem  = "系统"
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

// emitVerifierLog 将内部校验/还原流程的结构化日志转发到指定业务标签下。
func (a *App) emitVerifierLog(tag string) verifier.LogFunc {
	return func(level verifier.LogLevel, format string, args ...any) {
		switch level {
		case verifier.LogDiag:
			a.emitDiag(format, args...)
		case verifier.LogWarn:
			a.emitWarn(tag, format, args...)
		case verifier.LogOK:
			a.emitOK(tag, format, args...)
		default:
			a.emitInfo(tag, format, args...)
		}
	}
}

func formatTargetLanguages(langs []string) string {
	if len(langs) == 0 {
		return ""
	}
	names := make([]string, 0, len(langs))
	for _, code := range langs {
		names = append(names, region.LanguageName(region.Language(code)))
	}
	return strings.Join(names, "、")
}

func formatElapsed(d time.Duration) string {
	rounded := d.Round(time.Second)
	if rounded < time.Second {
		return "<1s"
	}
	return rounded.String()
}
