-- 能够使用VS远程调试
-- msvsmon是VS远程调试服务器，运行后会建立一个listen端口 4015
-- 4015并不是数据交互的唯一端口, 每次 VS连接后，msvsmon会再启动
-- 两个随机端口(多次)，这两个端口才会进行数据交互。
-- 
-- 1 下面脚本用在 natproxy_server端, 解析 msvsmon返回的数据得到
-- 其随机端口，修改随机端口指向 natproxy_server的两个代理端口. 
-- 2 当 natproxy server的代理端口收到数据时，要让 natproxy_client
-- 去连接真正的端口, 因此要修改 XdTask_ProxyClient的数据.  
-- 3 因此在开始的时候就要启动两个端口
local Natp = require "natproxy"


local xdcmsg_id_create_server     = 4113;  -- enumXdTaskId::XdTask_ProxyServer
local xdcmsg_id_create_client     = 4114;  -- enumXdTaskId::XdTask_ProxyClient
local xdcmsg_id_proxy_data        = 4115;  -- enumXdTaskId::XdTask_ProxyData
local sizeof_xdcmsg_proxy_id      = 8;     -- sizeof(XdTaskProxyId_t)
local offset_xdcmsg_server_port   = sizeof_xdcmsg_proxy_id+32; -- struct XdTaskProxyServer_t

local msvsmon_cmd_change_port_fmt = { 0x00, 0x00, 0x00, 0x00, 0x63, 0x70, 0x63, 0x74 };
local msvsmon_debug_port          = 4015;
local msvsmon_rand_proxy_port1    = 52680; 
local msvsmon_rand_proxy_port2    = 52681;
local msvsmon_rand_port1          = 0;
local msvsmon_rand_port2          = 0;
local natproxy_is_server          = 0;


-- natproxy callback functions
function on_send_xdc_msg(msg, msg_id, proxy_id, data_len)
	if natproxy_is_server == 0 then return end;

	if msg_id == xdcmsg_id_create_client then
		local conn_id, conn_port_old, conn_port_new = 0, 0, 0;

		conn_id       = Natp.read_xdcmsg_to_int(msg, 4, 4);
		conn_port_old = Natp.read_xdcmsg_to_int(msg, sizeof_xdcmsg_proxy_id+4, 4);

		if     conn_port_old == msvsmon_rand_proxy_port1 then conn_port_new = msvsmon_rand_port1;
		elseif conn_port_old == msvsmon_rand_proxy_port2 then conn_port_new = msvsmon_rand_port2;
		end

		if     conn_port_new ~= 0 then 
			Natp.write_xdcmsg_by_int(msg, sizeof_xdcmsg_proxy_id+4, 4, conn_port_new);
		end
		print("+++++++++++++++++++ lua send msg, create client -->", proxy_id, conn_id, conn_port_old, conn_port_new);
	end
end

function on_recv_xdc_msg(msg, msg_id, proxy_id, data_len)
	-- 根据 proxy_id 解析 natproxy_server接收到的数据. 
	-- proxy_id=4015是命令连接
	-- 其余应该是数据连接. 
	-- print("+++++++++++++++++++ lua recv msg -->", msg_id, proxy_id, data_len);
	if msg_id == xdcmsg_id_create_server then
		-- 1 当开始创建 server的时候，再创建两个随机端口的 nat_server
		--   需要 dst_xid
		local server_count, listen_port;
		server_count = Natp.read_xdcmsg_to_int(msg, 0, 4);
		listen_port  = Natp.read_xdcmsg_to_int(msg, offset_xdcmsg_server_port, 2);
		if listen_port == msvsmon_debug_port and server_count == 1 then
			natproxy_is_server = 1;
			Natp.write_xdcmsg_by_int(msg, 0, 4, 3);
			Natp.write_xdcmsg_by_int(msg, offset_xdcmsg_server_port+2, 2, msvsmon_rand_proxy_port1);
			Natp.write_xdcmsg_by_int(msg, offset_xdcmsg_server_port+4, 2, msvsmon_rand_proxy_port2);
		end
		return;
	end;

	if natproxy_is_server == 1 and data_len == 12 and proxy_id == msvsmon_debug_port and msg_id == xdcmsg_id_proxy_data then
		-- 2 判断是否是 4015端口返回的数据, 进行HOOK
		--   数据格式: 00 00 00 00 63 70 63 74 c7 cd c8 cd, 后 4位即为2个port. msvsmon_cmd_change_port_fmt
		local ret = Natp.compare_xdcmsg(msg, 0, msvsmon_cmd_change_port_fmt);
		if ret == 0 then
			-- 这儿修改 msvsmmon_rand_portX会有多线程安全隐患??? 由逻辑自身保证 ???
			msvsmon_rand_port1 = Natp.read_xdcmsg_to_int(msg, 8,  2);
			msvsmon_rand_port2 = Natp.read_xdcmsg_to_int(msg, 10, 2);
			Natp.write_xdcmsg_by_int(msg, 8,  2, msvsmon_rand_proxy_port1);
			Natp.write_xdcmsg_by_int(msg, 10, 2, msvsmon_rand_proxy_port2);
			print("+++++++++++++++++++ lua recv msg, msvsdbg change port -->", msvsmon_rand_port1, msvsmon_rand_port2, msvsmon_rand_proxy_port1, msvsmon_rand_proxy_port2);
		end
	end
end

function lua_init()
end

function lua_uninit()
	print('------uninit');
end
