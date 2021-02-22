#include <time.h>
#include <map>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef WIN32
#define  FMT_INT64            "%I64d"
#else
#include <unistd.h>

#define  BOOL                 int
#define  __int64              long long
#define  ULONG_PTR            intptr_t
#define  SOCKET               int

#define  FMT_INT64            "%lld"
#define  INVALID_SOCKET       -1
#define  SOCKET_ERROR         -1
#define  INFINITE             -1
#define  TRUE                 1
#define  FALSE                0

#define  CloseHandle(_h)      
#define  GetTickCount()       clock()
#define  GetCurrentThreadId() (int)(intptr_t)pthread_self()
#define  closesocket          close
#define  Sleep(_ms)           usleep(_ms*1000)
inline   int  GetLastError()  { return errno; }
#endif

#include "common/xsocket.h"
#include "common/simplifythread.h"
#include "common/lock.h"
#include "../netdata.h"


class CXdServer
{
public:
	typedef CSimplifyThread<CXdServer>           CXdServerThread;
	typedef std::map<SOCKET, CXdServerThread *>  XdServerThreadList_t;
	typedef std::map<ClientId_t, CXdSocket*>     ClientList_t;
	CXdServer()
	{}

	int  Start(const char * szIp, int nPort)
	{
		int nRet, nIp = 0;

		if(szIp && *szIp) nIp = inet_addr(szIp);
		nRet = m_xsock.Create(); Assert(!nRet);
		nRet = m_xsock.Listen(nIp, nPort);
		if(nRet)
		{
			XdLog("!! server start, listen failed --->%x:%d, %d,%d\n", nIp, nPort, nRet, GetLastError());
			return nRet;
		}

		nRet = m_thread.Start(this, &CXdServer::ThreadMain); Assert(!nRet);
		return nRet;
	}

	int  Stop()
	{
		m_xsock.Close();
		m_thread.SafeStop();

		for(XdServerThreadList_t::iterator it = m_lstThread.begin(); it != m_lstThread.end();)
		{
			XdServerThreadList_t::iterator itd = it; itd++;
			CXdServerThread  * pt = it->second;

			closesocket(it->first); pt->SafeStop();
			delete pt;  it = itd;
		}
		return 0;
	}

	void DumpInfo()
	{
		CGenericLockHandler h(m_lock);
		XdLog(">> manager, client list -->%d\n", m_lstClient.size());
		for(ClientList_t::iterator it = m_lstClient.begin(); it != m_lstClient.end(); it++)
		{
			XdLog("\t"FMT_INT64", %08x:%d\n", it->first, it->second->RemoteIp(), it->second->RemotePort());
		}
	}


private:
	void ThreadMain(void *)
	{
		while(1)
		{
			int         nRet;
			SOCKET      ns;
			sockaddr_in addr = {0};

			nRet = m_xsock.Accept(&ns, &addr);
			if(nRet)
			{
				XdLog("!! server accept error, exit --->%d, %d\n", nRet, GetLastError());
				return;
			}

			CXdServerThread * thread = new CXdServerThread;
			thread->Start(this, &CXdServer::WorkThreadMain, (void*)(ULONG_PTR)ns);

			CGenericLockHandler h(m_lock);
			m_lstThread[ns] = thread;
		}
	}

