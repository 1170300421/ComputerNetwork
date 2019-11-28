#include <stdio.h>
#include <Windows.h>
#include <process.h>
#include <string.h>

#define MAXSIZE 65507 //发送数据报文的最大长度
#define HTTP_PORT 80 //http 服务器端口

#define INVILID_WEBSITE "http://jwts.hit.edu.cn/"          //被屏蔽的网站
#define FISHING_WEB_SRC "http://www.hit.edu.cn/"    //钓鱼的源网址
#define FISHING_WEB_DEST  "http://www.hrbnu.edu.cn/"   //钓鱼的目的网址
#define FISHING_WEB_HOST "www.hrbnu.edu.cn"            //钓鱼目的地址的主机名

//Http 重要头部数据
struct HttpHeader{
    char method[4]; // POST 或者 GET，注意有些为 CONNECT，本实验暂 不考虑
    char url[1024]; // 请求的 url
    char host[1024]; // 目标主机
    char cookie[1024 * 10]; //cookie
    HttpHeader(){
        ZeroMemory(this,sizeof(HttpHeader));
    }
};

BOOL InitSocket();//初始化套接字
void ParseHttpHead(char *buffer,HttpHeader * httpHeader);// 解析 TCP 报文中的 HTTP 头部
BOOL ConnectToServer(SOCKET *serverSocket,char *host);//根据主机创建目标服务套接字，并连接
unsigned int __stdcall ProxyThread(LPVOID lpParameter);//线程执行函数
boolean ParseDate(char *buffer, char *field, char *tempDate);


//代理相关参数
SOCKET ProxyServer;
sockaddr_in ProxyServerAddr;
const int ProxyPort = 10240;

//缓存相关参数
boolean haveCache = FALSE;
boolean needCache = TRUE;
char * strArr[100];

void makeNewHTTP(char *buffer, char *value);
void makeFilename(char *url, char *filename);
void makeCache(char *buffer, char *url);
void getCache(char *buffer, char *filename);


struct ProxyParam{
    SOCKET clientSocket;
    SOCKET serverSocket;
};

int main(int argc, char* argv[]) {
    printf("代理服务器正在启动\n");
    printf("初始化...\n");
    if(!InitSocket()){
        printf("socket 初始化失败\n");
        return -1;
    }
    printf("代理服务器正在运行，监听端口 %d\n",ProxyPort);
    SOCKET acceptSocket = INVALID_SOCKET;
    ProxyParam *lpProxyParam;
    HANDLE hThread;
    while(true){
        acceptSocket = accept(ProxyServer,NULL,NULL);
        lpProxyParam = new ProxyParam;
        if(lpProxyParam == NULL){
            continue;
        }
        lpProxyParam->clientSocket = acceptSocket;
        hThread = (HANDLE)_beginthreadex(NULL, 0, &ProxyThread,(LPVOID)lpProxyParam, 0, 0);
        CloseHandle(hThread);
        Sleep(200);
    }
    closesocket(ProxyServer);
    WSACleanup();
    return 0;
}


//初始化套接字
BOOL InitSocket(){
    //加载套接字库（必须）
    WORD wVersionRequested;
    WSADATA wsaData;        //WSADATA结构体中主要包含了系统所支持的Winsock版本信息
    //套接字加载时错误提示
    int err;
    //版本 2.2
    wVersionRequested = MAKEWORD(2, 2);
    //加载 dll 文件 Scoket 库
    err = WSAStartup(wVersionRequested, &wsaData);
    if(err != 0){
        //找不到 winsock.dll
        printf("加载 winsock 失败，错误代码为: %d\n", WSAGetLastError());
        return FALSE;
    }
    if(LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) !=2)   {
        printf("不能找到正确的 winsock 版本\n");
        WSACleanup();
        return FALSE;
    }
    //AF_INET,PF_INET	IPv4 Internet协议
    //SOCK_STREAM	Tcp连接，提供序列化的、可靠的、双向连接的字节流。支持带外数据传输
    ProxyServer = socket(AF_INET, SOCK_STREAM, 0);
    if(INVALID_SOCKET == ProxyServer){
        printf("创建套接字失败，错误代码为：%d\n",WSAGetLastError());
        return FALSE;
    }
    ProxyServerAddr.sin_family = AF_INET;
    ProxyServerAddr.sin_port = htons(ProxyPort);      //将整型变量从主机字节顺序转变成网络字节顺序


    ProxyServerAddr.sin_addr.S_un.S_addr = inet_addr("127.0.0.1");//仅本机用户可访问服务器
    //ProxyServerAddr.sin_addr.S_un.S_addr = inet_addr("127.0.0.2");  //屏蔽用户
    if(bind(ProxyServer,(SOCKADDR*)&ProxyServerAddr,sizeof(SOCKADDR)) == SOCKET_ERROR){
        printf("绑定套接字失败\n");
        return FALSE;
    }
    if(listen(ProxyServer, SOMAXCONN) == SOCKET_ERROR){
        printf("监听端口%d 失败",ProxyPort);
        return FALSE;
    }
    return TRUE;
}

