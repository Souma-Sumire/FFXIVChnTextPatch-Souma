public asm_call_atkmodule_vf43_via_wndproc

.code

asm_call_atkmodule_vf43_via_wndproc proc
	ppfnCallWindowProcW:
		dq 0
	ppfnSetWindowLongPtrW:
		dq 0
	ppfnGetWindowLongPtrW:
		dq 0
	ppfnSetEvent:
		dq 0
	ppfnResetEvent:
		dq 0
	ppfnWaitForSingleObject:
		dq 0
	ppFrameworkInstance:
		dq 0
	ppfnFramework_GetUiModule:
		dq 0
	pGameHwnd:
		dq 0
	pEvent1:
		dq 0
	pEvent2:
		dq 0
	ppfnWndProcPrevious:
		dq 0
	ppfnAtkUnitManager_GetAddonByName:
		dq 0
	ppfnAtkTextNode_ToggleFontCache:
		dq 0
	ppAtkStage:
		dq 0
	offAtkModuleIsLobby:
		dq 0
	offAtkStageUnitManager:
		dq 0
	offNamePlateObjects:
		dq 0
	nNamePlateObjectStride:
		dq 0
	offNamePlateObjectNameText:
		dq 0
	offTextNodeFontCacheFlags:
		dq 0
	nNamePlateObjectCount:
		dq 0
	offUiModuleGetRaptureAtkModule:
		dq 0
	ppfnAtkModule_LoadFonts:
		dq 0
	nUseFontCacheFlag:
		dq 0
	szNamePlate:
		db "NamePlate", 0, 0, 0, 0, 0, 0, 0

		dq 0CC90CC90CC90CC90h

	MyWndProc:
		push rbx
		push rsi
		push rdi
		sub rsp, 40h
		; [rsp + 20h]: result of the previous window procedure
		; [rsp + 28h]: hWnd, then RaptureAtkModule
		; [rsp + 30h]: AddonNamePlate
		; rbx, rsi: loop variables; rdi is pushed only to keep the stack aligned
		mov qword ptr [rsp + 28h], rcx

		mov qword ptr [rsp + 20h], r9
		mov r9, r8
		mov r8, rdx
		mov rdx, rcx
		mov rcx, qword ptr [ppfnWndProcPrevious]
		call qword ptr [ppfnCallWindowProcW]

		mov rcx, qword ptr [rsp + 28h]
		mov qword ptr [rsp + 20h], rax

		mov rdx, -4
		mov r8, qword ptr [ppfnWndProcPrevious]
		call qword ptr [ppfnSetWindowLongPtrW]

		mov rcx, qword ptr [pEvent1]
		call qword ptr [ppfnSetEvent]

		mov rcx, qword ptr [pEvent2]
		mov edx, 0FFFFFFFFh
		call qword ptr [ppfnWaitForSingleObject]

		mov rcx, qword ptr [ppFrameworkInstance]
		mov rcx, qword ptr [rcx]
		call qword ptr [ppfnFramework_GetUiModule]
		; rax = pUiModule

		mov rcx, rax
		mov rax, qword ptr [rax]
		add rax, qword ptr [offUiModuleGetRaptureAtkModule]
		mov rax, qword ptr [rax]
		call rax  ; vf7 GetRaptureAtkModule(this)
		; rax = pAtkModule
		mov qword ptr [rsp + 28h], rax

		; Nameplate text nodes cache laid out glyphs, with pointers into the glyph tables of fonts, which reloading frees.
		; Turn the caches off; nameplates then lay out their text directly.
		; They are not turned back on: building a cache while the fonts are still loading makes one with unfilled
		; records, which crashes the game when drawn.
		mov qword ptr [rsp + 30h], 0
		mov rcx, qword ptr [ppAtkStage]
		mov rcx, qword ptr [rcx]
		test rcx, rcx
		jz NoNamePlate
		mov rdx, qword ptr [offAtkStageUnitManager]
		mov rcx, qword ptr [rcx + rdx]
		test rcx, rcx
		jz NoNamePlate
		lea rdx, qword ptr [szNamePlate]
		mov r8d, 1
		call qword ptr [ppfnAtkUnitManager_GetAddonByName]
		mov qword ptr [rsp + 30h], rax
		test rax, rax
		jz NoNamePlate

		xor esi, esi
	TurnOffNextCache:
		mov rax, qword ptr [rsp + 30h]
		mov rcx, qword ptr [offNamePlateObjects]
		mov rbx, qword ptr [rax + rcx]
		test rbx, rbx
		jz TurnOffCachesDone
		mov rax, qword ptr [nNamePlateObjectStride]
		imul rax, rsi
		add rax, qword ptr [offNamePlateObjectNameText]
		mov rcx, qword ptr [rbx + rax]
		test rcx, rcx
		jz TurnOffSkip
		mov rax, qword ptr [offTextNodeFontCacheFlags]
		mov dl, byte ptr [nUseFontCacheFlag]
		test byte ptr [rcx + rax], dl
		jz TurnOffSkip
		xor edx, edx
		call qword ptr [ppfnAtkTextNode_ToggleFontCache]
	TurnOffSkip:
		inc rsi
		cmp rsi, qword ptr [nNamePlateObjectCount]
		jb TurnOffNextCache
	TurnOffCachesDone:

	NoNamePlate:
		mov rcx, qword ptr [rsp + 28h]
		mov rdx, qword ptr [offAtkModuleIsLobby]
		movzx edx, byte ptr [rcx + rdx]
		mov r8, 1
		call qword ptr [ppfnAtkModule_LoadFonts]  ; vf43 LoadFonts(this, bIsLobby, bForceReload)

		mov rax, qword ptr [rsp + 20h]
		add rsp, 40h
		pop rdi
		pop rsi
		pop rbx
		ret

		dq 0CC90CC90CC90CC90h

	RedirectWndProc:
		sub rsp, 38h

		mov rcx, qword ptr [pGameHwnd]
		mov rdx, -4
		call qword ptr [ppfnGetWindowLongPtrW]
		mov qword ptr [ppfnWndProcPrevious], RAX
		mov rcx, qword ptr [pGameHwnd]
		mov rdx, -4
		lea r8, qword ptr [MyWndProc]
		call qword ptr [ppfnSetWindowLongPtrW]

		add rsp, 38h
		ret

		dq 0CC90CC90CC90CC90h
		dq 0CC90CC90CC90CC90h
asm_call_atkmodule_vf43_via_wndproc endp

end
