#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <ifaddrs.h>
#include <linux/if_link.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <pthread.h>

typedef struct Data{
	int isAvailable;//if we can open client on this index
	int fd;//fd of the client socket
	int isPremium;//is the client premium
	int premiumFd;//premium client fd
	int premiumLocation;//
	int currentStation;//curent station that the client is watching
	char* currentName;//
	int premiumSpeed;//speed of movie
}Data;

typedef struct frameSize{
	uint8_t row;
	uint8_t col;
}frameSize;

typedef struct premiumData{
	int socket;
	char* name;
}premiumData;

#define LIMIT_PRIMIUM 2
#define LIMIT_CLIENT 50
#define TIME_OF_FRAME 83333// 1/12 seconds in micro seconds.
#define STAY 1
#define MAX_INPUT 1024
#define TIMEOUT 300000//in micro seconds

int numPremium = 0;
int premiumTemp = -1;
Data data[LIMIT_CLIENT];
pthread_t* streams; //threads of the movies
pthread_t* premium[LIMIT_PRIMIUM];//aray of premium cients
pthread_t welcome_premium_thread;//premium welcome thread
FILE** files;
FILE* premiumFiles[LIMIT_PRIMIUM];
int* sock_files;
frameSize* frames;
uint16_t numOfMovies;
int welcome_sock;
int welcome_premium_sock;
uint16_t tcp_port;
uint16_t tcp_prim_port;
uint32_t mcast_ip;
uint32_t ip_addr_num;
uint16_t udp_port;
struct sockaddr_in welcome_addr;
struct sockaddr_in welcome_premium_addr;
int addrlen = sizeof(welcome_addr);
int ttl = 10;
int opt = 1;
int canStreamPremium = 0;

uint32_t get_ip();

void init_data(char* argv[]);

void create_movies(char* argv[]);

void free_all();

void close_all(int i);

int open_udp_socket(int i);

void* showMovieUdp(void* arg);

void* welcomePremium(void* arg);

void* showPremium(void* arg);

int open_welcome(uint32_t ip, uint16_t port);

int findMovie(int socket);

int findClient(int socket);

void controlClient(int i, char* argv[]);

void controlWelcomeSock(char* argv[]);

void invalid_command(char* replyString, int i);

int main(int argc, char* argv[]){
	fd_set fdset;
	int i,j, max_fd;
	char input[MAX_INPUT];
	struct timeval tv;
	//set premium array
	for (j=0; j<LIMIT_PRIMIUM; j++)
		premium[j] = NULL;
	//set timeout;
	tv.tv_sec = 0;
	tv.tv_usec = TIMEOUT;
	ip_addr_num = get_ip();//get servers ip address
	init_data(argv);
	numOfMovies = argc - 5;
	//get information
	tcp_port = htons(atoi(argv[1]));
	tcp_prim_port = htons(atoi(argv[2]));
	mcast_ip = inet_addr(argv[3]);
	udp_port = htons(atoi(argv[4]));
	//create sockets of movies
	create_movies(argv);
	welcome_premium_sock = open_welcome(ip_addr_num, tcp_prim_port);
	welcome_sock = open_welcome(ip_addr_num, tcp_port);
	//set socket timeout
	if ((setsockopt(welcome_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv)))==-1){
		perror("there is a problem in the set of the timeout");
		close_all(numOfMovies);
		free_all();
		exit(EXIT_FAILURE);
	}
	//create premium welcome thread
	if ((pthread_create(&welcome_premium_thread,NULL,welcomePremium,NULL))==-1){
		perror("Unable to open premium welcome thread");
		close_all(numOfMovies);
		free_all();
		exit(EXIT_FAILURE);
	}
	printf("please press q to quit\n");
	while(STAY){
		FD_ZERO(&fdset);
		FD_SET(fileno(stdin), &fdset);
		FD_SET(welcome_sock, &fdset);
		//find max_fd
		if (fileno(stdin)>welcome_sock) max_fd = fileno(stdin);
		else max_fd = welcome_sock;
		//set FD for all clients
		for (i=0; i<LIMIT_CLIENT; i++)
			if (data[i].isAvailable == 0){
				FD_SET(data[i].fd,&fdset);
				if (data[i].fd > max_fd) max_fd = data[i].fd;
			}
		select(max_fd+1,&fdset,NULL,NULL,NULL);//wait for an event
		if (FD_ISSET(fileno(stdin),&fdset)){//if somthing was typed
			fgets(input, MAX_INPUT, stdin);
			if (input == NULL)
				printf("No one will ever take me down!\n");
			if (!strcmp(input, "q\n")){//if q was typed
				printf("Have a nice day!\n");//free and close all
				pthread_cancel(welcome_premium_thread);
				close_all(numOfMovies);
				free_all();
				exit(EXIT_SUCCESS);
			}
			else printf("No one will ever take me down!\n");//if something else was typed
		}
		if (FD_ISSET(welcome_sock, &fdset))//if we got hello
			controlWelcomeSock(argv);
		for (i=0; i<LIMIT_CLIENT; i++){
			if (data[i].isAvailable == 0)
				if (FD_ISSET(data[i].fd,&fdset))//if we got a message from client
					controlClient(i, argv);
		}
	}
}

