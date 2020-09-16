#ifndef UDP_SOCKETS_H
#define UDP_SOCKETS_H

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

namespace UDP{
struct NetworkSocket{
	int socket_fd;
	
	explicit NetworkSocket(int fd):socket_fd(fd){}
	
	NetworkSocket(NetworkSocket&& other){
		socket_fd=other.socket_fd;
		other.socket_fd=-1;
	}
	
	NetworkSocket& operator=(NetworkSocket&& other){
		if(&other!=this){
			socket_fd=other.socket_fd;
			other.socket_fd=-1;
		}
		return *this;
	}
	
	~NetworkSocket(){
		if(socket_fd!=-1)
			close(socket_fd);
	}
};

struct NetworkInputSocket : public NetworkSocket{
	explicit NetworkInputSocket(int port):NetworkSocket(-1){
		int err;
		socket_fd=socket(PF_INET, SOCK_DGRAM, 0);
		if(socket_fd==-1){
			perror("socket() failed");
			exit(1);
		}
		int enable=1;
		err=setsockopt(socket_fd,SOL_SOCKET,SO_REUSEADDR,&enable,sizeof(enable));
		if(err==-1){
			perror("setsockopt() failed");
			exit(1);
		}
		
		{ //Set SO_RCVTIMEO to prevent recvfrom from blocking forever when there is no data
			struct timeval timeout;
			timeout.tv_sec = 0;
			timeout.tv_usec = 10000;
			err=setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
			if(err==-1){
				perror("setsockopt() failed");
				exit(1);
			}
		}
		
		struct sockaddr_in address;
		memset(&address, 0, sizeof(address));
		address.sin_family = AF_INET;
		address.sin_port = htons(port);
		address.sin_addr.s_addr = htonl(INADDR_ANY);
		err=bind(socket_fd, (const struct sockaddr*)&address, sizeof(address));
		if(err==-1){
			perror("bind() failed");
			exit(1);
		}
	}
};

struct NetworkOutputSocket : public NetworkSocket{
public:
	addrinfo* sock_addr;
private:
	addrinfo* raw_addr;
public:
	NetworkOutputSocket(const std::string& hostName, const std::string& serviceName):
	NetworkSocket(-1),sock_addr(nullptr),raw_addr(nullptr){
		struct addrinfo hints;
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_DGRAM;
		
		int err;
		err=getaddrinfo(hostName.c_str(), serviceName.c_str(), &hints, &raw_addr);
		if(err){
			std::cerr << "getaddrinfo failed: " << gai_strerror(errno) << std::endl;
			exit(1);
		}
		if(!raw_addr){
			std::cerr << "getaddrinfo() gave no results" << std::endl;
			exit(1);
		}
		for(sock_addr=raw_addr; sock_addr; sock_addr=sock_addr->ai_next){
			socket_fd=socket(sock_addr->ai_family,sock_addr->ai_socktype,sock_addr->ai_protocol);
			if(socket_fd!=-1)
				break;
		}
		if(socket_fd==-1){
			freeaddrinfo(raw_addr);
			perror("socket() failed");
			exit(1);
		}
	}
	
	NetworkOutputSocket(NetworkOutputSocket&& other):
	NetworkSocket(static_cast<NetworkSocket&&>(other)){
		sock_addr=other.sock_addr;
		other.sock_addr=nullptr;
		raw_addr=other.raw_addr;
		other.raw_addr=nullptr;
	}
	
	~NetworkOutputSocket(){
		if(raw_addr)
			freeaddrinfo(raw_addr);
	}
};
}

#endif //UDP_SOCKETS_H
