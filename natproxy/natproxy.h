#pragma  once
#include "common.h"
#include "xdclt.h"
#include "common/simplifythread.h"
#include "common/lock.h"
#include <map>


// ------------------------------------------------------------------
// 1 一个 proxy只有一个 xid和 一个 proxyid
// 2 可以建立多个连接 (connid)
// 3 xid+proxyid+conn_id确定一条连接. 
// 
class CClientSocket : public CXdSocket
{
public:
	CClientSocket() : m_ref(0) { memset(&m_proxyc, 0, sizeof(m_proxyc)); }
	~CClientSocket()  { Assert(m_ref == 0); XdLog(">> xsock, delete ..........%p\n", this);}

	int                 m_ref;
	XdTaskProxyClient_t m_proxyc;
};


class CNatProxyBase
{
public:
	#define BASENAME()                        m_bIsClient ? 'c': 's'
	typedef CSimplifyThread<CNatProxyBase>    CProxyClientThread;
	typedef std::map<int, CClientSocket*>     ClientSocketList_t;  // conn_id --> xsock
	CNatProxyBase(BOOL bClient)
		: m_xdc(0)
		, m_xid(0)
		, m_nProxyId(0)
		, m_nRun(0)
		, m_bIsClient(bClient)
	{}

	virtual ~CNatProxyBase()
	{
		Stop();
	}

	BOOL IsClient() { return m_bIsClient; }
	ClientId_t & ClientId() { return m_xid; }
	CXdClient  * XdClient() { return m_xdc; }

	void DumpInfo(char * szBuf, int nSize)
	{
		int nLen = 0;
		nLen   = _snprintf(szBuf, nSize-1, "(%c %d) ", BASENAME(), m_lstClient.size());
		szBuf += nLen; nSize -= nLen+5;

		CGenericLockHandler h(m_lock);
		for(ClientSocketList_t::iterator it = m_lstClient.begin(); it != m_lstClient.end(); it++)
		{
			nLen   = _snprintf(szBuf, nSize, "%d ", it->first);
			szBuf += nLen; nSize -= nLen; if(nLen >= nSize) break;
		}
	}

	virtual int  Init(CXdClient * xdc, ClientId_t xid, int nProxyId)
	{
		m_xdc = xdc; m_xid = xid; m_nProxyId = nProxyId;
		return 0;
	}

	virtual int  Start()
	{
		int nRet;

		InterlockedIncrement(&m_nRun);
		nRet  = m_thread.Start(this, &CNatProxyBase::ThreadClientMain); Assert(!nRet);
		return 0;
	}

	virtual int  Stop()
	{
		InterlockedDecrement(&m_nRun);
		m_thread.SafeStop();

		return 0;
	}

	virtual int  OnRecvXdMsg(XdMsgHeader_t * pMsg, XdTaskProxyId_t * pHead, void * szData, int nLen)
	{
		// 此处处理 XDSRV服务器返回的消息, 发给proxy. 
		// 注意和 proxy的接收数据不在一个线程, 而数据的顺序性由双方保证 ???
		int         nRet = 0, nDelete = 0, nConnId = 0;
		CClientSocket * xsock;

		Assert(m_xid == pMsg->nCidSrc);
		Assert(pHead->nProxyId == m_nProxyId && pHead->nConnectId);
		nConnId = pHead->nConnectId;
		xsock   = GetClientSocket(nConnId);
		if(!xsock)
		{
			XdLog("!! natproxy(%c), on xdcmsg, a invalid connect id -->(%I64d,%d,%d),%d\n", BASENAME(), m_xid, m_nProxyId, nConnId, pMsg->nMsgId);
			return 0;
		}

		Assert(pMsg->nMsgId == XdTask_ProxyData || pMsg->nMsgId == XdTask_ProxyDisconnect);
		if(pMsg->nMsgId == XdTask_ProxyDisconnect) nDelete = 1;
		else
		{
			nRet = xsock->SendAll((char*)szData, nLen);
			if(nRet)
			{
				nDelete = 1;
				nRet = SendDisconnectMsg(nConnId);
			}
		}

		if(nDelete)
		{
			XdLog(">> natproxy(%c), %s, to delete connect -->(%I64d,%d,%d),%d,%d,%p\n", BASENAME(), pMsg->nMsgId == XdTask_ProxyDisconnect ? "disconnect msg":"send failed", m_xid, m_nProxyId, nConnId, pMsg->nMsgId, nRet, xsock);
			DeleteClientSocket(nConnId, xsock);
			return nRet;
		}

		ReleaseClientSocketRef(xsock);
		return nRet;
	}

