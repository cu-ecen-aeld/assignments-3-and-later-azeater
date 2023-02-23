#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>

int main(int argc, char * argv[])
{
	openlog(NULL, 0, LOG_USER);

	if (argc < 3)
	{
		syslog(LOG_ERR,"Invalid number of arguments: %d",argc);
		return 1;
	}
	
	char * writestr = argv[2];
	char * writefile = argv[1];

	int fd = open(writefile, O_WRONLY | O_CREAT | O_TRUNC, 0644);

	if (fd == -1)
	{
		syslog(LOG_ERR,"Could not open file at %s", writefile);
		return 1;
	}

	syslog(LOG_DEBUG,"Writing %s to %s", writestr, writefile);

	ssize_t nr = write(fd, writestr, strlen(writestr));

	if (nr == -1)
	{
		syslog(LOG_ERR,"Could not write to file %s: %s", writefile, strerror(nr));
		return 1;
	}
	else if (nr != strlen(writestr))
	{
		syslog(LOG_ERR, "Write to file not completed: %ld of %ld", nr, sizeof(writestr));
		return 1;
	}

	return 0;
}

