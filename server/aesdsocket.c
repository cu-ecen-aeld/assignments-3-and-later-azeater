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
#include <pthread.h>
#include "queue.h"
#include <time.h>

#define BACKLOG 5
#define BUFSIZE 5 * 1024 * 1024
#define OUTPUT_LOCATION "/var/tmp/aesdsocketdata"

bool interrupt_caught = false;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct slist_data_s slist_data_t;
struct slist_data_s {
    int accepted_fd;
	bool completed;
	pthread_t thread;
	pthread_mutex_t * filemutex;
    SLIST_ENTRY(slist_data_s) entries;
};

typedef struct timer_data_s timer_data_t;
struct timer_data_s {
	pthread_mutex_t * filemutex;
};

/** Catches SIGINT and SIGTERM and raises flag **/
static void signal_handler(int signal_number)
{
	if (signal_number == SIGINT || signal_number == SIGTERM)
	{
		interrupt_caught = true;
	}
}

static void timer_thread (union sigval sigval)
{
    timer_data_t * td = (timer_data_t *)sigval.sival_ptr;
	time_t t;
	struct tm * ptm;
	char time_string[50];
	int outfd;

    if (pthread_mutex_lock(td->filemutex) != 0) 
	{
        printf("Error %d (%s) locking thread data!\n",errno,strerror(errno));
    } 
	else 
	{
        outfd = open(OUTPUT_LOCATION, O_RDWR | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
		t = time(NULL);
		ptm = localtime(&t);
		strftime(time_string, sizeof(time_string), "timestamp:%a, %d %b %Y %T %z\n", ptm);
		syslog(LOG_DEBUG, "Writing string %s to file\n", time_string);
		write(outfd, time_string, strlen(time_string));
		close(outfd);
        if (pthread_mutex_unlock(td->filemutex) != 0) 
		{
            printf("Error %d (%s) unlocking thread data!\n",errno,strerror(errno));
        }
    }
}

static void * socket_thread_entry(void * arg)
{
	//cast parameter into appropriate structure
	char ipstr[INET6_ADDRSTRLEN];
	int outfd = 0;
	slist_data_t * params = (slist_data_t *)arg;

	// Read IP address and print to syslog
	struct sockaddr_in addr;
	socklen_t addr_size = sizeof(struct sockaddr_in);
	getpeername(params->accepted_fd, (struct sockaddr *)&addr, &addr_size);
	strcpy(ipstr, inet_ntoa(addr.sin_addr));
	syslog(LOG_INFO, "Accepted connection from %s\n", ipstr);

	char recpacket[BUFSIZE];
	memset(recpacket, 0, BUFSIZE);
	int recvd_bytes = 0;
	int buffer_count = 0;
	char recvd_single;
	do
	{
		// Process incoming byte
		recvd_bytes = recv(params->accepted_fd, &recvd_single, 1, 0);
		// check for buffer overflow
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
			// On completed packet, write to file and read back to client
			syslog(LOG_DEBUG, "Writing string %s to file\n", recpacket);
			//acquire lock for file
			pthread_mutex_lock(params->filemutex);
			// Open temporary output file
			outfd = open(OUTPUT_LOCATION, O_RDWR | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
			write(outfd, recpacket, strlen(recpacket));
			struct stat stbuf;
			stat(OUTPUT_LOCATION, &stbuf);
			long filesize = (long)stbuf.st_size + 1;
			char *return_buffer = malloc(filesize);
			if (return_buffer == NULL)
			{
				syslog(LOG_DEBUG, "Can't allocate enough memory");
			}
			else
			{
				memset(return_buffer, '\0', filesize);
				pread(outfd, return_buffer, filesize, 0);
				send(params->accepted_fd, return_buffer, strlen(return_buffer), 0);
				free(return_buffer);
			}
			//close file
			close(outfd);
			//release lock
			pthread_mutex_unlock(params->filemutex);
			memset(recpacket, 0, BUFSIZE);
			buffer_count = 0;
			recvd_single = '\0';
		}
		else
		{
			// otherwise increment buffer
			buffer_count++;
		}
	} while (recvd_bytes > 0);

	// close socket and print
	close(params->accepted_fd);
	syslog(LOG_INFO, "Closed connection from %s\n", ipstr);
	params->completed = true;
	pthread_exit(NULL);
}

static void timespec_add( struct timespec *result,
                        const struct timespec *ts_1, const struct timespec *ts_2)
{
    result->tv_sec = ts_1->tv_sec + ts_2->tv_sec;
    result->tv_nsec = ts_1->tv_nsec + ts_2->tv_nsec;
    if( result->tv_nsec > 1000000000L ) {
        result->tv_nsec -= 1000000000L;
        result->tv_sec ++;
    }
}

int main(int argc, char *argv[])
{
	openlog("aesdsocket", 0, LOG_USER);

	// checks the daemon option
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
	struct addrinfo hints;
	struct addrinfo *servinfo;
	struct sockaddr_storage client_addr;
	socklen_t addr_size;
	struct sigaction new_action;
	struct sigevent sev;
	timer_t timerid;
	timer_data_t td;

	//Linked list implementation
	slist_data_t *threadp = NULL;
	slist_data_t *threadp_temp = NULL;

	SLIST_HEAD(slisthead, slist_data_s) head;
	SLIST_INIT(&head);

	// Subscribe to SIGTERM and SIGINT signals
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

	//timer setup
	int clock_id = CLOCK_MONOTONIC;
	memset(&sev, 0, sizeof(struct sigevent));
	sev.sigev_notify = SIGEV_THREAD;
	td.filemutex = &mutex;
	sev.sigev_value.sival_ptr = &td;
	sev.sigev_notify_function = timer_thread;

	if (timer_create(clock_id,&sev,&timerid) != 0) 
	{
        printf("Error %d (%s) creating timer!\n",errno,strerror(errno));
    } 
	else 
	{
		struct itimerspec its;
		struct timespec start_time;

		memset(&its, 0, sizeof(struct itimerspec));
		its.it_interval.tv_sec = 10;
		its.it_interval.tv_nsec = 0;
		clock_gettime(clock_id,&start_time);
		timespec_add(&its.it_value,&start_time,&its.it_interval);
		if(timer_settime(timerid, TIMER_ABSTIME, &its, NULL ) != 0) 
		{
            printf("Error %d (%s) setting timer\n",errno,strerror(errno));
        } 
	}

	// setup hints structure
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	// setup address structures
	ret = getaddrinfo(NULL, "9000", &hints, &servinfo);
	if (ret != 0)
	{
		syslog(LOG_ERR, "addrinfo error: %s\n", gai_strerror(ret));
		return -1;
	}

	// create socket
	sockfd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);

	if (sockfd == -1)
	{
		syslog(LOG_ERR, "Could not create socket. Error: %d\n", errno);
		return -1;
	}

	// bind port
	ret = bind(sockfd, servinfo->ai_addr, sizeof(struct sockaddr));

	if (ret != 0)
	{
		syslog(LOG_ERR, "bind error:%s\n", strerror(errno));
		return -1;
	}

	// Free structures after use
	freeaddrinfo(servinfo);

	// Start listening on port
	ret = listen(sockfd, BACKLOG);

	if (ret != 0)
	{
		syslog(LOG_ERR, "listen error:%d\n", ret);
		return -1;
	}

	addr_size = sizeof client_addr;

	// Fork if started as a daemon
	if (daemon)
	{
		if (fork())
			exit(EXIT_SUCCESS);
	}

	while (!interrupt_caught) // Continue to loop until SIGINT or SIGTERM sent
	{
		// Accept incoming connection
		acceptedfd = accept(sockfd, (struct sockaddr *)&client_addr, &addr_size);
		if (acceptedfd == -1)
		{
			syslog(LOG_ERR, "accept error:%s\n", strerror(errno));
			if (interrupt_caught)
			{
				break;
			}
			else
			{
				return -1;
			}
		}
		else
		{
			threadp = malloc(sizeof(slist_data_t));
			threadp->accepted_fd = acceptedfd;
			threadp->completed = false;
			threadp->filemutex = &mutex;
			ret = pthread_create(&threadp->thread, NULL, socket_thread_entry, threadp);
			SLIST_INSERT_HEAD(&head, threadp, entries);
		}

		SLIST_FOREACH_SAFE(threadp, &head, entries, threadp_temp)
		{
			if (threadp->completed)
			{
				pthread_join(threadp->thread, NULL);
				SLIST_REMOVE(&head, threadp, slist_data_s, entries);
				free(threadp);
			}
		}
	}

	// Close all remaining resources and delete file
	syslog(LOG_INFO, "Caught signal, exiting\n");
	if (timer_delete(timerid) != 0) 
	{
		printf("Error %d (%s) deleting timer!\n",errno,strerror(errno));
	}
	close(sockfd);
	remove(OUTPUT_LOCATION);
	exit(EXIT_SUCCESS);
}
