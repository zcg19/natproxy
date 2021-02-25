a simply tcp reverse proxy

1 client+server 
2 依赖lua库，VS+NAT调试需要lua脚本支持，若不进行VS调试，则不需要链接 lua库。
3 逻辑比较简单，就是一个反向代理，可以穿透NAT。初衷是为了调试nat环境下的vs，参考lua脚本 
4 需要将 server端部署在公网中。client端部署在两个不同的 nat环境中。 
5 client端的配置文件中设置 server的 IP/DOMAIN后，启动后会连接服务器端，然后按照提示连接另一个client。 
6 连接成功后就可以输入命令启动一个本地的端口来反向代理另一个 client的监听端口。
7 支持文件传输

8 使用这个可以干许多有意思的事，参考 sample
