#include"request.h"
#include"r_and_w.h"
#include<string>
#include "epoll.h"
#include"r_and_w.h"
#include<sys/mman.h>
#include <iostream>
#include<queue>

using namespace std;


pthread_mutex_t qlock = PTHREAD_MUTEX_INITIALIZER;


std::priority_queue<mytimer*, deque<mytimer*>, timerCmp> myTimerQueue;


pthread_mutex_t MimeType::lock = PTHREAD_MUTEX_INITIALIZER;



requestData::requestData(): 
    now_read_pos(0), state(STATE_PARSE_URI), h_state(h_start), 
    keep_alive(false), againTimes(0), timer(NULL)
{
    cout << "requestData constructed !" << endl;
}

requestData::requestData(int _epollfd, int _fd, std::string _path):
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
				break;
		}
	}

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




void http_request(const char * line, int fd) {

	char method[16], path[1024], protocol[16];
	sscanf(line, "%[^ ] %[^ ] %[^ ]", method, path, protocol);

//	printf("%s %s %s \n\n\n",method,path,protocol);

	// 码---中文来查找有无这个文件
	  decode_str(path, path);

 //   printf("path=\n%s\n",path);


	char *file = path + 1;

	if (strcmp(path, "/") == 0) {
		file = "./";
	}

	

	struct stat sbuf;
				

	int ret = stat(file, &sbuf);

	if (ret == -1) {
		send_error(fd, 404, "Not Found", "NO such file or direntry");
		return;
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

/*
	int src_fd = open(file, O_RDONLY, 0);
    char *src_addr = static_cast<char*>(mmap(NULL, sbuf.st_size, PROT_READ, MAP_PRIVATE, src_fd, 0));
    close(src_fd);
    munmap(src_addr, sbuf.st_size);
	*/
}



void requestData::handleRequest()
{
	
    char line[4096]={0};
	int len = readn(fd,line,4096);
	int cishu =0;
	if (len < 0) {
		send_error(fd, 404, "Not Found", "NO such file or direntry");
		delete this;
	}

	else if (len == 0) {
		if (errno == EAGAIN)
        {
            if (againTimes > AGAIN_MAX_TIMES)
            {
                delete this;
				return ;
			}
            else
                ++againTimes;
            }
            else if (errno != 0)
			{
                delete this;
				return ;
			}
	}
	else
	{
	    char line1[4096]={0};
    	for(int i=0;i<sizeof(line)-1;i++){
		    if(line[i]=='\r'&&line[i+1]=='\n'){
		    	line1[i]=line[i];
		    	line1[i+1]=line[i+1];
		    	break;
	    	}
		    else{
		    	line1[i]=line[i];
	        }
    	}


		if (strncasecmp(line1, "GET", 3) == 0||strncasecmp(line1, "get", 3) == 0) {
			http_request(line1, fd);
		}
		else
		{
			printf("no get\n");
		}

		delete this;
	}


	pthread_mutex_lock(&qlock);
	
    mytimer *mtimer = new mytimer(this, 500);

	this->addTimer(mtimer);



 	myTimerQueue.push(mtimer);

    pthread_mutex_unlock(&qlock);
	
/*
    __uint32_t _epo_event = EPOLLIN | EPOLLET | EPOLLONESHOT;
    int ret = epoll_mod(epollfd, fd, static_cast<void*>(this), _epo_event);
    if (ret < 0)
    {
        // 返回错误处理
        delete this;
        return;
    }
*/
}




requestData::~requestData()
{
    cout << "~requestData()" << endl;
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


mytimer::mytimer(requestData *_request_data, int timeout): deleted(false), request_data(_request_data)
{
    //cout << "mytimer()" << endl;
    struct timeval now;
    gettimeofday(&now, NULL);
    // 以毫秒计
    expired_time = ((now.tv_sec * 1000) + (now.tv_usec / 1000)) + timeout;
}


mytimer::~mytimer()
{
    cout << "~mytimer()" << endl;
    if (request_data != NULL)
    {
        cout << "request_data=" << request_data << endl;
        delete request_data;
        request_data = NULL;
    }
}
