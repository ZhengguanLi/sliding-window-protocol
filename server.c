#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <netdb.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#define MAXBUF 1024
#define FRAMESIZE 256
typedef int bool;
enum{false, true};
struct argsThread{
	int seq;
	int* ackList;
};

struct frame{
	int kind; // 0: transmission, 1: retransmission
	int seq;
	int ack;
	char info[FRAMESIZE];
};

char* usernames[6];
char* passwords[6];
void* timer(void* arg);
void getUsernamePassword(char username[], char password[], char buffer[]);
void loginServer();
int authenticate(char username[], char password[]);
int getFileSize(char* filename);
  
int main(int argc, char* argv[]){
	int udpSocket;
	int returnStatus = 0;
	int addrlen = 0;
	struct sockaddr_in udpServer, udpClient;
	char buf1[MAXBUF] = {0};
	char buf2[MAXBUF] = {0};
	char buffer[MAXBUF] = {0};	
	char filename[MAXBUF] = {0};
	int totalFrameNo;
	int remainder;
	int retransNo;
	int ackRecvNo;
	int nackRecvNo;

	/* check for the right number of arguments */
	if (argc < 3) {
		fprintf(stderr, "Usage: %s <port> <window size>\n", argv[0]);
		exit(1);
	}

	/* create a socket */
	udpSocket = socket(AF_INET, SOCK_DGRAM, 0);
	if (udpSocket == -1){
		fprintf(stderr, "Could not create a socket!\n");
		exit(1);
	}else{
		printf("Socket created!\n");
	}
	
	/* set up the server address and port, use INADDR_ANY to bind to all local addresses */
	/* use the port passed as argument */
	udpServer.sin_family = AF_INET;
	udpServer.sin_port = htons(atoi(argv[1]));

	/* bind to the socket */
	returnStatus = bind(udpSocket, (struct sockaddr*)&udpServer, sizeof(udpServer));
	if (returnStatus == 0) {
		fprintf(stderr, "Bind completed!\n");
	}else{
		fprintf(stderr, "Could not bind to address!\n");
		close(udpSocket);
		exit(1);
	}
	printf("--------------------------------------\n");
	
	// read usernames and passwords
	FILE* fp = fopen("userList.txt", "r");
	if(fp == NULL){
		fprintf(stderr, "can't open the file!\n");
	}

	for(int i = 0; i < 6;i++){
		usernames[i] = (char*)malloc(256 * sizeof(char));
		passwords[i] = (char*)malloc(256 * sizeof(char));
	}

	for(int i = 0; i < 6; i++){
		for(int j = 0; j < 2; j++){
			fscanf(fp, "%s", buffer);
			if(j % 2 == 0){
				strcpy(usernames[i], buffer);
			}else{
				strcpy(passwords[i], buffer);
			}
		}
	}
	fclose(fp);

	loginServer();

	while(true){
		char username[MAXBUF] = {0};
		char password[MAXBUF] = {0};
		addrlen = sizeof(udpClient);
		bzero(buf1, MAXBUF);
		returnStatus = recvfrom(udpSocket, buf1, MAXBUF, 0, (struct sockaddr*)&udpClient, &addrlen);
		getUsernamePassword(username, password, buf1);
		printf("received username and password from client!\n");

		int flag = authenticate(username, password);
		if(flag == 0){
			printf("a client connected, ");
			strcpy(buf1, "positive authenticate");
			returnStatus = sendto(udpSocket, buf1, strlen(buf1)+1, 0, (struct sockaddr*)&udpClient, sizeof(udpClient));
			if(returnStatus == -1){
				printf("couldn't send positve login message back, %d!\n", returnStatus);
			}else{
				printf("sent positive login message back!\n");
				break;
			}
		}else{
			printf("username/password did not match!\n");
			strcpy(buf1, "negative authenticate");
			returnStatus = sendto(udpSocket, buf1, strlen(buf1)+1, 0, (struct sockaddr*)&udpClient,sizeof(udpClient));
			printf("sent negative login message\n");
		}
	}
	
	// read file
	while(true){
		bzero(filename, MAXBUF);
		returnStatus = recvfrom(udpSocket, filename, MAXBUF, 0, (struct sockaddr*)&udpClient, &addrlen);
		printf("received file name from client: %s!\n", filename);
		
		fp = fopen(filename, "r");
		if(fp == NULL){
			printf("fopen failed!\n");
			strcpy(buf1, "negative for file open");
			returnStatus = sendto(udpSocket, buf1, strlen(buf1), 0, (struct sockaddr*)&udpClient,sizeof(udpClient));
			if(returnStatus > 0){
				printf("fopen failed message sent!\n");
			}else{
				printf("failed to send file open failure message!\n");
			}
		}else{
			printf("file opened, ");			 	
			strcpy(buf1, "positive for file open");
			returnStatus = sendto(udpSocket, buf1, strlen(buf1), 0, (struct sockaddr*)&udpClient,sizeof(udpClient));
			if(returnStatus > 0){
				printf("file opened message sent back!\n");
				break;
			}else{
				printf("failed to send file open success message back!\n");
			}
		}
	}
	
	// send window size
	int windowSize = argv[2][0] - 48; // string to integer
	returnStatus = sendto(udpSocket, argv[2], strlen(argv[2]), 0, (struct sockaddr*)&udpClient,sizeof(udpClient));
	printf("sent window size to client: %d\n", windowSize);
	
	// get total frame numbers
	int fileSize = getFileSize(filename);
	printf("file size: %dKB!\n", fileSize);

	totalFrameNo = fileSize / FRAMESIZE;
	remainder = fileSize % FRAMESIZE;
	if(remainder != 0){
		totalFrameNo++;
	}
	
	// send total frame number to client
	returnStatus = sendto(udpSocket, &totalFrameNo, strlen(argv[2]), 0, (struct sockaddr*)&udpClient,sizeof(udpClient));
	printf("sent total frame number to client: %d\n", totalFrameNo);

	int ackList[windowSize];
	memset(ackList, 0, windowSize * sizeof(int));
	ackRecvNo = totalFrameNo;

	for(int i = 0; i < totalFrameNo; i++){ // need to improve, totalFramNo here just fit when windowsize = 1
		struct frame frameList[windowSize]; // string to int, just use 1 instead
		//set ackList be 1, when receive ack from client, it will be set to 0
		for(int j = 0; j < windowSize; j++){
			ackList[j] = 0;
		}

		for(int j = 0; j < windowSize; j++){
			fread(buffer, 256, 1, fp);
			struct frame currFrame;
			currFrame.kind = 0;
			currFrame.seq = j;
			currFrame.ack = 1;
			bzero(currFrame.info, FRAMESIZE);
			memcpy(currFrame.info, buffer, 256);
						
			frameList[j] = currFrame;
			returnStatus = sendto(udpSocket, &currFrame, sizeof(currFrame), 0, (struct sockaddr*)&udpClient, sizeof(udpClient));
			
			// pthread_t thread;
			// set arguments of thread
			// struct argsThread args;
			// args.seq = 0;
			// args.ackList = ackList;
	
			// int s = pthread_create(&thread, NULL, timer, &args);
			
			// // receive frame from client
			// returnStatus = recvfrom(udpSocket, &currFrame, sizeof(currFrame), 0, (struct sockaddr*)&udpClient,sizeof(udpClient));
			// //if the frame is the ack frame, then set ackList to 1
			// if(currFrame.ack == 0){
			// 	ackList[currFrame.seq] = 1;
			// }
			// else{

			// }
			
			printf("one frame transfer done!\n");			
		}
	
		// check if need retransmission, ack
		bzero(buf1, MAXBUF);
		returnStatus = recvfrom(udpSocket, buf1, MAXBUF, 0, (struct sockaddr*)&udpClient, &addrlen);
		if(strcmp(buf1, "positive transfer") == 0){
			printf("a window transfer done!\n");
		}else{
			// retransmission
			printf("retransmission!\n");
			strcpy(buffer, "retransmission ready");
			returnStatus = sendto(udpSocket, buffer, strlen(buffer), 0, (struct	sockaddr*)&udpClient, sizeof(udpClient));
			int retransSeq = 0;
			retransNo = 0;
			retransNo = atoi(buffer); // retransmission number
			printf("Need %d retransmission\n\n", retransNo);
			for(int j = 0; j < retransNo; j++){
				// receive sequence number of frame need to retransmit
				returnStatus = recvfrom(udpSocket, &retransSeq, MAXBUF, 0, (struct sockaddr*)&udpClient, &addrlen);
				retransSeq = ntohs(retransSeq);

				// construct retransmission frame
				struct frame retransFrame;
				retransFrame = frameList[retransSeq];// copy the frame to retransFrame
				retransFrame.kind = 1;//set kind from transmission 0 to retransmission 1
				returnStatus = sendto(udpSocket, &retransFrame, sizeof(retransFrame), 0, (struct	sockaddr*)&udpClient,sizeof(udpClient));
			}
		}
		
	}
	fclose(fp);

	// get file creation time
	struct stat t_stat;
	stat(filename, &t_stat);
	struct tm * timeinfo = localtime(&t_stat.st_ctime); // or gmtime()

	// get ip address and port number
	char *ip = inet_ntoa(udpClient.sin_addr);
	int port = ntohs(udpClient.sin_port);

	printf("IP address: %s\n", ip);
	printf("Port number: %d\n", port);
	printf("File name: %s\n", filename);
	printf("File size: %d\n", fileSize);	
	printf("File time and date: %s", asctime(timeinfo));
	printf("Total frame number transmitted: %d\n", totalFrameNo);
	printf("Number of packets retransmitted: %d\n", retransNo);
	printf("Number of Acknowledgement Received: %d\n", ackRecvNo);
	printf("Number of Negative Acknowledgement Received with Sequence Number:%d\n", nackRecvNo);

	/* cleanup */
	close(udpSocket);
	return 0;
}