// 线程执行函数
unsigned int __stdcall ProxyThread(LPVOID lpParameter){
    char Buffer[MAXSIZE], fileBuffer[MAXSIZE];
    char *CacheBuffer;
    CacheBuffer = (char*)malloc(MAXSIZE*2);
    HttpHeader* httpHeader = new HttpHeader();
    ZeroMemory(Buffer,MAXSIZE);
    SOCKADDR_IN clientAddr;
    int length = sizeof(SOCKADDR_IN);
    int recvSize;
    int ret;
    recvSize = recv(((ProxyParam *)lpParameter)->clientSocket,Buffer,MAXSIZE,0);
    CacheBuffer = new char[recvSize + 1];
    ZeroMemory(CacheBuffer, recvSize + 1);
    memcpy(CacheBuffer, Buffer, recvSize);

    //解析http首部
    ParseHttpHead(CacheBuffer, httpHeader);

    //缓存
    char *DateBuffer;
    DateBuffer = (char*)malloc(MAXSIZE*2);
	ZeroMemory(DateBuffer, strlen(Buffer) + 1);
	memcpy(DateBuffer, Buffer, strlen(Buffer) + 1);
	char filename[100];
	ZeroMemory(filename, 100);
	makeFilename(httpHeader->url, filename);
	char *field = "Date";
	char date_str[30];  //保存字段Date的值
	ZeroMemory(date_str, 30);
	ZeroMemory(fileBuffer, MAXSIZE);
	FILE *in;
	if ((in = fopen(filename, "rb")) != NULL) {
		printf("\n有缓存\n");
		fread(fileBuffer, sizeof(char), MAXSIZE, in);
		fclose(in);
		ParseDate(fileBuffer, field, date_str);
		printf("date_str:%s\n", date_str);
		makeNewHTTP(Buffer, date_str);
		haveCache = TRUE;
		goto success;
	}

    //网站过滤：屏蔽一个网站
    if (strcmp (httpHeader->url, INVILID_WEBSITE) == 0) {
        //printf("\n*******************************************\n\n");
        printf("对不起，您访问的是网站已被屏蔽\n");
        goto error;
    }
    delete CacheBuffer;
    delete DateBuffer;

success:
    if(!ConnectToServer(&((ProxyParam *)lpParameter)->serverSocket,httpHeader->host)) {
        printf("连接目标服务器失败！！！\n");
        goto error;
    }
    printf("代理连接主机 %s 成功\n",httpHeader->host);
    //将客户端发送的 HTTP 数据报文直接转发给目标服务器
    ret = send(((ProxyParam *)lpParameter)->serverSocket,Buffer,strlen(Buffer) + 1,0);
    //等待目标服务器返回数据
    recvSize = recv(((ProxyParam *)lpParameter)->serverSocket,Buffer,MAXSIZE,0);
    if(recvSize <= 0){
        printf("返回目标服务器的数据失败！！！\n");
        goto error;
    }
	//有缓存时，判断返回的状态码是否是304，若是则将缓存的内容发送给客户端
	if (haveCache == TRUE) {
		getCache(Buffer, filename);
	}
	if (needCache == TRUE) {
		makeCache(Buffer, httpHeader->url);  //缓存报文
	}
    //将目标服务器返回的数据直接转发给客户端
    ret = send(((ProxyParam *)lpParameter)->clientSocket,Buffer,sizeof(Buffer),0);

//错误处理
error:
    printf("关闭套接字\n");
    delete Buffer;
    delete fileBuffer;
    delete filename;
    Sleep(200);
    closesocket(((ProxyParam*)lpParameter)->clientSocket);
    closesocket(((ProxyParam*)lpParameter)->serverSocket);
    delete  lpParameter;
    _endthreadex(0);
    return 0;
}

//  解析 TCP 报文中的 HTTP 头
void ParseHttpHead(char *buffer,HttpHeader * httpHeader){
    char *p;
    const char * delim = "\r\n";
    p = strtok(buffer,delim); // 第一次调用，第一个参数为被分解的字符串
    if(p[0] == 'G'){
        //GET 方式
        memcpy(httpHeader->method,"GET",3);
        memcpy(httpHeader->url,&p[4],strlen(p) -13); //'Get' 和 'HTTP/1.1' 各占 3 和 8 个，再加上俩空格，一共13个
        if(!strcmp("http://www.hit.edu.cn/",httpHeader->url)) {
        printf("\n*****************************\n\n");
		printf("***********检测到访问的是钓鱼网站：%s 已转到 目的网址 ：%s *************\n", FISHING_WEB_SRC,FISHING_WEB_DEST);
                memcpy(httpHeader->url, FISHING_WEB_DEST, strlen(FISHING_WEB_DEST));
                printf("访问的url是 ： %s\n",httpHeader->url);
        }
    }
    else if(p[0] == 'P'){
        //POST 方式
        memcpy(httpHeader->method,"POST",4);
        memcpy(httpHeader->url,&p[5],strlen(p) - 14); //'Post' 和 'HTTP/1.1' 各占 4 和 8 个，再加上俩空格，一共14个
    }
    p = strtok(NULL,delim);              // 第二次调用，需要将第一个参数设为 NULL
    while(p){
        switch(p[0]){
            case 'H'://Host
                memcpy(httpHeader->host,&p[6],strlen(p) - 6);
                if(!strcmp("www.hit.edu.cn",httpHeader->host)) {
                        memcpy(httpHeader->host, FISHING_WEB_HOST, strlen(FISHING_WEB_HOST)+1);

                }
                break;
            case 'C'://Cookie
                if(strlen(p) > 8){
                    char header[8];
                    ZeroMemory(header,sizeof(header));
                    memcpy(header,p,6);
                    if(!strcmp(header,"Cookie")){
                        memcpy(httpHeader->cookie,&p[8],strlen(p) -8);
                    }
                }
                break;
            default:
                break;
        }
        p = strtok(NULL,delim);
    }
}

