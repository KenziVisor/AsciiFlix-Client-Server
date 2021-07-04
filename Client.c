#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <pthread.h>

#define DELAY 300000//in micro seconds
#define Buffer_size 1024
#define STAY 1

typedef struct Data{
	uint16_t numStations;
	uint32_t multicastGroup;
	uint32_t currentMcastGroup;
	uint16_t portNumber;
	uint8_t row;
	uint8_t col;
	uint16_t premiumPort;
	uint16_t currentStation;
}Data;

void* streamTV(void* parm);
void printMenu();
void printPremiumMenu();

//global variables
Data data;
int isPremium = 0;
int canStream = 0;
int udp_sock;
int control_sock, premium_sock;
char* frame = NULL;
struct sockaddr_in mcast_addr;
int addrlen = sizeof(mcast_addr);
int TStoped = 1;

int main(int argc, char* argv[]){
	long server_addr_num = inet_addr(argv[1]);//get the address of the server
	int server_port = htons(atoi(argv[2]));//get the port of the server
	int opt = 1;
	int i;
	int user_input;
	struct sockaddr_in server_addr;
	struct ip_mreq mreq;
	char* printMcastAddr;
	pthread_t stream;
	fd_set fdset;
	uint8_t input_buffer[Buffer_size];
	uint8_t message[3];
	uint8_t speed;
	uint16_t printPort;
	uint16_t next_parm;
	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = DELAY;
	//open tcp socket and connect directly to the server
	if ((control_sock = socket(AF_INET, SOCK_STREAM, 0))==0){
		perror("could not open a socket");
		exit(EXIT_FAILURE);
	}
	//set control socket to reuse addresses
	if ((setsockopt(control_sock,SOL_SOCKET,SO_REUSEADDR | SO_REUSEPORT,&opt, sizeof(opt)))==-1){
		perror("could not set reuse");
		close(control_sock);
		exit(EXIT_FAILURE);
	}
	//set control sockets timeout
	if ((setsockopt(control_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv)))==-1){
		perror("there is a problem in the set of the timeout");
		close(control_sock);
		exit(EXIT_FAILURE);
	}
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = server_addr_num;
	server_addr.sin_port = server_port;
	//bind address to socket
	if ((connect(control_sock,(struct sockaddr*)&server_addr,addrlen))==-1){
		perror("could not connect to the server");
		close(control_sock);
		exit(EXIT_FAILURE);
	}
	//build and send hello message
	message[0] = 0;
	next_parm = 0;
	*(uint16_t*)&message[1] = htons(next_parm);
	if ((send(control_sock,message,3,0))!=3){
		perror("something wrong with sending hello");
		close(control_sock);
		exit(EXIT_FAILURE);
	}
	//wait for welcome message
	if ((recv(control_sock,input_buffer,Buffer_size,0))==-1){
		perror("problem with getting welcome from the server");
		close(control_sock);
		exit(EXIT_FAILURE);
	}
	//if we got invalid command
	if (input_buffer[0] == 4){
		for (i = 2; i<input_buffer[1]+2; i++)
			printf("%c",input_buffer[i]);
		printf("\n");
		close(control_sock);
		exit(EXIT_FAILURE);
	}
	//if we did not get wellcome message
	if (input_buffer[0] != 0){
		perror("somthing strange with the server. should have sent welcome");
		close(control_sock);
		exit(EXIT_FAILURE);
	}
	//recieve welcome information
	data.numStations = ntohs(*(uint16_t*)&input_buffer[1]);
	data.multicastGroup = htonl(*(uint32_t*)&input_buffer[3]);
	data.currentMcastGroup = data.multicastGroup;
	data.portNumber = *(uint16_t*)&input_buffer[7];
	data.row = input_buffer[9];
	data.col = input_buffer[10];
	//set frame
	if((frame = (char*)malloc(sizeof(char)*data.col*data.row))==NULL){
		perror("Memory allocation error");
		close(control_sock);
		exit(EXIT_FAILURE);
	}
	data.currentStation = 0;
	printPort = ntohs(data.portNumber);
	printMcastAddr = inet_ntoa(*(struct in_addr*)&data.multicastGroup);
	printf("Welcome to AsciiFlix!\n");
	printf(" the number of stations is %hu\n", data.numStations);
	printf(" Multicast group is: %s\n", printMcastAddr);
	printf(" Port number is: %hu\n", printPort);
	//open udp socket and thread to show the movie
	if ((udp_sock = socket(AF_INET, SOCK_DGRAM, 0))==0){
		perror("could not open a socket");
		free(frame);
		close(control_sock);
		exit(EXIT_FAILURE);
	}
	mcast_addr.sin_family = AF_INET;
	mcast_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	mcast_addr.sin_port = data.portNumber;
	if ((bind(udp_sock, (struct sockaddr*)&mcast_addr,addrlen))<0){
		perror("something is wrong with bind to multicast");
		free(frame);
		close(control_sock);
		close(udp_sock);
		exit(EXIT_FAILURE);
	}
	//set multicast address
	mreq.imr_multiaddr.s_addr = data.currentMcastGroup;
	mreq.imr_interface.s_addr = htonl(INADDR_ANY);
	if ((setsockopt(udp_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)))==-1){
		perror("could not connect to the multicast group");
		free(frame);
		close(control_sock);
		close(udp_sock);
		exit(EXIT_FAILURE);
	}
	canStream = 1;
	//open thread to stream TV
	if ((pthread_create(&stream, NULL, streamTV,NULL))!=0){
		perror("could not create the stream thread");
		free(frame);
		close(control_sock);
		close(udp_sock);
		exit(EXIT_FAILURE);
	}
	while(STAY){
		FD_ZERO(&fdset);
		FD_SET(fileno(stdin),&fdset);
		select(FD_SETSIZE, &fdset, NULL, NULL, NULL);//wait until event will happen
		if (FD_ISSET(fileno(stdin),&fdset)){
			while((getchar())!='\n');
			while(!TStoped);
			canStream = 0;
			if (!isPremium){//if we are not premium
				printMenu();
				scanf("%d", &user_input);
				switch(user_input){
				case 1:
					//if the client wanted to change movie
					printf("Enter station number\n");
					scanf("%hu", &next_parm);
					if (next_parm >= data.numStations){//if the station is valid
						printf("bad input!");
						canStream = 1;
						break;
					}
					//drop multicast address
					mreq.imr_multiaddr.s_addr = data.currentMcastGroup;
					mreq.imr_interface.s_addr = htonl(INADDR_ANY);
					if ((setsockopt(udp_sock, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq)))==-1){
						perror("could not disconnect the multicast group");
						free(frame);
						pthread_cancel(stream);
						close(control_sock);
						close(udp_sock);
						exit(EXIT_FAILURE);
					}
					//build and send ask film
					message[0] = 1;
					*(uint16_t*)&message[1] = htons(next_parm);
					if ((send(control_sock,message,3,0))!=3){
						perror("something wrong with sending AskFilm");
						free(frame);
						pthread_cancel(stream);
						close(control_sock);
						close(udp_sock);
						exit(EXIT_FAILURE);
					}
					//wait to recieve announce
					if ((recv(control_sock,input_buffer,Buffer_size,0))==-1){
						perror("problem with getting announce from the server");
						free(frame);
						pthread_cancel(stream);
						close(control_sock);
						close(udp_sock);
						exit(EXIT_FAILURE);
					}
					//if we got invalid command
					if (input_buffer[0] == 4){
						for (i = 2; i<input_buffer[1]+2; i++)
							printf("%c",input_buffer[i]);
						printf("\n");
						free(frame);
						pthread_cancel(stream);
						close(control_sock);
						close(udp_sock);
						exit(EXIT_FAILURE);
					}
					//if we did not get announce
					if (input_buffer[0] != 1){
						perror("somthing strange with the server. should have sent announce");
						free(frame);
						pthread_cancel(stream);
						close(control_sock);
						close(udp_sock);
						exit(EXIT_FAILURE);
					}
					//build new frame
					data.row = input_buffer[1];
					data.col = input_buffer[2];
					free(frame);
					if((frame = (char*)malloc(sizeof(char)*data.col*data.row))==NULL){
						perror("Memory allocation error");
						close(control_sock);
						close(udp_sock);
						exit(EXIT_FAILURE);
					}
					printf("Now playing: ");
					for (i = 4; i<input_buffer[3]+4; i++)
						printf("%c",input_buffer[i]);
					printf("\n");
					//set multicast address
					data.currentStation = next_parm;
					data.currentMcastGroup = htonl(ntohl(data.multicastGroup) + data.currentStation);
					mreq.imr_multiaddr.s_addr = data.currentMcastGroup;
					mreq.imr_interface.s_addr = htonl(INADDR_ANY);
					if ((setsockopt(udp_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)))==-1){
						perror("could not connect to the multicast group");
						free(frame);
						pthread_cancel(stream);
						close(control_sock);
						close(udp_sock);
						exit(EXIT_FAILURE);
					}
					canStream = 1;
					break;
				case 2:
					//client asks to be a premium
					//build and send premium message
					message[0] = 2;
					next_parm = 0;
					*(uint16_t*)&message[1] = htons(next_parm);
					if ((send(control_sock,message,3,0))!=3){
						perror("something wrong with sending GoPro");
						free(frame);
						pthread_cancel(stream);
						close(control_sock);
						close(udp_sock);
						exit(EXIT_FAILURE);
					}
					//wait for permit pro
					if ((recv(control_sock,input_buffer,Buffer_size,0))==-1){
						perror("problem with getting permit pro from the server");
						free(frame);
						pthread_cancel(stream);
						close(control_sock);
						close(udp_sock);
						exit(EXIT_FAILURE);
					}
					//if we got invalid command
					if (input_buffer[0] == 4){
						for (i = 2; i<input_buffer[1]+2; i++)
							printf("%c",input_buffer[i]);
						printf("\n");
						free(frame);
						pthread_cancel(stream);
						close(control_sock);
						close(udp_sock);
						exit(EXIT_FAILURE);
					}
					//if we did not get permit pro
					if (input_buffer[0] != 2){
						perror("somthing strange with the server. should have sent permit pro");
						free(frame);
						pthread_cancel(stream);
						close(control_sock);
						close(udp_sock);
						exit(EXIT_FAILURE);
					}
					//if we did not get permission
					if (input_buffer[1] == 0){
						printf("We are sorry, you cannot become premium\n");
						canStream = 1;
						break;
					}
					data.premiumPort = *(uint16_t*)&input_buffer[2];
					//open premium socket
					if ((premium_sock = socket(AF_INET, SOCK_STREAM, 0))==0){
						perror("could not open a socket");
						free(frame);
						pthread_cancel(stream);
						close(control_sock);
						close(udp_sock);
						exit(EXIT_FAILURE);
					}
					if ((setsockopt(premium_sock,SOL_SOCKET,SO_REUSEADDR | SO_REUSEPORT,&opt, sizeof(opt)))==-1){
						perror("could not set reuse");
						free(frame);
						pthread_cancel(stream);
						close(control_sock);
						close(udp_sock);
						close(premium_sock);
						exit(EXIT_FAILURE);
					}
					server_addr.sin_family = AF_INET;
					server_addr.sin_addr.s_addr = server_addr_num;
					server_addr.sin_port = data.premiumPort;
					//connect to premium socket
					if ((connect(premium_sock,(struct sockaddr*)&server_addr,addrlen))==-1){
						perror("could not connect to the server");
						free(frame);
						pthread_cancel(stream);
						close(control_sock);
						close(udp_sock);
						close(premium_sock);
						exit(EXIT_FAILURE);
					}
					//drop multicat address
					mreq.imr_multiaddr.s_addr = data.currentMcastGroup;
					mreq.imr_interface.s_addr = htonl(INADDR_ANY);
					if ((setsockopt(udp_sock, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq)))==-1){
						perror("could not disconnect the multicast group");
						free(frame);
						pthread_cancel(stream);
						close(control_sock);
						close(udp_sock);
						exit(EXIT_FAILURE);
					}
					isPremium = 1;
					canStream = 1;
					break;
				case 3:
					//client asked to return to watch
					canStream = 1;
					break;
				case 4:
					//client asked to quit
					printf("have a nice day!\n");
					free(frame);
					pthread_cancel(stream);
					close(control_sock);
					close(udp_sock);
					exit(EXIT_SUCCESS);
					break;
				default:
					printf("bad input!");
					canStream = 1;
					break;
				}
			}
			else{//if client is premium
				printPremiumMenu();
				scanf("%d", &user_input);
				switch(user_input){
				case 1:
					//client asked to change movie
					printf("Enter station number\n");
					scanf("%hu", &next_parm);
					if (next_parm >= data.numStations){
						printf("bad input!");
						canStream = 1;
						break;
					}
					//build and send ask film
					message[0] = 1;
					*(uint16_t*)&message[1] = htons(next_parm);
					if ((send(control_sock,message,3,0))!=3){
						perror("something wrong with sending AskFilm");
						free(frame);
						pthread_cancel(stream);
						close(control_sock);
						close(premium_sock);
						close(udp_sock);
						exit(EXIT_FAILURE);
					}
					//wait for announce
					if ((recv(control_sock,input_buffer,Buffer_size,0))==-1){
						perror("problem with getting announce from the server");
						free(frame);
						pthread_cancel(stream);
						close(control_sock);
						close(premium_sock);
						close(udp_sock);
						exit(EXIT_FAILURE);
					}
					//if we got invalid command
					if (input_buffer[0] == 4){
						for (i = 2; i<input_buffer[1]+2; i++)
							printf("%c",input_buffer[i]);
						printf("\n");
						free(frame);
						pthread_cancel(stream);
						close(control_sock);
						close(premium_sock);
						close(udp_sock);
						exit(EXIT_FAILURE);
					}
					//if we did not get announce
					if (input_buffer[0] != 1){
						perror("somthing strange with the server. should have sent announce");
						free(frame);
						pthread_cancel(stream);
						close(control_sock);
						close(premium_sock);
						close(udp_sock);
						exit(EXIT_FAILURE);
					}
					//build frame
					data.row = input_buffer[1];
					data.col = input_buffer[2];
					free(frame);
					if((frame = (char*)malloc(sizeof(char)*data.col*data.row))==NULL){
						perror("Memory allocation error");
						close(control_sock);
						close(premium_sock);
						close(udp_sock);
						exit(EXIT_FAILURE);
					}
					printf("Now playing: ");
					for (i = 4; i<input_buffer[3]+4; i++)
						printf("%c",input_buffer[i]);
					printf("\n");
					data.currentStation = next_parm;
					canStream = 1;
					break;
				case 2:
					//client wanted to change speed
					printf("Enter speed from 1 to 100\n");
					scanf("%hhu", &speed);
					if (speed < 0 || speed > 100){
						printf("bad input!");
						canStream = 1;
						break;
					}
					//build and send speedup
					message[0] = 3;
					message[1] = speed;
					if ((send(control_sock,message,2,0))!=2){
						perror("something wrong with sending SpeedUp");
						free(frame);
						pthread_cancel(stream);
						close(control_sock);
						close(premium_sock);
						close(udp_sock);
						exit(EXIT_FAILURE);
					}
					//wait for ack
					if ((recv(control_sock,input_buffer,Buffer_size,0))==-1){
						perror("problem with getting ack from the server");
						free(frame);
						pthread_cancel(stream);
						close(control_sock);
						close(premium_sock);
						close(udp_sock);
						exit(EXIT_FAILURE);
					}
					//if we got invalid command
					if (input_buffer[0] == 4){
						for (i = 2; i<input_buffer[1]+2; i++)
							printf("%c",input_buffer[i]);
						printf("\n");
						free(frame);
						pthread_cancel(stream);
						close(control_sock);
						close(premium_sock);
						close(udp_sock);
						exit(EXIT_FAILURE);
					}
					//if we did not get ack or the ack wasnt for speed up
					if (input_buffer[0] != 3 || input_buffer[1] != 3){
						perror("somthing strange with the server. should have sent Ack for SpeedUp");
						free(frame);
						pthread_cancel(stream);
						close(control_sock);
						close(premium_sock);
						close(udp_sock);
						exit(EXIT_FAILURE);
					}
					canStream = 1;
					break;
				case 3:
					//client asked release
					//build mssage and send
					message[0] = 4;
					next_parm = 0;
					*(uint16_t*)&message[1] = htons(next_parm);
					if ((send(control_sock,message,3,0))!=3){
						perror("something wrong with sending Release");
						free(frame);
						pthread_cancel(stream);
						close(control_sock);
						close(premium_sock);
						close(udp_sock);
						exit(EXIT_FAILURE);
					}
					//wait for ack
					if ((recv(control_sock,input_buffer,Buffer_size,0))==-1){
						perror("problem with getting ack from the server");
						free(frame);
						pthread_cancel(stream);
						close(control_sock);
						close(premium_sock);
						close(udp_sock);
						exit(EXIT_FAILURE);
					}
					//if we got invalid command
					if (input_buffer[0] == 4){
						for (i = 2; i<input_buffer[1]+2; i++)
							printf("%c",input_buffer[i]);
						printf("\n");
						free(frame);
						pthread_cancel(stream);
						close(control_sock);
						close(premium_sock);
						close(udp_sock);
						exit(EXIT_FAILURE);
					}
					//if we did not get ack or the ack is not for release
					if (input_buffer[0] != 3 || input_buffer[1] != 4){
						perror("somthing strange with the server. should have sent Ack for Release");
						free(frame);
						pthread_cancel(stream);
						close(control_sock);
						close(premium_sock);
						close(udp_sock);
						exit(EXIT_FAILURE);
					}
					//close socket
					isPremium = 0;
					close(premium_sock);
					data.currentMcastGroup = htonl(ntohl(data.multicastGroup) + data.currentStation);
					mreq.imr_multiaddr.s_addr = data.currentMcastGroup;
					mreq.imr_interface.s_addr = htonl(INADDR_ANY);
					if ((setsockopt(udp_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)))==-1){
						perror("could not connect to the multicast group");
						free(frame);
						pthread_cancel(stream);
						close(control_sock);
						close(udp_sock);
						exit(EXIT_FAILURE);
					}
					canStream = 1;
					break;
				case 4:
					//client asked to return to film
					canStream = 1;
					break;
				case 5:
					//client asked to quit
					printf("have a nice day!\n");
					free(frame);
					pthread_cancel(stream);
					close(control_sock);
					close(udp_sock);
					close(premium_sock);
					exit(EXIT_SUCCESS);
					break;
				default:
					printf("bad input!\n");
					canStream = 1;
					break;
				}
			}
		}
	}
}

