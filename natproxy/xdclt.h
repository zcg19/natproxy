/*********************************************
 * 数据交换的客户端
 * 功能: 通过服务器与对端建立连接，并利用了loopback进行数据转发。
 *   远程桌面和SMB不能用 ???
 * 
 * 1 与服务器的交互
 *    处理 3种消息参考服务器
 * 2 与对端的交互
 *   验证对方???
 *	 处理实际的功能 ???
**********************************************/
#pragma  once
#include <stdio.h>
#include <time.h>
#include <map>

#include "common/xsocket.h"
#include "../netdata.h"


class CXdClient
{
public:
	CXdClient()
		: m_bCinfo(false)
		, m_nSrvIp(0)
		, m_nSrvPort(0)
		, m_nDataId(0)
		, m_pf(0)
	{
		memset(&m_cinfo, 0, sizeof(m_cinfo));
		#ifdef _DEBUG
		//m_pf = fopen("d:/mylog/natproxy.log", "wb"); Assert(m_pf);
		#endif
	}

	virtual ~CXdClient()
	{
		if(m_pf) fclose(m_pf);
	}

	CXdSocket      * XdSocket()    { return &m_sock; }
	XdClientInfo_t * ClientInfo()  { return &m_cinfo; }
	int              ServerIp()    { return m_nSrvIp; }
	int              ServerPort()  { return m_nSrvPort; }
	BOOL             IsConnected() { return m_sock.IsConnected(); }

	void DumpCinfo(char * szInfo, int nSize)
	{
		_snprintf(szInfo, nSize-1, "%I64d:%08x:%d >>>%08x:%d", m_cinfo.nCid, m_cinfo.nNatIp, m_cinfo.nNatPort, m_sock.LocalIp(), m_sock.LocalPort());
	}


	virtual int  Start(const char * szIp, int nPort)
	{
		m_nSrvIp   = inet_addr(szIp);
		m_nSrvPort = nPort;
		return StartImpl();
	}

	virtual int  Stop()
	{
		m_sock.Close();
		return 0;
	}

	virtual int  SendMsg(XdMsgHeader_t * pMsg)
	{
		Assert(pMsg && pMsg->nSize < XD_MSG_MAX_SIZE);
		pMsg->nCidSrc   = m_cinfo.nCid;
		pMsg->nDataId   = InterlockedIncrement(&m_nDataId);
		pMsg->nSrvMsgId = XdMsgC_ExchangeData;

		#ifdef _DEBUG
		/*if(pMsg->nMsgId == XdTask_ProxyData)
		{
			char * szData = (char*)(pMsg+1);
			XdLog("-- xdc, send xdmsg -->%I64d, %d,%d,%d, (%d,%d)\n", pMsg->nCidDst, pMsg->nDataId, pMsg->nMsgId, pMsg->nSize, ((XdTaskProxyId_t*)(pMsg+1))->nProxyId, ((XdTaskProxyId_t*)(pMsg+1))->nConnectId);
			fprintf(m_pf, "send-----------------%I64d ->%I64d, %d,%d\n", pMsg->nCidSrc, pMsg->nCidDst, pMsg->nDataId, pMsg->nSize);
			for(int i = 0; i < sizeof(*pMsg); i++) fprintf(m_pf, "%02x ", ((unsigned char*)pMsg)[i]); fwrite("\n", 1, 1, m_pf);
			for(int i = 0; i < pMsg->nSize-sizeof(*pMsg); i++) fprintf(m_pf, "%02x ", (unsigned char)szData[i]); fwrite("\n", 1, 1, m_pf);
			fflush(m_pf);
		}*/
		#endif
		return m_sock.SendAll((char*)pMsg, pMsg->nSize);
	}

