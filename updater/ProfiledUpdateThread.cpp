// ProfiledUpdateThread.cpp : 实现文件
//

#include "stdafx.h"
#include "updater.h"
#include "ProfiledUpdateThread.h"

#define BUF_SIZE 1024

#ifdef _DEBUG
	#define DEFAULT_DEBUGLEVEL 2	//0 -> no messages, 1 -> Errors, 2 -> Warnings, 3 -> Information
#else
	#define DEFAULT_DEBUGLEVEL 0
#endif
// ProfiledUpdateThread

IMPLEMENT_DYNCREATE(CProfiledUpdateThread, CWinThread)

CProfiledUpdateThread::CProfiledUpdateThread()
{
}

CProfiledUpdateThread::~CProfiledUpdateThread()
{
}

BOOL CProfiledUpdateThread::InitInstance()
{
	m_iDebugLevel = DEFAULT_DEBUGLEVEL;		

	Init();
	StartCheckVersion();
	return TRUE;
}

int CProfiledUpdateThread::ExitInstance()
{
	Close();
	
	CWinThread::ExitInstance();
	return m_iResult;
}

BEGIN_MESSAGE_MAP(CProfiledUpdateThread, CWinThread)
	ON_THREAD_MESSAGE(UM_ERROR, OnError)
END_MESSAGE_MAP()

void CProfiledUpdateThread::Init(void)
{
	m_pInternetFile		= NULL;
	m_pInternetSession	= NULL;
	m_pConnection		= NULL;

	m_iTimeOut			= 3000;
	m_iRetryCount		= 0;
	m_lStartReadingByte = 0;

	m_iLastError		= ERROR_UNKNOWN;

	m_plog				= NULL;

	//设置默认返回值
	m_iResult = ERROR_UNKNOWN;

	

	if (m_iDebugLevel> 0)
	{
		TCHAR buffer[490];
		::GetModuleFileName(0, buffer, 490);
		LPTSTR pszFileName = _tcsrchr(buffer, L'\\') + 1;
		*pszFileName = L'\0';
		CString appdir(buffer);

		try
		{
			m_plog = new CPrintWriter(appdir + L"logs\\" + _T("updater.log"));
		}
		catch (CException* e)
		{
			e->Delete();
		}

		m_plog->println(_T("-----------------------------------------"));
		m_plog->println(_T("-----------------------------------------"));
		m_plog->println(_T("strating CProfiledUpdateThread Version 1.0.5"));
		m_plog->println(_T("TimeOut: %d"), m_iTimeOut);
		m_plog->println(_T("Log File: %s"), appdir + L"logs\\" + _T("updater.log"));
		m_plog->println(_T("DebugLevel: %d"), m_iDebugLevel);
		m_plog->println(_T("-----------------------------------------"));
	}
}

void CProfiledUpdateThread::Close(void)
{
	if(m_pInternetFile)
	{
		m_pInternetFile->Close();
		delete m_pInternetFile;
	}

	if(m_pInternetSession)
	{
		m_pInternetSession->Close();
		delete m_pInternetSession;
	}

	if(m_pConnection)
	{
		m_pConnection->Close();
		delete m_pConnection;
	}

	if(m_plog)
	{
		m_plog->Close();
		delete m_plog;
	}

	m_pInternetFile = NULL;
	m_pInternetSession = NULL;
	m_pConnection = NULL;
	m_plog = NULL;

	
}

