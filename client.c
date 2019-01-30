#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <netdb.h>
#define MAXBUF 1024
#define FRAMESIZE 256
typedef int bool;
enum{false, true};

struct frame {
	int kind;
	int seq;
	int ack;
	char info[FRAMESIZE];
};

int main(int argc, char* argv[]){
	int udpSocket;
	int returnStatus;
	int addrlen;
	struct sockaddr_in udpClient, udpServer;
	char buf1[MAXBUF];
	char buf2[MAXBUF];
	int windowSize = 1;
	int maxSeq;
	int currSeq;
	FILE *fp;
	
	if (argc < 5){
		fprintf(stderr, "Usage: %s <ip address> <port> <input_file> <output_file>\n", argv[0]);
		exit(1);
	}
	
	/* create a socket */
	udpSocket = socket(AF_INET, SOCK_DGRAM, 0);
	if(udpSocket == -1){
		fprintf(stderr, "Could not create a socket!\n");
		exit(1);
	}else{
		printf("Socket created!\n");
	}
	
	/* client address, use INADDR_ANY to use all local addresses */
	udpClient.sin_family = AF_INET;
	udpClient.sin_addr.s_addr = INADDR_ANY;
	udpClient.sin_port = 0;
	
	returnStatus = bind(udpSocket, (struct sockaddr*)&udpClient, sizeof(udpClient));
	if(returnStatus == 0){
		fprintf(stderr, "Bind completed!\n");
	}else{
		fprintf(stderr, "Could not bind to address!\n");
		close(udpSocket);
		exit(1);
	}
	printf("--------------------------------------\n");

	
	/* server address, use the command-line arguments */
	udpServer.sin_family = AF_INET;
	udpServer.sin_addr.s_addr = inet_addr(argv[1]);
	udpServer.sin_port = htons(atoi(argv[2]));
	addrlen = sizeof(udpServer);
	
	// read username and password
	while(true){
		printf("Please enter username: ");
		scanf("%s", buf1);

		printf("Please enter password: ");
		scanf("%s", buf2);
		
		strcat(buf1, "$");
		strcat(buf1, buf2);	

		// send username and password
		returnStatus = sendto(udpSocket, buf1, strlen(buf1), 0, (struct sockaddr*)&udpServer, sizeof(udpServer));
		if(returnStatus == -1){
			fprintf(stderr, "couldn't send username and password!\n");
		}else{
			printf("username and password sent!\n");
			bzero(buf1, MAXBUF);
			returnStatus = recvfrom(udpSocket, buf1, MAXBUF, 0, (struct sockaddr*)&udpServer, &addrlen);
			if(returnStatus == -1){
				fprintf(stderr, "couldn't receive confirmation: username and password!\n");
			}else{
				if(strcmp(buf1, "positive authenticate") == 0){
					printf("login succeeded!\n");
					break;
				}else if(strcmp(buf1, "negative authenticate") == 0){
					printf("username/password did not match, please try again\n\n");
				}
			}
		}
	}
	
	while(true){
		int flag = 1;
		printf("start to transfer file? (Y/N): ");
		scanf("%s", buf1);
		if(strcmp(buf1, "Y") == 0){
			printf("file name: %s\n", argv[3]);

			returnStatus = sendto(udpSocket, argv[3], strlen(argv[3]), 0, (struct sockaddr*)&udpServer, sizeof(udpServer));
			if(returnStatus == -1){
				fprintf(stderr, "couldn't send file name!\n");
			}else{
				printf("sent filename to server!\n");
				bzero(buf1, MAXBUF);
				bzero(buf2, MAXBUF);
				returnStatus = recvfrom(udpSocket, buf1, MAXBUF, 0, (struct sockaddr*)&udpServer, &addrlen);
				if(returnStatus == -1){
					fprintf(stderr, "couldn't receive input file name!\n");
				}else{
					if(strcmp(buf1, "positive for file open") == 0){
						printf("file name verified!\n");

						// create output file
						fp = fopen(argv[4], "wb+");
						if(fp == NULL){
							printf("failed to create output file\n");
						}else{
							printf("output file created!\n");
						}
						break;
					}else if(strcmp(buf1, "negative for file open") == 0){
						printf("Invalid file name, file does not exist or permission denied, please try again\n\n");
						flag = 0;
					}
				}
			}
		}else{
			return 0;
		}
	}

	// get window size
	returnStatus = recvfrom(udpSocket, &windowSize, MAXBUF, 0, (struct sockaddr*)&udpServer, &addrlen);
	windowSize = windowSize - 48; // only works from 0 to 9
	printf("received window size: %d!\n", windowSize);
	
	// get total frame numbers
	int totalFrameNo = 0;
	returnStatus = recvfrom(udpSocket, &totalFrameNo, MAXBUF, 0, (struct sockaddr*)&udpServer, &addrlen);
	printf("received total frame number: %d\n", totalFrameNo);

	int ackList[windowSize];
	memset(ackList, 0, windowSize*sizeof(int));
	struct frame frameList[windowSize];
	memset(frameList, 0, windowSize*sizeof(struct frame));
	
	int count = 0;
	for(int i = 0; i < totalFrameNo; i++){ //only fit when window size = 1
		for(int j = 0; j < windowSize; j++){
			ackList[j] = 0;
		}

		while(count < windowSize){
			// receive frame from server
			struct frame recvFrame;
			returnStatus = recvfrom(udpSocket, &recvFrame, sizeof(recvFrame), 0, (struct sockaddr*)&udpServer, &addrlen);
			if(recvFrame.seq >= 0 && recvFrame.seq < windowSize && ackList[recvFrame.seq] == 0){
				frameList[recvFrame.seq] = recvFrame;
				printf("sequence number: %d\n", recvFrame.seq);
				if(recvFrame.kind == 0){
					printf("type: transmission\n");
				}else{
					printf("type: retransmission\n");
				}
				
				ackList[recvFrame.seq] = 1; // store the ack for each frame in ackList 
				count++;
			}else{
				printf("error for receiving frame!\n");	
			}
		}

		// ask for retransmission if necessary
		if(count != windowSize){
			strcpy(buf1, "negative transfer");
			returnStatus = sendto(udpSocket, buf1, strlen(buf1), 0, (struct sockaddr*)&udpServer, sizeof(udpServer));
			returnStatus = recvfrom(udpSocket, buf1, MAXBUF, 0, (struct sockaddr*)&udpServer, &addrlen);
			if(strcmp(buf1, "retransmission ready") == 0){
				printf("retransmission: \n");
			}else{
				printf("error for receiving message\n");
			}

			//send sequence number that needs retransmission
			for(int j = 0; j < windowSize; j++){
				if(ackList[j] == 0){
					returnStatus = sendto(udpSocket, &j, sizeof(int), 0, (struct sockaddr*)&udpServer, sizeof(udpServer));
					returnStatus = recvfrom(udpSocket, &frameList[j], sizeof(frameList[i]), 0, (struct sockaddr*)&udpServer, &addrlen);//get retransmission frame
				}
			}
		}else{
			strcpy(buf1, "positive transfer");
			returnStatus = sendto(udpSocket, buf1, strlen(buf1), 0, (struct sockaddr*)&udpServer, sizeof(udpServer));

			// write to file in sequence order 
			for(int j = 0; j < windowSize; j++){
				fwrite(&recvFrame.info, strlen(frameList[j].info), 1, fp);
			}
			printf("a window transfer done!\n");
			count = 0;
		}
	}	
	
	fclose(fp);
	printf("file transfer done!\n");
	
	/* cleanup */
	close(udpSocket);
	return 0;
}


