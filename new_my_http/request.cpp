#include"request.h"
#include"r_and_w.h"
#include<string>
#include "epoll.h"
#include<sys/mman.h>
#include <iostream>
#include<queue>

using namespace std;
extern connection_pool * m_connPool;

pthread_mutex_t qlock = PTHREAD_MUTEX_INITIALIZER;



std::priority_queue<mytimer*, deque<mytimer*>, timerCmp> myTimerQueue;


pthread_mutex_t MimeType::lock = PTHREAD_MUTEX_INITIALIZER;



requestData::requestData() :
	now_read_pos(0), state(STATE_PARSE_URI), h_state(h_start),
	keep_alive(false), againTimes(0), timer(NULL)
{
	cout << "requestData constructed !" << endl;
}


requestData::requestData(int _epollfd, int _fd, std::string _path) :
	now_read_pos(0), state(STATE_PARSE_URI), h_state(h_start),
	keep_alive(false), againTimes(0), timer(NULL),
	path(_path), fd(_fd), epollfd(_epollfd)
{}






int requestData::getFd()
{
	return fd;
}

void requestData::setFd(int _fd)
{
	fd = _fd;
}




void requestData::addTimer(mytimer *mtimer)
{
	if (timer == NULL)
		timer = mtimer;
}




int epoll_mod(int epoll_fd, int fd, void *request, __uint32_t events)
{
	struct epoll_event event;
	event.data.ptr = request;
	event.events = events;
	if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event) < 0)
	{
		perror("epoll_mod error");
		return -1;
	}
	return 0;
}


void requestData::reset()
{
	againTimes = 0;
	content.clear();
	file_name.clear();
	path.clear();
	now_read_pos = 0;
	state = STATE_PARSE_URI;
	h_state = h_start;
	headers.clear();
	keep_alive = false;
}




int hexit(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;

	return 0;
}


void send_respond(int fd, int number, char *disp, const char *type, int len) {
	char buf[1024] = { 0 };
	sprintf(buf, "HTTP/1.1 %d %s\r\n", number, disp);
	writen(fd, buf, strlen(buf));

	sprintf(buf, "Content-Type:%s\r\n", type);
	sprintf(buf + strlen(buf), "Content-Length:%ld\r\n", len);

	writen(fd, buf, strlen(buf));
	writen(fd, (void *)"\r\n", 2);
}

//解码  码->中文
void decode_str(char *to, char *from)
{
	for (; *from != '\0'; ++to, ++from) {
		if (from[0] == '%' && isxdigit(from[1]) && isxdigit(from[2])) {
			*to = hexit(from[1]) * 16 + hexit(from[2]);
			from += 2;
		}
		else {
			*to = *from;
		}
	}
	*to = '\0';
}



//发送文件
void send_file(int fd, const char *file) {
	int fd_open = open(file, O_RDONLY);
	if (fd_open == -1) {
		send_error(fd, 404, "Not Found", "NO such file or direntry");
		return;
	}
	int n = 0;
	char buf[1024] = { 0 };



	while ((n = read(fd_open, buf, sizeof(buf))) > 0) {
		int ret;
		ret = send(fd, buf, n, 0);
		if (ret == -1) {
			if (errno == EAGAIN || errno == EINTR) {
				continue;
			}
			else
			{
				close(fd_open);
				break;
			}
		}
	}
	close(fd_open);

}

// 通过文件名获取文件的类型
const char *get_file_type(const char *name)
{
	const char* dot;

	// 自右向左查找‘.’字符, 如不存在返回NULL
	dot = strrchr(name, '.');
	if (dot == NULL)
		return "text/plain; charset=utf-8";
	if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0)
		return "text/html; charset=utf-8";
	if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0)
		return "image/jpeg";
	if (strcmp(dot, ".gif") == 0)
		return "image/gif";
	if (strcmp(dot, ".png") == 0)
		return "image/png";
	if (strcmp(dot, ".css") == 0)
		return "text/css";
	if (strcmp(dot, ".au") == 0)
		return "audio/basic";
	if (strcmp(dot, ".wav") == 0)
		return "audio/wav";
	if (strcmp(dot, ".avi") == 0)
		return "video/x-msvideo";
	if (strcmp(dot, ".mov") == 0 || strcmp(dot, ".qt") == 0)
		return "video/quicktime";
	if (strcmp(dot, ".mpeg") == 0 || strcmp(dot, ".mpe") == 0)
		return "video/mpeg";
	if (strcmp(dot, ".vrml") == 0 || strcmp(dot, ".wrl") == 0)
		return "model/vrml";
	if (strcmp(dot, ".midi") == 0 || strcmp(dot, ".mid") == 0)
		return "audio/midi";
	if (strcmp(dot, ".mp3") == 0)
		return "audio/mpeg";
	if (strcmp(dot, ".ogg") == 0)
		return "application/ogg";
	if (strcmp(dot, ".pac") == 0)
		return "application/x-ns-proxy-autoconfig";

	return "text/plain; charset=utf-8";
}