BOOL CProfiledUpdateThread::EstablishConnection(void)
{
	CString sServer;
	CString sObject;
	INTERNET_PORT nPort;
	DWORD dwServiceType;

	if(m_pInternetSession)
	{
		if (m_iDebugLevel > 1)
		{
			m_plog->println(_T("warning : \"EstablishConnection\" : InternetSession must be closed."));
		}

		m_pInternetSession->Close();
		delete m_pInternetSession;
	}

	m_iRetryCount = 0;

	try
	{
		m_pInternetSession = new CInternetSession();
	}
	catch (CException* pEx)
	{
		pEx->Delete();

		if (m_iDebugLevel > 0)
		{
			m_plog->println(_T("warning : \"EstablishConnection\" : InternetSession Failed."));
		}
	}

	m_pInternetSession->SetOption(INTERNET_OPTION_CONNECT_TIMEOUT, m_iTimeOut);
	m_pInternetSession->SetOption(INTERNET_OPTION_DATA_RECEIVE_TIMEOUT, m_iTimeOut);
	m_pInternetSession->SetOption(INTERNET_OPTION_CONNECT_RETRIES, 3);

	if (!AfxParseURL(m_sURL, dwServiceType, sServer, sObject, nPort))
	{
		return FALSE;
	}

	try
	{
		m_pConnection = m_pInternetSession->GetHttpConnection(sServer, nPort);

		unsigned long ulFlags = INTERNET_FLAG_KEEP_CONNECTION | INTERNET_FLAG_RELOAD;	//保持连接 | 强制从服务器下载
		if (dwServiceType == AFX_INET_SERVICE_HTTPS)
		{
			ulFlags = ulFlags | INTERNET_FLAG_SECURE;
		}

		// 打开一个Http请求
		m_pInternetFile = m_pConnection->OpenRequest(HTTP_VERB_GET, sObject, NULL, 1, NULL, NULL, ulFlags);

		// 处理请求(包括代理设置)
		ProcessHttpRequest((CHttpFile*)m_pInternetFile);
		return TRUE;

	}
	catch(CException* pEx)
	{
		pEx->Delete();
		return FALSE;
	}
	return TRUE;
}

void CProfiledUpdateThread::GetCurrentVersion(void)
{
	TCHAR buffer[490];
	::GetModuleFileName(0, buffer, 490);
	LPTSTR pszFileName = _tcsrchr(buffer, L'\\') + 1;
	*pszFileName = L'\0';

	m_sPathApp = buffer;

	m_sPathApp = m_sPathApp + _T("emule.exe");

	CString path = m_sPathApp;
	CString sFileVersion;
	CString	strSubBuild;		//建立日期

	if(m_fvi.Create(path))
	{
		sFileVersion = m_fvi.GetFileVersion();
		m_iMajor = m_fvi.GetFileVersion(3);
		m_iMinor = m_fvi.GetFileVersion(2);
		m_iUpdate = m_fvi.GetFileVersion(1);

		AfxExtractSubString(strSubBuild,sFileVersion, 3, '.');
		strSubBuild.Replace(_T(" Unicode"), _T(""));
		m_iBuild = _ttoi(strSubBuild);
	}
}

int CProfiledUpdateThread::GetLastError(void)
{
	return m_iLastError;
}

DWORD CProfiledUpdateThread::GetServerType(CString sURL)
{
	CString sServer, sObject, sProxy, sAuthentication;
	INTERNET_PORT nPort;
	DWORD dwServiceType;

	if (PathFileExists(sURL))
	{
		dwServiceType = AFX_INET_SERVICE_FILE;
	}
	else
	{
		if (!AfxParseURL(sURL, dwServiceType, sServer, sObject, nPort))
		{
			return AFX_INET_SERVICE_FILE;
		}
	}

	return dwServiceType;
}

CString CProfiledUpdateThread::GetUpdateInfo(CString sURL)
{
	switch (GetServerType(sURL))
	{
	case AFX_INET_SERVICE_HTTP:
	case AFX_INET_SERVICE_HTTPS:
		if (Open(sURL))
		{
			DWORD dwStatusCode = 0;

			((CHttpFile*) m_pInternetFile)->QueryInfoStatusCode(dwStatusCode);

			if (dwStatusCode == HTTP_STATUS_OK)
			{
				char szBuffer[BUFFER_DOWNLOADFILE];
				memset(szBuffer, 0, BUFFER_DOWNLOADFILE);
				int iFileBytes;			//文件大小
				int iFileBytesCopied;	//已经复制的文件
				int iBytesRead;			//读取的字节

				iFileBytes = static_cast<int>(m_pInternetFile->SeekToEnd());

				// 检测是否跳过文件
				if (iFileBytes != m_lStartReadingByte)
				{
					//将网络的文件指针移动到文件开始
					m_pInternetFile->Seek(m_lStartReadingByte, CFile::begin);
					iFileBytesCopied = m_lStartReadingByte;

					//获取版本信息保存在szBuffer中
					while (iBytesRead = m_pInternetFile->Read(szBuffer, BUFFER_DOWNLOADFILE))
					{
						iFileBytesCopied += iBytesRead;
					}
				}

				CString VersionInfo(szBuffer);

				if(!VersionInfo.IsEmpty())
				{
					return VersionInfo;
				}
			}
		}
		break;
	}

	return NULL;
}

BOOL CProfiledUpdateThread::Open(CString sURL)
{
	if(m_pInternetFile)
	{
		Close();
	}

	m_sURL = sURL;
	return EstablishConnection();
}

