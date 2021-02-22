#include "proxym.h"
#include <conio.h>


int  ProxyManagerMain()
{
	int              nRet;
	char             szPath[MAX_PATH] = {0};
	CNatProxyManager proxym;

	GetModuleFileNameA(0, szPath, _countof(szPath));
	nRet = proxym.Start(szPath);
	if(nRet) return nRet;

	XdLog(
		">> main, start ok, user cmd:\n"
		"\t --> s(create a nat server proxy)\n"
		"\t --> r(refresh proxy connect)\n"
		"\t --> f(push file)\n"
		"\t --> p(pull file)\n"
		);
	while(1)
	{
		int   n,  nPort = 0, nProxyPort = 0;
		char  ch = 0, szTmp[1024] = {0};
		ClientId_t nCid = 0;

		fflush(stdin); n = ::_kbhit();
		if(n) ch = getchar();

		switch(ch)
		{
		case 's':
			XdLog(">> input format= 'cid:port:proxy_port' ---> "); fflush(stdin);
			scanf("%I64d:%d:%d", &nCid, &nPort, &nProxyPort);
			if(!nCid || !nPort)
			{
				XdLog(">> cid or port is invalidate!!!\n");
				break;
			}
			nRet = proxym.CreateNatProxyServer(nCid, 0, nPort, nProxyPort);
			break;
		case 'S':
			XdLog(">> start a natproxy server for vsdebug\n");
			nRet = proxym.CreateNatProxyServer(3, 0, 4015);
			break;
		case 'r':
			nRet = proxym.RefreshProxyList();
			break;
		case 'f':
		case 'p':
			{
				char szPath[MAX_PATH] = {0};
				XdLog(">> input format= 'cid:path' ---> "); fflush(stdin);
				scanf("%I64d:%[^\n]", &nCid, szPath);
				if(!nCid || !szPath[0])
				{
					XdLog(">> cid or path is invalidate!!!\n");
					break;
				}
				if(ch == 'f') nRet = proxym.PushFile(nCid, szPath);
				if(ch == 'p') nRet = proxym.PullFile(nCid, szPath);
			}
			break;
		case 't':
		case 'T':
			XdLog(">> input format= 'cid' ---> ");
			scanf("%I64d", &nCid); fflush(stdin);
			nRet = proxym.TalkToPeer(nCid, "hello!!!");
			if(ch == 't') break;
			while(1)
			{
				fflush(stdin); scanf("%[^\n]", szTmp);
				if(!stricmp(szTmp, "exit")) { XdLog(">> exit talk --->%I64d\n", nCid); break; }
				nRet = proxym.TalkToPeer(nCid, szTmp);
				if(nRet) break;
			}
			break;
		case 'm':
			proxym.DumpInfo();
			break;
		case 'q':
			proxym.Stop();
			if(g_ls)  Script_Uninit(g_ls);
			XdLog(">> proxym exit ok!!!\n");
			return 0;
		default:
			//if(ch) XdLog(">> unknown command -->(%c)\n", ch);
			if(!proxym.IsRun())
			{
				// 重新连接 ???
				XdLog(">> proxym exit ???\n");
				return 0;
			}

			Sleep(1000);
			break;
		}
	}
}


void main()
{
	WSADATA wsa;
	WSAStartup(MAKEWORD(2,2), &wsa);

	ProxyManagerMain();

	XdLog(">> game over ..........\n");
	WSACleanup();
	fflush(stdin); getchar();
}
