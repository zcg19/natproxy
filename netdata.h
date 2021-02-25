#pragma once


#ifndef Assert
#include <assert.h>
#define Assert assert
#endif


// 修改RECV/SEND缓冲区大小需要修改服务器(xdsrv)
#define LOCAL_ADDRESS_IPV4              0x0100007f
#define DEFAULT_RECV_BUFFER_SIZE		1024 * 64
#define DEFAULT_SEND_BUFFER_SIZE		1024 * 64
#define DEFAULT_CONNECT_DEADED_TIMEOUT  1000*60*60
#define FIRST_HANDLESHAKE0              'xdch'


enum enumXdError
{
	XdError_Ok = 0, 
	XdError_ErrorBase = 0x1000, 
	XdError_InvalidMsg_Size, 
	XdError_InvalidMsg_Cinfo, 
	XdError_InvalidMsg_AssertCid, 
	XdError_InvalidMsg_UnknownCid, 
	XdError_DuplicateCinfo, 
	XdError_UnknownMsgId, 
	XdError_UnknownSelfCinfo, 
};

enum enumXdMsg
{
	XdMsgC_ExchangeData   = 99, 
	XdMsgS_ClientInfo     = 122, 
	XdMsgC_ClientInfo, 
	XdMsgS_BreakConnect   = 103, 
};


typedef __int64 
ClientId_t;

typedef struct XdMsgHeader_t
{
	int             nSize;            // = sizeof(header)+size(data)
	int             nDataId;
	int             nSrvMsgId:8;      // enumSrvMsg
	int             nMsgId:24;        // 服务器不使用
	ClientId_t      nCidDst;
	ClientId_t      nCidSrc;
	// data
}XdMsgHeader_t;

typedef struct XdClientInfo_t
{
	ClientId_t      nCid;
	int             nNatIp;
	int             nNatPort;
}XdClientInfo_t;

typedef struct XdBreakConnect_t
{
	ClientId_t      nCid;
}XdBreakConnect_t;


inline BOOL IsValidXdMsgSize(XdMsgHeader_t * pMsg)
{
	// 消息最大长度不能超过发送缓冲区和接收缓冲区的最小值
	#define XD_MSG_MAX_SENDBUF_SIZE  1000 * 16
	#define XD_MSG_MAX_SIZE          1024 * 16
	return pMsg->nSize >= sizeof(*pMsg) && pMsg->nSize < XD_MSG_MAX_SIZE;
}


// -------------------------------------------------------------
// xdclient extension.  
enum enumXdTaskId
{
	XdTask_Talk = 0x1000, 
	XdTask_Ping, 
	XdTask_Result, 

	// ---- file
	XdTask_PostFileInfo, 
	XdTask_PostFileContent, 
	XdTask_PostFileComplete, 
	XdTask_PullFile, 

	// ---- proxy
	XdTask_AssertProxyServer = 0x1010, 
	XdTask_ProxyServer, 
	XdTask_ProxyClient, 
	XdTask_ProxyData,           // id+data.
	XdTask_ProxyDisconnect,     // id
};


typedef struct XdTaskFileInfo_t
{
	__int64         nFileSize;
	int             nFileId;
	char            szPath[260];
}XdTaskFileInfo_t;

typedef struct XdTaskFileComplete_t
{
	int             nFileId;
	char            nResult;    // 0=ok, 1=no_memory, 2=open_failed, 3=unknown_id, 9=connect_break.
	char            nDir;       // 0=send, 1= recv.
}XdTaskFileComplete_t;

typedef struct XdTaskPullFile_t
{
	char            szPath[260];
}XdTaskPullFile_t;

typedef struct XdTaskProxyId_t
{
	unsigned int    nProxyId;
	unsigned int    nConnectId;
}XdTaskProxyId_t;

typedef struct XdTaskProxyClient_t
{
	XdTaskProxyId_t head;
	int             nSrvIp;
	unsigned short  nSrvPort, nRetry;
}XdTaskProxyClient_t;

typedef struct XdTaskProxyServer_t
{
	union
	{
		XdTaskProxyId_t head;
		struct
		{
			int     nCount;
			int     nIndex;
		};
	};
	int             nListenIp[8];
	unsigned short  nListenPort[8];
	unsigned short  nListenProxyPort[8];
}XdTaskProxyServer_t;


#define ATTACH_XDMSG_EXDATA(_buf, _st, _mid, _xid) \
	XdMsgHeader_t    * __h = (XdMsgHeader_t*)(_buf); _st * __t = (_st*)(__h+1); \
	__h->nCidDst = _xid; __h->nMsgId = _mid; __h->nSize = sizeof(*__h)+sizeof(*__t);
#define ALLOC_XDMSG_EXDATA(_st, _mid, _xid)  \
	char __buf[sizeof(XdMsgHeader_t)+sizeof(_st)] = {0}; \
	ATTACH_XDMSG_EXDATA(__buf, _st, _mid, _xid);
#define XDMSG()        __h
#define XDMSG_EXDATA() __t