	void WorkThreadMain(void* p)
	{
		SOCKET             s = (SOCKET)(ULONG_PTR)p;
		CXdSocket::State_t state = {0}; 

		CXdSocket          xsock;
		ClientId_t         cid   = 0;
		int                nSize = 1024 * 1024, nOff = 0;
		char             * szBuf = 0;

		szBuf = (char*)malloc(nSize); Assert(szBuf);
		state.connect = 1; state.socket = 1;
		xsock.Attach(s, state);
		xsock.GetSockAddr();

		cid = CreateClientId(&xsock); Assert(cid);
		if(DownloadClient(&xsock) || ExchangeClientInfo(&xsock, cid))
		{
			DeleteClientId(cid);
			XdLog(">> xdsession, a invalidate client("FMT_INT64")\n", cid);
			return;
		}

		while(1)
		{
			int  nRet, nRecvedLen = 0, nHandledLen = 0;

			int  nId = ::GetCurrentThreadId();
			nRet = xsock.Recv(szBuf+nOff, nSize-nOff, &nRecvedLen);
			if(nRet)
			{
				XdLog(">> xdsession, recv failed --->%d, %d,%d\n", s, nRet, GetLastError());
				break;
			}

			//XdLog(">> xdsession, recv data -->%d,%d\n", nOff, nRecvedLen);
			if(nRecvedLen == 0) continue;
			nRecvedLen += nOff; nHandledLen = nRecvedLen; nOff = 0;
			nRet = ExchangeDatas(&xsock, cid, szBuf+nOff, &nHandledLen);
			if(nRet)
			{
				XdLog(">> xdsession, handle data failed --->%d, %d,%d\n", s, nRet, GetLastError());
				break;
			}

			Assert(nRecvedLen >= nHandledLen && nHandledLen >= 0);
			nOff = nRecvedLen  - nHandledLen;
			if(nOff > 0 && nHandledLen > 0) memmove(szBuf, szBuf+nHandledLen, nOff);
		}

		free(szBuf);
		DeleteClientId(cid);
		CGenericLockHandler h(m_lock);
		XdServerThreadList_t::iterator it = m_lstThread.find(s);
		Assert(it != m_lstThread.end()); m_lstThread.erase(it);
	}


private:
	BOOL IsValidMsg(XdMsgHeader_t * pMsg)
	{
		if(pMsg->nSrvMsgId != XdMsgC_ExchangeData) return FALSE;
		return IsValidXdMsgSize(pMsg);
	}

	ClientId_t CreateClientId(CXdSocket * xsock)
	{
		// 要保证唯一性
		ClientId_t  id;
		CGenericLockHandler h(m_lock);
		while(1)
		{
			time_t  t = ::time(0);
			__int64 r = t + GetTickCount();

			if(m_lstClient.end() == m_lstClient.find(r))
			{
				id = r;
				break;
			}
		}

		Assert(xsock);
		m_lstClient[id] = xsock;
		return id;
	}

	void DeleteClientId(ClientId_t id)
	{
		CGenericLockHandler h(m_lock);
		ClientList_t::iterator it = m_lstClient.find(id);
		if(it != m_lstClient.end()) m_lstClient.erase(it);
	}

	CXdSocket * GetXdSocketByCid(ClientId_t & nDst, ClientId_t & nSrc)
	{
		CGenericLockHandler h(m_lock);
		ClientList_t::iterator it = m_lstClient.find(nDst);
		if(it == m_lstClient.end())
		{
			if(nDst == 2)
			{
				nDst =  nSrc;
				it   =  m_lstClient.find(nSrc); Assert(it != m_lstClient.end());
				return it->second;
			}

			if(nDst == 3 && m_lstClient.size() == 2)
			{
				ClientList_t::iterator    it2;
				it   = m_lstClient.begin(); it2 = it++;
				nDst = it->first != nSrc ? it->first : it2->first;
				Assert(it->first == nSrc || it2->first == nSrc);
				return it->first != nSrc ?  it->second : it2->second;
			}
		}

		return it != m_lstClient.end() ? it->second : 0;
	}

