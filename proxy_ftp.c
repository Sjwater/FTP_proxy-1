//-------Internet Applications Coursework---
//------- Created by   ---------------------
//------- WANG Wenyu   ----------
//------- June 17th, 2016 ------------------


#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <fcntl.h>
#define BUFFSIZE 2000
#define PORT21 21
#define PORT_FOR_DATA 38522
#define TITLE 50

char respon220[] = "220 FTP Server is ready...\r\n";
char respon226[] = "226 Transfer complete\r\n";
const char respon150_part[] = "150 Opening BINARY mode data connection for ";
const char proxy_port[] = "PORT 192,168,56,101,150,122\r\n";
const char proxy_pas_port[] = "227 Entering Passive Mode (192,168,56,101,150,122)\r\n";
const char serverIP[] = "192.168.56.1";
char client_port_info[30];
char server_port_info[30];
int server_port_PASV;
char container[BUFFSIZE+2];
char G_filename[TITLE+2];
int recvsize;
int active;
int hasCache = -1;

fd_set master_set, working_set;  //file descriptor
int proxy_cmd_socket    = 0;     
int accept_cmd_socket   = 0;     
int connect_cmd_socket  = 0;     
int proxy_data_socket   = 0;     
int accept_data_socket  = 0;     
int connect_data_socket = 0;     

int total = 0, success = 0, fail = 0;

int bindAndListenSocket(int);
void downloadAndCache(int);
int acceptCmdSocket(int);
int connectToServer(int);
void dealClientCmd();
int dealPassive();
int acceptDataSocket(int);
int connectDataSocket();
int makeCache(char *);
int appendContent(char *, int);
void downloadCache();
int retrCache(char *);


