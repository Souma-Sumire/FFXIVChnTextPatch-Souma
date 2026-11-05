package gameproc

import (
	"errors"
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

func TestClassifyCommandLine(t *testing.T) {
	cases := []struct {
		label    string
		command  string
		wantName string
		wantType ProcessType
		wantOK   bool
	}{
		{
			"Wine 加载器带 Windows 路径",
			`/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin/wine64-preloader "C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\ffxiv_dx11.exe"`,
			"ffxiv_dx11.exe", TypeGame, true,
		},
		{
			"正斜杠 Windows 路径",
			`/usr/bin/wine64 Z:/games/FFXIV/game/ffxiv.exe`,
			"ffxiv.exe", TypeGame, true,
		},
		{
			"只有启动器",
			`/usr/bin/wine64 C:\games\FFXIV\boot\ffxivboot64.exe`,
			"ffxivboot64.exe", TypeOfficialLauncher, true,
		},
		{
			"启动器与游戏同时出现时优先判游戏",
			`/usr/bin/wine64 C:\ffxivboot.exe C:\ffxiv_dx11.exe`,
			"ffxiv_dx11.exe", TypeGame, true,
		},
		{
			"ps 自身",
			`ps -A -o pid=,command=`,
			"", TypeUnknown, false,
		},
		{
			"参数里的同名字符串不算进程",
			`/usr/bin/editor --file=/tmp/ffxivboot.exe`,
			"", TypeUnknown, false,
		},
	}
	for _, tc := range cases {
		name, pType, ok := classifyCommandLine(tc.command)
		if ok != tc.wantOK || pType != tc.wantType || (tc.wantOK && name != tc.wantName) {
			t.Errorf("%s: classifyCommandLine = (%q, %v, %v), want (%q, %v, %v)",
				tc.label, name, pType, ok, tc.wantName, tc.wantType, tc.wantOK)
		}
	}
}

func TestParseProcessLine(t *testing.T) {
	cases := []struct {
		line       string
		wantPID    uint32
		wantDetail string
		wantOK     bool
	}{
		{"  123 /Applications/XIV on Mac.app/Contents/MacOS/XIV on Mac", 123, "/Applications/XIV on Mac.app/Contents/MacOS/XIV on Mac", true},
		{`456 /usr/bin/wine64 C:\games\FFXIV\game\ffxiv_dx11.exe`, 456, `/usr/bin/wine64 C:\games\FFXIV\game\ffxiv_dx11.exe`, true},
		{"  789 ", 0, "", false},
		{"abc /sbin/launchd", 0, "", false},
		{"", 0, "", false},
	}
	for _, tc := range cases {
		pid, detail, ok := parseProcessLine(tc.line)
		if tc.wantOK {
			if !ok || pid != tc.wantPID || detail != tc.wantDetail {
				t.Errorf("parseProcessLine(%q) = (%d, %q, %v), want (%d, %q, true)",
					tc.line, pid, detail, ok, tc.wantPID, tc.wantDetail)
			}
		} else if ok {
			t.Errorf("parseProcessLine(%q) 应被跳过，实际 (%d, %q)", tc.line, pid, detail)
		}
	}
}

// TestWineProcessConflicts 串起 ps 行解析到冲突判定的整条非 Windows 链路：
// Wine 进程的 comm 不是游戏目录内的路径，只有 Path 留空才能拦住汉化。
func TestWineProcessConflicts(t *testing.T) {
	line := `  321 /usr/bin/wine64-preloader "C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\ffxiv_dx11.exe"`
	pid, command, ok := parseProcessLine(line)
	if !ok {
		t.Fatal("ps 行应解析成功")
	}
	name, pType, ok := classifyCommandLine(command)
	if !ok || pType != TypeGame {
		t.Fatalf("Wine 命令行里应识别出游戏本体，实际 (%q, %v, %v)", name, pType, ok)
	}
	proc := ProcessInfo{PID: pid, Name: name, Type: pType}
	if !isConflicting(proc, filepath.FromSlash("/Users/me/Games/FFXIV")) {
		t.Fatal("Wine 下运行的游戏进程必须判为冲突")
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

	// 1. 无进程冲突
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