uint32_t get_ip(){//get ip address of server
	uint32_t answer = 0;
	struct sockaddr_in *addr;
	struct ifaddrs *ifaddr, *ifa;
	if(getifaddrs(&ifaddr)==-1){
		perror("could not getifaddr");
		exit(EXIT_FAILURE);
	}
	for (ifa = ifaddr; ifa!=NULL; ifa=ifa->ifa_next)
		if (!(strcmp(ifa->ifa_name, "eth0"))&&ifa->ifa_addr->sa_family == AF_INET){
			addr = (struct sockaddr_in*)ifa->ifa_addr;
			answer = *(uint32_t*)&addr->sin_addr;
			break;
		}
	freeifaddrs(ifaddr);
	return answer;
}

//initialize data struct 
void init_data(char* argv[]){
	int i;
	for (i=0; i<LIMIT_CLIENT; i++){
		data[i].fd = 0;
		data[i].isAvailable = 1;
		data[i].isPremium = 0;
		data[i].premiumFd = 0;
		data[i].premiumLocation = -1;
		data[i].currentStation = 0;
		data[i].currentName = argv[5];
		data[i].premiumSpeed = TIME_OF_FRAME;
	}
}

//open files and create sockets for movies
void create_movies(char* argv[]){
	int i;
	//alocate memory for files
	if ((streams = (pthread_t*)calloc(numOfMovies, sizeof(pthread_t)))==NULL){
		perror("Memory allocation error");
		exit(EXIT_FAILURE);
	}
	if ((files = (FILE**)calloc(numOfMovies, sizeof(FILE*)))==NULL){
		perror("Memory allocation error");
		free(streams);
		exit(EXIT_FAILURE);
	}
	if ((sock_files = (int*)calloc(numOfMovies, sizeof(int)))==NULL){
		perror("Memory allocation error");
		free(streams);
		free(files);
		exit(EXIT_FAILURE);
	}
	if ((frames = (frameSize*)calloc(numOfMovies, sizeof(frameSize)))==NULL){
		perror("Memory allocation error");
		free(streams);
		free(files);
		free(sock_files);
		exit(EXIT_FAILURE);
	}
	//open all files
	for (i = 0; i<numOfMovies; i++){
		if((files[i] = fopen(argv[5+i], "r"))==NULL){
			perror("unable to open file");
			close_all(i);
			free_all();
			exit(EXIT_FAILURE);
		}
		sock_files[i] = open_udp_socket(i);//open sockets
		//open threads
		if(pthread_create(&streams[i],NULL, showMovieUdp, (void*)&sock_files[i])==-1){
			perror("unable to open a thread");
			close_all(i);
			close(sock_files[i]);
			fclose(files[i]);
			free_all();
			exit(EXIT_FAILURE);
		}
	}
}