//编码 中文->码
void encode_str(char* to, int tosize, const char* from)
{
	int tolen;

	for (tolen = 0; *from != '\0' && tolen + 4 < tosize; ++from) {
		if (isalnum(*from) || strchr("/_.-~", *from) != (char*)0) {
			*to = *from;
			++to;
			++tolen;
		}
		else {
			sprintf(to, "%%%02x", (int)*from & 0xff);
			to += 3;
			tolen += 3;
		}
	}
	*to = '\0';
}



void send_dir(int fd, const char * file) {
	int ret;

	char buf[4094] = { 0 };
	sprintf(buf, "<html><head><title>目录名: %s</title></head>", file);
	sprintf(buf + strlen(buf), "<body><h1>当前目录: %s</h1><table>", file);

	char path[1024] = { 0 };
	char enstr[1024] = { 0 };

	struct dirent** ptr;
	int num = scandir(file, &ptr, NULL, alphasort);



	for (int i = 0; i < num; i++) {

		char *name = ptr[i]->d_name;
		sprintf(path, "%s/%s", file, name);

		//判断是否存在
		struct stat st;
		stat(path, &st);


		//编码  中文->码
		encode_str(enstr, sizeof(enstr), name);


		if (S_ISDIR(st.st_mode)) {  		// 目录

			sprintf(buf + strlen(buf),
				"<tr><td><a href=\"%s/\">%s/</a></td><td>%ld</td></tr>",
				enstr, name, (long)st.st_size);
		}

		else if (S_ISREG(st.st_mode)) {     //是一个普通文件
			sprintf(buf + strlen(buf),
				"<tr><td><a href=\"%s\">%s</a></td><td>%ld</td></tr>",
				enstr, name, (long)st.st_size);
		}

		/*
				int src_fd = open(file, O_RDONLY, 0);
				char *src_addr = static_cast<char*>(mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, src_fd, 0));
				close(src_fd);
				munmap(src_addr, st.st_size);

		*/

		ret = writen(fd, buf, strlen(buf));
		memset(buf, 0, sizeof(buf));
	}

	sprintf(buf + strlen(buf), "</table></body></html>");
	send(fd, buf, strlen(buf), 0);
}







