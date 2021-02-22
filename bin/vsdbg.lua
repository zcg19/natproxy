-- �ܹ�ʹ��VSԶ�̵���
-- msvsmon��VSԶ�̵��Է����������к�Ὠ��һ��listen�˿� 4015
-- 4015���������ݽ�����Ψһ�˿�, ÿ�� VS���Ӻ�msvsmon��������
-- ��������˿�(���)���������˿ڲŻ�������ݽ�����
-- 
-- 1 ����ű����� natproxy_server��, ���� msvsmon���ص����ݵõ�
-- ������˿ڣ��޸�����˿�ָ�� natproxy_server����������˿�. 
-- 2 �� natproxy server�Ĵ���˿��յ�����ʱ��Ҫ�� natproxy_client
-- ȥ���������Ķ˿�, ���Ҫ�޸� XdTask_ProxyClient������.  
-- 3 ����ڿ�ʼ��ʱ���Ҫ���������˿�
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
	-- ���� proxy_id ���� natproxy_server���յ�������. 
	-- proxy_id=4015����������
	-- ����Ӧ������������. 
	-- print("+++++++++++++++++++ lua recv msg -->", msg_id, proxy_id, data_len);
	if msg_id == xdcmsg_id_create_server then
		-- 1 ����ʼ���� server��ʱ���ٴ�����������˿ڵ� nat_server
		--   ��Ҫ dst_xid
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
		-- 2 �ж��Ƿ��� 4015�˿ڷ��ص�����, ����HOOK
		--   ���ݸ�ʽ: 00 00 00 00 63 70 63 74 c7 cd c8 cd, �� 4λ��Ϊ2��port. msvsmon_cmd_change_port_fmt
		local ret = Natp.compare_xdcmsg(msg, 0, msvsmon_cmd_change_port_fmt);
		if ret == 0 then
			-- ����޸� msvsmmon_rand_portX���ж��̰߳�ȫ����??? ���߼�����֤ ???
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