//free dynamic allocated memory
void free_all(){
	int i;
	free(streams);
	free(files);
	free(sock_files);
	free(frames);
	for (i=0; i<LIMIT_PRIMIUM; i++)
		if (premium[i] != NULL){
			pthread_cancel(*premium[i]);
			free(premium[data[i].premiumLocation]);
			premium[i] = NULL;
		}
	if (premiumFiles[i] != NULL) fclose(premiumFiles[i]);
}

//open udp socket
int open_udp_socket(int i){
	int sock;
	//open socket
	if ((sock = socket(AF_INET, SOCK_DGRAM, 0))==0){
		perror("unable to open udp socket");
		close_all(i);
		fclose(files[i]);
		free_all();
		exit(EXIT_FAILURE);
	}
	//set multicast address
	if ((setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, (void*)&ttl, sizeof(ttl)))==-1){
		perror("unable to change ttl for socket");
		close_all(i);
		close(sock);
		fclose(files[i]);
		free_all();
	}
	if ((setsockopt(sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))==-1){
		perror("unable to make 'reuse' for socket");
		close_all(i);
		close(sock);
		fclose(files[i]);
		free_all();
	}
	return sock;
}

//close files and sockets
void close_all(int i){
	int j;
	for (j=0; j<i; j++){
		pthread_cancel(streams[i]);
		fclose(files[j]);
		close(sock_files[i]);
	}
}

//find index of requested movie
int findMovie(int socket){
	int i;
	for (i=0; i<numOfMovies; i++)
		if (sock_files[i] == socket)
			return i;
	return -1;
}

//find index of requested client
int findClient(int socket){
	int i;
	for (i=0; i<LIMIT_CLIENT; i++)
		if (data[i].fd == socket)
			return i;
	return -1;
}

//open welcome socket
int open_welcome(uint32_t ip, uint16_t port){
	int sock;
	struct sockaddr_in* address;
	if (port == tcp_port) address = &welcome_addr;
	else address = &welcome_premium_addr;
	if ((sock = socket(AF_INET,SOCK_STREAM,0))==0){
		perror("unable to open welcome socket");
		close_all(numOfMovies);
		free_all();
		exit(EXIT_FAILURE);
	}
	if ((setsockopt(sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))==-1){
		perror("unable to make 'reuse' for socket");
		close_all(numOfMovies);
		free_all();
		exit(EXIT_FAILURE);
	}
	address->sin_family = AF_INET;
	address->sin_addr.s_addr = ip;
	address->sin_port = port;
	if ((bind(sock,(struct sockaddr*)address, sizeof(*address)))<0){
		perror("could not operate bind");
		close_all(numOfMovies);
		free_all();
		exit(EXIT_FAILURE);
	}
	if ((listen(sock, SOMAXCONN))<0){
		perror("problem with function listen");
		close_all(numOfMovies);
		free_all();
		exit(EXIT_FAILURE);
	}
	return sock;
}