	virtual int  XdcSendMsg1(XdMsgHeader_t * pMsg)
	{
		pMsg->nCidDst = m_xid;
		Script_OnSendXdcMsg(g_ls, pMsg, pMsg->nSize);
		return m_xdc->SendMsg(pMsg);
	}


protected:
	virtual CClientSocket * GetClientSocket(int nConnId)
	{
		CGenericLockHandler h(m_lock);
		ClientSocketList_t::iterator it = m_lstClient.find(nConnId);
		if(it != m_lstClient.end())
		{
			it->second->m_ref++;
			return it->second;
		}

		return 0;
	}

	virtual void DeleteClientSocket(int nConnId, CClientSocket * sclt)
	{
		CGenericLockHandler h(m_lock);
		ClientSocketList_t::iterator it = m_lstClient.find(nConnId);
		if(it != m_lstClient.end()) m_lstClient.erase(it);
		Assert(sclt && sclt->m_ref > 0); sclt->m_ref--;
		if(sclt->m_ref == 0) delete sclt;
	}

	virtual void ReleaseClientSocketRef(CClientSocket * sclt)
	{
		CGenericLockHandler h(m_lock);
		Assert(sclt && sclt->m_ref > 0); sclt->m_ref--;
	}

	virtual int  GetConnectIdBySocket(SOCKET s)
	{
		return s;
	}


protected:
	void ThreadClientMain(void*)
	{
		int    nSize = XD_MSG_MAX_SENDBUF_SIZE;
		char * szBuf = (char*)malloc(nSize); Assert(szBuf);

		while(m_nRun > 0)
		{
			int     nRet;
			fd_set  fdRead;
			timeval tm = { 1, 0};

			FD_ZERO(&fdRead);
			if(!m_lstClient.empty())
			{
				CGenericLockHandler h(m_lock);
				for(ClientSocketList_t::iterator it = m_lstClient.begin(); it != m_lstClient.end(); it++)
				{
					FD_SET(it->second->Socket(), &fdRead);
				}
			}

			if(!fdRead.fd_count)
			{
				Sleep(50);
				continue;
			}

			nRet = ::select(fdRead.fd_count+1, &fdRead, 0, 0, &tm);
			if(nRet == 0) continue;
			if(nRet <  0)
			{
				int nErr = GetLastError();
				if(nErr == 10038) continue;
				XdLog("!! natproxy(%c), select read clients error -->%d,%d\n", BASENAME(), nRet, nErr);
				break;
			}

			for(int i = 0; i < (int)fdRead.fd_count; i++)
			{
				if(FD_ISSET(fdRead.fd_array[i], &fdRead))
				{
					nRet = HandleClientData(fdRead.fd_array[i], szBuf, nSize);
					if(nRet) break;
				}
			}

			if(nRet) break;
		}

		free(szBuf);
		m_nRun = 0;
	}

	int  HandleClientData(SOCKET s, char * szBuf, int nSize)
	{
		int  nRet, nConnId = 0, nLen = 0, nOff = 0;
		CClientSocket * xsock = 0;

		nConnId = GetConnectIdBySocket(s);
		if(nConnId) xsock = GetClientSocket(nConnId);
		if(!xsock)
		{
			// 这儿由于对端发了一个Disconnect消息后导致为空
			return 0;
		}

		nOff    = sizeof(XdMsgHeader_t)+sizeof(XdTaskProxyId_t);
		nRet    = xsock->Recv(szBuf+nOff, nSize-nOff, &nLen);
		if(nRet)
		{
			XdLog("!! natproxy(%c), recv data failed, disconnect this connect(%I64d,%d,%d),%d!!!(%d,%d)%p\n", BASENAME(), m_xid, m_nProxyId, nConnId, s, nRet, GetLastError(), xsock);
			nRet = SendDisconnectMsg(nConnId);
			DeleteClientSocket(nConnId, xsock);
			return 0;
		}
		else
		{
			ATTACH_XDMSG_EXDATA(szBuf, XdTaskProxyId_t, XdTask_ProxyData, 0);
			XDMSG_EXDATA()->nProxyId   = m_nProxyId;
			XDMSG_EXDATA()->nConnectId = nConnId;
			XDMSG()->nSize            += nLen;
			nRet =   XdcSendMsg1(XDMSG());
			ReleaseClientSocketRef(xsock);
			if(nRet) XdLog("?? natproxy(%c), send to xdserver msg failed, exit natproxy(%I64d,%d,%d),%d!!!(%d,%d)\n", BASENAME(), m_xid, m_nProxyId, nConnId, s, nRet, GetLastError());
			return nRet;
		}
	}

