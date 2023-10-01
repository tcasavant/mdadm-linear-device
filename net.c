#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <err.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include "net.h"
#include "jbod.h"

/* the client socket descriptor for the connection to the server */
int cli_sd = -1;

/* attempts to read n bytes from fd; returns true on success and false on
 * failure */
static bool nread(int fd, int len, uint8_t *buf) {
  int n = 0;
  int r;
  while (n < len){
    r = read(fd, buf, len);
    if (r < 0)
      return false;
    n += r;
  }
  return true;
}

/* attempts to write n bytes to fd; returns true on success and false on
 * failure */
static bool nwrite(int fd, int len, uint8_t *buf) {
  int n = 0;
  int r;
  while (n < len){
    r = write(fd, buf, len);
    if (r < 0)
      return false;
    n += r;
  }
  return true;
}

/* attempts to receive a packet from fd; returns true on success and false on
 * failure */
static bool recv_packet(int fd, uint32_t *op, uint16_t *ret, uint8_t *block) {

  uint8_t header[HEADER_LEN];
  if (nread(fd, 8, header) == false)
    return false;

  uint16_t len = (header[0] | (header[1]<<8)) & 0xffff;
  len = ntohs(len);
  if (len > HEADER_LEN)
    if (nread(fd, len - HEADER_LEN, block) == false)
      return false;

  return true;
}

/* attempts to send a packet to sd; returns true on success and false on
 * failure */
static bool send_packet(int sd, uint32_t op, uint8_t *block) {
  uint16_t len = HEADER_LEN;
  uint8_t sendbuf[HEADER_LEN + JBOD_BLOCK_SIZE];
  if (block != NULL){
    len += JBOD_BLOCK_SIZE;
    memcpy(sendbuf + 8, block, JBOD_BLOCK_SIZE);
  }
  len = htons(len);
  op = htonl(op);

  memcpy(sendbuf, &len, sizeof(len));
  memcpy(sendbuf + 2, &op, sizeof(op));

  return nwrite(cli_sd, ntohs(len), sendbuf);
}

/* attempts to connect to server and set the global cli_sd variable to the
 * socket; returns true if successful and false if not. */
bool jbod_connect(const char *ip, uint16_t port) {
  // Create a socket
  cli_sd = socket(AF_INET, SOCK_STREAM, 0);
  if (cli_sd == -1) {
    printf("Error on socket creation [%s]\n", strerror(errno));
    return false;
  }
  // Create address and convert to network byte order
  struct sockaddr_in caddr;

  caddr.sin_family = AF_INET;
  caddr.sin_port = htons(port);
  if (inet_aton(ip, &caddr.sin_addr) == 0){
    return false;
  }

  // Connect socket to address
  if (connect(cli_sd, (const struct sockaddr *)&caddr, sizeof(caddr)) == -1){
    return false;
  }

  return true;
}

/* disconnects from the server and resets cli_sd */
void jbod_disconnect(void) {
  // Disconnect socket from address
  close (cli_sd);
  cli_sd = -1;

}

/* sends the JBOD operation to the server and receives and processes the
 * response. */
int jbod_client_operation(uint32_t op, uint8_t *block) {
  /* write packet; */
  if (op >> 26 == JBOD_WRITE_BLOCK){
    send_packet(cli_sd, op, block);
  }
  else{
    send_packet(cli_sd, op, NULL);
  }

  uint16_t ret = 0;
  recv_packet(cli_sd, &op, &ret, block);
  ret = ntohs(ret);

  return ret;
}
