#include "proxym.h"
#include "net/clt/ipaddress.h"


static void ResolveIpv4Address(char * szDomain)
{
	int  nIp = 0;
	SocketAddressList_t lstAddr;

	nIp = inet_addr(szDomain);
	if(nIp && nIp != -1) return ;

	ResolveAddress(szDomain, 80, lstAddr);
	for(SocketAddressList_t::iterator it = lstAddr.begin(); it != lstAddr.end(); it++)
	{
		if(it->nType == SocketAddress_t::Type_IpV4 && it->addr4.sin_addr.S_un.S_addr && it->addr4.sin_addr.S_un.S_addr != -1)
		{
			strcpy(szDomain, inet_ntoa(it->addr4.sin_addr));
			break;
		}
	}
}


int  CNatProxyManager::Start(const char * szExePath)
{
	char szTmp[128] = {0}, szPath[MAX_PATH] = {0};
	int  nRet, nPort = 0;

	_snprintf(m_szConfig, sizeof(m_szConfig), "%s.cfg", szExePath);
	GetPrivateProfileStringA("common", "script_path", "", szPath, _countof(szPath), m_szConfig);
	if(szPath[0] == '.' || szPath[2] != ':')
	{
		char  szExePath2[MAX_PATH] = {0}, *p;
		int   nOffset = szPath[0] == '.' ? 2 : 0; 

		if(nOffset == 2 && szPath[1] == '.') nOffset = 3;
		strcpy(szExePath2, szExePath);
		if(p = strrchr(szExePath2, '\\'))
		{
			*p = 0;
			if(nOffset == 3) p = strrchr(szExePath2, '\\'); *p = 0;
			Assert(p); strcpy(szTmp, szPath);
			_snprintf(szPath, sizeof(szPath), "%s\\%s", szExePath2, szTmp+nOffset);
		}
	}
	nPort = GetPrivateProfileIntA("xdserver", "port", 12345, m_szConfig);
	GetPrivateProfileStringA("xdserver", "ip", "127.0.0.1", szTmp, _countof(szTmp), m_szConfig);
	GetPrivateProfileStringA("common", "recv_filepath", ".", m_szRecvFilePath, _countof(m_szRecvFilePath), m_szConfig);

	ResolveIpv4Address(szTmp);
	if(*szPath) g_ls = Script_Init(szPath);
	nRet = m_xdc.Start(szTmp, nPort);
	if(nRet)
	{
		XdLog("?? manager, xdc connect server failed -->(%d)(%08x:%d)\n", GetLastError(), m_xdc.ServerIp(), m_xdc.ServerPort());
		return nRet;
	}

	memset(szTmp, 0, sizeof(szTmp));
	m_xdc.DumpCinfo(szTmp, sizeof(szTmp));
	XdLog(">> manager, xdc cinfo -->%s\n", szTmp);

	InterlockedIncrement(&m_nRun);
	nRet = m_thread.Start(this, &CNatProxyManager::ThreadMain); Assert(!nRet);
	return 0;
}

int  CNatProxyManager::Stop()
{
	m_xdc.Stop();
	InterlockedDecrement(&m_nRun);
	InterlockedDecrement(&m_nRunFile);
	m_thread.SafeStop();
	m_threadFile.SafeStop();

	for(NatProxyMultList_t::iterator it1 = m_lstProxy.begin(); it1 != m_lstProxy.end(); it1++)
	{
		for(NatProxyList_t::iterator it2 = it1->second.begin(); it2 != it1->second.end(); it2++)
		{
			Assert(it2->second);
			it2->second->Stop();
			delete it2->second;
		}
	}

	for(FileInfoList_t::iterator it = m_lstFileSend.begin(); it != m_lstFileSend.end(); it++)
	{
		fclose(it->second->pf); delete it->second;
	}

	for(FileInfoList_t::iterator it = m_lstFileRecv.begin(); it != m_lstFileRecv.end(); it++)
	{
		fclose(it->second->pf); delete it->second;
	}

	m_lstFileSend.clear();
	m_lstProxy.clear();
	m_nRun = 0; m_nRunFile = 0;
	return 0;
}

