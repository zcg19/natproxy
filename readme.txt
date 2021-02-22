tcp reverse proxy

1 client+server 
2 逻辑比较简单，就是一个反向代理。初衷是为了调试nat环境下的vs，参考lua脚本 
3 需要将 server端部署在公网中。client端部署在两个不同的 nat环境中。 
4 client端的配置文件中设置 server的 IP/DOMAIN后，启动后会连接服务器端，然后按照提示连接另一个client。 
5 连接成功后就可以输入命令启动一个本地的端口来反向代理另一个 client的监听端口，