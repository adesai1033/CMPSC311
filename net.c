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
// wrapper of read()
//  used to ensure read call completes because it is a network call
static bool nread(int fd, int len, uint8_t *buf)
{

  int num_read = 0;
  while (num_read < len)
  {
    int n = read(fd, &buf[num_read], len - num_read);
    if (n < 0)
    {
      return false; // read error
    }
    if (n == 0)
    {
      return false; // end of file
    }
    else
    {
      num_read += n;
    }
  }
  return true; // success
}

/* attempts to write n bytes to fd; returns true on success and false on
 * failure */
// wrapper of write()
//  used to ensure write completes because it is a network operation
static bool nwrite(int fd, int len, uint8_t *buf)
{
  int num_wrote = 0;
  while (num_wrote < len)
  {
    int n = write(fd, &buf[num_wrote], len - num_wrote);
    if (n < 0)
    {
      return false;
    }
    if (n == 0)
    {
      return false;
    }
    else
    {
      num_wrote += n;
    }
  }
  return true;
}

/* attempts to receive a packet from fd; returns true on success and false on
 * failure */
static bool recv_packet(int fd, uint32_t *op, uint16_t *ret, uint8_t *block)
{
  // network byte order to unix
  // only need block when reading
  uint8_t recvbuf[HEADER_LEN + JBOD_BLOCK_SIZE];
  if (!nread(fd, HEADER_LEN, recvbuf))
  {
    return false;
  }
  // inputs are like placeholder values that I should return something to
  //  to figure out if we need to a block check the size of len (which is in recv buf in order of specified protocol)
  //  if the size is more than 8, we need to call nread again
  // when i called nread first time it was with len 8 so I only get the first 8 bytes which describe length, opcode, return value
  uint16_t len = 0;
  memcpy(&len, recvbuf, sizeof(len));
  memcpy(op, recvbuf + sizeof(len), sizeof(*op));
  memcpy(ret, recvbuf + sizeof(len) + sizeof(*op), sizeof(*ret));
  len = ntohs(len);
  *op = ntohl(*op);
  *ret = ntohs(*ret);
  if (len == 264) // len can only be 264 or 8
  {               // I need block
    if (nread(fd, JBOD_BLOCK_SIZE, block))
    {
      //*block = ntohl(*block);
      return true;
    }
    else
    {
      return false;
    }
  }
  return true;
}

/* attempts to send a packet to sd; returns true on success and false on
 * failure */
static bool send_packet(int sd, uint32_t op, uint8_t *block)
{
  // unix to network byte order
  uint16_t len = HEADER_LEN;
  bool needBlock = false;

  if (op >> 26 == JBOD_WRITE_BLOCK) // for send packet I only need the block if the operation is write because I will need to give the server the data I want to write
                                    // whereas when sending a packet that is a read request block won't have any data and I will only need block when receiving the read req
  {
    needBlock = true;
    len += JBOD_BLOCK_SIZE;
  }
  uint16_t newlen = htons(len); // I need new len because when calling nwrite() len should not be in network byte order but it should when memcpying

  uint8_t sendbuf[HEADER_LEN + JBOD_BLOCK_SIZE]; // send buf is always 264 bytes

  memcpy(sendbuf, &newlen, sizeof(newlen)); // use newlen here which is len in network byte order
  uint32_t myOp = htonl(op);                // C is pass by value but I need the address of op to use it in memcpy so I create a local variable which will have an address whereas a parameter will not
  memcpy(sendbuf + 2, &myOp, sizeof(op));
  if (needBlock)
  {
    memcpy(sendbuf + 8, block, JBOD_BLOCK_SIZE);
  }
  return (nwrite(sd, len, sendbuf)); // returns true/false based on success/failure of nwrite
}

/* attempts to connect to server and set the global cli_sd variable to the
 * socket; returns true if successful and false if not. */
bool jbod_connect(const char *ip, uint16_t port)
{
  struct sockaddr_in caddr;
  caddr.sin_family = AF_INET;
  caddr.sin_port = htons(port);
  if (inet_aton(ip, &caddr.sin_addr) == 0)
  {
    cli_sd = -1;
    return false;
  }
  cli_sd = socket(AF_INET, SOCK_STREAM, 0); // change - PF_INET
  if (cli_sd == -1)
  {
    return false;
  }

  int c = connect(cli_sd, (struct sockaddr *)&caddr, sizeof(caddr)); // change - no const

  if (c == -1)
  {
    cli_sd = -1;
    return false;
  }
  // cli_sd = 1; //change
  return true;
}

/* disconnects from the server and resets cli_sd */
void jbod_disconnect(void)
{

  close(cli_sd);
  cli_sd = -1;
}

/* sends the JBOD operation to the server and receives and processes the
 * response. */
int jbod_client_operation(uint32_t op, uint8_t *block)
{
  uint32_t myOp = 0;
  uint16_t ret = 0;

  if (!send_packet(cli_sd, op, block))
  {
    return -1;
  }
  else
  {
    if (!recv_packet(cli_sd, &myOp, &ret, block))
    {
      return -1;
    }
  }

  return ret; // Change
}
