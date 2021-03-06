// monitor/net.cpp
// network functions for monitor module

#if MONITOR

#include "core.h"
#include "util.h"
#include "net.h"
#include "monitor.h"

namespace monitor {

DWORD getDevMacAddress(unsigned char* mac , const char* ip) {

  //  IPAddr srcip = inet;
  ULONG macAddr[2];
  ULONG phyAddrLen = 6;  // default to length of six bytes
  int i;
  struct in_addr destip;

  destip.s_addr = inet_addr(ip);

  DWORD ret = SendARP((IPAddr) destip.S_un.S_addr, inet_addr(config.adptrip), macAddr, &phyAddrLen);

  if (phyAddrLen) {
    BYTE *bMacAddr = (BYTE *) & macAddr;
    for (i = 0; i < (int) phyAddrLen; i++) { mac[i] = (char) bMacAddr[i]; }
  }

  return ret;
}

}
#endif
