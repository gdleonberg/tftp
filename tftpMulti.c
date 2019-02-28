//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <strings.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/unistd.h>
#include <sys/queue.h>
#include <arpa/inet.h>
#include <sys/select.h>

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

#define PROGNAME "./tftp"
#define PACKET_SIZE 516
#define RRQ 1
#define WRQ 2
#define DATA 3
#define ACK 4
#define ERROR 5
#define FILENOTFOUND 1

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

typedef struct reqPacket reqPacket;
struct reqPacket
{
	uint16_t opcode;
	char* fileName;
	char* mode;
};

typedef struct clientList clientList;
typedef struct client client;

struct clientList
{
	client* nextNode;
};

struct client
{
	client* nextNode;
	FILE* myFile;
	int s2;
	int prevBytes;
	int currBytes;
	int blockNum;
	struct sockaddr_in clientaddr;
	int clientaddrSize;
};

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

int sendPacket(client* currClient, char* buf, int bufLen);
void parseReq(char*, reqPacket*);
void buildErr(char *buf);

void append(clientList* l, FILE* myFile, int s2, struct sockaddr_in info);
void rem(clientList* l, int s2);
client* get(clientList* l, int s2);
int exists(clientList *l, int s2);
int length(clientList *l);
client* getI(clientList *l, int index);
void list_init(clientList *l);

