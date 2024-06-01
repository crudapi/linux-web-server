// CLinuxWebServer.cpp: 定义应用程序的入口点。
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

#define BUF_SIZE 1024
#define SMAlL_BUF 100

void* request_handler(void* msg);
void send_data(FILE* fp, char* ct, char* filename);
char* content_type(const char* file);
void send_error(FILE* fp);
void error_handing(char* message);
void splitStr(char* arr, char separator, vector<string>& strList);
off_t get_file_size(char* file_name);

int main(int argc, char* argv[])
{
	cout << "Hello Linux Web Server." << endl;

	int server_socket, client_socket;
	sockaddr_in server_addr, client_addr;
	int client_addr_size;
	char buf[BUF_SIZE];

	pthread_t t_id;

	char* port = "8765";

	if (argc == 2) {
		cout << "Usage:" << argv[0] << " <port>\n";
		port = argv[1];
	}

	server_socket = socket(PF_INET, SOCK_STREAM, 0);

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

	if (listen(server_socket, 32) == -1) 
	{
		error_handing("listen error!");
	}

	while (1) 
	{
		cout << "accept begin...." << endl;
		client_addr_size = sizeof(client_addr);
		client_socket = accept(server_socket, (struct sockaddr*)&client_addr, (socklen_t*)& client_addr_size);
		cout << "accept one client: " << inet_ntoa(client_addr.sin_addr) << ntohs(client_addr.sin_port) << endl;

		pthread_create(&t_id, NULL, request_handler, &client_socket);
		pthread_detach(t_id);
	}

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
	char req_line[SMAlL_BUF];

	FILE* client_read;
	FILE* client_write;

	char method[10];
	char ct[30];
	char file_name[30];

	client_read = fdopen(client_socket, "r");
	client_write = fdopen(dup(client_socket), "w");

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
	char fullPath[BUF_SIZE] = "/mnt/d/work/code/git/github.com/crudapi/crudapi-website/";
	strcat(fullPath, filename);
	long size = get_file_size(fullPath);
	sprintf(cnt_len, "Content-length:%d\r\n", size);

	send_file = fopen(fullPath, "r");
	if (send_file == NULL) {
		cout << fullPath << endl;
		send_error(fp);
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