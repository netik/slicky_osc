// Client side implementation of UDP client-server model 
#include <stdio.h> 
#include <stdlib.h> 
#include <unistd.h> 
#include <string.h> 
#include <sys/types.h> 
#include <sys/socket.h> 
#include <arpa/inet.h> 
#include <netinet/in.h> 
#include "tinyosc.h"
  
#define PORT     9000
#define MAXLINE 1024 

static const long hextable[] = { 
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1, 0,1,2,3,4,5,6,7,8,9,-1,-1,-1,-1,-1,-1,-1,10,11,12,13,14,15,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,10,11,12,13,14,15,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
};

// Driver code 
ssize_t udpSend(char *payload, socklen_t payload_len) { 
    int sockfd; 
    char buffer[MAXLINE]; 
    ssize_t bytes_sent;
    struct sockaddr_in servaddr; 
    int n; 
    socklen_t len;
    
    // Creating socket file descriptor 
    if ( (sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) { 
        perror("socket creation failed"); 
        exit(EXIT_FAILURE); 
    } 
  
    memset(&servaddr, 0, sizeof(servaddr)); 
      
    // Filling server information 
    servaddr.sin_family = AF_INET; 
    servaddr.sin_port = htons(PORT); 
    servaddr.sin_addr.s_addr = INADDR_ANY; 

    bytes_sent = sendto(sockfd, 
      (const char *)payload, 
      payload_len, 
      0x0, 
      (const struct sockaddr *) &servaddr,  
      sizeof(servaddr)); 
    /*
    n = recvfrom(sockfd, (char *)buffer, MAXLINE,  
                MSG_WAITALL, (struct sockaddr *) &servaddr, 
                &len); 
    buffer[n] = '\0'; 
    printf("Server : %s\n", buffer); 
    */

    close(sockfd); 
    return bytes_sent; 
} 

/** 
 * @brief convert a hexidecimal string to a signed long
 * will not produce or process negative numbers except 
 * to signal error.
 * 
 * @param hex without decoration, case insensitive. 
 * 
 * @return -1 on error, or result (max (sizeof(long)*8)-1 bits)
 */
long hexdec(const char *hex) {
   long ret = 0; 
   while (*hex && ret >= 0) {
      ret = (ret << 4) | hextable[*hex++];
   }
   return ret; 
}

int main(int argc, char *argv[]) {
  // declare a buffer for writing the OSC packet into
  char buffer[1024];

  if (argc != 3) {
    printf("Usage: %s command value\n\ncommand is typically setcolor or blink followed by a hex value\n\n", argv[0]);
    return 1; 
  }

  char command[255];
  snprintf(command,255,"/%s",argv[1]);

  printf("client sending %s", command);

  // write the OSC packet to the buffer
  // returns the number of bytes written to the buffer, negative on error
  // note that tosc_write will clear the entire buffer before writing to it
  int len;

  if (strncmp(command,"/setcolorhex",13) == 0) {
    len = tosc_writeMessage(
        buffer, sizeof(buffer),
        command, // the address
        "s",   // the format; 'f':32-bit float, 's':ascii string, 'i':32-bit integer
        argv[2]);
  } else {
    long color = hexdec(argv[2]);
    len = tosc_writeMessage(
        buffer, sizeof(buffer),
        command, // the address
        "i",   // the format; 'f':32-bit float, 's':ascii string, 'i':32-bit integer
        color);
  }
  // send the data out of the socket
  udpSend(buffer, len);

}