int main(int argc, const char *argv[]){

    struct timeval timeout;          //time limitation for select() 
    int selectResult = 0;     //returned by select()
    int select_sd = 10;    //the largest file descriptor for select() 
    
    FD_ZERO(&master_set);   //clear master_set
    bzero(&timeout, sizeof(timeout));
    
    proxy_cmd_socket = bindAndListenSocket(PORT21);  //initialize proxy_cmd_socket
    proxy_data_socket = bindAndListenSocket(PORT_FOR_DATA); //initialize proxy_data_socket
    
    FD_SET(proxy_cmd_socket, &master_set);  //add proxy_cmd_socket into master_set
    FD_SET(proxy_data_socket, &master_set);//add proxy_data_socket into master_set
    
    timeout.tv_sec = 5000;    // time out after 5000ms
    timeout.tv_usec = 0;    //ms
    
    while (1) {
        FD_ZERO(&working_set); //clear the working_set
        memcpy(&working_set, &master_set, sizeof(master_set)); //copy contents of master_set into working_set
        
        //select(): listenning, only react to read operations 
        selectResult = select(select_sd, &working_set, NULL, NULL, &timeout);
        
        // fail
        if (selectResult < 0) {
            perror("select() failed\n");
            exit(1);
        }
        
        // timeout
        if (selectResult == 0) {
            printf("select() timed out.\n");
            continue;
        }
        
        // when selectResult > 0, loop to check which socket has a status change
        int i;
        int count_test = 0;
        for (i = 0; i < select_sd; i++) {

            //check if the changed file discriptor is in working_set
            if (FD_ISSET(i, &working_set)) {
                //1: client tries to connect proxy
                if (i == proxy_cmd_socket) {
                    //------------------   FLAG_begin   ----------------------
                    printf("\n-------- 1: i = %d, ", i);
                    count_test ++;
                    printf("count_test = %d\n", count_test);
                    //------------------   FLAG_end    -----------------------

                    accept_cmd_socket = acceptCmdSocket(proxy_cmd_socket);  //accept to build command connection between proxy and client
                    connect_cmd_socket = connectToServer(PORT21); //connect to build the command connection between proxy and server
                    
                    //add the socket to master_set
                    FD_SET(accept_cmd_socket, &master_set);
                    FD_SET(connect_cmd_socket, &master_set);
                }
                
                //2: reveive command from client, transport to server
                if (i == accept_cmd_socket) {

                    //------------------   FLAG_begin   ----------------------
                    printf("\n-------- 2: i = %d, ", i);
                    count_test ++;
                    printf("count_test = %d\n", count_test);
                    //------------------   FLAG_end    -----------------------

                    memset(container, '\0', BUFFSIZE); 
                    recvsize = recv(i, container, BUFFSIZE, 0);
                    if(recvsize == 0 ){ //failed to receive from client
                        printf("Proxy: RECV client -_- \n");
                        close(i); //close the socket if failed in recieving
                        close(connect_cmd_socket);
                        //after closing the socket, remove it from master_set and select() stops listening to the socket
                        FD_CLR(i, &master_set);
                        FD_CLR(connect_cmd_socket, &master_set);
                        accept_cmd_socket = 0;
                        connect_cmd_socket = 0;
                    }else{  //revieve command from client fine
                        container[recvsize] = '\0';
                        printf("Proxy: RECV client ^_^\nsize = %d, message = [%s]\n", recvsize, container);
                        //modify received message if necessary
                        dealClientCmd();
                        //transport command from client to server
                        if(send(connect_cmd_socket, container, strlen(container), 0) == 0){
                            printf("Proxy: SEND server fails -_- \n");
                        }else{
                            printf("Proxy: SEND server succeeded ^_^ \n");
                        }
                    }
                    
                }
                //receive command from server and trasport to client
                if (i == connect_cmd_socket) {
                    //------------------   FLAG_begin   ----------------------
                    printf("\n-------- 3: i = %d, ", i);
                    count_test ++;
                    printf("count_test = %d\n", count_test);
                    //------------------   FLAG_end    -----------------------

                    //receive from server
                    memset(container, '\0', BUFFSIZE); 
                    recvsize = recv(i, container, BUFFSIZE, 0);
                    if(recvsize == 0 ){     //failed to receive from server
                        printf("Proxy: RECV server fails -_- \n");
                        close(i);   
                        close(accept_cmd_socket);
                        FD_CLR(i, &master_set);
                        FD_CLR(accept_cmd_socket, &master_set);
                        connect_cmd_socket = 0; 
                        accept_cmd_socket = 0;
                    }else{  //revieve command from server fine
                         container[recvsize] = '\0';
                         printf("Proxy: RECV server succeeded ^_^\nsize = %d, message = [%s]\n", recvsize, container);
                         //detect PASV port, modify
                         if(dealPassive() == 1){
                            recvsize = strlen(container);
                         }
                         //transport command from server to client
                         if(send(accept_cmd_socket, container, recvsize, 0) == 0){
                            printf("Proxy: SEND client failed -_- \n");
                         }else{
                            printf("Proxy: SEND client  succeeded ^_^ \n");
                         }
                         break;
                    }
                }
                
                if (i == proxy_data_socket) {
                    //------------------   FLAG_begin   ----------------------
                    printf("\n-------- 4: i = %d\n", i);
                    count_test ++;
                    printf("count_test = %d\n", count_test);
                    //------------------   FLAG_end    -----------------------

                    if(active == 1){  
                        //active mode (active = 1)
                        printf("\n---------ACTIVE CREATE-----------\n");                        
                        //check 1: download 2:retrieve from cache 3:non-file
                        if(hasCache == 0 || hasCache == -1){
                            //if LIST or download form server
                            connect_data_socket = connectDataSocket();
                            FD_SET(connect_data_socket, &master_set);
                            accept_data_socket = acceptDataSocket(i);
                            FD_SET(accept_data_socket, &master_set);  
                            printf("connect_data_socket = %d, accept_data_socket = %d\n", connect_data_socket, accept_data_socket);                                       
                        } else{
                            //do nothing. data connection with lient has already been established afer recieving RETR
                        }                    
                        printf("-----------------------------------------\n\n");
                    }else if(active == 0){    
                        //passive mode (active = 0)
                        printf("\n---------PASSIVE CREATE-----------\n");
                        if(hasCache == 0 || hasCache == -1){
                            //if LIST or download form server
                            connect_data_socket = connectToServer(server_port_PASV);
                            accept_data_socket  = acceptDataSocket(i);
                            FD_SET(accept_data_socket, &master_set);
                            FD_SET(connect_data_socket, &master_set); 
                            printf("connect_data_socket = %d, accept_data_socket = %d\n", connect_data_socket, accept_data_socket);                      
                        }else{
                            //establish data connection with clien to download from porxy cache
                            accept_data_socket  = acceptDataSocket(i);
                            FD_SET(accept_data_socket, &master_set);
                        }
                        printf("--------------------------------------\n");

                    }else{
                        printf("MODE error -_-\n");
                    }
                    fflush(stdout);
                    getchar();getchar();
                }
                
                if (i == accept_data_socket) {
                    //------------------   FLAG_begin   ----------------------
                    printf("\n-------- 5: i = %d", i);
                    count_test ++;
                    printf("count_test = %d\n", count_test);
                    //------------------   FLAG_end    -----------------------

                    //receive from server
                    memset(container, '\0', BUFFSIZE); 
                    recvsize = recv(i, container, BUFFSIZE, 0);

                    if(recvsize == 0 ){ 
                        //failed to receive data
                        if(active == 1){
                            printf(">> >> Proxy: DOWNLOAD server-proxy failed -_- \n\n");
                            hasCache = -1;
                        }else if(active == 0){
                            printf(">> >> Proxy: UPLOAD client-proxy failed -_- \n\n");
                        }else{
                            printf("Mode blurred at accept_data_socket");
                        }                       
                        close(i); 
                        close(connect_data_socket);
                        FD_CLR(i, &master_set);
                        FD_CLR(connect_data_socket, &master_set);
                        accept_data_socket = 0;
                        connect_data_socket = 0;
                        hasCache = -1;
                    }else{  
                        //recieve data fine  
                        container[recvsize] = '\0';
                        //judge UPLOAD/DOWNLOAD with respect to "active"'s value
                        if(active == 1){
                            //1: for active-DOWNLOAD
                            downloadAndCache(connect_data_socket);
                        }else if(active == 0){
                            //2: for passive-UPLOAD
                            printf("Proxy: UPLOAD client-proxy ^_^\nsize = %d\n", recvsize);
                            if(send(connect_data_socket, container, recvsize, 0) == 0){
                                printf("Proxy: SEND data to server failed -_- \n");
                            }else{
                                printf("Proxy: SEND data to server succeeded ^_^ \n");
                             }
                        }else{
                            //3: unexpected
                            printf("Mode blurred at accept_data_socket");
                        }                        
                     }
                 }
                
                if (i == connect_data_socket) {
                    //------------------   FLAG_begin   ----------------------
                    printf("\n-------- 6: i = %d\n", i);
                    count_test ++;
                    printf("count_test = %d\n", count_test);
                    //------------------   FLAG_end    -----------------------

                    //try to recieve data
                    recvsize = recv(i, container, BUFFSIZE, 0);//data buffer
                    if(recvsize == 0 ){ 
                        //failed to receive data
                        if(active == 1){
                            printf(">> >> Proxy: UPLOAD client-proxy failed -_- \n\n");
                        }else if(active == 0){
                            printf(">> >> Proxy: DOWNLOAD server-proxy succeeded -_- \n\n");
                            hasCache = -1;
                        }else{
                            printf("Mode blurred at connect_data_socket");
                        }                       
                        close(i); 
                        close(accept_data_socket);
                        FD_CLR(i, &master_set);
                        FD_CLR(accept_data_socket, &master_set);
                        accept_data_socket = 0;
                        connect_data_socket = 0;
                        hasCache = -1;
                    }else{  
                        //recieve data fine
                        container[recvsize] = '\0'; 
                        //judge UPLOAD/DOWNLOAD with respect to "active"'s value                      
                        if(active == 1){
                            //1: for active-UPLOAD, forward client's data to server
                            printf("Proxy: UPLOAD to server ^_^\nsize = %d\n", recvsize);
                             if(send(accept_data_socket, container, recvsize, 0) == 0){
                                printf("Proxy: SEND data to server failed -_- \n");
                             }else{
                                printf("Proxy: SEND data to server succeeded ^_^ \n");
                             }
                        }else if(active == 0){
                            //2: for passive-DOWNLOAD, forward server's data to client
                            downloadAndCache(accept_data_socket); 
                        }else{
                            //3: unexpected
                            printf("Mode blurred at connect_data_socket");
                        }             
                         
                    }
                }
            }
        }

    }
    
    return 0;
}

