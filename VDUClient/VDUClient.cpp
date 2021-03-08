// VDUClient.cpp : Defines the class behaviors for the application.
//

#include "pch.h"
#include "framework.h"
#include "VDUClient.h"
#include "VDUClientDlg.h"
#include "VDUSession.h"
#include "VDUFilesystem.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// VDUClient
BEGIN_MESSAGE_MAP(VDUClient, CWinApp)
	ON_COMMAND(ID_HELP, &CWinApp::OnHelp)
END_MESSAGE_MAP()

LPTOP_LEVEL_EXCEPTION_FILTER oldFilter = NULL;
LONG WINAPI OnUnhandledException(_EXCEPTION_POINTERS* ExceptionInfo)
{
	
	return oldFilter(ExceptionInfo);
}

// VDUClient construction
VDUClient::VDUClient() : m_srefThread(nullptr), m_svc(nullptr), m_svcThread(nullptr), m_testMode(FALSE)
{
	oldFilter = SetUnhandledExceptionFilter(OnUnhandledException);

	// support Restart Manager
	m_dwRestartManagerSupportFlags = AFX_RESTART_MANAGER_SUPPORT_RESTART;

	// TODO: add construction code here,
	// Place all significant initialization in InitInstance
	m_session = new CVDUSession(_T(""));
}

VDUClient::~VDUClient()
{
	delete m_session;
}

CVDUSession* VDUClient::GetSession()
{
	return m_session;
}

CWinThread* VDUClient::GetSessionRefreshingThread()
{
	return m_srefThread;
}

CWinThread* VDUClient::GetFileSystemServiceThread()
{
	return m_svcThread;
}

BOOL VDUClient::IsTestMode()
{
	return m_testMode;
}

CVDUFileSystemService* VDUClient::GetFileSystemService()
{
	return m_svc;
}

// The one and only VDUClient object
VDUClient vduClient;

// VDUClient initialization
BOOL VDUClient::InitInstance()
{
	//Check if an instance is already running

	// InitCommonControlsEx() is required on Windows XP if an application
	// manifest specifies use of ComCtl32.dll version 6 or later to enable
	// visual styles.  Otherwise, any window creation will fail.
	INITCOMMONCONTROLSEX InitCtrls;
	InitCtrls.dwSize = sizeof(InitCtrls);
	// Set this to include all the common control classes you want to use
	// in your application.
	InitCtrls.dwICC = ICC_WIN95_CLASSES;
	InitCommonControlsEx(&InitCtrls);

	CWinApp::InitInstance();

	AfxEnableControlContainer();

	// Create the shell manager, in case the dialog contains
	// any shell tree view or shell list view controls.
	CShellManager *pShellManager = new CShellManager;

	// Activate "Windows Native" visual manager for enabling themes in MFC controls
	CMFCVisualManager::SetDefaultManager(RUNTIME_CLASS(CMFCVisualManagerWindows));

	// Standard initialization
	// If you are not using these features and wish to reduce the size
	// of your final executable, you should remove from the following
	// the specific initialization routines you do not need
	// Change the registry key under which our settings are stored
	// TODO: You should modify this string to be something appropriate
	// such as the name of your company or organization
	SetRegistryKey(VFSNAME);

	//Check if WinFSP is installed on the system
	HKEY hkey;
	if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, _T("SYSTEM\\CurrentControlSet\\Services\\WinFsp"), 0, KEY_READ, &hkey) != ERROR_SUCCESS)
	{
		MessageBox(NULL, _T("WinFSP is not insalled on the system!"), TITLENAME, MB_ICONERROR);
		return FALSE;
	}
	else
		RegCloseKey(hkey);

	BOOL c_silent = FALSE;
	//Check for startup options
	int argc;
	if (LPWSTR* argv = CommandLineToArgvW(m_lpCmdLine, &argc))
	{
		for (int i = 0; i < argc; i++)
		{
			LPWSTR arg = argv[i];

			if (!wcscmp(arg, _T("-silent")))
				c_silent = TRUE;
			else if (!wcscmp(arg, _T("-testmode")))
			{
				m_testMode = TRUE;
				APP->WriteProfileInt(SECTION_SETTINGS, _T("AutoLogin"), FALSE);
			}
		}
		LocalFree(argv);
	}

	CString preferredLetter = APP->GetProfileString(SECTION_SETTINGS, _T("PreferredDriveLetter"), _T(""));
	if (preferredLetter.IsEmpty())
		APP->WriteProfileString(SECTION_SETTINGS, _T("PreferredDriveLetter"), _T("V:"));
	preferredLetter = APP->GetProfileString(SECTION_SETTINGS, _T("PreferredDriveLetter"), _T(""));

	//Create worker thread
	m_srefThread = AfxBeginThread(ThreadProcLoginRefresh, (LPVOID)nullptr);

	CVDUClientDlg* pDlg = new CVDUClientDlg(AfxGetMainWnd());
	m_pMainWnd = pDlg;
	if (pDlg->Create(IDD_VDUCLIENT_DIALOG, AfxGetMainWnd()))
	{
		if (IsTestMode())
		{
			pDlg->ShowWindow(SW_HIDE);
		}
		else if (c_silent)
		{
			pDlg->ShowWindow(SW_MINIMIZE);
			pDlg->ShowWindow(SW_HIDE);
		}
		else
		{
			pDlg->ShowWindow(SW_SHOWNORMAL);
		}
	}
	else
	{
		//Failed to create dialog?
		return FALSE;
	}

	m_svcThread = AfxBeginThread(ThreadProcFilesystemService, (LPVOID)(m_svc = new CVDUFileSystemService(preferredLetter)));

	//TODO: Use this later
	//ShutdownBlockReasonCreate(WND->GetSafeHwnd(), _T("Please exit the application"));

	// Delete the shell manager created above.
	/*if (pShellManager != nullptr)
	{
		delete pShellManager;
	}*/

