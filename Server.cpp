#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include "record.h"
#include <vector>
#include <limits>
#include <pthread.h>
#include <iostream>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
using namespace std;

// server configuration
int PORT_NUMBER;

// arduino
int arduino;
char cfstate = 'c';
char seasonmode = 's';
float highest = 30;
float lowest = 23;

// temperature global variable
float highest_temp = numeric_limits<float>::min();
float lowest_temp = numeric_limits<float>::max();
float recent_temp = 0;
int counter = 0;
float sum = 0;
float average = 0;
//record hourly high/low/avg temp
vector<Record> records;

pthread_mutex_t recent_temp_lock;
pthread_mutex_t records_lock;


float ctof(float c){
    float f = 32 + c*9/5;
    return f;
}


void* server(void* a){
    // structs to represent the server and client
    struct sockaddr_in server_addr,client_addr;

    int sock; // socket descriptor

    // 1. socket: creates a socket descriptor that you later use to make other system calls
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
      perror("Socket");
      exit(1);
    }
    int temp;
    if (setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,&temp,sizeof(int)) == -1) {
      perror("Setsockopt");
      exit(1);
    }

    // configure the server
    server_addr.sin_port = htons(PORT_NUMBER); // specify port number
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    bzero(&(server_addr.sin_zero),8);

    // 2. bind: use the socket and associate it with the port number
    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) == -1) {
                      perror("Unable to bind");
                      exit(1);
    }

    // 3. listen: indicates that we want to listn to the port to which we bound; second arg is number of allowed connections
    if (listen(sock, 5) == -1) {
                      perror("Listen");
                      exit(1);
    }

    // once you get here, the server is set up and about to start listening
    printf("\nServer configured to listen on port %d\n", PORT_NUMBER);
    fflush(stdout);
    while(true){

        // 4. accept: wait here until we get a connection on that port
        int sin_size = sizeof(struct sockaddr_in);
        int fd = accept(sock, (struct sockaddr *)&client_addr,(socklen_t *)&sin_size);
        printf("Server got a connection from (%s, %d)\n", inet_ntoa(client_addr.sin_addr),ntohs(client_addr.sin_port));

        // buffer to read data into
        char request[1024];

        // 5. recv: read incoming message into buffer
        int bytes_received = recv(fd,request,1024,0);
        // null-terminate the string
        request[bytes_received] = '\0';
        printf("Here comes the message:\n");
        printf("%s\n", request);

        const char s[] = " ";
        char *token;
        token = strtok(request, s);
        token = strtok(NULL, s);
        printf("token is %s\n", token);

        if(arduino == -1){
          //if arduino is not connected, send error message
          char reply[200];
          strcpy(reply, "{\"fail\":\"fail\"}");
          send(fd, reply, strlen(reply), 0);
          printf("arduino does not connect \n");
        }
        else{
            //when receive convert instruction from watch, send the instruction to arduino
          if(strcmp(token, "/convert") == 0){
            printf("convert: compare token %s\n", token);
            if(cfstate == 'c'){
              cfstate = 'f';
              int bytes_written = write(arduino, "f", 1);
              printf("send convert request to arduino \n");
            }
            else{
              cfstate = 'c';
              int bytes_written = write(arduino, "c", 1);
              printf("send convert request to arduino \n");
            }
          }
        //when receive convert mode instruction from watch, send the instruction to arduino
          else if(strcmp(token, "/summer") == 0){
            seasonmode = 's';
            highest = 30;
            lowest = 23;
            int bytes_written = write(arduino, "s", 1);
            char reply[100];
            strcpy(reply, "{\"mode\": \"summer\"}");
            send(fd, reply, strlen(reply), 0);
            printf("send summer mode request to arduino");
          }
        //when receive convert mode instruction from watch, send the instruction to arduino
          else if(strcmp(token, "/winter") == 0){
            seasonmode = 'w';
            highest = 10;
            lowest = 0;
            char reply[100];
            int bytes_written = write(arduino, "w", 1);
            strcpy(reply, "{\"mode\": \"winter\"}");
            send(fd, reply, strlen(reply), 0);
            printf("send winter mode request to arduino");
          }
         //when receive pause instruction from watch, send the instruction to arduino
          else if(strcmp(token, "/pause") == 0){
            printf("pause: compare token %s\n", token);
            int bytes_written = write(arduino, "p", 1);
            printf("send pause request to arduino ");
          }
        //when receive resume instruction from watch, send the instruction to arduino
          else if(strcmp(token, "/resume") == 0){
            int bytes_written = write(arduino, "r", 1);
          }
        //when receive trend instruction from watch, send the instruction to arduino
          else if(strcmp(token, "/trend") == 0){
            int start = 0;
            if(records.size()>=10){
              start = records.size()-10;
            }
        //send last 10 record data to pebble watch to draw the trend
            char reply[100];
            strcpy(reply, "{\"data\": \"");
            for(int i=start;i<records.size();i++){
              float record_avg = records.at(i).getAvg();
              int avg_convert = (int)(5*record_avg-125);
              char data[20];
              sprintf(data, "%d", avg_convert);
              strcat(reply, data);
              strcat(reply, "#");
            }
            strcat(reply, "\"}");
            send(fd, reply, strlen(reply), 0);
          }
        //when receive start instruction from watch, send the instruction to arduino
          else if(strcmp(token, "/start") == 0){
            if(recent_temp == 0){
              //if cannot connect to arduino, send error message
               char reply[200];
              strcpy(reply, "{\"fail\":\"fail\"}");
              send(fd, reply, strlen(reply), 0);
              printf("arduino does not connect \n");

            }else{
            //send high, low, avg temp data to watch
            pthread_mutex_lock(&recent_temp_lock);
            char reply[200];
            strcpy(reply, "{\n\"recentTemp\": \"");
            char high[20];
            if(cfstate == 'f') sprintf(high, "%.2f", ctof(highest_temp));
            else sprintf(high, "%.2f", highest_temp);
            char low[20];
            if(cfstate == 'f') sprintf(low, "%.2f", ctof(lowest_temp));
            else sprintf(low, "%.2f", lowest_temp);
            char avg[20];
            if(cfstate == 'f') sprintf(avg, "%.2f", ctof(average));
            else sprintf(avg, "%.2f", average);
            char current[20];
            if(cfstate == 'f') sprintf(current, "%.2f", ctof(recent_temp));
            else sprintf(current, "%.2f", recent_temp);
            strcat(reply, current);
            strcat(reply, "\",\n");
            strcat(reply, "\"high\": \"");
            strcat(reply, high);
            strcat(reply, "\",\n");
            strcat(reply, "\"low\": \"");
            strcat(reply, low);
            strcat(reply, "\",\n");
            strcat(reply, "\"avg\": \"");
            strcat(reply, avg);
            strcat(reply, "\",\n");
            //if current temp is out of comfortable range, send warning to watch
            strcat(reply, "\"warning\": \"");
            if(recent_temp>highest){
              strcat(reply, "true\"");
            }
            else strcat(reply, "false\"");
            strcat(reply, "\n}");
            send(fd, reply, strlen(reply), 0);
            pthread_mutex_unlock(&recent_temp_lock);
          }
        }
      }
        close(fd);

    }
    close(sock);
    printf("Server closed connection\n");
}

