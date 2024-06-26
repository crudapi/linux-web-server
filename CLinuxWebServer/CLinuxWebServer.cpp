﻿// CLinuxWebServer.cpp: 定义应用程序的入口点。
//

#include "CLinuxWebServer.h"

using namespace std;

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include<signal.h>
#include<sys/wait.h>
#include<sys/select.h>
#include<sys/time.h>
#include<sys/epoll.h>
#include<sys/poll.h>

#define BUF_SIZE 1024
#define SMAlL_BUF 100
#define EPOOL_SIZE 50

void* request_handler(void* msg);
void send_data(FILE* fp, char* ct, char* filename);
char* content_type(const char* file);
void send_error(FILE* fp);
void error_handing(char* message);
void splitStr(char* arr, char separator, vector<string>& strList);
off_t get_file_size(char* file_name);
void handler(int sig);

void handler(int sig)
{
	pid_t id;
	while ((id = waitpid(-1, NULL, WNOHANG)) > 0)//while循环处理需要回收的子进程
		printf("wait child success : % d\n", id);
}

int main(int argc, char* argv[])
{
	cout << "Hello Linux Web Server1." << endl;

	int server_socket, clnt_sock;
	struct sockaddr_in server_addr, client_addr;
	struct timeval timeout;
	fd_set reads, cpy_reds;
	int fd_max, str_len, fd_num, i;
	int client_addr_size;
	char buf[BUF_SIZE];

	//epool
	struct epoll_event* ep_events;
	struct epoll_event event;
	int epfd, event_cnt;



	pthread_t t_id;

	char* port = "8765";

	if (argc == 2) {
		cout << "Usage:" << argv[0] << " <port>\n";
		port = argv[1];
	}

	server_socket = socket(PF_INET, SOCK_STREAM, 0);
	if (server_socket < 0) 
	{
		error_handing("create socket error!");
	}

	int on = 1;
	if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
		close(server_socket);
		error_handing("setsockopt error!");
	}
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(atoi(port));

	if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) 
	{
		error_handing("bind error!");
	}

	if (listen(server_socket, 5) == -1) 
	{
		error_handing("listen error!");
	}

	char* type = "POOL";
	if (argc == 3) {
		type = argv[2];
	}
	//signal(SIGCHLD, handler);//指定SIGCHLD信号来到时，需要被handler函数处理
	signal(SIGCHLD, SIG_IGN); //内核把僵尸子进程转交给init1号进程去处理释放，省去了父进程wait这个子进程的麻烦;

	FD_ZERO(&reads);
	FD_SET(server_socket, &reads);
	fd_max = server_socket;



	//pool
	struct pollfd* fds;
	nfds_t nfds = 1;
	int current_num = 1;

	if (type == "SELECT") {
		while (1) {
			cpy_reds = reads;
			timeout.tv_sec = 5;
			timeout.tv_usec = 50000;

			if ((fd_num = select(fd_max + 1, &cpy_reds, 0, 0, &timeout)) == -1) {
				cout << "select error" << endl;
				break;
			}

			if (fd_num == 0) {
				cout << "fd_num == 0 continue" << endl;
				continue;
			}

			for (i = 0; i < fd_max + 1; i++) {
				if (FD_ISSET(i, &cpy_reds))
				{
					if (i == server_socket) //connection
					{
						client_addr_size = sizeof(clnt_sock);
						clnt_sock = accept(server_socket, (struct sockaddr*)&clnt_sock, (socklen_t*)&client_addr_size);
						if (clnt_sock < 0)
						{
							error_handing("create client socket error!");
						}

						FD_SET(clnt_sock, &cpy_reds);

						if (fd_max < clnt_sock)
						{
							fd_max = clnt_sock;
						}
						cout << "accept clnt_sock: " << clnt_sock << endl;
					} 
					else
					{
						int* copy_client_socket = new int;
						*copy_client_socket = i;
						request_handler((void*)copy_client_socket);
						FD_CLR(i, &cpy_reds);
						close(i);
					}
				}
			}
		}
	}
	else if (type == "POOL") {
		fds = (struct pollfd*)malloc(sizeof(struct pollfd) * 1024);
		fds[0].fd = server_socket;
		fds[0].events = POLLIN;
		int ret = 0;
		while (1) {

			ret = poll(fds, nfds, -1);
			if (ret == -1) {
				cout << "poll error" << endl;
				break;
			}

			if (fds[0].revents & POLLIN) //connection
			{
				client_addr_size = sizeof(clnt_sock);
				clnt_sock = accept(server_socket, (struct sockaddr*)&clnt_sock, (socklen_t*)&client_addr_size);
				if (clnt_sock < 0)
				{
					error_handing("POOL create client socket error!");
				}

				fds[current_num].fd = clnt_sock;
				fds[current_num].events = POLLIN;
				++current_num;
				++nfds;

				cout << "POOL accept clnt_sock: " << clnt_sock << "current_num: " << current_num << endl;
			}

			for (i = 1; i < current_num; ++i)
			{
				if (fds[i].revents & POLLIN) //connection
				{
					int* copy_client_socket = new int;
					*copy_client_socket = fds[i].fd;
					request_handler((void*)copy_client_socket);
					close(fds[i].fd);
					fds[i].fd = -1;
					fds[i].events = 0;
				}
			}
		}
	}
	else if (type == "EPOOL") {
		epfd = epoll_create(EPOOL_SIZE);
		ep_events = (struct epoll_event*)malloc(sizeof(struct epoll_event) * EPOOL_SIZE);

		event.events = EPOLLIN;
		event.data.fd = server_socket;

		epoll_ctl(epfd, EPOLL_CTL_ADD, server_socket, &event);

		while (1) {
			event_cnt = epoll_wait(epfd, ep_events, EPOOL_SIZE, -1);
			if (event_cnt == -1) {
				cout << "select error" << endl;
				break;
			}

			for (i = 0; i < event_cnt; ++i)
			{
				if (ep_events[i].data.fd == server_socket) //connection
				{
					client_addr_size = sizeof(clnt_sock);
					clnt_sock = accept(server_socket, (struct sockaddr*)&clnt_sock, (socklen_t*)&client_addr_size);
					if (clnt_sock < 0)
					{
						error_handing("create client socket error!");
					}

					event.events = EPOLLIN;
					event.data.fd = clnt_sock;
					epoll_ctl(epfd, EPOLL_CTL_ADD, clnt_sock, &event);

					cout << "accept clnt_sock: " << clnt_sock << endl;
				}
				else
				{
					int* copy_client_socket = new int;
					*copy_client_socket = ep_events[i].data.fd;
					request_handler((void*)copy_client_socket);
					epoll_ctl(epfd, EPOLL_CTL_DEL, ep_events[i].data.fd, NULL);
					close(ep_events[i].data.fd);
				}
			}
		}
	}
	else {
		while (1)
		{
			cout << "accept begin...." << endl;
			client_addr_size = sizeof(client_addr);
			int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, (socklen_t*)&client_addr_size);
			if (client_socket < 0)
			{
				error_handing("create client socket error!");
			}
			cout << "accept one client: " << inet_ntoa(client_addr.sin_addr) << ":" << ntohs(client_addr.sin_port) << endl;


			if (type == "P") {
				cout << "fork = " << client_socket << endl;
				pid_t pid = fork();
				if (pid < 0)
				{
					close(server_socket);
					error_handing("fork error!");
				}
				else if (pid == 0)
				{
					close(server_socket);//子进程，关闭server_socket

					int* copy_client_socket = new int;
					*copy_client_socket = client_socket;
					request_handler((void*)copy_client_socket);

					//close(client_socket);
					exit(0);

				}
				else if (pid > 0) //父进程，关闭client_socket
				{
					cout << "fork pid = " << pid << endl;
					close(client_socket);
				}

			}
			else if (type == "THREAD") {
				cout << "pthread_create client_socket = " << client_socket << endl;

				int* copy_client_socket = new int;
				*copy_client_socket = client_socket;
				pthread_create(&t_id, NULL, request_handler, (void*)copy_client_socket);
				pthread_detach(t_id);
			}
		}
	}

	close(server_socket);
	close(epfd);

	return 0;
}


