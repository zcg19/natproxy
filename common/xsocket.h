#pragma once


#include <memory.h>

#ifndef Assert
#include <assert.h>
#define Assert assert
#endif

#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")

#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <arpa/inet.h>

#endif


#ifndef XdLog
#define XdLog(_fmt, ...)  { \
	char __stime[32] = {0}; time_t __now; struct tm * __curtime; \
	time(&__now); __curtime = localtime(&__now); strftime(__stime, sizeof(__stime), "%H:%M:%S", __curtime); \
	printf("[%s][%5d]" _fmt, __stime, GetCurrentThreadId(), __VA_ARGS__); \
}
#endif


class CXdSocket
{
public:
	typedef union State_t
	{
		int              value;
		struct
		{
			unsigned int socket : 1;
			unsigned int connect : 1;
			unsigned int async : 1;
			unsigned int addr : 1;
		};
	}State_t;

	CXdSocket()
		: m_socket(0)
		, m_nRecvTimeout(0)
		, m_user(0)
	{
		m_state.value = 0;
		memset(&m_addrLocal,  0, sizeof(m_addrLocal));
		memset(&m_addrRemote, 0, sizeof(m_addrRemote));
	}

	virtual ~CXdSocket()
	{
		if(m_state.socket) closesocket(m_socket);
	}

	SOCKET Socket()             { return m_socket; }
	BOOL   IsConnected()        { return m_state.connect == 1; }
	int    LocalIp()            { return htonl(*(int*)&m_addrLocal.sin_addr); }
	int    LocalPort()          { return htons(m_addrLocal.sin_port); }
	int    RemoteIp()           { return htonl(*(int*)&m_addrRemote.sin_addr); }
	int    RemotePort()         { return htons(m_addrRemote.sin_port); }
	void * GetUserData()        { return m_user; }
	void   SetUserData(void* p) { m_user = p; }

	void Attach(SOCKET s, State_t state)
	{
		m_socket = s;
		m_state  = state;
	}

	void GetSockAddr()
	{
		if(!m_state.addr && m_state.socket)
		{
			int nLen = sizeof(sockaddr_in);

			getsockname(m_socket, (sockaddr*)&m_addrLocal,  &nLen);
			getpeername(m_socket, (sockaddr*)&m_addrRemote, &nLen);
			m_state.addr = 1;
		}
	}

	int  Create()
	{
		if(!m_state.socket)
		{
			if(m_socket != INVALID_SOCKET) Close();
			m_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if(m_socket == INVALID_SOCKET) return -1;
			m_state.socket = 1;
		}

		return 0;
	}

	void Close()
	{
		if(m_socket)
		{
			closesocket(m_socket);
			m_socket = 0;
		}
		m_state.value = 0;
	}

	int  Listen(int nIp, int nPort)
	{
		int nRet;
		sockaddr_in addr = {0};

		if(nRet = Create()) return nRet;

		*(int*)&addr.sin_addr  = nIp;
		addr.sin_port          = htons(nPort);
		addr.sin_family        = AF_INET;

		Assert(!m_state.connect);
		if(!bind(m_socket, (sockaddr*)&addr, sizeof(addr)))
		{
			int nRet = listen(m_socket, 200);
			if(!nRet)  m_state.connect = 1;
			return nRet != SOCKET_ERROR ? 0 : -1;
		}

		return -1;
	}

	int  Accept(SOCKET * ps, sockaddr_in * paddr)
	{
		Assert(m_state.connect);
		int    len  = sizeof(sockaddr_in);
		SOCKET s    = accept(m_socket, (sockaddr*)paddr, &len);
		if(s != INVALID_SOCKET) *ps = s;

		return s != INVALID_SOCKET ? 0 : -1;
	}

	int  Connect(int nIp, int nPort)
	{
		int nRet;
		sockaddr_in addr = {0};

		if(nRet = Create()) return nRet;

		addr.sin_family        = AF_INET;
		*(int*)&addr.sin_addr  = nIp;
		addr.sin_port          = htons(nPort);

		Assert(!m_state.connect && m_state.socket);
		nRet = ::connect(m_socket, (sockaddr*)&addr, sizeof(addr));
		if(nRet) return nRet;

		m_state.connect = 1;
		return 0;
	}