void* receiveTemp(void* p){
    // configure
    arduino = open("/dev/cu.usbmodem1411", O_RDWR);
    if(arduino == -1){
        int* result = new int(0);
        pthread_exit(result);
    }
    struct termios options;
    tcgetattr(arduino, &options);
    cfsetispeed(&options, 9600);
    cfsetospeed(&options, 9600);
    tcsetattr(arduino, TCSANOW, &options);

    // read the message from sensor
    char buf[100];
    buf[0] = '\0';
    char message[200];

    int flag = 0;
    while(1){
      int bytes_read = read(arduino, buf, 100);
      if(bytes_read != 0){
        buf[bytes_read] = '\0';
        if(flag == 1){
           message[0] = '\0';
           flag = 0;
        }
        strcat(message, buf);

        if(buf[bytes_read-1] == '\n'){
          flag = 1;
          if(message[0] != '\n'){
            // each time get a temperature, save it into the vector
            printf("The message is ");
            printf("%s\n", message);
            float temp = atof(message);
            printf("The temperature is %f \n", temp);
            if(temp<50 && temp>highest_temp) highest_temp = temp;
            if(temp>0 && temp<lowest_temp) lowest_temp = temp;
            pthread_mutex_lock(&recent_temp_lock);
            recent_temp = temp;
            pthread_mutex_unlock(&recent_temp_lock);
            counter++;
            sum += temp;
            printf("%d", counter);
            //add the high, low, avg of each hour into the vector
            if(counter== 15){
                average = sum/counter;
                Record record(highest_temp, lowest_temp, average);
                pthread_mutex_lock(&records_lock);
                records.push_back(record);
                printf("push record");
                pthread_mutex_unlock(&records_lock);
                counter = 0;
                sum = 0;
                highest_temp = numeric_limits<float>::min();
                lowest_temp = numeric_limits<float>::max();
            }


          }
        }
      }

    }
}




int main(int argc, char *argv[]){
  if(argc != 2) {
      std::cout<<"please input port number"<<endl;
      return 0;
  }
  PORT_NUMBER = atoi(argv[1]);
  pthread_t t1,t2;
  pthread_create(&t1, NULL, &receiveTemp, NULL);
  pthread_create(&t2, NULL, &server, NULL);
  pthread_mutex_init(&recent_temp_lock, NULL);
  pthread_mutex_init(&records_lock, NULL);
  pthread_join(t1, NULL);
  pthread_join(t2, NULL);
  return 0;
}
