Very simple terminal and socket text client
===========================


Usage: tty_term [args]

	Where args are:

-  `-S, --socket`         open socket
-  `-d, --dumpfile=arg`   dump data to this file
-  `-e, --eol=arg`        end of line: n (default), r, nr or rn
-  `-h, --help`           show this help
-  `-n, --name=arg`       serial device path or server name/IP
-  `-p, --port=arg`       socket port (none for UNIX)
-  `-s, --speed=arg`      baudrate (default: 9600)
-  `-t, --timeout=arg`    timeout for select() in ms (default: 100)