	int  DownloadClient(CXdSocket * xsock)
	{
		// 下面这个增加为了下载客户端文件. 
		int          nRet = 0, nLen = 0;
		char         szBuf[4096] = {0}, *p1 = 0, *p2 = 0;
		const char * szHeadFmt = "HTTP/1.1 200 OK\r\nConetent-Length: %d\r\nContent-Type: application/octet-stream\r\nConnection: Close\r\n\r\n";
		FILE       * pf = 0;

		xsock->SetRecvTimeout(10);
		nRet = xsock->Recv(szBuf, sizeof(szBuf), &nLen);
		if(nLen == 0 && GetLastError() == WSAETIMEDOUT)
		{
			xsock->SetRecvTimeout(60*60*24);
			return 0;
		}

		p1 = strstr(szBuf, "\r\n\r\n"); p2 = strstr(szBuf, " HTTP/1.1\r\n");
		if(strstr(szBuf, "GET /file?name=") != szBuf) return -5;
		if(!p1 || !p2 || (p1-szBuf) != (nLen-4)) return -5;
		p1 = szBuf+15; *p2 = 0;
		pf = fopen(p1, "rb"); if(!pf) return -5;
		fseek(pf, 0, SEEK_END); nLen = ftell(pf); fseek(pf, 0, SEEK_SET); 
		if(nLen == 0) { fclose(pf); return -5; }

		nLen = sprintf(szBuf, szHeadFmt, nLen);
		nRet = xsock->Send(szBuf, nLen, &nLen);
		while(1)
		{
			nLen = fread(szBuf, 1, sizeof(szBuf), pf);
			if(nLen > 0) nRet = xsock->Send(szBuf, nLen, 0);
			if(nLen < sizeof(szBuf)) break;
		}
		fclose(pf);
		return -5;
	}

	int  ExchangeClientInfo(CXdSocket * xsock, ClientId_t nDst)
	{
		int  nRet, nLen = 0;
		char szBuf[sizeof(XdMsgHeader_t)+sizeof(XdClientInfo_t)] = {0};
		XdMsgHeader_t  * lpMsg  = (XdMsgHeader_t*)szBuf;
		XdClientInfo_t * lpData = (XdClientInfo_t*)(lpMsg+1);

		lpMsg->nCidDst     = nDst;
		lpMsg->nSrvMsgId   = XdMsgS_ClientInfo;
		lpMsg->nSize       = sizeof(szBuf);
		lpData->nCid       = nDst;
		lpData->nNatIp     = xsock->RemoteIp();
		lpData->nNatPort   = xsock->RemotePort();

		nRet = xsock->SendAll(szBuf, sizeof(szBuf));
		if(!nRet)
		{
			nRet = xsock->RecvAll(szBuf, 4, &nLen);
			if(nLen == 4 && lpMsg->nSize == sizeof(szBuf)) nRet = xsock->RecvAll(szBuf+4, sizeof(szBuf)-4, &nLen);
			if(!nRet && nLen == sizeof(szBuf)-4 && 
				lpMsg->nCidDst == 0 && lpMsg->nCidSrc == nDst && lpData->nCid == nDst && 
				lpMsg->nSrvMsgId == XdMsgC_ClientInfo && lpData->nNatIp && lpData->nNatPort)
			{
				XdLog(">> accept a connection -->"FMT_INT64", %x:%d\n", nDst, lpData->nNatIp, lpData->nNatPort);
				return 0;
			}
			if(!nRet) nRet = -2;
		}
		return nRet;
	}

	int  SendBreakConnectMsg(CXdSocket * xsock, ClientId_t nDstCid, ClientId_t nBreakCid)
	{
		int  nRet;
		char szBuf[sizeof(XdMsgHeader_t)+sizeof(XdBreakConnect_t)] = {0};
		XdMsgHeader_t    * lpMsg  = (XdMsgHeader_t*)szBuf;
		XdBreakConnect_t * lpData = (XdBreakConnect_t*)(lpMsg+1);

		lpMsg->nCidDst   = nDstCid;
		lpMsg->nSrvMsgId = XdMsgS_BreakConnect;
		lpMsg->nSize     = sizeof(szBuf);
		lpData->nCid     = nBreakCid;

		nRet = xsock->SendAll(szBuf, sizeof(szBuf));
		return nRet;

	}

