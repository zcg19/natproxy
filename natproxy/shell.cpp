/*****************************************************
 * 增加脚本处理数据功能
 * 如果不处理，可以支持内网的反向代理. 
 * 但有的服务不仅仅是一个连接, 可能还会启动另外的一个端口. 
 * 例如: vs的远程调试服务端. 
 *****************************************************/
extern "C"
{
	#include "d:\opensrc\lua-master\lua.h"
	#include "d:\opensrc\lua-master\lualib.h"
	#include "d:\opensrc\lua-master\lauxlib.h"
}
#include "natproxy.h"

#ifdef _DEBUG
#pragma  comment(lib, "../debug/lua.lib")
#else
#pragma  comment(lib, "../release/lua.lib")
#endif


typedef struct XdcMsg_t
{
	XdMsgHeader_t   * head;
	int               len, proxy_id;
	void            * data;
}XdcMsg_t;

CCriticalSetionObject g_lock;


static int  lua_exception_handler(lua_State * ls)
{
	const char * msg = lua_tostring(ls, 1);
	if (!msg)
	{
		// error object not a string.
		return 1;
	}

	luaL_traceback(ls, ls, msg, 1);
	return 1;
}

static int  lua_rspcall(lua_State * ls, int arg, int res, const char * msg)
{
	int  ret, stack_base = lua_gettop(ls) - arg;
	lua_pushcfunction(ls, lua_exception_handler);
	lua_insert(ls, stack_base);

	ret = lua_pcall(ls, arg, res, stack_base);
	lua_remove(ls, stack_base);

	if (ret != LUA_OK)
	{
		std::string  err;
		err = lua_tostring(ls, -1);
		XdLog(">> lua engine, failed of lua_pcall(%s) -->%d,%s\n", msg, ret, err.c_str());
		lua_pop(ls, 1);

		// to lua
		lua_getglobal(ls, "printf");
		lua_pushstring(ls, err.c_str());
		lua_pcall(ls, 1, 0, 0);
	}
	return ret;
}

static int  LoadLuaFile(void * s, const char * file)
{
	int nRet;
	lua_State * ls = (lua_State*)s;

	nRet = luaL_loadfile(ls, file);
	if(nRet != LUA_OK)
	{
		const char * err = lua_tostring(ls, -1);
		XdLog(">> lua engine, failed to load file -->%d,%s\n", nRet, err);
		lua_pop(ls, 1);
		return nRet;
	}

	nRet = lua_rspcall(ls, 0, 0, "dofile");
	return nRet;
}

static void Lua_OnSendXdcMsg(void * s, XdcMsg_t * msg)
{
	lua_State  * ls = (lua_State*)s;
	const char * lua_func = "on_send_xdc_msg";

	CGenericLockHandler h(g_lock);
	lua_getglobal(ls, lua_func);
	lua_pushlightuserdata(ls, msg);
	lua_pushinteger(ls, msg->head->nMsgId);
	lua_pushinteger(ls, msg->proxy_id);
	lua_pushinteger(ls, msg->len);
	lua_rspcall(ls, 4, 0, lua_func);
}

static void Lua_OnRecvXdcMsg(void * s, XdcMsg_t * msg)
{
	lua_State  * ls = (lua_State*)s;
	const char * lua_func = "on_recv_xdc_msg";

	CGenericLockHandler h(g_lock);
	lua_getglobal(ls, lua_func);
	lua_pushlightuserdata(ls, msg);
	lua_pushinteger(ls, msg->head->nMsgId);
	lua_pushinteger(ls, msg->proxy_id);
	lua_pushinteger(ls, msg->len);
	lua_rspcall(ls, 4, 0, lua_func);
}


static int  Lua_GetXdcMsgInfo(lua_State * ls)
{
	XdcMsg_t * msg   = (XdcMsg_t*)lua_touserdata(ls, 1);

	if(!lua_istable(ls, 2))
	{
		XdLog(">> lua engine, error -->lua call 'get_xdcmsg_info' param2 is not 'table'\n");
		return 0;
	}

	Assert(msg->head);
	lua_pushinteger(ls, msg->head->nCidSrc); lua_setfield(ls, 2, "src_xid");
	lua_pushinteger(ls, msg->head->nCidDst); lua_setfield(ls, 2, "dst_xid");
	lua_pushinteger(ls, msg->head->nMsgId);  lua_setfield(ls, 2, "msg_id");
	lua_pushinteger(ls, msg->head->nDataId); lua_setfield(ls, 2, "data_id");
	return 0;
}

static int  Lua_ReadXdcMsgToInt(lua_State * ls)
{
	XdcMsg_t * msg   = (XdcMsg_t*)lua_touserdata(ls, 1);
	int        off   = (int)lua_tointeger(ls, 2);
	int        size  = (int)lua_tointeger(ls, 3);
	__int64    value = 0;

	if(msg->len < off+size)
	{
		Assert(0);
		return 0;
	}

	memcpy(&value, (char*)msg->data+off, size);
	lua_pushinteger(ls, value);
	return 1;
}