	int  Recv(char * szBuf, int nSize, int * nLen)
	{
		int nRet;

		Assert(nSize > 0);
		if(!m_state.connect) return -1;
		nRet = ::recv(m_socket, szBuf, nSize, 0);
		if(nRet > 0 || (nRet < 0 && !IsSocketError(GetLastError())))
		{
			*nLen = nRet > 0 ? nRet : 0;
			return 0;
		}

		m_state.connect = 0;
		return nRet ? -2 : -3;
	}

	int  Send(const char * szData, int nSize, int * nLen)
	{
		int nRet;

		if(!m_state.connect) return -1;
		nRet = ::send(m_socket, szData, nSize, 0);
		if(nRet > 0 || !IsSocketError(GetLastError()))
		{
			if(nLen) *nLen = nRet > 0 ? nRet : 0;
			return 0;
		}

		m_state.connect = 0;
		return -2;
	}

	int  RecvAll(char * szBuf, int nSize, int * pLen, bool bTimeoutExit = false)
	{
		char * szRecvBuf = szBuf;
		int    nRet = 0, nRecvSize = nSize;

		while(1)
		{
			int  nRecvedLen = 0;
			nRet = Recv(szRecvBuf, nRecvSize, &nRecvedLen);
			if(nRet) return nRet;
			if(bTimeoutExit && nRecvedLen == 0) return 0; // <==> no data
			if(nRecvedLen == nRecvSize) break;

			szRecvBuf += nRecvedLen; nRecvSize -= nRecvedLen;
			Assert(nRecvedLen >= 0 && nRecvSize > 0);
		}

		*pLen = nSize;
		return nRet;
	}

	int  SendAll(const char * szData, int nSize)
	{
		int  nRet = 0, nSendLen = nSize;
		const char * p = szData;

		while(nSendLen > 0)
		{
			int  nSendedLen = 0;
			nRet = Send(p, nSendLen, &nSendedLen);
			if(nRet) return nRet;
			if(nSendedLen == nSendLen) break;

			p += nSendedLen; nSendLen -= nSendedLen;
			Assert(nSendedLen >= 0 && nSendLen > 0);
		}

		return nRet;
	}

	int  SetAsyncMode(BOOL bFlag = TRUE)
	{
		int nRet;

		Assert(m_state.socket);
		#ifdef _WIN32
		m_state.async = bFlag ? 1:0;
		nRet = ::ioctlsocket(m_socket, FIONBIO, (unsigned long*)&bFlag);
		#else
		nRet = fcntl(m_socket, F_GETFL);
		if(nRet >= 0)
		{
			if(bFlag) nRet |= O_NONBLOCK;
			else      nRet &= ~O_NONBLOCK;
			nRet = fcntl(m_socket, F_SETFL, nRet);
			if(nRet >= 0) nRet = 0;
		}
		#endif
		return nRet == 0 ?  0 : -1;
	}

	int  SetRecvTimeout(int nTimeout)
	{
		int  nRet;

		m_nRecvTimeout = nTimeout; Assert(m_state.socket); 
		#ifdef _WIN32
		int    tm = m_nRecvTimeout * 1000;
		nRet = ::setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, (char*)&tm, sizeof(tm));
		#else
		struct timeval tv = { nTimeout, 0, };
		nRet = ::setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv));
		#endif
		return nRet;
	}

	bool IsSocketError(int nErr)
	{
		if(!nErr) return false;

		#ifdef _WIN32
		if(m_nRecvTimeout > 0)
		{
			if(nErr == WSAETIMEDOUT) return false;
		}

		if(m_state.async)
		{
			if(nErr == WSAEWOULDBLOCK) return false;
		}
		#endif

		return true;
	}


protected:
	SOCKET             m_socket;
	int                m_nRecvTimeout;
	State_t            m_state;
	sockaddr_in        m_addrLocal, m_addrRemote;
	void             * m_user;
};


inline int InitSocket(int nVesion = AF_INET)
{
	int nRet = 0;

	#ifdef WIN32
	WSADATA wData  = {0};
	nRet = ::WSAStartup(MAKEWORD(2, 0), &wData);
	#endif

	return nRet;
}

inline void UninitSocket()
{
	#ifdef WIN32
	::WSACleanup();
	#endif
}