	int  SendDisconnectMsg(int nConnId)
	{
		ALLOC_XDMSG_EXDATA(XdTaskProxyId_t, XdTask_ProxyDisconnect, 0);
		XDMSG_EXDATA()->nProxyId   = m_nProxyId;
		XDMSG_EXDATA()->nConnectId = nConnId;

		XdLog(">> natproxy(%c), send dissconect msg -->%I64d, %d\n", BASENAME(), m_xid, m_nProxyId);
		return XdcSendMsg1(XDMSG());
	}


protected:
	CXdClient           * m_xdc;
	ClientId_t            m_xid;
	CProxyClientThread    m_thread;
	volatile long         m_nRun;
	BOOL                  m_bIsClient;
	int                   m_nProxyId;

	CCriticalSetionObject m_lock;
	ClientSocketList_t    m_lstClient;
};


class CNatProxyClient : public CNatProxyBase
{
public:
	CNatProxyClient()
		: CNatProxyBase(TRUE)
	{}


	int  Start(CXdClient * xdc, ClientId_t xid, int nProxyId)
	{
		Init(xdc, xid, nProxyId);
		return CNatProxyBase::Start();
	}
	
	int  CreateClient(XdMsgHeader_t * pMsg, XdTaskProxyClient_t * pProxyc)
	{
		int  nRet, nConnId;

		Assert(pMsg->nCidSrc == m_xid);
		Assert(pProxyc->head.nProxyId == m_nProxyId && pProxyc->head.nConnectId);
		Assert(pMsg->nMsgId == XdTask_ProxyClient);
		nConnId = pProxyc->head.nConnectId;

		if(!IsValidConnectId(nConnId))
		{
			XdLog("!! natproxyc create client, invalid connect id -->%I64d,%d,%d\n", m_xid, m_nProxyId, nConnId);
			return -1;
		}

		CClientSocket * sclt = new CClientSocket; Assert(sclt);
		sclt->m_proxyc = *pProxyc;

		if(sclt->m_proxyc.nSrvIp == 0) sclt->m_proxyc.nSrvIp = LOCAL_ADDRESS_IPV4;
		if(sclt->m_proxyc.nRetry <= 0) sclt->m_proxyc.nRetry = 1;

		for(int i = 0; i < sclt->m_proxyc.nRetry; i++)
		{
			nRet = sclt->Connect(sclt->m_proxyc.nSrvIp, sclt->m_proxyc.nSrvPort);
			if(!nRet) break;
			Sleep(1000);
		}

		if(nRet)
		{
			XdLog("!! natproxyc, connect pserver failed(%d) -->%I64d,%d,%d >>>%08x:%d\n", GetLastError(), m_xid, m_nProxyId, nConnId, sclt->m_proxyc.nSrvIp, sclt->m_proxyc.nSrvPort);
			delete sclt;
			return nRet;
		}
		else
		{
			sclt->GetSockAddr();
			sclt->SetAsyncMode();
			CGenericLockHandler h(m_lock);
			Assert(m_lstConnId[sclt->Socket()] == 0);
			m_lstClient[nConnId] = sclt;
			m_lstConnId[sclt->Socket()] = nConnId;
			return 0;
		}
	}


private:
	bool IsValidConnectId(int nConnId)
	{
		CGenericLockHandler h(m_lock);
		return m_lstClient.find(nConnId) == m_lstClient.end();
	}

	virtual void DeleteClientSocket(int nConnId, CClientSocket * sclt)
	{
		{
			CGenericLockHandler h(m_lock);
			SocketProxyIdList_t::iterator it2 = m_lstConnId.find(sclt->Socket());
			if(it2 != m_lstConnId.end()) m_lstConnId.erase(it2);
		}
		CNatProxyBase::DeleteClientSocket(nConnId, sclt);
	}