//client menu
void controlClient(int i, char* argv[]){
	int j;
	int yes = 1;
	double calc;
	int rowTemp, colTemp;
	time_t timeCheck;
	uint8_t speed;
	uint8_t input_buffer[3];
	uint8_t* announce_msg;
	uint8_t permitPro_msg[4];
	uint8_t ack_msg[2];
	char* filmName;
	char temp1, temp2;
	uint8_t filmNameSize;
	int receive = recv(data[i].fd, input_buffer, 3, 0);
	uint16_t stationNumber;
	//if we did not recieve anything
	if (receive == -1){
		invalid_command("Problem with receiving your message", i);
		return;
	}
	//if the client closed the socket
	if (receive == 0){
		if (data[i].isPremium){//if we are premium-> close and free all
			pthread_cancel(*premium[i]);
			free(premium[data[i].premiumLocation]);
			premium[i] = NULL;
			fclose(premiumFiles[data[i].premiumLocation]);
			close(data[i].premiumFd);
		}
		close(data[i].fd);
		data[i].isAvailable = 1;
		return;
	}
	switch(input_buffer[0]){
	case 0:
		//if we got hello
		invalid_command("You are acting strange! already sent hello", i);
		break;
	case 1:
		//if we got ask film
		stationNumber = ntohs(*(uint16_t*)&input_buffer[1]);
		//if the movie number is valid
		if (stationNumber >= numOfMovies){
			invalid_command("The station number is too big!", i);
			break;
		}
		filmName = argv[stationNumber + 5];
		filmNameSize = strlen(filmName);
		//build announce message
		announce_msg = (uint8_t*)calloc(4+filmNameSize,sizeof(uint8_t));
		announce_msg[0] = 1;
		announce_msg[1] = frames[stationNumber].row-1;
		announce_msg[2] = frames[stationNumber].col;
		announce_msg[3] = filmNameSize;
		for (j=4; j<filmNameSize+4; j++)
			announce_msg[j] = filmName[j-4];
		//send announce
		if ((send(data[i].fd, announce_msg, 4+filmNameSize, 0))!=4+filmNameSize){
			invalid_command("There is a problem with sending Announce message", i);
			break;
		}
		data[i].currentStation = stationNumber;
		data[i].currentName = argv[5+stationNumber];
		//if we are premium open new file and close the old one
		if (data[i].isPremium){
			canStreamPremium = 0;
			usleep(10);
			fclose(premiumFiles[data[i].premiumLocation]);
			premiumFiles[data[i].premiumLocation] = fopen(data[i].currentName, "r");
			fscanf(premiumFiles[data[i].premiumLocation], "%d%c%d%c", &rowTemp, &temp1, &colTemp, &temp2);
			canStreamPremium = 1;
		}
		free(announce_msg);
		break;
	case 2:
		//if we got permit pro
		permitPro_msg[0] = 2;
		//if we reached the premium limit or this client is already premium
		if (numPremium >= LIMIT_PRIMIUM || data[i].isPremium){
			permitPro_msg[1] = 0;
			*(uint16_t*)&permitPro_msg[2] = 0;
		}
		else{
			//build permit pro
			permitPro_msg[1] = 1;
			*(uint16_t*)&permitPro_msg[2] = tcp_prim_port;
		}
		if ((send(data[i].fd, permitPro_msg, 4, 0))!=4){
			invalid_command("There is a problem with sending PermitPro message", i);
			break;
		}
		if (numPremium >= LIMIT_PRIMIUM || data[i].isPremium) break;
		timeCheck = clock();
		//busy polling on welcome premium activity
		while (premiumTemp == -1);
		timeCheck = (clock() - timeCheck)*1000000/CLOCKS_PER_SEC;
		if (timeCheck > TIMEOUT){
			invalid_command("Timeout!", i);
			break;
		}
		//set premium
		data[i].isPremium = 1;
		numPremium++;
		data[i].premiumFd = premiumTemp;
		premiumTemp = -1;
		if (setsockopt(data[i].premiumFd, IPPROTO_TCP, TCP_NODELAY, (char*)&yes, sizeof(int))==-1){
			perror("problem with setsockopt");
			close_all(numOfMovies);
			free_all();
			exit(EXIT_FAILURE);
		}
		//find availiable place in premium array
		for (j=0; j<LIMIT_PRIMIUM; j++){
			if (premium[j] == NULL){
				data[i].premiumLocation = j;
				premiumFiles[data[i].premiumLocation] = fopen(data[i].currentName, "r");
				fscanf(premiumFiles[data[i].premiumLocation], "%d%c%d%c", &rowTemp, &temp1, &colTemp, &temp2);
				premium[j] = (pthread_t*)malloc(sizeof(pthread_t));
				canStreamPremium = 1;
				//open thread
				if (pthread_create(premium[j], NULL, showPremium, (void*)&data[i].fd)){
					perror("problem with open showPremium thread");
					close_all(numOfMovies);
					free_all();
					exit(EXIT_FAILURE);
				}
				break;
			}
		}
		break;
	case 3:
		//if we got release
		if (!data[i].isPremium){
			invalid_command("You acting strange! you are not premium!", i);
			break;
		}
		speed = input_buffer[1];
		//if speed is not in the limits
		if (speed < 0 || speed > 100){
			invalid_command("You are acting strange! speed is of bounds", i);
			break;
		}
		ack_msg[0] = 3;
		ack_msg[1] = 3;
		//send ack
		if ((send(data[i].fd, ack_msg, 2, 0))!=2){
			invalid_command("There is a problem with sending Ack message", i);
			break;
		}
		//clculate new time of frame
		calc = (double)100/(speed+100);
		calc = TIME_OF_FRAME * calc;
		data[i].premiumSpeed = (int)calc;
		break;
	case 4:
		//if we got release
		if (!data[i].isPremium){
			invalid_command("You are acting strange! you are not premium!", i);
			break;
		}
		ack_msg[0] = 3;
		ack_msg[1] = 4;
		//send ack
		if ((send(data[i].fd, ack_msg, 2, 0))!=2){
			invalid_command("There is a problem with sending Ack message", i);
			break;
		}
		//close premium thred and files
		pthread_cancel(*premium[data[i].premiumLocation]);
		free(premium[data[i].premiumLocation]);
		premium[data[i].premiumLocation] = NULL;
		fclose(premiumFiles[data[i].premiumLocation]);
		close(data[i].premiumFd);
		data[i].premiumSpeed = TIME_OF_FRAME;
		data[i].isPremium = 0;
		numPremium--;
		break;
	default:
		invalid_command("You are acting strange! your first Byte isn't in the protocol!", i);
		break;
	}
}

