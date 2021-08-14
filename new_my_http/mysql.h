#ifndef _MYSQL
#define _MYSQL

#include<mysql/mysql.h>
#include"string"
#include <stdio.h>
#include <list>

#include <exception>
#include <pthread.h>
#include <semaphore.h>


class sem
{
public:
    sem()
    {
        if (sem_init(&m_sem, 0, 0) != 0)
        {
            throw std::exception();
        }
    }
    sem(int num)
    {
        if (sem_init(&m_sem, 0, num) != 0)
        {
            throw std::exception();
        }
    }
    ~sem()
    {
        sem_destroy(&m_sem);
    }
    bool wait()
    {
        return sem_wait(&m_sem) == 0;
    }
    bool post()
    {
        return sem_post(&m_sem) == 0;
    }

private:
    sem_t m_sem;
};



class connection_pool
{
public:
	MYSQL *GetConnection();				 //获取数据库连接
	bool ReleaseConnection(MYSQL *conn); //释放连接
	int GetFreeConn();					 //获取连接
	void DestroyPool();					 //销毁所有连接

	//单例模式
	static connection_pool *GetInstance();

	void init(std::string url, std::string User, std::string PassWord, std::string DataBaseName, int Port, int MaxConn, int close_log); 

private:
	connection_pool();
	~connection_pool();

	int m_MaxConn;  //最大连接数
	int m_CurConn;  //当前已使用的连接数
	int m_FreeConn; //当前空闲的连接数
	pthread_mutex_t lock;
	std::list<MYSQL *> connList; //连接池
	sem reserve;
    
public:
	std::string m_url;			 //主机地址
	std::string m_Port;		 //数据库端口号
	std::string m_User;		 //登陆数据库用户名
	std::string m_PassWord;	 //登陆数据库密码
	std::string m_DatabaseName; //使用数据库名
	int m_close_log;	//日志开关
};






class connectionRAII{
public:
	connectionRAII(MYSQL **con, connection_pool *connPool);
	~connectionRAII();
	
private:
	MYSQL *conRAII;
	connection_pool *poolRAII;
};






#endif