void requestData::handleRequest()
{
	do {
		int i;
		char line[4096] = { 0 };
		char path[2048];
		int tem= 0;
		char tchar[1024];
		int len;
		if(1==state){
			len = readn(fd, line, 4096);
			cout<<line<<endl;
			if (len < 0) {
				send_error(fd, 404, "Not Found", "NO such file or direntry");
				state=-1;
				break;
			}
			else if (len == 0) {
				state=-1;
				break;
			}

			for (i = tem; i < len; i++) {
				//printf("i=%d %c\n",i,line[i]);
				if (line[i] == '\r'&&line[i + 1] == '\n') {
					tchar[i - tem] = line[i];
					tchar[i - tem+1] = line[i+1];
					break;
				}
				else {
					tchar[i - tem] = line[i];

				}
			}
			if (i >= len) {
				state=-1;
				break;
			}
			tem = i + 2;

		//	cout<<tchar<<endl;

			//strncasecmp-----比较字符串的前n个字符
			if (strncasecmp(tchar, "GET", 3) == 0 )
				state=2;
			else if(strncasecmp(tchar, "POST", 4) == 0)
				state=3;
			else{
				state=-1;
				break;
			}




			while (tem < len)
			{
			//	printf("%d %d\n", tem, len);

				if (line[tem] == '\r') {
					break;
				}

				for (i = tem; i < len; i++) {
				
					if (line[i] == '\r'&&line[i + 1] == '\n') {
					
						tchar[i - tem] = line[i];
						tchar[i - tem + 1] = line[i + 1];
						break;
					}
					else {
						tchar[i - tem] = line[i];
					}
				}

				if (i >= len) {
					state=-1;
					break;
				}
				//cout<<"state:"<<state<<endl;
				tchar[i - tem + 2] = '\0';
				tem = i + 2;

				//tchar负责分割行 
				//first 和 second  代表 ：前后的值
				char first[128];
				char second[128];

				for (i = 0;; i++) {
					if (tchar[i] == '\0') {
						state=-1;
						break;
					}
					if (tchar[i] == ':') {
						break;
					}
					first[i] = tchar[i];
				}

				first[i] = '\0';
				i += 2;

				int j = i;

				for (;; i++) {
					if (tchar[i] == '\0') {
						state=-1;
						break;
					}
					if (tchar[i] == '\r'&&tchar[i + 1] == '\n')
					{
					
						break;
					}
					second[i - j] = tchar[i];
				}

				second[i - j] = '\0';

			//	printf("first=%s  second = %s!\n",first,second);
				if (strcmp(second, "keep-alive") == 0) {
					keep_alive = true;
				}
			}


		
		}

		if(2==state){  //GET
			int j;
			int judge=0;
			for(j=4;;j++){
				if(line[j]=='?') judge=1;
				if(line[j]==' ') break;
				path[j-4]=line[j];
			}
			path[j-4]='\0';
			//printf("\npath=%s\n",path);

			if(judge==1)
				state=4;
			else
				state=5;

		}
		if(3==state){   //POST
			cout << "it is post!!!!!!" << endl;
			int j=5;

			int judge = 0;

			for (j = 5;j<len; j++) {
				if (line[j] == ' ') break;
				path[j - 5] = line[j];
			}
	
			path[j - 5] = '\0';
			 printf("\npath=%s\n",path);

			j = 0;
			char post[1024];
				
			while (tem < len)
			{
				if (line[tem] != ' '&&line[tem] != '\r'&&line[tem] != '\n') {
					break;
				}
				tem++;
			}
			
			char id[1024];
			int i_id = 0;

			char passwd[1024];
			int i_passwd = 0;
			judge = 0;

			for (j = tem; j < len; j++) {
				
				if (judge == 0) {
					if (line[j] == '=') { judge = 1; continue; }
				}
				if (judge == 1) {
		
					if (line[j] == '&') { id[i_id] = '\0'; judge = 2; continue; }
				
					id[i_id++] = line[j];
				}
				if (judge == 2) {
					if (line[j] == '=') { judge = 3; continue; }
				}
				if (judge == 3) {
					passwd[i_passwd++] = line[j];
				}
			}
			//cout<<judge<<endl;

			if (judge != 3) {
				state = -1;
				break;
			}
			passwd[i_passwd] = '\0';

			printf("%s-------%s-------\n",id,passwd);

			cout<<"?????";
			
			MYSQL *mysql = NULL;
			
			connectionRAII mysqlcon(&mysql, m_connPool);
			
			char sql[1024] = { 0 };
			sprintf(sql, "SELECT * FROM people where id='%s' and passwd='%s'", id, passwd);

			if (mysql_query(mysql, sql))
				//if (mysql_query(mysql, "SELECT * FROM people"))
			{
				printf("mysql_restore_result(): %s\n", mysql_error(mysql));
			}
			//从表中检索完整的结果集
			MYSQL_RES *result = mysql_store_result(mysql);
			if (!mysql_fetch_row(result)) {
				strcpy(path, "/error.html");
			}
			state = 5;

		}
		if(4==state){   //get ---- ?
			char id[1024];
			int i_id=0;

			char passwd[1024];
			int i_passwd=0;
			int judge=0;
			for(int j=0;;j++){
				if(path[j]=='\0') break;
				if(path[j]==' ') break;
				if(path[j]=='?'){path[j]='\0' ;judge=1;continue;}
				if(judge==1){
					if(path[j]=='=') {judge=2;continue;}
				}
				if(judge==2){
					if(path[j]=='&') {id[i_id]='\0'; judge=3;continue;}
					id[i_id++]=path[j];
				}
				if(judge==3){
					if(path[j]=='=') {judge=4;continue;}
				}
				if(judge==4){
					passwd[i_passwd++]=path[j];
				}
			}
			passwd[i_passwd]='\0';

			if(judge!=4){
			    state=-1;
			    break;
			}

		//	printf("%s-------%s\n",id,passwd);

			MYSQL *mysql = NULL;

    		connectionRAII mysqlcon(&mysql, m_connPool);
			char sql[1024]={0};
			sprintf(sql, "SELECT * FROM people where id='%s' and passwd='%s'", id,passwd );
		//	cout<<sql<<endl;

			if (mysql_query(mysql, sql))
			//if (mysql_query(mysql, "SELECT * FROM people"))
		    {
				printf("mysql_restore_result(): %s\n", mysql_error(mysql));
  			}
			//从表中检索完整的结果集
    		MYSQL_RES *result = mysql_store_result(mysql);
			if(!mysql_fetch_row(result)){
				strcpy(path,"/error.html");
			}
			state=5;
		}



		if(5==state){   //get no ?
			cout<<"path is"<<path<<endl;
			decode_str(path, path);

			char *file = path + 1;

			if (strcmp(path, "/") == 0) {
				file = "./";
			}

				//printf("file = %s\n",file);

			struct stat sbuf;


			int ret = stat(file, &sbuf);

			if (ret == -1) {
				send_error(fd, 404, "Not Found", "NO such file or direntry");
                state=-1;
				break;
			}
			if (S_ISDIR(sbuf.st_mode)) {  		// 目录
				// 发送头信息
				send_respond(fd, 200, "OK", get_file_type(".html"), -1);
				// 发送目录信息
				send_dir(fd, file);
			}


			if (S_ISREG(sbuf.st_mode)) {     //是一个普通文件
				//回应http协议应答
				send_respond(fd, 200, "OK", get_file_type(file), sbuf.st_size);

				//发送文件
				send_file(fd, file);
			}

		}



	} while (false);



	

	if(-1==state||keep_alive==false){
		delete this;
		return;
	}

	cout<<"chang";



	this->reset();

	pthread_mutex_lock(&qlock);

	mytimer *mtimer = new mytimer(this, 500);


	this->addTimer(mtimer);
	myTimerQueue.push(mtimer);

	pthread_mutex_unlock(&qlock);

	__uint32_t _epo_event = EPOLLIN | EPOLLET | EPOLLONESHOT;
	if (epoll_mod(epollfd, fd, static_cast<void*>(this), _epo_event) < 0)
	{
		// 返回错误处理
		delete this;
		return;
	}
}