void* streamTV(void* parm){
	int i,j;
	int recieve;
	int firstFrame = 1;
	while(STAY){
		if (!isPremium){//if not premium
			if (canStream){//if we can stream
				TStoped = 0;
				//recieve frame
				if ((recieve = recvfrom(udp_sock, frame, data.col*data.row*sizeof(char), 0, (struct sockaddr*)&mcast_addr, (socklen_t*)&addrlen))==-1){
					perror("problem with recieving udp packets");
					free(frame);
					close(control_sock);
					close(udp_sock);
					exit(EXIT_FAILURE);
				}
				//if main socket was closed
				if (recieve == 0){
					printf("server closed his main socket\n");
					free(frame);
					close(control_sock);
					close(udp_sock);
					exit(EXIT_FAILURE);
				}
				//print frame
				if (!firstFrame){
					for (i = 0; i < data.row; i++)
						printf("\033[1A\033[2K\r");
				}
				else firstFrame = 0;
				for (i = 0; i<data.row; i++){
					for (j=0; j<data.col; j++)
						printf("%c", frame[j+i*data.col]);
				}
			}
		}
		else{//ip premium
			//recieve frame
			if ((recieve = recv(premium_sock, frame, data.col*data.row*sizeof(char),0))==-1){
				perror("problem with premium channel");
				free(frame);
				close(control_sock);
				close(premium_sock);
				close(udp_sock);
				exit(EXIT_FAILURE);
			}
			//if we can stream
			if (canStream){
				TStoped = 0;
				//print frame
				if (!firstFrame){
					for (i = 0; i < data.row; i++)
						printf("\033[1A\033[2K\r");
				}
				else firstFrame = 0;
				for (i = 0; i<data.row; i++){
					for (j=0; j<data.col; j++)
						printf("%c", frame[j+i*data.col]);
					}
			}
		}
		TStoped = 1;
		usleep(10);
	}
	pthread_exit(NULL);
}

void printMenu(){
	printf("Please enter your choice:\n");
	printf("1. Change Movie\n");
	printf("2. Ask For Premium\n");
	printf("3. Return To Watch\n");
	printf("4. Quit Program\n");
}

void printPremiumMenu(){
	printf("Please enter your choice:\n");
	printf("1. Change Movie\n");
	printf("2. Go Faster\n");
	printf("3. Leave Pro\n");
	printf("4. Return To Watch\n");
	printf("5. Quit Program\n");
}