void downloadAndCache(int sock){
    char buff_temp[50];
    printf("Proxy: DOWNLOAD from server ^_^\nsize = %d\n", recvsize);
     //transport command from server to client
     if(send(sock, container, recvsize, 0) == 0){
        printf("Proxy: SEND data to client failed -_- \n");
     }else{
        printf("Proxy: SEND data to client succeeded ^_^ \n");
        printf("## CHECK A ## %x-%x-%x-%x\n", container[0],container[1], container[2], container[3]);
     }  
     //if coming data is not file, skip the block
     printf("NOTICE: hasCache = %d\n", hasCache);
     if(hasCache != -1) {   
        total ++;              
        if(appendContent(G_filename, recvsize) == -1){
            fail ++;
            printf("append file error\n");
        }else{
            success ++;
            printf("PROXY: write file fine.\n");
        }
        printf("total = %d, file_success = %d, file_fail = %d\n",total, success, fail);
    }   
    fflush(stdout);
}

int bindAndListenSocket(int portNum){

    int listening_sock;
    struct sockaddr_in proxyaddr;
    int c;
    int flag = 1;

    //create sockeet
    listening_sock = socket(AF_INET , SOCK_STREAM , 0); 
    if(listening_sock == -1){
        printf("Failed to create listening_sock");
    }else{
        printf("Listening_sock = %d\n", listening_sock);
    }

    if(setsockopt(listening_sock, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(int)) == -1){ 
        perror("setsockopt"); 
        exit(1); 
    }   

    //prepare poxcyaddr structure
    proxyaddr.sin_family = AF_INET;
    proxyaddr.sin_addr.s_addr = INADDR_ANY;
    proxyaddr.sin_port = htons( portNum );

    //bind
    if(bind(listening_sock,(struct sockaddr *)&proxyaddr , sizeof(proxyaddr)) < 0){
        printf("Bind failed\n");
        return 0;
    }else{
        printf("Bind OK.\n");
    }

    
    //Listen
    printf("Socket %d waitng for connetction towards PORT %d ... \n", listening_sock, portNum);
    fflush(stdout);
    listen(listening_sock, 3);

    return listening_sock;
        
}

