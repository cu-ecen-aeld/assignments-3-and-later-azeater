#! /bin/sh

case "$1" in
    start)
        echo "Starting aesdsocket"
        start-stop-daemon -S -n aesdsocket -a /bin/aesdsocket -- -d
        if [ $? -ne 0 ]; then
            start-stop-daemon -S -n aesdsocket -a usr/bin/aesdsocket -- -d
        fi
        ;;
    stop)
        echo "Stopping aesdsocket"
        start-stop-daemon -K -n aesdsocket
        ;;
    *)
        echo "Usage: $0 {start|stop}"
    exit 1
esac

exit 0