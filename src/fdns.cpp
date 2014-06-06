#if FDNS

#include "core.h"
#include "net.h"
#include "fdns.h"

bool fdns_running = false;

namespace fdns {

Sockets s;
LocalBuffers lb;
NetworkData nd;

void cleanup(int et) {
  if (s.server) { shutdown(s.server, 2); Sleep(1000); closesocket(s.server); }
  if (et) {
    fdns_running = false;
    Sleep(1000);
    logMesg("FDNS stopped", LOG_INFO);
    pthread_exit(NULL);
  } else {
    logMesg("FDNS closed network connections", LOG_INFO);
    return;
  }
}

void *main(void *arg) {

  fdns_running = true;

  logMesg("FDNS starting", LOG_INFO);

  net.failureCounts[FDNS_IDX] = 0;

  int len, flags, ip4[4], n;
  char *ip4str, *m = lb.msg;
  ip4str = strdup(config.fdnsip);

  for (int i=0; i < 4; i++) { ip4[i] = atoi(strsep(&ip4str, ".")); }

  do { if (!net.ready) Sleep(1000); } while (fdns_running);

  s.server = socket(AF_INET, SOCK_DGRAM, 0);

  if (setsockopt(s.server, SOL_SOCKET, SO_REUSEADDR, "1", sizeof(int)) == -1) {
    net.failureCounts[FDNS_IDX]++;
    cleanup(1);
  }

  if (s.server == INVALID_SOCKET) {
    sprintf(lb.log, "FDNS: cannot open socket, error %u", WSAGetLastError());
    logMesg(lb.log, LOG_DEBUG);
    net.failureCounts[FDNS_IDX]++;
    cleanup(1);
  }

  memset(&nd.sa, 0, sizeof(nd.sa));
  memset(&nd.ca, 0, sizeof(nd.ca));

  nd.sa.sin_family = AF_INET;
  nd.sa.sin_addr.s_addr = inet_addr(config.adptrip);
  nd.sa.sin_port = htons(DNSPORT);

  if (bind(s.server, (struct sockaddr *) &nd.sa, sizeof(nd.sa)) == SOCKET_ERROR) {
    sprintf(lb.log, "FDNS: cannot bind dns port, error %u", WSAGetLastError());
    logMesg(lb.log, LOG_DEBUG);
    net.failureCounts[FDNS_IDX]++;
    cleanup(1);
  }

  len = sizeof(nd.ca);
  flags = 0;

  // change to message monitor
  do {
    n = recvfrom(s.server, m, DNSMSG_SIZE, flags, (struct sockaddr *) &nd.ca, &len);
    if (n < 0) continue;
    // Same Id
    m[2]=0x81; m[3]=0x80; // Change Opcode and flags
    m[6]=0; m[7]=1; // One answer
    m[8]=0; m[9]=0; // NSCOUNT
    m[10]=0; m[11]=0; // ARCOUNT
    // Keep request in message and add answer
    m[n++]=0xC0; m[n++]=0x0C; // Offset to the domain name
    m[n++]=0x00; m[n++]=0x01; // Type 1
    m[n++]=0x00; m[n++]=0x01; // Class 1
    m[n++]=0x00; m[n++]=0x00; m[n++]=0x00; m[n++]=0x3c; // TTL
    m[n++]=0x00; m[n++]=0x04; // Size --> 4
    m[n++]=ip4[0]; m[n++]=ip4[1]; m[n++]=ip4[2]; m[n++]=ip4[3]; // IP
    // Send the answer
    sendto(s.server, m, n, flags, (struct sockaddr *) &nd.ca, len);
  } while (fdns_running);

  cleanup(1);
}

}
#endif
