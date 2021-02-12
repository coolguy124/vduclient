// VDUClient.cpp : Defines the class behaviors for the application.
//

#include "pch.h"
#include "framework.h"
#include "VDUClient.h"
#include "VDUClientDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// VDUClient
BEGIN_MESSAGE_MAP(VDUClient, CWinApp)
	ON_COMMAND(ID_HELP, &CWinApp::OnHelp)
END_MESSAGE_MAP()


// VDUClient construction
VDUClient::VDUClient() : m_srefThread(nullptr), m_svc(nullptr), m_svcThread(nullptr)
{
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

// The one and only VDUClient object
VDUClient vduClient;

// VDUClient initialization
BOOL VDUClient::InitInstance()
{
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

	BOOL silent = FALSE;
	//Check for startup options
	int argc;
	LPWSTR* argv = CommandLineToArgvW(m_lpCmdLine, &argc);
	if (argv)
	{
		for (int i = 0; i < argc; i++)
		{
			LPWSTR arg = argv[i];

			if (!wcscmp(arg, _T("-silent")))
				silent = TRUE;
		}
	}
	LocalFree(argv);

	//Create worker thread
	m_srefThread = AfxBeginThread(ThreadProcLoginRefresh, (LPVOID)nullptr);

	CVDUClientDlg* pDlg = new CVDUClientDlg(AfxGetMainWnd());
	m_pMainWnd = pDlg;
	//INT_PTR nResponse = dlg.DoModal();
	if (pDlg->Create(IDD_VDUCLIENT_DIALOG, AfxGetMainWnd()))
	{
		if (silent)
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

	// Delete the shell manager created above.
	/*if (pShellManager != nullptr)
	{
		delete pShellManager;
	}*/

#if !defined(_AFXDLL) && !defined(_AFX_NO_MFC_CONTROLS_IN_DIALOGS)
	ControlBarCleanUp();
#endif

	// Since the dialog has been closed, return FALSE so that we exit the
	//  application, rather than start the application's message pump.
	return TRUE;
}

INT VDUClient::ExitInstance()
{
	WND->DestroyWindow(); //Make sure dialog window cleans up properly
	return CWinApp::ExitInstance();
}

UINT VDUClient::ThreadProcLoginRefresh(LPVOID)
{
	while (TRUE)
	{
		CVDUSession* session = APP->GetSession();
		ASSERT(session);

		CTime expires = session->GetAuthTokenExpires();

		//Not set up yet, we wait
		if (expires <= 0)
		{
			Sleep(1000);
			continue;
		}

		SYSTEMTIME cstime;
		GetSystemTime(&cstime);
		CTime now(cstime);
		CTimeSpan delta = expires - now;

		//Sleep till its time to refresh or refresh immediately
		if (delta < 3)
		{
			CString headers;
			headers += APIKEY_HEADER;
			headers += _T(": ");
			headers += session->GetAuthToken();
			headers += _T("\r\n");

			CWinThread* refreshThread = AfxBeginThread(CVDUConnection::ThreadProc, (LPVOID)
				(new CVDUConnection(session->GetServerURL(), VDUAPIType::GET_AUTH_KEY, CVDUSession::CallbackLoginRefresh, headers)),0,0,CREATE_SUSPENDED);
			ASSERT(refreshThread);
			refreshThread->m_bAutoDelete = FALSE;
			INT resumeResult = refreshThread->ResumeThread();
			WaitForSingleObject(refreshThread->m_hThread, INFINITE);

			DWORD exitCode = 0;
			GetExitCodeThread(refreshThread->m_hThread, &exitCode);
			delete refreshThread;

			if (exitCode == 2) //Failed to refresh, we wait for a bit
				Sleep(4000);
		}
		//else
		//{
		Sleep(/*delta.GetTimeSpan() - */1000);
		//}
	}
}