	virtual int  GetConnectIdBySocket(SOCKET s)
	{
		CGenericLockHandler h(m_lock);
		SocketProxyIdList_t::iterator it = m_lstConnId.find(s);
		return it != m_lstConnId.end() ? it->second : 0;
	}


private:
	typedef std::map<SOCKET, int>  SocketProxyIdList_t;
	SocketProxyIdList_t   m_lstConnId;
};


class CNatProxyServer : public CNatProxyBase
{
public:
	CNatProxyServer()
		: CNatProxyBase(FALSE)
	{}

	int  Start(CXdClient * xdc, ClientId_t xid, XdTaskProxyServer_t * pProxys)
	{
		int nRet, nIdx = pProxys->nIndex;

		m_proxys = *pProxys; Assert(m_proxys.nListenPort[nIdx]);
		if(m_proxys.nListenIp[nIdx] == 0)   m_proxys.nListenIp[nIdx] = LOCAL_ADDRESS_IPV4;
		Init(xdc, xid, m_proxys.nListenPort[nIdx]);

		nRet = m_srv.Listen(m_proxys.nListenIp[nIdx], m_proxys.nListenProxyPort[nIdx] ? m_proxys.nListenProxyPort[nIdx] : m_proxys.nListenPort[nIdx]);
		if(nRet)
		{
			XdLog("!! natproxys, listen port failed(%d) -->%I64d, %08x:%d!\n", GetLastError(), m_xid, m_proxys.nListenIp[nIdx], m_proxys.nListenPort[nIdx]);
			return nRet;
		}

		nRet = m_threadSrv.Start(this, &CNatProxyServer::ThreadServerMain); Assert(!nRet); 
		return CNatProxyBase::Start();
	}

	virtual int  Stop()
	{
		CNatProxyBase::Stop();

		m_srv.Close();
		m_threadSrv.SafeStop();

		m_nRun = 0;
		return 0;
	}


private:
	void ThreadServerMain(void*)
	{
		Assert(m_srv.IsConnected());
		while(m_nRun > 0)
		{
			int    nRet;
			SOCKET sn = 0;
			sockaddr_in addr = {0};
			CXdSocket::State_t state = {0}; 

			nRet = m_srv.Accept(&sn, &addr);
			if(nRet)
			{
				XdLog("!! natproxys, accept error, loop exit -->%d\n", GetLastError());
				break;
			}

			CClientSocket * xsock = new CClientSocket; Assert(xsock);
			state.connect = 1; state.socket = 1;
			xsock->Attach(sn, state);
			xsock->GetSockAddr();
			xsock->SetAsyncMode();

			{
				CGenericLockHandler h(m_lock);
				ClientSocketList_t::iterator it = m_lstClient.find(sn);
				if(it != m_lstClient.end())
				{
					delete xsock;
					XdLog("?? natproxys, find old socket!!!!");
					continue;
				}

				m_lstClient[sn] = xsock;
			}

			nRet = StartNewProxyClient(sn, &addr, xsock);
			if(nRet)
			{
				XdLog("!! natproxys, send xdmsg failed -->%d\n", GetLastError());
				break;
			}
		}

		m_nRun = 0;
	}

	int  StartNewProxyClient(SOCKET s, sockaddr_in * addr, CXdSocket * xsock)
	{
		ALLOC_XDMSG_EXDATA(XdTaskProxyClient_t, XdTask_ProxyClient, 0);
		XdTaskProxyClient_t * clt = XDMSG_EXDATA();

		Assert(m_xdc && m_proxys.nListenPort[m_proxys.nIndex]);
		clt->head.nProxyId   = m_nProxyId;
		clt->head.nConnectId = s;
		clt->nSrvIp          = m_proxys.nListenIp[m_proxys.nIndex];
		clt->nSrvPort        = m_proxys.nListenPort[m_proxys.nIndex];
		return XdcSendMsg1(XDMSG());
	}


private:
	typedef CSimplifyThread<CNatProxyServer> CProxyServerThread;
	CProxyServerThread    m_threadSrv;

	CXdSocket             m_srv;
	XdTaskProxyServer_t   m_proxys;
};