static int  Lua_WriteXdcMsgByInt(lua_State * ls)
{
	XdcMsg_t * msg   = (XdcMsg_t*)lua_touserdata(ls, 1);
	int        off   = (int)lua_tointeger(ls, 2);
	int        size  = (int)lua_tointeger(ls, 3);
	__int64    value = lua_tointeger(ls, 4);

	if(msg->len < off+size)
	{
		Assert(0);
		return 0;
	}

	memcpy((char*)msg->data+off, &value, size);
	return 0;
}

static int  Lua_CompareXdcMsg(lua_State * ls)
{
	XdcMsg_t * msg   = (XdcMsg_t*)lua_touserdata(ls, 1);
	int        off   = (int)lua_tointeger(ls, 2);
	int        len   = 0, i;

	if(!lua_istable(ls, 3))
	{
		Assert(0);
		return 0;
	}

	lua_len(ls, 3); len = (int)lua_tointeger(ls, -1); 
	for(i = 0; (i+off) < msg->len && i < len; i++)
	{
		unsigned char ch;

		lua_geti(ls, 3, i+1); ch = (unsigned char)lua_tointeger(ls, -1);
		if(ch != ((unsigned char*)msg->data)[i+off]) break;
	}

	lua_pop(ls, i+1);
	lua_pushinteger(ls, len == i ? 0 : -1);
	return 1;
}


static int  luaopen_natproxy(lua_State * ls)
{
	const luaL_Reg  natproxy[] = 
	{
		{ "get_xdcmsg_info",        Lua_GetXdcMsgInfo }, 
		{ "read_xdcmsg_to_int",     Lua_ReadXdcMsgToInt }, 
		{ "write_xdcmsg_by_int",    Lua_WriteXdcMsgByInt }, 
		{ "compare_xdcmsg",         Lua_CompareXdcMsg }, 
		{ 0, 0, }, 
	};

	luaL_newlib(ls, natproxy);
	return 1;
}





void * g_ls = 0;
void * Script_Init(const char * szPath)
{
	lua_State  * ls = luaL_newstate();
	const char * lua_func = "lua_init";
	int          nRet;

	luaL_openlibs(ls);
	luaL_requiref(ls, "natproxy", luaopen_natproxy, 0);
	lua_pop(ls, 1);

	nRet = LoadLuaFile(ls, szPath);
	if(nRet != LUA_OK)
	{
		XdLog(">> shell init, load lua file error -->%d(%s)\n", nRet, szPath);
		lua_close(ls);
		return 0;
	}

	lua_getglobal(ls, lua_func);
	lua_rspcall(ls, 0, 0, lua_func);
	return ls;
}

void   Script_Uninit(void * s)
{
	lua_State  * ls = (lua_State*)s;
	const char * lua_func = "lua_uninit";

	lua_getglobal(ls, lua_func);
	lua_rspcall(ls, 0, 0, lua_func);

	lua_close(ls);
}

void   Script_OnRecvXdcMsg(void * s, void * szData, int nLen)
{
	XdcMsg_t   msg = {0};

	if(!s) return;
	msg.head = (XdMsgHeader_t*)szData;
	msg.data = msg.head+1;
	msg.len  = nLen- sizeof(*msg.head);

	switch(msg.head->nMsgId)
	{
	// 1
	case XdTask_Talk:
	case XdTask_AssertProxyServer:
		break;

	case XdTask_ProxyServer:
	case XdTask_ProxyClient:
		Lua_OnRecvXdcMsg(s, &msg);
		break;

	// 2
	case XdTask_ProxyDisconnect:
		break;
	case XdTask_ProxyData:
		msg.proxy_id = ((XdTaskProxyId_t*)(msg.data))->nProxyId;
		msg.data = (char*)msg.data + sizeof(XdTaskProxyId_t); msg.len  -= sizeof(XdTaskProxyId_t);
		Lua_OnRecvXdcMsg(s, &msg);
		break;

	// 3
	case XdTask_PostFileInfo:
	case XdTask_PostFileContent:
	case XdTask_PostFileComplete:
	case XdTask_PullFile:
		break;
	default:
		break;
	}
}

void   Script_OnSendXdcMsg(void * s, void * szData, int nLen)
{
	XdcMsg_t   msg = {0};

	if(!s) return;
	msg.head = (XdMsgHeader_t*)szData;
	msg.data = msg.head+1;
	msg.len  = nLen- sizeof(*msg.head);

	switch(msg.head->nMsgId)
	{
	case XdTask_ProxyDisconnect:
	case XdTask_Talk:                // 1
	case XdTask_AssertProxyServer:   // 2
		break;
	case XdTask_ProxyData:
		msg.proxy_id = ((XdTaskProxyId_t*)(msg.data))->nProxyId;
		msg.data = (char*)msg.data + sizeof(XdTaskProxyId_t); msg.len  -= sizeof(XdTaskProxyId_t);
	case XdTask_ProxyClient:
	case XdTask_ProxyServer:         // 2
		Lua_OnSendXdcMsg(s, &msg);
		break;

	case XdTask_PostFileInfo:
	case XdTask_PostFileContent:
	case XdTask_PostFileComplete:
	case XdTask_PullFile:
		break;
	default:
		break;
	}
}