	virtual int  RecvMsg(void * szData, int nSize)
	{
		// 判断 lpMsg->nSize如果为 0, 则这个包无效. 
		// 验证失败不返回错误，丢弃这个包吧
		int  nRet, nLen = 0;
		XdMsgHeader_t * lpMsg;

		Assert(nSize >= sizeof(XdMsgHeader_t));
		nRet = m_sock.RecvAll((char*)szData, sizeof(XdMsgHeader_t), &nLen, true);
		if(nRet) return nRet;
		if(nLen == 0) return 0;

		lpMsg = (XdMsgHeader_t*)szData;
		if(!AssertRecvMsg(lpMsg))
		{
			lpMsg->nSize = 1;
			return 0;
		}

		Assert(lpMsg->nSize <= nSize);
		nRet = m_sock.RecvAll((char*)szData+sizeof(*lpMsg), lpMsg->nSize-sizeof(*lpMsg), &nLen, false);
		if(nRet) lpMsg->nSize = 2;

		// -------------------------------------------------
		// 测试包的完整性
		static std::map<ClientId_t, int> g_testConnId;
		int nLastId = g_testConnId[lpMsg->nCidSrc];
		g_testConnId[lpMsg->nCidSrc] = lpMsg->nDataId;
		Assert(nLastId == 0 || nLastId + 1 == lpMsg->nDataId);
		// -------------------------------------------------

		#ifdef _DEBUG
		/*if(lpMsg->nMsgId == XdTask_ProxyData)
		{
			XdLog("-- xdc, recv xdmsg <--%I64d,%d, %d,%d,%d (%d,%d)\n", lpMsg->nCidSrc, lpMsg->nDataId, lpMsg->nMsgId, lpMsg->nSize, nRet, 
				((XdTaskProxyId_t*)((char*)szData+sizeof(*lpMsg)))->nProxyId, ((XdTaskProxyId_t*)((char*)szData+sizeof(*lpMsg)))->nConnectId);
			fprintf(m_pf, "recv-----------------%I64d ->%I64d, %d,%d\n", lpMsg->nCidSrc, lpMsg->nCidDst, lpMsg->nDataId, lpMsg->nSize, nLen);
			for(int i = 0; i < sizeof(*lpMsg); i++) fprintf(m_pf, "%02x ", ((unsigned char*)lpMsg)[i]); fwrite("\n", 1, 1, m_pf);
			for(int i = 0; i < nLen; i++) fprintf(m_pf, "%02x ", ((unsigned char*)szData+sizeof(*lpMsg))[i]); fwrite("\n", 1, 1, m_pf);
			fflush(m_pf);
		}*/
		#endif
		return nRet;
	}


protected:
	int  StartImpl()
	{
		// 同步可能会卡死...
		char   szBuf[sizeof(XdMsgHeader_t)+sizeof(XdClientInfo_t)] = {0};
		int    nRet, nRecvSize = sizeof(szBuf);
		char * p = szBuf;

		XdLog(">> xdclient, xdserver config -->%08x:%d\n", m_nSrvIp, m_nSrvPort);
		nRet = m_sock.Connect(m_nSrvIp, m_nSrvPort);
		if(nRet) return -1;

		while(1)
		{
			int nRecvedLen = 0;
			nRet = m_sock.Recv(p, nRecvSize, &nRecvedLen);
			if(nRet) return nRet;
			if(nRecvSize == nRecvedLen) break;

			p += nRecvedLen; nRecvSize -= nRecvedLen;
			Assert(nRecvSize > 0 && nRecvedLen >= 0);
		}

		XdMsgHeader_t * pMsg = (XdMsgHeader_t*)szBuf;
		if(!AssertCinfoMsg(pMsg))
		{
			XdLog(">> xcclt, connect ok but cinfo is invalid !!!\n");
			return XdError_InvalidMsg_Cinfo;
		}

		m_bCinfo = TRUE;
		memcpy(&m_cinfo, pMsg+1, sizeof(m_cinfo));
		m_sock.GetSockAddr();

		// send client info.
		pMsg->nCidSrc = m_cinfo.nCid; pMsg->nCidDst = 0; pMsg->nSrvMsgId += 1;
		((XdClientInfo_t*)(pMsg+1))->nNatIp = m_sock.LocalIp(); ((XdClientInfo_t*)(pMsg+1))->nNatPort = m_sock.LocalPort();
		return m_sock.SendAll(szBuf, sizeof(szBuf));
	}

	BOOL AssertCinfoMsg(XdMsgHeader_t * pMsg)
	{
		return 
			pMsg->nSrvMsgId  == XdMsgS_ClientInfo && 
			pMsg->nSize   == sizeof(XdMsgHeader_t)+sizeof(XdClientInfo_t) && 
			pMsg->nCidDst != 0 && pMsg->nCidSrc == 0 && 
			pMsg->nCidDst == ((XdClientInfo_t*)(pMsg+1))->nCid
			;
	}

	BOOL AssertRecvMsg(XdMsgHeader_t * pMsg)
	{
		if(!IsValidXdMsgSize(pMsg)) return FALSE;

		switch(pMsg->nSrvMsgId)
		{
		case XdMsgS_BreakConnect:
			if(pMsg->nCidSrc != 0) return FALSE;
			if(pMsg->nSize != sizeof(*pMsg)+sizeof(XdBreakConnect_t)) return FALSE;
			return TRUE;
		case XdMsgC_ExchangeData:
			return pMsg->nCidSrc && pMsg->nCidDst == m_cinfo.nCid;
		default:
			return FALSE;
		}
	}


protected:
	CXdSocket                  m_sock;
	XdClientInfo_t             m_cinfo;
	bool                       m_bCinfo;
	int                        m_nSrvIp, m_nSrvPort;
	volatile long              m_nDataId;
	FILE                     * m_pf;
};