	int  ExchangeDatas(CXdSocket * xsock, ClientId_t nSrc, char* lpData, int* lpnDataLen)
	{
		int  nLen = *lpnDataLen;

		while(1)
		{
			int nRet, nHandledLen = nLen;

			nRet = ExchangeData(xsock, nSrc, lpData, &nHandledLen);
			if(nRet) return nRet;
			if(nHandledLen == 0) break;

			//XdLog(">> exchange data2 -->%d,%d,  %d\n", nLen, nHandledLen, ((XdMsgHeader_t *)lpData)->nSize);
			Assert(nLen >= nHandledLen);
			lpData += nHandledLen; nLen -= nHandledLen;
			if(nLen == 0) break;
		}

		*lpnDataLen -= nLen;
		return 0;
	}

	int  ExchangeData(CXdSocket * xsock, ClientId_t nSrc, char* lpData, int* lpnDataLen)
	{
		CXdSocket     * pDstXsock;
		int             nRet, nDataLen = *lpnDataLen;
		XdMsgHeader_t * pMsg = (XdMsgHeader_t *)lpData;

		if(nDataLen < sizeof(*pMsg))
		{
			*lpnDataLen = 0;
			return 0;
		}

		if(!IsValidMsg(pMsg))
		{
			// 数据无效直接删除连接, 否则会导致后序也不能恢复
			XdLog("!! xdsession, invalid net data -->("FMT_INT64"->"FMT_INT64",%d), (%d,%d,%d)\n", pMsg->nCidSrc, pMsg->nCidDst, pMsg->nSrvMsgId, pMsg->nMsgId, pMsg->nSize, nDataLen);
			return -1;
		}

		// 不完整的数据, 即使是无效XID也要保证 MSG的数据完整性.
		if(nDataLen < pMsg->nSize)
		{
			*lpnDataLen = 0;
			return 0;
		}

		//printf("-- xdsession, recv data ok --->("FMT_INT64"->"FMT_INT64",%d,%d), (%d,%d,%d)\n", pMsg->nCidSrc, pMsg->nCidDst, pMsg->nSrvMsgId, pMsg->nMsgId, pMsg->nDataId, pMsg->nSize, nDataLen);
		Assert(pMsg->nCidSrc == nSrc);
		*lpnDataLen = nDataLen = pMsg->nSize;

		// 先判断是否有 cid
		pDstXsock = (CXdSocket*)GetXdSocketByCid(pMsg->nCidDst, pMsg->nCidSrc);
		if(!pDstXsock)
		{
			SendBreakConnectMsg(xsock, pMsg->nCidSrc, pMsg->nCidDst);
			XdLog("!! xdsession, not find peer data("FMT_INT64"->"FMT_INT64"), drop data(%d,%d,%d)!!!\n", pMsg->nCidSrc, pMsg->nCidDst, pMsg->nSrvMsgId, pMsg->nMsgId, pMsg->nSize, nDataLen);
			return 0;
		}

		nRet = pDstXsock->SendAll(lpData, nDataLen);
		if(nRet)
		{
			XdLog(">> xdsession, send data failed --->%d,%d\n", nRet, GetLastError());
			return -1;
		}

		return 0;
	}


private:
	CXdServerThread       m_thread;
	CXdSocket             m_xsock;

	CCriticalSetionObject m_lock;
	XdServerThreadList_t  m_lstThread;
	ClientList_t          m_lstClient;
};


int  main(int argc, char ** argv)
{
	int  nRet, nPort = 22222;
	CXdServer  xdsrv;

	if(argc == 2)
	{
		nPort = atoi(argv[1]);
		if(!nPort) nPort = 22222;
	}

	XdLog(">> server config: -->%d\n", nPort);
	InitSocket();
	nRet = xdsrv.Start(0, nPort);
	if(nRet)
	{
		XdLog(">> server start failed -->%d,%d\n", nRet, GetLastError());
		return 1;
	}

	while(1)
	{
		char ch = 0;

		fflush(stdin); 
		ch = getchar();

		if(ch == 'q')
		{
			xdsrv.Stop();
			break;
		}

		switch(ch)
		{
		case 'm':
			xdsrv.DumpInfo();
			break;
		default:
			Sleep(1000);
			break;
		}
	}

	UninitSocket();
	XdLog(">> process exit ....... (%d)\n", nRet);
	getchar();
	return 0;
}

