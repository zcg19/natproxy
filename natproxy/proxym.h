#pragma  once
#include "natproxy.h"


class CXdProxyClient : public CXdClient
{
public:
	CXdProxyClient(CCriticalSetionObject & lock)
		: m_lock(lock)
	{}

	virtual int  SendMsg(XdMsgHeader_t * pMsg)
	{
		CGenericLockHandler h(m_lock);
		return CXdClient::SendMsg(pMsg);
	}


private:
	CCriticalSetionObject  & m_lock;
};


class CNatProxyManager
{
public:
	CNatProxyManager()
		: m_nRun(0)
		, m_nRunFile(0)
		, m_xdc(m_lock)
	{}

	~CNatProxyManager()
	{
		Stop();
	}

	int  IsRun() { return m_nRun > 0; }
	int  Start(const char * szExePath);
	int  Stop();

	int  PushFile(ClientId_t xid, const char * szPath);
	int  PullFile(ClientId_t xid, const char * szPath);
	void DumpInfo();

	int  CreateNatProxyServer(ClientId_t xid, int nIp, int nPort, int nProxyPort = 0)
	{
		// XdTask_AssertProxyServer验证 xid并创建 ProxyServer. 
		ALLOC_XDMSG_EXDATA(XdTaskProxyServer_t, XdTask_AssertProxyServer, xid);
		XdTaskProxyServer_t * proxys = XDMSG_EXDATA();
		proxys->nCount       = 1;
		proxys->nListenIp[0] = nIp; proxys->nListenPort[0] = nPort; proxys->nListenProxyPort[0] = nProxyPort;

		Assert(m_xdc.IsConnected());
		return XdcSendMsg2(XDMSG());
	}

	int  TalkToPeer(ClientId_t xid, const char * szAword, bool bIsPing = false)
	{
		int  nLen = 0;
		char szBuf[1024] = {0};
		ATTACH_XDMSG_EXDATA(szBuf, char, bIsPing ? XdTask_Ping:XdTask_Talk, xid);

		if(szAword)  nLen = strlen(szAword)+1;
		if(!szAword) return 0;
		if(nLen+sizeof(XdMsgHeader_t)>sizeof(szBuf)) return 0;

		Assert(m_xdc.IsConnected());
		strcpy(XDMSG_EXDATA(), szAword); 
		XDMSG()->nSize = sizeof(XdMsgHeader_t)+nLen;
		return XdcSendMsg2(XDMSG());
	}

	int  RefreshProxyList()
	{
		CGenericLockHandler h(m_lock);
		for(NatProxyMultList_t::iterator it1 = m_lstProxy.begin(); it1 != m_lstProxy.end(); it1++)
		{
			TalkToPeer(it1->first, "hello!!!");
		}
		return 0;
	}


private:
	void ThreadMain(void*);
	void ThreadMainSendFile(void*);
	int  HandleXdcMsg(XdMsgHeader_t * pMsg);
	int  CreateNatProxyServerImpl(ClientId_t xid, XdTaskProxyServer_t * proxys);

	CNatProxyBase * GetNatProxy(ClientId_t xid, int nProxyId)
	{
		CGenericLockHandler h(m_lock);
		NatProxyMultList_t::iterator it = m_lstProxy.find(xid);
		if(it != m_lstProxy.end())
		{
			NatProxyList_t::iterator it2 = it->second.find(nProxyId);
			return it2 != it->second.end() ? it2->second : 0;
		}

		return 0;
	}

	int  DeleteNatProxy(ClientId_t xid)
	{
		CGenericLockHandler h(m_lock);
		NatProxyMultList_t::iterator it = m_lstProxy.find(xid);
		if(it == m_lstProxy.end()) return 0;

		// 这儿会造成 lock卡死一段时间 ???
		for(NatProxyList_t::iterator it2 = it->second.begin(); it2 != it->second.end(); it2++)
		{
			it2->second->Stop();
			delete it2->second;
		}

		m_lstProxy.erase(it);
		return 0;
	}

	int  XdcSendMsg2(XdMsgHeader_t * pMsg)
	{
		Assert(pMsg->nCidDst);
		Script_OnSendXdcMsg(g_ls, pMsg, pMsg->nSize);
		return m_xdc.SendMsg(pMsg);
	}

	void DeleteSendFile(ClientId_t xid)
	{
		CGenericLockHandler h(m_lockFile);
		for(FileInfoList_t::iterator it = m_lstFileSend.begin(); it != m_lstFileSend.end(); it++)
		{
			it->second->nResult = 9;
		}
	}


private:
	typedef struct XdFileInfo_t
	{
		ClientId_t  id;
		int         nFileId;
		int         nResult;
		__int64     nFileSize, nHandleLen;
		FILE      * pf;
		char        szPath[MAX_PATH];
	}XdFileInfo_t;
	typedef std::map<int, XdFileInfo_t*>         FileInfoList_t;
	typedef std::map<int, CNatProxyBase*>        NatProxyList_t;
	typedef std::map<ClientId_t, NatProxyList_t> NatProxyMultList_t;
	typedef CSimplifyThread<CNatProxyManager>    CNatProxyMgrThread;

	CNatProxyMgrThread    m_thread, m_threadFile;
	CXdProxyClient        m_xdc;
	char                  m_szConfig[MAX_PATH], m_szRecvFilePath[MAX_PATH];

	CCriticalSetionObject m_lock, m_lockFile;
	NatProxyMultList_t    m_lstProxy;
	FileInfoList_t        m_lstFileSend, m_lstFileRecv;
	volatile long         m_nRun, m_nRunFile;
};