//accept connection from a coming client
int acceptCmdSocket(int proxy_cmd_socket){
    int working_sock;
    struct sockaddr_in clinetaddr;
    int c;
    c = sizeof(struct sockaddr_in);
    working_sock = accept(proxy_cmd_socket, (struct sockaddr *)&clinetaddr, (socklen_t*)&c);
    if (working_sock < 0){
       printf("cdm accept failed -_-\n");
       return 1;
    }
    printf("Cmd connection accepted, clinet is served at %d\n", working_sock);
    return working_sock;
}

int connectToServer(int portNum){

    int sock;
    struct sockaddr_in server;  

    //create socket to connect server
    sock = socket(AF_INET , SOCK_STREAM , 0);
    if(sock == -1){
        printf("Proxy: Failed to create socket to connect server.\n");
    }else{
        printf("Proxy: connect to server via socket %d\n", sock);
    }

    //server addr
    server.sin_addr.s_addr = inet_addr(serverIP);
    server.sin_family = AF_INET;
    server.sin_port = htons(portNum);

    //try to connect server
    if (connect(sock , (struct sockaddr *)&server , sizeof(server)) < 0){
        printf("Proxy: Connection error\n");
        return 1;
    }
    printf("Porxy-Server connected\n");

    return sock;
}

int dealPassive(){
    char cmd_index[4];
    int calPort(char *);

    memset(cmd_index, '\0', 4);
    memcpy(cmd_index, container, 3);
    cmd_index[3] = '\0';
    printf("%s, size = %d", cmd_index, strlen(cmd_index));
    if(strcmp(cmd_index, "227") != 0){
        printf("PASV instruction -_-\n");
        return -1;
    }else{
        printf("\n----------Response 227----------\n");
        printf("Dealing with port\n");
        printf("1: record server port info\n");
        memset(server_port_info, '\0', sizeof(server_port_info));
        memcpy(server_port_info, container, strlen(container));
        printf("[%s]\n", server_port_info);
        printf("2: calculate portnumber\n");
        server_port_PASV = calPort(server_port_info);
        printf("3: modify container\n");
        memset(container, '\0', sizeof(container));
        memcpy(container, proxy_pas_port, strlen(proxy_pas_port));
        container[strlen(proxy_pas_port)] = '\0';
        printf("length: %d\n", strlen(container));
        printf("[%s]\n", container);
        printf("-------------------------\n\n");
        getchar();
        //set flag
        active = 0;
    }
    return 1;
}
void dealClientCmd(){
    char client_cmd[4];
    memset(client_cmd, '\0', 4);
    memcpy(client_cmd, container, 4);
    printf("Dealing with cmd [%s]\n", client_cmd);
    fflush(stdout);
    //modify command
    if(strcmp(client_cmd, "PORT") == 0){  
        printf("\n-------PORT--------\n");
        printf("1: record client port Info\n");
        memcpy(client_port_info, container, strlen(container));
        client_port_info[strlen(container)] = '\0';
        printf("[%s]\n", client_port_info);
        printf("2: modify container\n");
        memset(container, '\0', sizeof(container));
        memcpy(container, proxy_port, strlen(proxy_port));
        container[strlen(proxy_port)] = '\0';
        printf("length: %d\n", strlen(container));
        printf("[%s]\n", container);
        printf("-------------------------\n\n");
        //set flag
        active = 1;         
    } 
    
    else if(strcmp(client_cmd, "PASV") == 0){       
        printf("active = 0 now\n");       
        //set flag
        active = 0;
    }
    
    else if(strcmp(client_cmd, "SIZE") == 0){
        char lookfor_file[TITLE], lookfor_file_long[TITLE];
        //get file name
        memset(lookfor_file, '\0', TITLE);
        memset(lookfor_file_long,'\0', TITLE);
        memcpy(lookfor_file_long, container+5, strlen(container));
        printf("TITLE SIZE1 = %d\n", strlen(lookfor_file_long));
        memcpy(lookfor_file, lookfor_file_long, strlen(lookfor_file_long)-2);
        printf("TITLE SIZE2 = %d\n", strlen(lookfor_file));
        //check if file is in cache
        if(open(lookfor_file, O_RDONLY, 0) < 0){
            printf("No cache for [%s]\n", lookfor_file);
            printf("Downoad from server.\n");
            //set flag
            hasCache = 0;
        }else{
            printf("File [%s] is in proxy's cache\n", lookfor_file);
            printf("No data connection with server\n");
            fflush(stdout);
            //set flag
            hasCache = 1;
        }
        getchar();
    }
       
    else if(strcmp(client_cmd, "RETR") == 0){
        getchar();getchar();
        //deal with file name
        char filename_long[TITLE];
        memset(filename_long, '\0', TITLE);
        memcpy(filename_long, container+5, strlen(container));
        printf("TITLE SIZE1 = %d\n", strlen(filename_long));
        //save file name 
        memset(G_filename, '\0', sizeof(G_filename));
        memcpy(G_filename, filename_long, strlen(filename_long)-2);
        printf("TITLE SIZE2 = %d\n", strlen(G_filename));
        printf("try to create [%s]\n", G_filename);
        //when the file is not in cache
        if(hasCache == 0){
            printf("cache = 0, Do nothing\n");          
        }else if(hasCache == 1){
            //file is in cache
            printf("cache = 1, Download from cache\n");
            //do not send RETR to server
            memset(container, '\0', sizeof(container));       
            //for active data connection, connect to client before send file
            //for passive, data connection has been established in main loop
            if(active == 1){
                char respon150[100];
                memset(respon150, '\0', 100);
                memcpy(respon150, respon150_part, strlen(respon150_part));
                strcat(respon150, "'");
                strcat(respon150, G_filename);
                strcat(respon150, "'.");
                strcat(respon150, "\r\n");
                //1: send response 150
                if(send(accept_cmd_socket, respon150, strlen(respon150), 0) == 0){
                    printf("Proxy: RESP150 to server failed -_- \n");
                }else{
                    printf("Proxy: RESP150 data to server succeeded ^_^ \n");
                }
                memset(respon150, '\0', 100);
                connect_data_socket = connectDataSocket();
                FD_SET(connect_data_socket, &master_set);
            }
            downloadCache();
        }else{
            printf("Download flag blurred, hasCache = %d now\n", hasCache);
        }
        //set flag to indicate not-file
    }
}