int  CNatProxyManager::PushFile(ClientId_t xid, const char * szPath)
{
	FILE         * pf;
	XdFileInfo_t * pFileInfo = 0;
	const char   * p1 = 0;
	static int     g_nFileId = 1;

	pFileInfo = new XdFileInfo_t;
	if(!(pf = fopen(szPath, "rb")) || !(pFileInfo = new XdFileInfo_t))
	{
		XdLog(">> manager msg, open file path failed(%d,%p), %s\n", GetLastError(), pFileInfo, szPath);
		if(pf) fclose(pf);
		return -1;
	}

	// 检查是否在队列中 ???
	memset(pFileInfo, 0, sizeof(*pFileInfo));
	p1 = strrchr(szPath, '\\');
	pFileInfo->id      = xid;
	pFileInfo->pf      = pf;
	pFileInfo->nFileId = g_nFileId++;
	strcpy(pFileInfo->szPath, p1 ? p1+1 : szPath);

	{
		CGenericLockHandler h(m_lock);
		Assert(m_lstFileSend.find(pFileInfo->nFileId) == m_lstFileSend.end());
		m_lstFileSend[pFileInfo->nFileId] = pFileInfo;
	}

	if(m_threadFile.IsStop())
	{
		InterlockedIncrement(&m_nRunFile);
		int nRet = m_threadFile.Start(this, &CNatProxyManager::ThreadMainSendFile);
		if( nRet)  return nRet;
	}

	return 0;
}

int  CNatProxyManager::PullFile(ClientId_t xid, const char * szPath)
{
	ALLOC_XDMSG_EXDATA(XdTaskPullFile_t, XdTask_PullFile, xid);
	strcpy(XDMSG_EXDATA()->szPath, szPath);

	return XdcSendMsg2(XDMSG());
}

void CNatProxyManager::DumpInfo()
{
	char szTmp[256] = {0};
	m_xdc.DumpCinfo(szTmp, sizeof(szTmp));

	printf(">> proxym info, -----------xdc: %s, proxy: %d\n", szTmp, m_lstProxy.size());
	CGenericLockHandler h(m_lock);
	for(NatProxyMultList_t::iterator it1 = m_lstProxy.begin(); it1 != m_lstProxy.end(); it1++)
	{
		printf("\t%I64d -->", it1->first);
		for(NatProxyList_t::iterator it2 = it1->second.begin(); it2 != it1->second.end(); it2++)
		{
			memset(szTmp, 0, sizeof(szTmp));
			it2->second->DumpInfo(szTmp, sizeof(szTmp));
			printf("%d, %s", it2->first, szTmp);
		}
		printf("\n");
	}
}


void CNatProxyManager::ThreadMain(void*)
{
	int    nRet, nSize = DEFAULT_RECV_BUFFER_SIZE;
	char * szBuf = (char*)malloc(nSize); Assert(szBuf);

	while(m_nRun)
	{
		XdMsgHeader_t * pMsg;

		pMsg = (XdMsgHeader_t*)szBuf;
		nRet = m_xdc.RecvMsg(szBuf, nSize);
		if(nRet)
		{
			XdLog("!! manager loop, recv xdmsg failed and break loop, xdc disconnect???(%d)\n", nRet);
			break;
		}

		if(!pMsg || pMsg->nSize < sizeof(*pMsg))
		{
			if(pMsg->nSize > 0) XdLog("** manager loop, recv a invalid msg, drop it!!!\n");
			continue;
		}

		Assert(pMsg->nSize > 0);
		nRet = HandleXdcMsg(pMsg);
		if(nRet)
		{
			XdLog("!! manager loop, handle xdc msg failed and break loop, xdc disconnect???(%d,%d)\n", nRet, GetLastError());
			break;
		}
	}

	free(szBuf);
	m_nRun = 0;
}