void loginServer(){
	char username[MAXBUF] = {0};
	char password[MAXBUF] = {0};
	while(true){
		// read username and password
		printf("Please enter username: ");
		scanf("%s", username);

		printf("Please enter password: ");
		scanf("%s", password);

		int flag = authenticate(username, password);
		if(flag == 0){
			printf("login succeeded, waiting for file request...\n");
			break;
		}else{
			printf("username/password did not match!\n");
		}
	}
}

void getUsernamePassword(char username[], char password[], char buffer[]){
	// read username
	int len = strlen(buffer);
	int start = 0;
	for(int i = 0;i < len;i++){
		if(buffer[i] != '$'){
			username[i] = buffer[i];
		}else{
			start = i + 1;
			break;
		}
	}

	// read password
	for(int i = start; i < len; i++){
		password[i - start] = buffer[i];
	}
}

int authenticate(char username[], char password[]){
	int index = -1;//-1 means no matching username;
	for(int i = 0; i < 6; i++){
		if(strcmp(usernames[i],username) == 0 && strcmp(passwords[i], password) == 0){
			return 0;				
		}
	}
	return -1;
}

void* timer(void* arg){
	struct argsThread args;
	args = *(struct argsThread*)arg;
	int seq = args.seq;
	sleep(0.00001);
	if(args.ackList[seq] == 1){
		printf("Time out!\n");
	}
}

int getFileSize(char* filename){  
    struct stat statbuf;  
    stat(filename,&statbuf);  
    int size=statbuf.st_size;  
    return size;  
}