int acceptDataSocket(int proxy_data_socket){
    int working_sock;
    struct sockaddr_in someaddr;
    int c;
    c = sizeof(struct sockaddr_in);
    working_sock = accept(proxy_data_socket, (struct sockaddr *)&someaddr, (socklen_t*)&c);
    if (working_sock < 0){
       printf("Data connection failed -_-\n");
       return 1;
    }
    printf("Data connection accepted at %d\n", working_sock);
    return working_sock;
}

int connectDataSocket(){
    int sock;
    struct sockaddr_in client_data; 
    void getclientip(char *, char *);
    int calPort(char *);
    int flag = 1;

    //create socket to connect client_data
    sock = socket(AF_INET , SOCK_STREAM , 0);
    if(sock == -1){
        printf("Proxy_data: Failed to create socket to connect clinet.\n");
        return -1;
    }else{
        printf("Proxy_data: connect to client via socket %d\n", sock);
    }

    if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(int)) == -1){ 
        perror("setsockopt"); 
        exit(1); 
    }

    //client_data addr
    char client_ip[20];
    //get IP and port from command
    getclientip(client_port_info,client_ip);
    int client_port = calPort(client_port_info);
    client_data.sin_addr.s_addr = inet_addr(client_ip);
    client_data.sin_family = AF_INET;
    client_data.sin_port = htons(client_port);

    //try to connect with respect calculated result
    if (connect(sock , (struct sockaddr *)&client_data , sizeof(client_data)) < 0){
        printf("PORT: proxy-clinet -_-\n");
        close(sock);
        return -1;
    }
    printf("PORT: proxy-clinet ^_^\n");
    return sock;
}