#if !defined(_AFXDLL) && !defined(_AFX_NO_MFC_CONTROLS_IN_DIALOGS)
	ControlBarCleanUp();
#endif

	//In test mode we execute input actions and quit with proper code
	if (IsTestMode())
	{
		int argc;
		if (LPWSTR* argv = CommandLineToArgvW(m_lpCmdLine, &argc))
		{
			for (int i = 0; i < argc; i++)
			{
				LPWSTR arg = argv[i];
				INT result;

				if (!wcscmp(arg, _T("-server")))
				{
					LPWSTR server = argv[++i];//TODO: Fix crash 

					GetSession()->Reset(server);
				}
				else if (!wcscmp(arg, _T("-user")))
				{
					LPWSTR user = argv[++i];

					result = GetSession()->Login(user, _T(""), FALSE);
					if (result != EXIT_SUCCESS)
					{
						AfxPostQuitMessage(result);
						return TRUE;
					}
				}
				else if (!wcscmp(arg, _T("-logout")))
				{
					result = GetSession()->Logout(FALSE);
					if (result != EXIT_SUCCESS)
					{
						AfxPostQuitMessage(result);
						return TRUE;
					}
				}
				else if (!wcscmp(arg, _T("-accessfile")))
				{
					LPWSTR token = argv[++i];

					result = GetSession()->AccessFile(token, FALSE);
					if (result != EXIT_SUCCESS)
					{
						AfxPostQuitMessage(result);
						return TRUE;
					}
				}
				else if (!wcscmp(arg, _T("-deletefile")))
				{
					LPWSTR token = argv[++i];

					CVDUFile vdufile = GetFileSystemService()->GetVDUFileByToken(token);

					//Make sure file gets deleted
					GetFileSystemService()->DeleteFileInternal(token);

					result = GetFileSystemService()->DeleteVDUFile(vdufile, FALSE);
					if (result != EXIT_SUCCESS)
					{
						AfxPostQuitMessage(result);
						return TRUE;
					}
				}
				else if (!wcscmp(arg, _T("-rename")))
				{
					LPWSTR token = argv[++i];
					LPWSTR name = argv[++i];

					CVDUFile vdufile = GetFileSystemService()->GetVDUFileByToken(token);

					result = GetFileSystemService()->UpdateVDUFile(vdufile, name, FALSE);
					if (result != EXIT_SUCCESS)
					{
						AfxPostQuitMessage(result);
						return TRUE;
					}
				}
				else if (!wcscmp(arg, _T("-write")))
				{
					LPWSTR token = argv[++i];
					LPWSTR text = argv[++i];

					CVDUFile vdufile = GetFileSystemService()->GetVDUFileByToken(token);

					//Simulate writing to a text file
					HANDLE hFile = CreateFile(GetFileSystemService()->GetDrivePath() + _T("\\") + vdufile.m_name,
						GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0);
					if (hFile != INVALID_HANDLE_VALUE)
					{
						if (WriteFile(hFile, text, (DWORD)wcslen(text) * sizeof(*text), NULL, NULL))
						{
							vdufile.m_length = GetFileSize(hFile, NULL);
							CloseHandle(hFile);
							result = GetFileSystemService()->UpdateVDUFile(vdufile, _T(""), FALSE);
							if (result != EXIT_SUCCESS)
							{
								AfxPostQuitMessage(result);
								return TRUE;
							}
							continue;
						}
						else
						{
							CloseHandle(hFile);
						}
					}

					AfxPostQuitMessage(/*GetLastError()*/EXIT_FAILURE);
					return TRUE;
				}
			}
			LocalFree(argv);
		}

		//Implicit quit after all test actions have been done
		AfxPostQuitMessage(EXIT_SUCCESS);
	}

	return TRUE;
}