//  根据主机创建目标服务器套接字，并连接
BOOL ConnectToServer(SOCKET *serverSocket,char *host){
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(HTTP_PORT);
    HOSTENT *hostent = gethostbyname(host);
    if(!hostent){
        return FALSE;
    }
    in_addr Inaddr = *( (in_addr*) *hostent->h_addr_list);
    serverAddr.sin_addr.s_addr = inet_addr(inet_ntoa(Inaddr));
    *serverSocket = socket(AF_INET,SOCK_STREAM,0);
    if(*serverSocket == INVALID_SOCKET){
        return FALSE;
    }
    if(connect(*serverSocket,(SOCKADDR *)&serverAddr,sizeof(serverAddr)) == SOCKET_ERROR){

        closesocket(*serverSocket);
        return FALSE;
    }
    return TRUE;
}

//分析HTTP头部的field字段，如果包含该field则返回true，并获取日期
boolean ParseDate(char *buffer, char *field, char *tempDate) {
	char *p, *ptr, temp[5];
	const char *delim = "\r\n";
	ZeroMemory(temp, 5);
	p = strtok(buffer, delim);
	int len = strlen(field) + 2;
	while (p) {
		if (strstr(p, field) != NULL) {
			memcpy(tempDate, &p[len], strlen(p) - len);
			return TRUE;
		}
		p = strtok(NULL, delim);
	}
	return TRUE;
}

//改造HTTP请求报文
void makeNewHTTP(char *buffer, char *value) {
	const char *field = "Host";
	const char *newfield = "If-Modified-Since: ";
	char temp[MAXSIZE];
	ZeroMemory(temp, MAXSIZE);
	char *pos = strstr(buffer, field);
    int i = 0;
	for (i = 0; i < strlen(pos); i++) {
		temp[i] = pos[i];
	}
	*pos = '\0';
	while (*newfield != '\0') {  //插入If-Modified-Since字段
		*pos++ = *newfield++;
	}
	while (*value != '\0') {
		*pos++ = *value++;
	}
	*pos++ = '\r';
	*pos++ = '\n';
	for (i = 0; i < strlen(temp); i++) {
		*pos++ = temp[i];
	}
}

//根据url构造文件名
void makeFilename(char *url, char *filename) {
	while (*url != '\0') {
		if (*url != '/' && *url != ':' && *url != '.') {
			*filename++ = *url;
		}
		url++;
	}
    strcat(filename, ".txt");
}

//进行缓存
void makeCache(char *buffer, char *url) {
	char *p, *ptr, num[10], tempBuffer[MAXSIZE + 1];
	const char * delim = "\r\n";
	ZeroMemory(num, 10);
	ZeroMemory(tempBuffer, MAXSIZE + 1);
	memcpy(tempBuffer, buffer, strlen(buffer));
	p = strtok(tempBuffer, delim);//提取第一行
	memcpy(num, &p[9], 3);
	if (strcmp(num, "200") == 0) {  //状态码是200时缓存

		char filename[100] = { 0 };  // 构造文件名
		makeFilename(url, filename);
		printf("filename : %s\n", filename);
		FILE *out;
		out = fopen(filename, "w");
		fwrite(buffer, sizeof(char), strlen(buffer), out);
		fclose(out);
		printf("\n*************************************\n\n");
		printf("\n该网页已被缓存知本地\n");
	}
}

//获取缓存
void getCache(char *buffer, char *filename) {
	char *p, *ptr, num[10], tempBuffer[MAXSIZE + 1];
	const char * delim = "\r\n";
	ZeroMemory(num, 10);
	ZeroMemory(tempBuffer, MAXSIZE + 1);
	memcpy(tempBuffer, buffer, strlen(buffer));
	p = strtok(tempBuffer, delim);//提取第一行
	memcpy(num, &p[9], 3);
	if (strcmp(num, "304") == 0) {  //主机返回的报文中的状态码为304时返回已缓存的内容
        printf("\n*************************************\n\n");
		printf("从本机获得缓存\n");
		ZeroMemory(buffer, strlen(buffer));
		FILE *in = NULL;
		if ((in = fopen(filename, "r")) != NULL) {
			fread(buffer, sizeof(char), MAXSIZE, in);
			fclose(in);
		}
		needCache = FALSE;
	}
}