BOOL CProfiledUpdateThread::ParseUpdateInfo(CString UpdateInfo)
{
	try
	{
		GetCurrentVersion();

		CString		strSubMajor;		//主版本号
		CString		strSubMinor;		//副版本号
		CString		strSubUpdate;		//更新号
		CString		strSubBuild;		//建立日期
		CString		strSubURL;			//下载地址

		UINT     iMajor;			//主版本号
		UINT     iMinor;			//副版本号
		UINT	 iUpdate;			//更新号
		UINT     iBuild;			//建立日期

		AfxExtractSubString(strSubMajor,UpdateInfo, 0, '\\'); //分析字符串，得到主版本号
		AfxExtractSubString(strSubMinor, UpdateInfo, 1, '\\');//分析字符串，得到副版本号
		AfxExtractSubString(strSubUpdate, UpdateInfo, 2, '\\');//分析字符串，得到更新号
		AfxExtractSubString(strSubBuild, UpdateInfo, 3, '\\');//分析字符串，得到建立日期
		AfxExtractSubString(strSubURL, UpdateInfo, 4, '\\');//分析字符串，得到下载地址

		iMajor		= _ttoi((LPCTSTR)strSubMajor);				//主版本号
		iMinor		= _ttoi(strSubMinor);						//副版本号
		iUpdate		= _ttoi(strSubUpdate);	//更新号
		iBuild		= _ttoi(strSubBuild);						//建立日期

		int iCurrent = m_iMajor*10000000 + m_iMinor*100000 + m_iUpdate*10000 + m_iBuild;	//当前版本
		int iNewVersion = iMajor*10000000 + iMinor*100000 + iUpdate*10000 + iBuild;			//服务器端版本

		//m_plog->println(_T("当前版本：m_iMajor: %d m_iMinor: %d m_iUpdate%d m_iBuild: %d"), m_iMajor, m_iMinor, m_iUpdate, m_iBuild);
		//m_plog->println(_T("服务器端版本：iMajor: %d iMinor: %d iUpdate%d iBuild: %d"), iMajor, iMinor, iUpdate, iBuild);

		if(iNewVersion - iCurrent > 0)
		{
			char url[BUFFER_DOWNLOADFILE];
			memset(url, 0, BUFFER_DOWNLOADFILE);
			WideCharToMultiByte(CP_ACP, 0, strSubURL, strSubURL.GetLength(), url, BUFFER_DOWNLOADFILE, NULL, NULL);

			HANDLE hMapping;
			LPSTR lpData;

			hMapping = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, _T("Update"));

			if(hMapping == NULL)
			{
				m_iLastError = ERROR_MEMNOTOPEN;
				return FALSE;
			}

			lpData=(LPSTR)MapViewOfFile(hMapping, FILE_MAP_ALL_ACCESS, 0, 0, BUF_SIZE);
			CopyMemory((PVOID)lpData, url, strlen(url));

			if(lpData == NULL)
			{  
				m_iLastError = ERROR_WRITEMEM;
				return FALSE;
			}

			if(strSubURL.IsEmpty())
			{
				return FALSE;
			}

			return TRUE;
		}
		else
		{
			m_iLastError = ERROR_NONEWVERSION;
			return FALSE;
		}
	}
	catch (CException* e)
	{
		e->Delete();
		m_iLastError = ERROR_CHECKFAIL;
	}

	return FALSE;
}

void CProfiledUpdateThread::ProcessHttpRequest(CHttpFile* pFile)
{
	BOOL bRetry = FALSE;

	// 处理Http请求
	do 
	{
		try
		{
			pFile->SendRequest();						// 发送空的请求
		}
		catch (CException* pEx)
		{
			bRetry = ProcessHttpException(pFile, pEx);	//处理Http异常
			pEx->Delete();
		}
	}
	while (bRetry && m_iRetryCount++ <= 1);
}