void error_handing(char* message)
{
	fputs(message, stderr);
	fputc('\n', stderr);
	exit(1);
}

void send_error(FILE* fp)
{
	char protocol[] = "HTTP/1.0 400 Bad Request\r\n";
	char server[] = "Server: Linux Web Server \r\n";
	char cnt_len[] = "Content-length:15\r\n";
	char cnt_type[] = "Content-type:text/html\r\n\r\n";
	char content[] = "<h1>Error!</h1>";

	fputs(protocol, fp);
	fputs(server, fp);
	fputs(cnt_len, fp);
	fputs(cnt_type, fp);
	fputs(content, fp);

	fflush(fp);
}

char* content_type(const char* file)
{
	//GET /css/bootstrap.min.css HTTP/1.1

	char extension[SMAlL_BUF];
	char file_name[SMAlL_BUF];
	strcpy(file_name, file);

	vector<string> strList;
	splitStr(file_name, '.', strList);

	strcpy(extension, strList[strList.size() - 1].c_str());

	if (!strcmp(extension, "html") || !strcmp(extension, "htm"))
	{
		return "text/html";
	} 
	else if (!strcmp(extension, "css"))
	{
		return "text/css";
	}
	else if (!strcmp(extension, "js"))
	{
		return "application/javascript";
	}
	else if (!strcmp(extension, "png"))
	{
		return "image/png";
	}
	else if (!strcmp(extension, "jpeg") || !strcmp(extension, "jpg"))
	{
		return "image/jpeg";
	}
	else {
		return "text/plain";
	}
}