void controlWelcomeSock(char* argv[]){
	int i;
	uint8_t input_buffer[3];
	uint8_t welcome_msg[11];
	//cheack for avaliable places in data array
	for (i = 0; i <LIMIT_CLIENT; i++){
		if (data[i].isAvailable){
			data[i].isAvailable = 0;
			data[i].fd = accept(welcome_sock, (struct sockaddr*)&welcome_addr, (socklen_t*)&addrlen);
			data[i].isPremium = 0;
			data[i].premiumFd = 0;
			data[i].premiumLocation = -1;
			data[i].currentStation = 0;
			data[i].currentName = argv[5];
			data[i].premiumSpeed = TIME_OF_FRAME;
			break;
		}
	}
	//if we got hello message
	if ((recv(data[i].fd, input_buffer, 3, 0))==-1){
		invalid_command("There is a problem with the welcome socket", i);
		return;
	}
	if (input_buffer[0] != 0){
		invalid_command("You are acting strange!", i);
		return;
	}
	//build welcome message
	welcome_msg[0]=0;
	*(uint16_t*)&welcome_msg[1] = htons(numOfMovies);
	*(uint32_t*)&welcome_msg[3] = ntohl(mcast_ip);
	*(uint16_t*)&welcome_msg[7] = udp_port;
	welcome_msg[9] = frames[0].row-1;
	welcome_msg[10] = frames[0].col;
	if ((send(data[i].fd, welcome_msg, 11, 0))==-1)
		invalid_command("There is a problem with sending Welcome message", i);
}

