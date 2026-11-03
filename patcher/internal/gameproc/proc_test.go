package gameproc

import (
	"errors"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestClassifyProcess(t *testing.T) {
	tests := []struct {
		name     string
		wantType ProcessType
		wantOk   bool
	}{
		{"ffxiv_dx11.exe", TypeGame, true},
		{"FFXIV_DX11.EXE", TypeGame, true},
		{"ffxiv.exe", TypeGame, true},
		{"ffxiv_dx11", TypeGame, true},
		{"ffxiv", TypeGame, true},

		{"ffxivboot64.exe", TypeOfficialLauncher, true},
		{"ffxivboot.exe", TypeOfficialLauncher, true},
		{"ffxivlauncher64.exe", TypeOfficialLauncher, true},
		{"ffxivlauncher.exe", TypeOfficialLauncher, true},
		{"ffxivupdater64.exe", TypeOfficialLauncher, true},
		{"ffxivupdater.exe", TypeOfficialLauncher, true},
		{"ffxivconfig64.exe", TypeOfficialLauncher, true},
		{"ffxivsysinfo64.exe", TypeOfficialLauncher, true},

		{"XIVLauncher.exe", TypeThirdPartyLauncher, true},
		{"xivlauncher.exe", TypeThirdPartyLauncher, true},
		{"XIVLauncher.Core.exe", TypeThirdPartyLauncher, true},
		{"xivlaunchercn.exe", TypeThirdPartyLauncher, true},
		{"XIVLauncherCN.Core.exe", TypeThirdPartyLauncher, true},
		{"ffxivquicklauncher.exe", TypeThirdPartyLauncher, true},
		{"xivlauncher", TypeThirdPartyLauncher, true},
		{"XIV on Mac", TypeThirdPartyLauncher, true},

		{"notepad.exe", TypeUnknown, false},
		{"chrome.exe", TypeUnknown, false},
		{"cmd.exe", TypeUnknown, false},
		{"", TypeUnknown, false},
	}

	for _, tt := range tests {
		gotType, gotOk := ClassifyProcess(tt.name)
		if gotOk != tt.wantOk || gotType != tt.wantType {
			t.Errorf("ClassifyProcess(%q) = (%v, %v), want (%v, %v)", tt.name, gotType, gotOk, tt.wantType, tt.wantOk)
		}
	}
}

func TestIsPathUnder(t *testing.T) {
	parent := filepath.Join("D:", "Games", "SquareEnix", "FINAL FANTASY XIV - A Realm Reborn")
	sub := filepath.Join(parent, "game", "ffxiv_dx11.exe")
	sibling := filepath.Join("D:", "Games", "SquareEnix", "FINAL FANTASY XIV - A Realm Reborn_CN", "game", "ffxiv_dx11.exe")
	otherDrive := filepath.Join("C:", "Games", "ffxiv_dx11.exe")

	if !isPathUnder(sub, parent) {
		t.Errorf("expected %q to be under %q", sub, parent)
	}
	if !isPathUnder(parent, parent) {
		t.Errorf("expected parent to be under parent")
	}
	if isPathUnder(sibling, parent) {
		t.Errorf("expected %q NOT to be under %q", sibling, parent)
	}
	if isPathUnder(otherDrive, parent) {
		t.Errorf("expected %q NOT to be under %q", otherDrive, parent)
	}
	if isPathUnder("", parent) {
		t.Errorf("expected empty child NOT to be under parent")
	}
	if isPathUnder(sub, "") {
		t.Errorf("expected child NOT to be under empty parent")
	}
}

func TestIsConflicting(t *testing.T) {
	gameDir := filepath.Join("D:", "Games", "FFXIV")

	p1 := ProcessInfo{Name: "ffxiv_dx11.exe", Path: filepath.Join(gameDir, "game", "ffxiv_dx11.exe"), Type: TypeGame}
	if !isConflicting(p1, gameDir) {
		t.Errorf("expected p1 to conflict")
	}

	p2 := ProcessInfo{Name: "ffxiv_dx11.exe", Path: filepath.Join("D:", "Games", "FFXIV_CN", "game", "ffxiv_dx11.exe"), Type: TypeGame}
	if isConflicting(p2, gameDir) {
		t.Errorf("expected p2 NOT to conflict")
	}

	p3 := ProcessInfo{Name: "ffxiv_dx11.exe", Path: "", Type: TypeGame}
	if !isConflicting(p3, gameDir) {
		t.Errorf("expected p3 to conflict when path is empty")
	}

	p4 := ProcessInfo{Name: "ffxivboot64.exe", Path: filepath.Join(gameDir, "boot", "ffxivboot64.exe"), Type: TypeOfficialLauncher}
	if !isConflicting(p4, gameDir) {
		t.Errorf("expected p4 to conflict")
	}

	p5 := ProcessInfo{Name: "XIVLauncher.exe", Path: filepath.Join("C:", "Users", "Test", "AppData", "Local", "XIVLauncher", "XIVLauncher.exe"), Type: TypeThirdPartyLauncher}
	if !isConflicting(p5, gameDir) {
		t.Errorf("expected third-party launcher p5 to conflict")
	}
}

func TestFormatConflictError(t *testing.T) {
	err1 := formatConflictError([]ProcessInfo{
		{Name: "ffxiv_dx11.exe", Type: TypeGame},
	})
	if err1 == nil || !strings.Contains(err1.Error(), "游戏客户端正在运行") || !strings.Contains(err1.Error(), "ffxiv_dx11.exe") {
		t.Errorf("unexpected err1: %v", err1)
	}

	err2 := formatConflictError([]ProcessInfo{
		{Name: "XIVLauncher.exe", Type: TypeThirdPartyLauncher},
	})
	if err2 == nil || !strings.Contains(err2.Error(), "游戏启动器正在运行") || !strings.Contains(err2.Error(), "XIVLauncher.exe") {
		t.Errorf("unexpected err2: %v", err2)
	}

	err3 := formatConflictError([]ProcessInfo{
		{Name: "ffxiv_dx11.exe", Type: TypeGame},
		{Name: "ffxivboot64.exe", Type: TypeOfficialLauncher},
	})
	if err3 == nil || !strings.Contains(err3.Error(), "游戏及启动器正在运行") {
		t.Errorf("unexpected err3: %v", err3)
	}
}

func TestCheckGameNotRunningWithMock(t *testing.T) {
	origLister := procLister
	origKiller := procKiller
	defer func() {
		procLister = origLister
		procKiller = origKiller
	}()

	tmpDir := t.TempDir()
	sqpackDir := filepath.Join(tmpDir, "game", "sqpack", "ffxiv")
	if err := os.MkdirAll(sqpackDir, 0o755); err != nil {
		t.Fatal(err)
	}
	indexFile := filepath.Join(sqpackDir, "0a0000.win32.index")
	if err := os.WriteFile(indexFile, []byte("fake index data"), 0o644); err != nil {
		t.Fatal(err)
	}

	// 1. 无进程冲突且文件未锁
	procLister = func() ([]ProcessInfo, error) {
		return nil, nil
	}
	if err := CheckGameNotRunningWithOpts(CheckOptions{GameDir: tmpDir}); err != nil {
		t.Errorf("expected no error, got %v", err)
	}

	// 2. 检测到启动器正在运行，未提供回调时直接报错
	procLister = func() ([]ProcessInfo, error) {
		return []ProcessInfo{
			{PID: 101, Name: "XIVLauncher.exe", Type: TypeThirdPartyLauncher},
		}, nil
	}
	if err := CheckGameNotRunningWithOpts(CheckOptions{GameDir: tmpDir}); err == nil || !strings.Contains(err.Error(), "XIVLauncher.exe") {
		t.Errorf("expected launcher conflict error, got %v", err)
	}

	// 3. 用户在确认弹窗中选择取消，返回 ErrCancelled
	err := CheckGameNotRunningWithOpts(CheckOptions{
		GameDir: tmpDir,
		OnLauncherConfirm: func(names []string) (bool, error) {
			return false, nil
		},
	})
	if !errors.Is(err, ErrCancelled) {
		t.Errorf("expected ErrCancelled, got %v", err)
	}

	// 4. 用户在确认弹窗中同意强制结束，成功杀死并放行
	var killedPids []uint32
	procKiller = func(pid uint32) error {
		killedPids = append(killedPids, pid)
		return nil
	}
	var killedNamesLogged []string
	err = CheckGameNotRunningWithOpts(CheckOptions{
		GameDir: tmpDir,
		OnLauncherConfirm: func(names []string) (bool, error) {
			return true, nil
		},
		OnLaunchersKilled: func(killed []string) {
			killedNamesLogged = killed
		},
	})
	if err != nil {
		t.Errorf("expected success after killing launcher, got: %v", err)
	}
	if len(killedPids) != 1 || killedPids[0] != 101 {
		t.Errorf("expected PID 101 killed, got %v", killedPids)
	}
	if len(killedNamesLogged) != 1 || killedNamesLogged[0] != "XIVLauncher.exe" {
		t.Errorf("expected killedNamesLogged XIVLauncher.exe, got %v", killedNamesLogged)
	}

	// 5. 游戏本体正在运行，即使提供启动器回调也不允许杀死本体，直接报错阻断
	procLister = func() ([]ProcessInfo, error) {
		return []ProcessInfo{
			{PID: 102, Name: "ffxiv_dx11.exe", Path: filepath.Join(tmpDir, "game", "ffxiv_dx11.exe"), Type: TypeGame},
			{PID: 103, Name: "XIVLauncher.exe", Type: TypeThirdPartyLauncher},
		}, nil
	}
	err = CheckGameNotRunningWithOpts(CheckOptions{
		GameDir: tmpDir,
		OnLauncherConfirm: func(names []string) (bool, error) {
			t.Fatal("game client running should not trigger launcher confirm")
			return true, nil
		},
	})
	if err == nil || !strings.Contains(err.Error(), "游戏客户端正在运行") {
		t.Errorf("expected game client running error, got %v", err)
	}

	// 6. KillLauncherProcesses 独立调用验证
	procLister = func() ([]ProcessInfo, error) {
		return []ProcessInfo{
			{PID: 201, Name: "ffxivboot64.exe", Path: filepath.Join(tmpDir, "boot", "ffxivboot64.exe"), Type: TypeOfficialLauncher},
		}, nil
	}
	killedPids = nil
	killedList, err := KillLauncherProcesses(tmpDir)
	if err != nil || len(killedList) != 1 || killedList[0] != "ffxivboot64.exe" {
		t.Errorf("KillLauncherProcesses failed: %v, killed: %v", err, killedList)
	}
}
