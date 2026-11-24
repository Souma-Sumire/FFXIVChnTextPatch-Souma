package main

import "github.com/egoist/mygo"

var (
	evPatchLog              = mygo.NewEvent[PatchLogEvent]("patch-log")
	evPatchProgress         = mygo.NewEvent[ProgressEvent]("patch-progress")
	evResourceProgress      = mygo.NewEvent[ProgressEvent]("resource-progress")
	evSyncProgress          = mygo.NewEvent[ProgressEvent]("sync-progress")
	evAppUpdateProgress     = mygo.NewEvent[ProgressEvent]("app-update-progress")
	evCSVCopyProgress       = mygo.NewEvent[ProgressEvent]("csv-copy-progress")
	evFontBuildProgress     = mygo.NewEvent[ProgressEvent]("font-build-progress")
	evFontCompilerProgress  = mygo.NewEvent[ProgressEvent]("font-compiler-progress")
	evUnknownConfirm        = mygo.NewEvent[string]("unknown-confirm")
	evLauncherConfirm       = mygo.NewEvent[string]("launcher-confirm")
	evElemEditorNativeClose = mygo.NewEvent[struct{}]("elem-editor:native-close")

	evElemEditorOpen = mygo.NewEvent[string]("elem-editor:open")
	evElemEditorSync = mygo.NewEvent[string]("elem-editor:sync")

	evElemEditorUpdate  = mygo.NewEvent[any]("elem-editor:update")
	evElemEditorBypass  = mygo.NewEvent[any]("elem-editor:bypass")
	evElemEditorCompare = mygo.NewEvent[any]("elem-editor:compare")
	evElemEditorCommit  = mygo.NewEvent[any]("elem-editor:commit")
	evElemEditorCancel  = mygo.NewEvent[any]("elem-editor:cancel")

	progressEvents = map[string]*mygo.Event[ProgressEvent]{
		"patch-progress":         evPatchProgress,
		"resource-progress":      evResourceProgress,
		"sync-progress":          evSyncProgress,
		"app-update-progress":    evAppUpdateProgress,
		"csv-copy-progress":      evCSVCopyProgress,
		"font-build-progress":    evFontBuildProgress,
		"font-compiler-progress": evFontCompilerProgress,
	}

	stringEvents = map[string]*mygo.Event[string]{
		"unknown-confirm":  evUnknownConfirm,
		"launcher-confirm": evLauncherConfirm,
	}

	elemEditorMainEvents = map[string]*mygo.Event[any]{
		"elem-editor:update":  evElemEditorUpdate,
		"elem-editor:bypass":  evElemEditorBypass,
		"elem-editor:compare": evElemEditorCompare,
		"elem-editor:commit":  evElemEditorCommit,
		"elem-editor:cancel":  evElemEditorCancel,
	}

	elemEditorSubEvents = map[string]*mygo.Event[string]{
		"elem-editor:open": evElemEditorOpen,
		"elem-editor:sync": evElemEditorSync,
	}
)