void CNatProxyManager::ThreadMainSendFile(void*)
{
	int    nRet  = 0, nSize = 1024*8;
	char * szBuf = (char*)malloc(nSize); Assert(szBuf);

	while(m_nRunFile)
	{
		XdFileInfo_t * pFileInfo = 0;
		{
			CGenericLockHandler h(m_lockFile);
			if(!m_lstFileSend.empty()) pFileInfo = m_lstFileSend.begin()->second;
		}

		if(!pFileInfo)
		{
			Sleep(500);
			continue;
		}

		// ??? 文件可能读取的时候正在被修改, 例如日志文件, 可能增加也可能减少. 
		FILE * pf = pFileInfo->pf; Assert(pf);
		int    nOff, nLen;

		fseek(pf, 0, SEEK_END);
		pFileInfo->nFileSize = ftell(pf);
		fseek(pf, 0, SEEK_SET);

		XdLog(">> manager file, start to send file %I64d:%d:%I64d:%s\n", pFileInfo->id, pFileInfo->nFileId, pFileInfo->nFileSize, pFileInfo->szPath);
		if(!pFileInfo->nResult)
		{
			ALLOC_XDMSG_EXDATA(XdTaskFileInfo_t, XdTask_PostFileInfo, pFileInfo->id);
			XDMSG_EXDATA()->nFileId   = pFileInfo->nFileId;
			XDMSG_EXDATA()->nFileSize = pFileInfo->nFileSize;
			strcpy(XDMSG_EXDATA()->szPath, pFileInfo->szPath);
			nRet = m_xdc.SendMsg(XDMSG());
			if(nRet) XdLog(">> manager file, send file start info failed(%d) -->%d\n", GetLastError(), pFileInfo->nFileId);
		}

		nOff = sizeof(XdMsgHeader_t)+sizeof(XdTaskProxyId_t);
		while(!nRet && m_nRunFile && !pFileInfo->nResult)
		{
			if((nLen = fread(szBuf+nOff, 1, nSize-nOff, pf)) > 0)
			{
				ATTACH_XDMSG_EXDATA(szBuf, XdTaskProxyId_t, XdTask_PostFileContent, pFileInfo->id);
				XDMSG_EXDATA()->nProxyId = pFileInfo->nFileId;
				XDMSG()->nSize += nLen;
				pFileInfo->nHandleLen += nLen;
				nRet = m_xdc.SendMsg(XDMSG());
			}

			if(nLen+nOff < nSize) break;
			if(pFileInfo->nHandleLen >= pFileInfo->nFileSize) break;
		}

		XdLog(">> manager file, send file complete -->(%d,%d),%d,%I64d,%I64d\n", nRet, pFileInfo->nResult, pFileInfo->nFileId, pFileInfo->nFileSize, pFileInfo->nHandleLen);
		if(!nRet && !pFileInfo->nResult)
		{
			ALLOC_XDMSG_EXDATA(XdTaskFileComplete_t, XdTask_PostFileComplete, pFileInfo->id);
			XDMSG_EXDATA()->nFileId = pFileInfo->nFileId;
			Assert(pFileInfo->nHandleLen == pFileInfo->nFileSize);
			nRet = m_xdc.SendMsg(XDMSG());
			if(nRet) XdLog(">> manager file, send file end info failed(%d) -->%d\n", GetLastError(), pFileInfo->nFileId);
		}

		fclose(pf); nOff = pFileInfo->nFileId; delete pFileInfo;
		CGenericLockHandler h(m_lockFile);
		m_lstFileSend.erase(nOff);
		if(nRet)    break;
	}

	free(szBuf);
	m_nRunFile = 0;
}