BOOL CProfiledUpdateThread::ProcessHttpException(CHttpFile* pFile, CException* pEx)
{
	BOOL bReturn = FALSE;
	DWORD dwFlags;

	DWORD dwLastError = ((CInternetException*)pEx)->m_dwError;	//获取错误

	// 检测是否是有效的异常
	if (pEx && pEx->IsKindOf(RUNTIME_CLASS(CInternetException)))
	{
		CInternetException* pException = (CInternetException*) pEx;
		DWORD dwLastError = pException->m_dwError;		//获取错误

		switch (dwLastError)
		{
			// 忽略安全错误
		case ERROR_INTERNET_INVALID_CA:

			dwFlags = SECURITY_FLAG_IGNORE_CERT_CN_INVALID  | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID | SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_REVOCATION | SECURITY_FLAG_IGNORE_WRONG_USAGE;
			pFile->SetOption(INTERNET_OPTION_SECURITY_FLAGS, (LPVOID)&dwFlags, sizeof(dwFlags));

			bReturn = true;
			break;

		case ERROR_INTERNET_INVALID_URL:
			bReturn = false;
			break;

		default:
			bReturn = true;
			break;
		}
	}

	return bReturn;
}

void CProfiledUpdateThread::StartCheckVersion(void)
{
	//获取版本信息
	CString UpdateInfo;

#ifdef _DEBUG
	UpdateInfo = GetUpdateInfo(HTTP_LOCAL_ADDRES_DEBUG_DOWNLOAD);
#endif

#ifdef _BETA
	UpdateInfo = GetUpdateInfo(HTTP_REMOTE_ADDRES_BEAT_DOWNLOAD);
#endif

#ifdef _RELEASE
	UpdateInfo = GetUpdateInfo(HTTP_REMOTE_ADDRES_REALEASE_DOWNLOAD);
#endif

	if(UpdateInfo.IsEmpty())
	{
		PostThreadMessage(UM_ERROR, ERROR_SERVER, 0);
	}

	//解析版本信息
	if(ParseUpdateInfo(UpdateInfo))
	{
		m_iResult = RESULT_NEWVERSION;
		PostThreadMessage(WM_QUIT, 0, 0);
	}
	else
	{
		int lastError = GetLastError();
		PostThreadMessage(UM_ERROR, lastError, 0);
	}
}
// ProfiledUpdateThread 消息处理程序
void CProfiledUpdateThread::OnError(WPARAM wParam, LPARAM lParam)
{
	int iError = (int)wParam;

	switch (iError)
	{
	case ERROR_NONEWVERSION:
		if(m_iDebugLevel > 0)
		{
			m_plog->println(_T("error %d: %s"), iError, _T("No new version"));
		}
		
		m_iResult = ERROR_NONEWVERSION;
		break;
	case ERROR_NOCONNECTION:
		if(m_iDebugLevel > 0)
		{
			m_plog->println(_T("error %d: %s"), iError, _T("No connection"));
		}

		m_iResult = ERROR_NOCONNECTION;
		break;
	case ERROR_SERVER:
		if(m_iDebugLevel > 0)
		{
			m_plog->println(_T("error %d: %s"), iError, _T("Do not connection to Server"));
		}

		m_iResult = ERROR_SERVER;
		break;
	case ERROR_CHECKFAIL:
		if(m_iDebugLevel > 0)
		{
			m_plog->println(_T("error %d: %s"), iError, _T("Check failed"));
		}

		m_iResult = ERROR_CHECKFAIL;
		break;
	case ERROR_MEMNOTCREATE:
		if(m_iDebugLevel > 0)
		{
			m_plog->println(_T("error %d: %s"), iError, _T("Do not create memory"));
		}

		m_iResult = ERROR_MEMNOTCREATE;
		break;
	case ERROR_MEMNOTOPEN:
		if(m_iDebugLevel > 0)
		{
			m_plog->println(_T("error %d: %s"), iError, _T("Do not open memory"));
		}

		m_iResult = ERROR_MEMNOTOPEN;
		break;
	case ERROR_MEMNOTMAP:
		if(m_iDebugLevel > 0)
		{
			m_plog->println(_T("error %d: %s"), iError, _T("Do not map memory"));
		}

		m_iResult = ERROR_MEMNOTMAP;
		break;
	case ERROR_WRITEMEM:
		if(m_iDebugLevel > 0)
		{
			m_plog->println(_T("error %d: %s"), iError, _T("Do not map memory"));
		}

		m_iResult = ERROR_WRITEMEM;
		break;
	case ERROR_UNKNOWN:
		if(m_iDebugLevel > 0)
		{
			m_plog->println(_T("error %d: %s"), iError, _T("Unknow Error"));
		}

		m_iResult = ERROR_UNKNOWN;
		break;
	default:
		if(m_iDebugLevel > 0)
		{
			m_plog->println(_T("error %d: %s"), iError, _T("Other Error"));
		}

		m_iResult = iError;
		break;
	}
	PostThreadMessage(WM_QUIT, 0, 0);
}
