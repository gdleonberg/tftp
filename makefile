all: tftp.c tftpMulti.c
	gcc tftp.c -o tftp
	gcc -std=gnu99 tftpMulti.c -o tftpMulti

clean:
	rm tftp
	rm tftpMulti