requestData::~requestData()
{
//	cout << "~requestData()" << endl;
	struct epoll_event ev;
	// 超时的一定都是读请求，没有"被动"写。
	ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
	ev.data.ptr = (void*)this;
	epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, &ev);
	if (timer != NULL)
	{
		timer->clearReq();
		timer = NULL;
	}
	close(fd);
}



void requestData::seperateTimer()
{
	if (timer)
	{
		timer->clearReq();
		timer = NULL;
	}
}




















void mytimer::setDeleted()
{
	deleted = true;
}



bool mytimer::isvalid()
{
	struct timeval now;
	gettimeofday(&now, NULL);   //获得当前精确时间
	/*
	long int tv_sec; // 秒数
	long int tv_usec; // 微秒数
	*/

	size_t temp = ((now.tv_sec * 1000) + (now.tv_usec / 1000));
	if (temp < expired_time)   //还没到时间
	{
		return true;
	}
	else
	{
		this->setDeleted();
		return false;
	}
}


void mytimer::clearReq()
{
	request_data = NULL;
	this->setDeleted();
}




bool mytimer::isDeleted() const
{
	return deleted;
}


size_t mytimer::getExpTime() const
{
	return expired_time;
}



bool timerCmp::operator()(const mytimer *a, const mytimer *b) const
{
	return a->getExpTime() > b->getExpTime();
}


mytimer::mytimer(requestData *_request_data, int timeout) : deleted(false), request_data(_request_data)
{
	//cout << "mytimer()" << endl;
	struct timeval now;
	gettimeofday(&now, NULL);
	// 以毫秒计
	expired_time = ((now.tv_sec * 1000) + (now.tv_usec / 1000)) + timeout;
}


mytimer::~mytimer()
{
//	cout << "~mytimer()" << endl;
	if (request_data != NULL)
	{
		cout << "request_data=" << request_data << endl;
		delete request_data;
		request_data = NULL;
	}
}