//derive client ip from the PORT message it sent to proxy
void getclientip(char*buff,char*clientip){
    int n=0;char bufftemp[1024];
    strcpy(bufftemp,buff);
    char *ptr=strchr(bufftemp,',');
    ptr--;
    while(1){
        if((*ptr<'0')||(*ptr>'9'))
            break;
        ptr--;
    }
    ptr++;  //ip starts here
    char *ptrport=strrchr(bufftemp,',');
    ptrport--;
    while(*ptrport!=',')
    {
        ptrport--;
    }
    *ptrport='\0';  //ip ends
    while(ptrport!=ptr)
    {
        ptrport--;
        if(*ptrport==',')
            *ptrport='.';
    }
    strcpy(clientip,ptr);
    printf("client_ip = %s\n", clientip);
}

//derive client port number from the PORT message it sent to proxy
int calPort(char *buff){
    char buff2[1024];
    memset(buff2,0,sizeof(buff2));
    int portnumber1, portnumber2,portnumber; //for port number of server 
    char lastcharacter; // check if it is a digit number 
    char *porttemp;
    memcpy(buff2,buff,strlen(buff));
    lastcharacter=buff2[strlen(buff2)-1];
    if(lastcharacter<'0'||lastcharacter>'9'){
        buff2[strlen(buff2)-1]='\0';    // discard the last bit if it is not digit
        buff2[strlen(buff2)]='\0';//
    }

    porttemp=strrchr(buff2,',');
    porttemp++;
    portnumber2=atoi(porttemp);
    porttemp--;
    memset(porttemp,0,1);
    porttemp=strrchr(buff2,',');
    porttemp++;
    portnumber1=atoi(porttemp);
    portnumber=portnumber1*256+portnumber2; //port at which the server is waiting for clinet's data connection. 服务器等待客户端数据连接的端口

    printf("port = %d\n", portnumber);

    return portnumber;
}


