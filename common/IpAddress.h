#ifndef __IpAddress_H__
#define __IpAddress_H__


#include <winsock2.h>
#include <ws2tcpip.h>
#include <vector>


typedef struct SocketAddress_t
{
	enum  { Type_IpUnknown = 0, Type_IpV4, Type_IpV6 };

	int               nType;
	union
	{
		sockaddr_in   addr4;
		sockaddr_in6  addr6;
	};

	SocketAddress_t()
	{
		memset(this, 0, sizeof(*this));
	}

	BOOL              IsValid() const
	{
		return nType != Type_IpUnknown;
	}

	const sockaddr*   Addr() const
	{
		switch(nType)
		{
		case Type_IpV4: return (sockaddr*)&addr4;
		case Type_IpV6: return (sockaddr*)&addr6;
		}

		return 0;
	}

	int               AddrLen() const
	{
		switch(nType)
		{
		case Type_IpV4: return sizeof(sockaddr_in);
		case Type_IpV6: return sizeof(sockaddr_in6);
		}

		return 0;
	}
}SocketAddress_t;


typedef std::vector<SocketAddress_t> 
SocketAddressList_t;


inline int  ResolveAddress(const char * szIpOrDomain, int nPort, SocketAddressList_t & sockAddrList)
{
	char szService[64] = {0};
	sprintf_s(szService, sizeof(szService) - 1, "%d", nPort);

	addrinfo   addrHints = {0};
	addrinfo * pAddrInfo, * pAddrInfoList = 0;

	addrHints.ai_family = AF_INET;
	int nRet = ::getaddrinfo(szIpOrDomain, szService, &addrHints, &pAddrInfoList);
	if(nRet)
	{
		printf("[error]: getaddrinfo, errno = %d(%s:%s)\n", ::GetLastError(), szIpOrDomain, szService);
		return ::GetLastError();
	}

	pAddrInfo = pAddrInfoList;

	while(pAddrInfo)
	{
		SocketAddress_t sockAddr;

		if(pAddrInfo->ai_addrlen > 0)
		{
			switch(pAddrInfo->ai_family)
			{
			case PF_INET:
				sockAddr.nType = SocketAddress_t::Type_IpV4;
				memcpy(&sockAddr.addr4, (sockaddr_in*)pAddrInfo->ai_addr, sizeof(sockaddr_in));
				break;

			case PF_INET6:
				sockAddr.nType = SocketAddress_t::Type_IpV6;
				memcpy(&sockAddr.addr6, (sockaddr_in6*)pAddrInfo->ai_addr, sizeof(sockaddr_in6));
				break;
			}
		}

		if(sockAddr.IsValid()) sockAddrList.push_back(sockAddr);
		pAddrInfo = pAddrInfo->ai_next;
	}

	::freeaddrinfo(pAddrInfoList);
	return 0;
}

inline int  ReuseAddress(SOCKET socket)
{
	int  nValue = 1;
	return setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&nValue, sizeof(nValue));
}


#endif
