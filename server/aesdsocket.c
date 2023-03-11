#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <netdb.h>
#include <syslog.h>
#include <arpa/inet.h>
#include <string.h>
#include <sys/stat.h>
#include <signal.h>
#include <stdbool.h>
#include <fcntl.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <stdio.h>
#include <unistd.h>

#define BACKLOG 5
#define BUFSIZE 5*1024*1024
#define OUTPUT_LOCATION "/var/tmp/aesdsocketdata"

bool interrupt_caught = false;

/** Catches SIGINT and SIGTERM and raises flag **/
static void signal_handler (int signal_number)
{
	if (signal_number == SIGINT || signal_number == SIGTERM)
	{
		interrupt_caught = true;
	}
}

int main(int argc, char * argv[])
{
	openlog("aesdsocket", 0, LOG_USER);

	//checks the daemon option
	bool daemon = false;
    	int opt;
    	while ((opt = getopt(argc, argv, "d")) != -1) 
	{
        	if (opt == 'd')
		{
			daemon = true;
			break;
		}
    	}
	
	int ret = 0;
	int sockfd = 0;
	int acceptedfd = 0;
	int outfd = 0;
	struct addrinfo hints;
	struct addrinfo *servinfo;
	struct sockaddr_storage client_addr;
	socklen_t addr_size;
	char ipstr[INET6_ADDRSTRLEN];
	struct sigaction new_action;
	
	//Subscribe to SIGTERM and SIGINT signals
	memset(&new_action, 0, sizeof(struct sigaction));
	new_action.sa_handler = signal_handler;
	if (sigaction(SIGTERM, &new_action, NULL) != 0)
	{
		perror("sigaction");
		syslog(LOG_ERR, "Could not subscribe to SIGTERM");
	}
	if (sigaction(SIGINT, &new_action, NULL) != 0)
	{
		perror("sigaction");
		syslog(LOG_ERR, "Could not subscribe to SIGINT");
	}

	//setup hints structure
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags  = AI_PASSIVE;


	//create socket
	sockfd = socket(AF_INET, SOCK_STREAM, 0);

	if (sockfd == -1)
	{
		syslog(LOG_ERR, "Could not create socket. Error: %d\n", errno);
		return -1;
	}
	
	//setup address structures
	ret = getaddrinfo(NULL, "9000", &hints, &servinfo);
	if (ret != 0)
	{
		syslog(LOG_ERR, "addrinfo error: %s\n", gai_strerror(ret));
		return -1;
	}	
	
	//bind port
	ret = bind(sockfd, servinfo->ai_addr, sizeof(struct sockaddr));

	if (ret != 0)
	{
		syslog(LOG_ERR, "bind error:%d\n", ret);
		return -1;
	}
	
	//Free structures after use
	freeaddrinfo(servinfo);

	//Start listening on port
	ret = listen(sockfd, BACKLOG);

	if (ret != 0)
	{
		syslog(LOG_ERR, "listen error:%d\n", ret);
		return -1;
	}
	
	addr_size = sizeof client_addr;

	//Fork if started as a daemon
	if(daemon)
	{
        	if(fork()) exit(EXIT_SUCCESS);
    	}

	while (!interrupt_caught) //Continue to loop until SIGINT or SIGTERM sent
	{
		//Accept incoming connection
		acceptedfd = accept(sockfd, (struct sockaddr *)&client_addr , &addr_size);
		if (acceptedfd == -1)
        	{
                	syslog(LOG_ERR, "accept error:%d\n", acceptedfd);
			if (interrupt_caught)
			{
				break;
			}
			else
			{
                		return -1;
			}
        	}
		//Read IP address and print to syslog
		struct sockaddr_in addr;
		socklen_t addr_size = sizeof(struct sockaddr_in);
		getpeername(acceptedfd, (struct sockaddr *)&addr, &addr_size);
		strcpy(ipstr, inet_ntoa(addr.sin_addr));
		syslog(LOG_INFO, "Accepted connection from %s\n", ipstr); 
		
		//Open temporary output file
		outfd = open(OUTPUT_LOCATION, O_RDWR | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

		char recpacket[BUFSIZE];
		memset(recpacket, 0, BUFSIZE);
		int recvd_bytes = 0;
		int buffer_count = 0;
		char recvd_single;
		do
		{
			//Process incoming byte
			recvd_bytes = recv(acceptedfd, &recvd_single, 1, 0);
			//check for buffer overflow
			if (buffer_count >= BUFSIZE)
			{
				syslog(LOG_ERR, "Discarding oversized string\n");
				memset(&recpacket, 0, BUFSIZE);
				buffer_count = 0;
				continue;
			}

			recpacket[buffer_count] = recvd_single;

			if (recvd_single == '\n')
			{
				//On completed packet, write to file and read back to client
				syslog(LOG_DEBUG, "Writing string %s to file\n", recpacket);
				write(outfd, recpacket, strlen(recpacket));
				struct stat stbuf;
				stat(OUTPUT_LOCATION, &stbuf);
				long filesize = (long)stbuf.st_size + 1;
				char * return_buffer = malloc(filesize);
				if (return_buffer == NULL)
				{
					syslog(LOG_DEBUG, "Can't allocate enough memory");
					break;
				}
				memset(return_buffer, '\0', filesize); 
				pread(outfd, return_buffer, filesize, 0);
				send(acceptedfd, return_buffer, strlen(return_buffer), 0);
				free(return_buffer);
				memset(recpacket, 0, BUFSIZE);
				buffer_count = 0;
				recvd_single = '\0';
			}
			else
			{
				//otherwise increment buffer
				buffer_count++;
			}
		}
		while (recvd_bytes > 0);

		//close socket and print
		close(acceptedfd);
		syslog(LOG_INFO, "Closed connection from %s\n", ipstr);
		close(outfd);
	}

	//Close all remaining resources and delete file
	syslog(LOG_INFO, "Caught signal, exiting\n");
	close(sockfd);
	close(outfd);
	remove(OUTPUT_LOCATION);
	exit(EXIT_SUCCESS);
}