void err()
{
	perror(PROGNAME);
	exit(1);
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

int main(int argc, char* argv[])
{
	// variables needed
	char buf[PACKET_SIZE];
	clientList clients;
	list_init(&clients);
	fd_set master, read_fds;
	FILE* myFile;
	int s = 0, s2 = 0, fdMax = 0;
	
	// check arguments
	if(argc < 2)
	{
		printf("Not enough arguments!\n");
		exit(0);
	}
	
	// socket
	if( (s = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
		err();
	
	// select setup
	fdMax = s+1;
	FD_ZERO(&master);
	FD_ZERO(&read_fds);
	FD_SET(s, &master);
	FD_SET(s, &read_fds);

	// bind setup
	struct sockaddr_in servaddr;
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(atoi(argv[1]));
	
	append(&clients, NULL, s, servaddr);

	// bind
	if( bind(s, (struct sockaddr*) &servaddr, sizeof(servaddr)) < 0)
		err();
	append(&clients, NULL, s, servaddr);
	
	while( select(fdMax, &read_fds, NULL, NULL, NULL ) )
	{	
		// select random socket that's ready
		int temp = 0;
		while(temp == 0)
		{
			int temp2 = length(&clients);
			int i = (random() % temp2) - 1;
			client* curr = getI(&clients, i);
			int fd = curr->s2;
			
			if( FD_ISSET(fd, &read_fds) )
			{
				s2 = fd;
				break;
			}
		}
		
		printf("\n\nCurrently handling socket %d\n", s2);
		
		// read from socket
		struct sockaddr_in clientaddr;
		int clientaddrSize = sizeof(clientaddr);
		if(recvfrom(s2, buf, sizeof(buf), 0, (struct sockaddr *)&clientaddr, &clientaddrSize) < 0)
			err();
		
		// if on master socket, handle request
		if(s2 == s)
		{
			// parse request
			reqPacket req;
			parseReq(buf, &req);

			// get client IP info
			char* clientIP = inet_ntoa(clientaddr.sin_addr);

			// print request and IP
			char* op = (req.opcode == RRQ) ? "RRQ" : "WRQ";
			printf("%s %s %s from %s:%s\n", op, req.fileName, req.mode, clientIP, argv[1]);

			// file not found
			if((myFile = fopen(req.fileName, "r")) == NULL) // file not found, send error and finish
			{
				// construct error message
				bzero(&buf, sizeof(buf));
				buildErr(buf);

				// write error message
				if(sendto(s2, buf, sizeof(buf), 0, (struct sockaddr*)&clientaddr, clientaddrSize) < 0)
					err();
			}
			
			// file found
			else
			{
				// make second random port socket for data transfer
				if((s2 = socket(AF_INET, SOCK_DGRAM, 0)) < 0) 
					err();
				
				// add connection info to list of connections
				append(&clients, myFile, s2, clientaddr);
				FD_SET(s2, &master);
				
				// update fdMax
				if((s2 + 1) > fdMax)
					fdMax = s2 + 1;
				
				// send first packet, if that was it for the file rem connection
				if(sendPacket(get(&clients, s2), buf, sizeof(buf)) < 516)
				{
					FD_CLR(s2, &master);
					rem(&clients, s2);
					close(s2);
				}
				
			}
		}

		// otherwise it's on a new socket we created, time to deal with data and acks
		else
		{
			printf("Already existing client\n");
			client* currClient = get(&clients, s2);
			
			int temp = 0;
			if( (temp = sendPacket(currClient, buf, sizeof(buf))) < 516)
			{
				FD_CLR(currClient->s2, &master);
				rem(&clients, currClient->s2);
				close(currClient->s2);
			}
			
		}
	
		// refresh list of socket fds to select from
		read_fds = master;
	}

}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

void buildErr(char *buf)
{
	buf[0] = ERROR >> 8;
	buf[1] = ERROR;
	buf[2] = FILENOTFOUND >> 8;
	buf[3] = FILENOTFOUND;
	strcpy(buf+4, "File not found.");
	return;
}

void parseReq(char* buf, reqPacket* packet)
{
	packet->opcode = ((uint16_t)buf[0] << 8) |( uint16_t)buf[1];
	packet->fileName = buf + 2;
	packet->mode = buf + 2 + strlen(packet->fileName) + 1;
	return;
}

int sendPacket(client* currClient, char* buf, int bufLen)
{
	int numBytes = 4;
	
	for(int i = 0; i < bufLen; i++)
		buf[i] = 0;
	
	numBytes += fread(buf+4, 1, 512, currClient->myFile);
	buf[0] = DATA >> 8;
	buf[1] = DATA;
	buf[2] = currClient->blockNum >> 8;
	buf[3] = currClient->blockNum;
	
	printf("%d %d %d\n", currClient->s2, currClient->blockNum, numBytes);
	
	if(sendto(currClient->s2, buf, numBytes, 0, (struct sockaddr*)(&(currClient->clientaddr)), currClient->clientaddrSize) < 0)
		err();
	
	currClient->blockNum++;
	currClient->prevBytes = currClient->currBytes;
	currClient->currBytes = numBytes;
	
	for(int i = 0; i < bufLen; i++)
		buf[i] = 0;
	return numBytes;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

void append(clientList* l, FILE* myFile, int s2, struct sockaddr_in info)
{
	client *temp = l->nextNode, *prev = l->nextNode;
	while(temp != NULL)
	{
		prev = temp;
		temp = temp->nextNode;
	}
	
	if(temp == prev)
	{
		temp = malloc(sizeof(client));
		l->nextNode = temp;
	}
	else
	{
		temp = malloc(sizeof(client));
		prev->nextNode = temp;
	}
	
	temp->nextNode = NULL;
	temp->myFile = myFile;
	temp->s2 = s2;
	temp->clientaddr = info;
	temp->clientaddrSize = sizeof(info);
	temp->prevBytes = 0;
	temp->currBytes = 0;
	temp->blockNum = 1;
}

void rem(clientList* l, int s2)
{
	client *curr = l->nextNode, *prev;
	
	while(curr->s2 != s2)
	{
		prev = curr;
		curr = curr->nextNode;
	}
	
	prev->nextNode = NULL;
	free(curr);
}

client* get(clientList* l, int s2)
{
	client *curr = l->nextNode;
	
	while(curr->s2 != s2)
	{	curr = curr->nextNode;	}
	
	return curr;
}

int exists(clientList *l, int s2)
{
	int flag = 0;
	client* curr = l->nextNode;
	
	while(curr != NULL)
	{
		if(curr->s2 == s2)
			flag = 1;
		curr = curr->nextNode;
	}
	
	return flag;
}

int length(clientList *l)
{
	int ret = 1;
	client* curr = l->nextNode;
	
	while(curr != NULL)
	{
		ret++;
		curr = curr->nextNode;
	}
	
	return ret;
}

client* getI(clientList *l, int index)
{
	int i = 0;
	client* curr = l->nextNode;
	
	for(i = 0; i < index; i++)
	{	curr = curr->nextNode;	}
	
	return curr;
}

void list_init(clientList *l)
{
	l->nextNode = NULL;
}