void* showMovieUdp(void* arg){
	int j;
	int socket = *(int*)arg;
	char temp1, temp2;
	int row, col;
	int dur;
	int i = findMovie(socket);//find requested movie
	uint32_t my_mcast = htonl(ntohl(mcast_ip)+i);
	char buffer[MAX_INPUT];
	struct sockaddr_in mcast_address;
	socklen_t len = sizeof(mcast_address);
	//set multicast address
	mcast_address.sin_family = AF_INET;
	mcast_address.sin_addr.s_addr = my_mcast;
	mcast_address.sin_port = udp_port;
	fscanf(files[i], "%d%c%d%c", &row, &temp1, &col, &temp2);
	frames[i].row = row;
	frames[i].col = col;
	while(STAY){
		fscanf(files[i], "%d", &dur);//scan frame from file
		temp1 = fgetc(files[i]);
		if (feof(files[i])){//if we got to the end of the file go to the begining
			fseek(files[i], 0, SEEK_SET);
			fscanf(files[i], "%d%c%d%c", &row, &temp1, &col, &temp2);
			continue;
		}
		for (j=0; j<(frames[i].row-1); j++)
			fgets(&buffer[j*frames[i].col],frames[i].col,files[i]);
		//send frame
		if(sendto(socket, buffer, (frames[i].row-1)*frames[i].col, 0, (struct sockaddr*)&mcast_address, len)==-1){
			perror("Problem with send udp packets");
			close_all(i);
			free_all();
			exit(EXIT_FAILURE);
		}
		memset(buffer, 0, MAX_INPUT);//clear buffer
		usleep(dur*TIME_OF_FRAME);
	}
	pthread_exit(NULL);
}

//wait for accept
void* welcomePremium(void* arg){
	int temp;
	while(STAY){
		temp = accept(welcome_premium_sock, (struct sockaddr*)&welcome_premium_addr, (socklen_t*)&addrlen);
		premiumTemp = temp;
	}
	pthread_exit(NULL);
}

void* showPremium(void* arg){
	int j;
	int socket = *(int*)arg;
	char temp1, temp2;
	int row, col;
	int dur;
	int i = findClient(socket);
	char buffer[MAX_INPUT];
	while(STAY){
		if (canStreamPremium){//if we can stream
			fscanf(premiumFiles[data[i].premiumLocation], "%d", &dur);
			temp1 = fgetc(premiumFiles[data[i].premiumLocation]);
			if (feof(premiumFiles[data[i].premiumLocation])){//if we got to the end of the file
				fseek(premiumFiles[data[i].premiumLocation], 0, SEEK_SET);//go to the begining of the file
				fscanf(premiumFiles[data[i].premiumLocation], "%d%c%d%c", &row, &temp1, &col, &temp2);
				continue;
			}
			//read frame from file
			for (j=0; j<(frames[data[i].currentStation].row-1); j++)
				fgets(&buffer[j*frames[data[i].currentStation].col],frames[data[i].currentStation].col,premiumFiles[data[i].premiumLocation]);
			//send frame
			if(send(data[i].premiumFd, buffer, (frames[data[i].currentStation].row-1)*frames[data[i].currentStation].col, 0)==-1){
				perror("Problem with send premium packets");
				close_all(i);
				free_all();
				exit(EXIT_FAILURE);
			}
			memset(buffer, 0, MAX_INPUT);
			usleep(dur*data[i].premiumSpeed);
		}
		usleep(10);
	}
	pthread_exit(NULL);
}

void invalid_command(char* replyString, int i){
	int j;
	uint8_t replyStringSize = strlen(replyString);
	uint8_t* invalid_command_buffer = (uint8_t*)calloc(2+replyStringSize,sizeof(uint8_t));;
	invalid_command_buffer[0] = 4;
	invalid_command_buffer[1] = replyStringSize;
	for (j=2; j<replyStringSize+2; j++)
		invalid_command_buffer[j] = replyString[j-2];
	send(data[i].fd, invalid_command_buffer, replyStringSize + 2, 0);
	close(data[i].fd);
	data[i].isAvailable = 1;
	free(invalid_command_buffer);
}
