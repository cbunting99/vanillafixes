#include <shlwapi.h>
#include "util.c"

int InjectDll(LPWSTR pDllPath, HANDLE hTargetProcess) {
	int dllPathLen = (wcslen(pDllPath) + 1) * sizeof(WCHAR);
	LPVOID pRemoteDllPath = VirtualAllocEx(
		hTargetProcess,
		NULL,
		dllPathLen,
		MEM_RESERVE | MEM_COMMIT,
		PAGE_READWRITE
	);

	WriteProcessMemory(hTargetProcess, pRemoteDllPath, pDllPath, dllPathLen, NULL);

	HANDLE hThread =
		CreateRemoteThread(hTargetProcess, NULL, 0, (LPTHREAD_START_ROUTINE)&LoadLibraryW, pRemoteDllPath, 0, NULL);
	AssertMessageBox(hThread, L"Failed to create remote thread");

	WaitForSingleObject(hThread, 10000);

	DWORD threadResult = 0;
	GetExitCodeThread(hThread, &threadResult);

	CloseHandle(hThread);

	AssertMessageBox(VirtualFreeEx(hTargetProcess, pRemoteDllPath, 0, MEM_RELEASE), L"Failed to clean up remote data");
	AssertMessageBox(threadResult && threadResult != STILL_ACTIVE,
		L"DllMain failed after injection.\n\n"
		L"Make sure you have a compatible game client."
	);

	return 0;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
	int cpuInfo[4] = {0};
	__cpuid(cpuInfo, 0x80000007);

    AssertMessageBox(cpuInfo[3] & (1 << 8),
		L"Your processor does not support the \"Invariant TSC\" feature.\n\n"
		L"VanillaFixes can only work reliably on modern (2007+) Intel and AMD processors."
	);

	LPWSTR pWowDirectory = HeapAlloc(GetProcessHeap(), 0, MAX_PATH * sizeof(WCHAR));
	GetModuleFileName(NULL, pWowDirectory, MAX_PATH);

	/* Don't attempt to resize the buffer - 1.12 won't be able to run an executable with a long UNC path anyway */
	AssertMessageBox(GetLastError() != ERROR_INSUFFICIENT_BUFFER, L"GetLastError() == ERROR_INSUFFICIENT_BUFFER");

	PathRemoveFileSpec(pWowDirectory);

	LPWSTR pWowExePath = UtilGetPath(pWowDirectory, L"WoW.exe");
	LPWSTR pPatcherPath = UtilGetPath(pWowDirectory, L"VfPatcher.dll");
	LPWSTR pNamPowerPath = UtilGetPath(pWowDirectory, L"nampower.dll");

	AssertMessageBox(GetFileAttributes(pWowExePath) != INVALID_FILE_ATTRIBUTES,
		L"WoW.exe not found. Extract VanillaFixes into the same directory as the game.");
	AssertMessageBox(GetFileAttributes(pPatcherPath) != INVALID_FILE_ATTRIBUTES,
		L"VfPatcher.dll not found. Extract all files into the game directory.");

	pWowExePath = UtilSetCustomExecutable(pCmdLine, pWowExePath);

	STARTUPINFO startupInfo = {0};
	PROCESS_INFORMATION processInfo = {0};

	startupInfo.cb = sizeof(startupInfo);

	LPWSTR pWowCmdLine = UtilGetWowCmdLine(pWowExePath, pCmdLine);
	DWORD wowCreationFlags = CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT;

	BOOL processCreated =
		CreateProcess(NULL, pWowCmdLine, NULL, NULL, FALSE, wowCreationFlags, NULL, NULL, &startupInfo, &processInfo);

	AssertMessageBox(processCreated, L"CreateProcess failed for WoW.exe");

	int injectError = InjectDll(pPatcherPath, processInfo.hProcess);
	/* Also inject Nampower if present in the game directory */
	if(GetFileAttributes(pNamPowerPath) != INVALID_FILE_ATTRIBUTES) {
		injectError = injectError || InjectDll(pNamPowerPath, processInfo.hProcess);
	}

	if(injectError) {
		TerminateProcess(processInfo.hProcess, 0);
		return injectError;
	}

	ResumeThread(processInfo.hThread);

	CloseHandle(processInfo.hProcess);
    CloseHandle(processInfo.hThread);

	return 0;
}