void splitStr(char* arr, char separator, vector<string>& strList)
{
	int i = 0;

	// Temporary string used to split the string.
	string s;
	while (arr[i] != '\0')
	{
		if (arr[i] != separator)
		{
			// Append the char to the temp string.
			s += arr[i];
		}
		else
		{
			//cout << s << endl;
			strList.push_back(s);
			s.clear();
		}
		i++;
	}
	//cout << s << endl;
	strList.push_back(s);
}

void* request_handler(void* msg)
{
	int client_socket = *((int*)msg);
	delete msg;
	cout << "client_socket = " << client_socket << endl;

	char req_line[SMAlL_BUF];

	FILE* client_read;
	FILE* client_write;

	char method[10];
	char ct[30];
	char file_name[30];

	client_read = fdopen(client_socket, "r");
	client_write = fdopen(dup(client_socket), "w");
	if (client_write == NULL)
	{
		error_handing("create client_write error!");
	}

	if (client_read == NULL)
	{
		error_handing("create client_read error!");
	}

	fgets(req_line, SMAlL_BUF, client_read);
	
	//cout << req_line << endl;

	//GET /css/bootstrap.min.css HTTP/1.1
	vector<string> strList;
	splitStr(req_line, ' ', strList);

	if (strstr(req_line, "HTTP/") == NULL)
	{
		send_error(client_write);
		fclose(client_read);
		fclose(client_write);
		return NULL;
	}

	strcpy(method, strList[0].c_str());
	strcpy(file_name, strList[1].c_str());
	strcpy(ct, content_type(file_name));

	//cout << file_name << endl;

	if (strcmp(method, "GET") != 0)
	{
		send_error(client_write);
		fclose(client_read);
		fclose(client_write);
		return NULL;
	}

	fclose(client_read);
	send_data(client_write, ct, file_name);

	//pthread_exit(0);
	//close(client_socket);
}

off_t get_file_size(char* file_name)
{
	int ret;
	int fd = -1;
	struct stat file_stat;

	fd = open(file_name, O_RDONLY); // 打开文件
	if (fd == -1) {
		printf("Open file %s failed : %s\n", file_name, strerror(errno));
		return -1;
	}
	ret = fstat(fd, &file_stat);    // 获取文件状态
	if (ret == -1) {
		printf("Get file %s stat failed:%s\n", file_name, strerror(errno));
		close(fd);
		return -1;
	}
	close(fd);
	return file_stat.st_size;
}

void send_data(FILE* fp, char* ct, char* filename)
{
	cout << "send_data " << filename << endl;
	char protocol[] = "HTTP/1.0 200 OK\r\n";
	char server[] = "Server: Linux Web Server \r\n";
	char cnt_len[SMAlL_BUF];
	char cnt_type[SMAlL_BUF];
	char buf[BUF_SIZE];
	FILE* send_file;

	sprintf(cnt_type, "Content-type:%s\r\n\r\n", ct);
	char fullPath[BUF_SIZE] = "/mnt/d/work/code/git/github.com/crudapi/crudapi-website";
	strcat(fullPath, filename);
	long size = get_file_size(fullPath);
	if (size < 0) {
		cout << fullPath << endl;
		send_error(fp);
		fclose(fp);
		return;
	}

	sprintf(cnt_len, "Content-length:%d\r\n", size);

	send_file = fopen(fullPath, "r");
	if (send_file == NULL) {
		cout << fullPath << endl;
		send_error(fp);
		fclose(fp);
		return;
	}

	fputs(protocol, fp);
	fputs(server, fp);
	fputs(cnt_len, fp);
	fputs(cnt_type, fp);


	while (fgets(buf, BUF_SIZE, send_file) != NULL)
	{
		fputs(buf, fp);
		fflush(fp);
	}

	fflush(fp);
	fclose(fp);
}