int appendContent(char *filename, int recvsize){
    int writefile;
    if((writefile = (open(filename, O_RDWR|O_APPEND|O_CREAT, 0))) < 0){
        printf("open file [%s] error\n", filename);
        return -1;
    }
    if(write(writefile, container, recvsize) != recvsize){
        printf("append error -_-\n");
        close(writefile);
        return -1;
    }else{
        printf("cache file [%s] seems ok\n", filename);
        close(writefile);
        printf("## CHECK B ## %x-%x-%x-%x\n", container[0],container[1], container[2], container[3]);
        //getchar();getchar();
        return 1;
    }

}
// download from cache 
int retrCache(char *filename){ 
    char cachewords[BUFFSIZE+2];
    int readsize;
    int readfile = open(G_filename, O_RDWR, 0);
    printf("readfile = %d\n", readfile);
    //when the file goes wrong, give a notice
    if(readfile < 0){
        printf("Read cache -_- \n");
        printf("Sorry to inform you cannot download it\n");
        printf("Because the code has met something unexpected\n");
        return -1;
    }else{  //retrieve data from file, continously read until reaching EOF          
        //read file, transport via client-proxy data connection created just above
        memset(cachewords, '\0', sizeof(cachewords));
        int sendsize;
        //loop until file ends
        while((readsize = read(readfile, cachewords, BUFFSIZE)) > 0){                   
            //different trasport active and passive
            if(active == 1){
                printf("connect_data_socket No: %d, size = %d \n", connect_data_socket, readsize);
                sendsize = send(connect_data_socket, cachewords, readsize, 0);
            }else{
                printf("accept_data_socket No: %d, size = %d \n", accept_data_socket, readsize);
                sendsize = send(accept_data_socket, cachewords, readsize, 0);
            }
            //check if data has been trasport successfully
            if(sendsize == 0){
                printf("Proxy: SEND data to client failed\n");
            }else{
                printf("Proxy: SEND data to client succeeded\n");
            }
            //clear buffer
            memset(cachewords, '\0', sizeof(cachewords));
        }
        //close and clear
        if(active == 1){
            close(connect_data_socket);
            FD_CLR(connect_data_socket, &master_set);
            connect_data_socket = 0;
        }else{
            close(accept_data_socket);
            FD_CLR(accept_data_socket, &master_set);
            accept_data_socket = 0;
        }
        close(readfile);
        memset(G_filename, '\0', TITLE);
        printf("send cache data done\n");    
        hasCache = -1;
    }
    return 1;
}

void downloadCache(){  
    printf("---------------cache---------------------\n");
    printf("accept_data_socket = %d\n", accept_data_socket);
    printf("connect_data_socket = %d\n", connect_data_socket);
    printf("------------------------------------------\n");
    
    if(active == 0){
        char respon150[100];
        memset(respon150, '\0', 100);
        memcpy(respon150, respon150_part, strlen(respon150_part));
        strcat(respon150, "'");
        strcat(respon150, G_filename);
        strcat(respon150, "'.");
        strcat(respon150, "\r\n");
        //1: send response 150
        if(send(accept_cmd_socket, respon150, strlen(respon150), 0) == 0){
            printf("Proxy: RESP150 to server -_- \n");
        }else{
            printf("Proxy: RESP150 data to server  ^_^ \n");
        }
        memset(respon150, '\0', 100);
    }
    //2: trasport data to clinet, from cache
    if(retrCache(G_filename) < 0){
        printf("Faild to retrieve [%s] from cache\n", G_filename);
    }else{
        printf("Dwonlad from cache, seems nice\n");
        //3: send response 226
        printf("## check accept_cmd_socket: %d\n", accept_cmd_socket);
        if(send(accept_cmd_socket, respon226, strlen(respon226), 0) == 0){
            printf("Proxy: RESP226 to client -_- \n");
        }else{
            printf("Proxy: RESP226 to client  ^_^ \n");
        }
    }
    fflush(stdout);
}
                 