INT VDUClient::ExitInstance()
{
	//TODO: Make sure to try to send all changes before natural exit?
	//ShutdownBlockReasonDestroy(WND->GetSafeHwnd());
	/*if (auto* svc = GetFileSystemService())
	{
		svc->Stop();
	}*/ //No need to shut down, FSP handles shut down on quit
	/*if (auto* t = GetSessionRefreshingThread())
	{
		t->Delete();
	}*/
	if (auto* s = GetSession())
		if (s->IsLoggedIn())
			AfxBeginThread(CVDUConnection::ThreadProc,(LPVOID)new CVDUConnection(s->GetServerURL(), VDUAPIType::DELETE_AUTH_KEY));

	if (m_pMainWnd)
		WND->DestroyWindow();
	return CWinApp::ExitInstance();
}

UINT VDUClient::ThreadProcFilesystemService(LPVOID service)
{
	CVDUFileSystemService* svc = (CVDUFileSystemService*) service;
	ASSERT(svc);
	//Run the service here forever
	return svc->Run();
}

UINT VDUClient::ThreadProcLoginRefresh(LPVOID)
{
	while (TRUE)
	{
		VDU_SESSION_LOCK;

		CTime expires = session->GetAuthTokenExpires();

		//Session is not set up yet, we wait
		if (expires <= 0)
		{
			VDU_SESSION_UNLOCK;
			Sleep(1000);
			continue;
		}

		//NOTE: CTime::GetCurrentTime() is offset by timezone, do not use
		SYSTEMTIME cstime;
		SecureZeroMemory(&cstime, sizeof(cstime));
		GetSystemTime(&cstime);
		CTime now(cstime);
		CTimeSpan delta = expires - now;

		//Sleep till its time to refresh or refresh immediately
		if (delta < 3)
		{
			CWinThread* refreshThread = AfxBeginThread(CVDUConnection::ThreadProc, (LPVOID)
				(new CVDUConnection(session->GetServerURL(), VDUAPIType::GET_AUTH_KEY, CVDUSession::CallbackLoginRefresh)),0,0,CREATE_SUSPENDED);

			ASSERT(refreshThread);
			refreshThread->m_bAutoDelete = FALSE;
			DWORD resumeResult = refreshThread->ResumeThread();

			VDU_SESSION_UNLOCK;

			//We wait for our refreshing thread to finish
			if (resumeResult != 0xFFFFFFFF) //Should not happen, but dont get stuck
				WaitForSingleObject(refreshThread->m_hThread, INFINITE);

			DWORD exitCode = 0;
			GetExitCodeThread(refreshThread->m_hThread, &exitCode);

			//With m_bAutoDelete we are responsibile for deleting the thread
			delete refreshThread;

			if (exitCode == 2) //Failed to refresh due to connection, we wait for a bit
			{
				Sleep(4000);
			}
		}
		else //Sleep until its time to check again
		{
			VDU_SESSION_UNLOCK;
			Sleep(1000); //TODO: delta timespan ?
			continue;
		}
	}

	return EXIT_SUCCESS;
}