int  CNatProxyManager::CreateNatProxyServerImpl(ClientId_t xid, XdTaskProxyServer_t * proxys)
{
	int    nRet, nProxyId;
	CNatProxyServer * psrv = new CNatProxyServer; Assert(psrv);

	nProxyId = proxys->nListenPort[proxys->nIndex];
	nRet     = psrv->Start(&m_xdc, xid, proxys);
	if(nRet)
	{
		XdLog(">> manager, create proxys failed -->%I64d,%d,%d\n", xid, nProxyId, GetLastError());
		return nRet;
	}

	CGenericLockHandler h(m_lock);
	NatProxyMultList_t::iterator it = m_lstProxy.find(xid);
	if(it == m_lstProxy.end())
	{
		NatProxyList_t lst;
		lst[nProxyId]   = psrv;
		m_lstProxy[xid] = lst;
		return 0;
	}

	NatProxyList_t::iterator it2 = it->second.find(nProxyId);
	if(it2 != it->second.end())
	{
		delete psrv;
		XdLog(">> manager, create proxys failed, existed --->%I64d,%d,%d\n", xid, nProxyId, it2->second->IsClient());
		return -1;
	}

	it->second[nProxyId] = psrv;
	return 0;
}


int  CNatProxyManager::HandleXdcMsg(XdMsgHeader_t * pMsg)
{
	int nRet = 0;
	XdTaskProxyId_t * pid   = 0;
	CNatProxyBase   * proxy = 0;

	// 1 先处理 XDSRV消息. 
	switch(pMsg->nSrvMsgId)
	{
	case XdMsgS_BreakConnect:
		{
			// delete server???
			XdBreakConnect_t * pBreak = (XdBreakConnect_t*)(pMsg+1);
			XdLog(">> manager loop, xdserver said the cid(%I64d) is disconnect !!!\n", pBreak->nCid);
			DeleteNatProxy(pBreak->nCid);
			DeleteSendFile(pBreak->nCid);
		}
		return 0;
	}

	if( pMsg->nMsgId == XdTask_ProxyClient || 
		pMsg->nMsgId == XdTask_ProxyDisconnect || 
		pMsg->nMsgId == XdTask_ProxyData)
	{
		Assert(pMsg->nSize >= sizeof(*pMsg)+sizeof(XdTaskProxyId_t));
		pid   = (XdTaskProxyId_t*)(pMsg+1);
		proxy = GetNatProxy(pMsg->nCidSrc, pid->nProxyId);
	}

	// 2 脚本处理
	Script_OnRecvXdcMsg(g_ls, pMsg, pMsg->nSize);

	// 3 再处理 TASK消息. 
	switch(pMsg->nMsgId)
	{
	case XdTask_Talk:
		{
			const char * str = (const char *)(pMsg+1);
			XdLog(">> manager msg, cid(%I64d) say -->%s\n", pMsg->nCidSrc, str);
		}
		break;
	case XdTask_AssertProxyServer:
		{
			// 启动一个 client去测试连接 ???
			Assert(pMsg->nSize == sizeof(*pMsg)+sizeof(XdTaskProxyServer_t));
			pMsg->nCidDst = pMsg->nCidSrc; pMsg->nMsgId = XdTask_ProxyServer;
			nRet = XdcSendMsg2(pMsg);
			XdLog(">> manager msg, recv a assert proxy server msg(%d) --->%I64d\n", nRet, pMsg->nCidSrc);
		}
		break;

	case XdTask_ProxyServer:
		{
			XdTaskProxyServer_t * proxys = (XdTaskProxyServer_t*)(pMsg+1);

			Assert(pMsg->nSize == sizeof(*pMsg)+sizeof(XdTaskProxyServer_t));
			for(int i = 0; i < proxys->nCount; i++)
			{
				proxys->nIndex = i; Assert(proxys->nListenPort[i]);
				nRet = CreateNatProxyServerImpl(pMsg->nCidSrc, proxys);
				XdLog(">> manager msg, create proxy server(%d) --->%I64d,%08x:%d\n", nRet, pMsg->nCidSrc, proxys->nListenIp[i], proxys->nListenPort[i]);
			}
		}
		break;

	case XdTask_ProxyClient:
		{
			CNatProxyClient * pclt = (CNatProxyClient*)proxy;

			Assert(pMsg->nSize == sizeof(*pMsg)+sizeof(XdTaskProxyClient_t));
			if(!proxy)
			{
				pclt = new CNatProxyClient; Assert(pclt);
				nRet = pclt->Start(&m_xdc, pMsg->nCidSrc, pid->nProxyId); Assert(!nRet);
				if(nRet) break;

				CGenericLockHandler h(m_lock);
				m_lstProxy[pMsg->nCidSrc][pid->nProxyId] = pclt;
			}

			Assert(pclt->IsClient());
			nRet = pclt->CreateClient(pMsg, (XdTaskProxyClient_t*)(pMsg+1));
			if(nRet) break;

			XdLog(">> manager msg, create a client ok --->%I64d,%d,%d >>>%08x:%d\n", pMsg->nCidSrc, pid->nProxyId, pid->nConnectId, ((XdTaskProxyClient_t*)(pMsg+1))->nSrvIp, ((XdTaskProxyClient_t*)(pMsg+1))->nSrvPort);
		}
		break;

	case XdTask_ProxyDisconnect:
	case XdTask_ProxyData:
		{
			if(!proxy)
			{
				XdLog(">> manager msg, recv a invalid proxy cid, drop--->%I64d,%d,%d,%d\n", pMsg->nCidSrc, pid->nProxyId, pMsg->nMsgId, pMsg->nDataId);
				return 0;
			}

			nRet = proxy->OnRecvXdMsg(pMsg, (XdTaskProxyId_t*)(pMsg+1), ((char*)pMsg+sizeof(*pMsg)+sizeof(XdTaskProxyId_t)), pMsg->nSize-sizeof(*pMsg)-sizeof(XdTaskProxyId_t));
		}
		break;

	case XdTask_PullFile:
		{
			XdTaskPullFile_t * pFileInfo = (XdTaskPullFile_t*)(pMsg+1);

			Assert(pMsg->nSize == sizeof(*pMsg)+sizeof(XdTaskPullFile_t));
			int nRet2 = PushFile(pMsg->nCidSrc, pFileInfo->szPath);
			XdLog(">> manager msg, recv pull file -->%I64d:%d:%s\n", pMsg->nCidSrc, nRet2, pFileInfo->szPath);
		}
		break;

	case XdTask_PostFileInfo:
		{
			FILE             * pf;
			XdFileInfo_t     * pFileInfo = 0;
			XdTaskFileInfo_t * pFileInfoTask = (XdTaskFileInfo_t*)(pMsg+1);
			char               szPath[MAX_PATH] = {0}, nErr = 0;

			Assert(m_lstFileRecv.find(pFileInfoTask->nFileId) == m_lstFileRecv.end());
			_snprintf(szPath, sizeof(szPath)-1, "%s\\%s", m_szRecvFilePath, pFileInfoTask->szPath);

			pFileInfo = new XdFileInfo_t;
			if(pFileInfo) memset(pFileInfo, 0, sizeof(*pFileInfo));
			if(!pFileInfo || !(pf = fopen(szPath, "wb")))
			{
				ALLOC_XDMSG_EXDATA(XdTaskFileComplete_t, XdTask_PostFileComplete, pMsg->nCidSrc);
				XDMSG_EXDATA()->nFileId = pFileInfoTask->nFileId;
				XDMSG_EXDATA()->nDir    = 1; XDMSG_EXDATA()->nResult = !pFileInfo ? 1 : 2;
				nRet = XdcSendMsg2(XDMSG());
				XdLog(">> manager msg, recv file exchange info, err -->%d:%s\n", XDMSG_EXDATA()->nResult, szPath);
				if(pFileInfo) delete pFileInfo;
				break;
			}

			strcpy(pFileInfo->szPath, szPath);
			pFileInfo->id        = pMsg->nCidSrc;
			pFileInfo->nFileId   = pFileInfoTask->nFileId;
			pFileInfo->nFileSize = pFileInfoTask->nFileSize;
			pFileInfo->pf        = pf;
			XdLog(">> manager msg, recv file exchange info -->%I64d,%d,%I64d,%s\n", pFileInfo->id, pFileInfo->nFileId, pFileInfo->nFileSize, pFileInfo->szPath);
			CGenericLockHandler h(m_lockFile);
			m_lstFileRecv[pFileInfo->nFileId] = pFileInfo;
		}
		break;
	case XdTask_PostFileContent:
		{
			XdFileInfo_t     * pFileInfo = 0;
			XdTaskProxyId_t  * pProxyId = (XdTaskProxyId_t*)(pMsg+1);

			Assert(pMsg->nSize > sizeof(XdTaskProxyId_t));
			{
				CGenericLockHandler h(m_lockFile);
				FileInfoList_t::iterator it = m_lstFileRecv.find(pProxyId->nProxyId);
				if(it != m_lstFileRecv.end()) pFileInfo = it->second;
			}

			if(!pFileInfo)
			{
				ALLOC_XDMSG_EXDATA(XdTaskFileComplete_t, XdTask_PostFileComplete, pMsg->nCidSrc);
				XDMSG_EXDATA()->nFileId = pProxyId->nProxyId;
				XDMSG_EXDATA()->nDir    = 1; XDMSG_EXDATA()->nResult = 3; 
				nRet = XdcSendMsg2(XDMSG());
				break;
			}

			Assert(pFileInfo->id == pMsg->nCidSrc);
			Assert(pFileInfo->pf);
			pFileInfo->nHandleLen += pMsg->nSize-sizeof(*pMsg)-sizeof(*pProxyId);
			fwrite((char*)(pMsg+1)+sizeof(*pProxyId), 1, pMsg->nSize-sizeof(*pMsg)-sizeof(*pProxyId), pFileInfo->pf);
		}
		break;
	case XdTask_PostFileComplete:
		{
			// 文件发送不是应答型的, 发送者先打开文件并发送, 可能接收者收到第一个数据包的时候发送者就发送完成了. 
			// 此时如果接收者发生了错误(1,2), 就会造成发送者收到很多的完成消息
			XdFileInfo_t         * pFileInfo = 0;
			XdTaskFileComplete_t * pFileComp = (XdTaskFileComplete_t*)(pMsg+1);

			XdLog(">> manager msg, recv file exchange complete -->%d,(%d,%d)\n", pFileComp->nFileId, pFileComp->nDir, pFileComp->nResult);
			if(pFileComp->nDir == 0)
			{
				// 可能发生连接中断找不到的情况. 
				{
					CGenericLockHandler h(m_lockFile);
					FileInfoList_t::iterator it = m_lstFileRecv.find(pFileComp->nFileId);
					if(it == m_lstFileRecv.end()) break;

					pFileInfo = it->second; m_lstFileRecv.erase(it);
				}

				if(pFileComp->nResult == 0)
				{
					Assert(pFileInfo->nFileSize == pFileInfo->nHandleLen);
					Assert(pFileInfo->pf);
				}
				fclose(pFileInfo->pf); delete pFileInfo;
			}
			else
			{
				// 中断, 在接收端打开失败的时候, 或者连接中断的时候. 
				// 可能出现在 SEND列表中找不到的情况, 若找到, 只需要设置 RESULT即可. 
				CGenericLockHandler h(m_lockFile);
				FileInfoList_t::iterator it = m_lstFileSend.find(pFileComp->nFileId);
				if(it == m_lstFileSend.end()) break;

				pFileInfo = it->second;
				pFileInfo->nResult = pFileComp->nResult; Assert(pFileInfo->nResult);
			}
		}
		break;

	default:
		Assert(0);
		break;
	}

	return nRet;
}
