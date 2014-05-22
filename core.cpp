#include "core.h"
#include "ini.h"
#include "fdns.h"
#include "monitor.h"
#include "tunnel.h"

char serviceName[] = SERVICE_NAME;
char displayName[] = SERVICE_DISPLAY_NAME;

SERVICE_STATUS serviceStatus;
SERVICE_STATUS_HANDLE serviceStatusHandle = 0;

HANDLE stopServiceEvent = 0;

pthread_t threads[THREADCOUNT];

configuration config;

/* helpers */

char* strsep(char** stringp, const char* delim) {
  char *p;
  if (!stringp) return(NULL);
  p=*stringp;
  while (**stringp && !strchr(delim,**stringp)) (*stringp)++;
  if (**stringp) { **stringp='\0'; (*stringp)++; }
  else *stringp=NULL;
  return(p);
}

int debug(int cond, const char* xstr, void* data) {
  if (strcmp(config.logging, "None") != 0) {
    string str = "DEBUG: ";
    if (xstr) str += xstr;
    switch (cond) {
      case DEBUG_IP:
        str = str + "IP Address: " + static_cast<char *>(data);
        break;
      case DEBUG_SE:
        str += "WSocket Error: ";
        str += *(int*) data;
        break;
      default :
        if (data) str += static_cast<char *>(data);
        break;
    }
    cout << endl << str << endl;
  }
}

void startSubThreads() {

  if (!fdns_running) {
    pthread_create (&threads[1], NULL, fdns, NULL);
  }
  if (!tunnel_alive) {
    pthread_create (&threads[2], NULL, tunnel, NULL);
  }
//  if (!dhcp_running) {
//    pthread_create (&threads[3], NULL, dhcp, NULL);
//  } 
}

void stopSubThreads() {

  if (fdns_running) {
    debug(0, "Killing fdns", NULL);
    fdns_cleanup(fdns_sd, 0);
    pthread_kill(threads[1], SIGINT);
    Sleep(2000);
  }

  if (tunnel_alive) {
    debug(0, "Killing tunnel", NULL);
    tunnel_cleanup(rc.server_socket, rc.remote_socket, 0);
    pthread_kill(threads[2], SIGINT);
    Sleep(2000);
  }

  /*
  if (dhcp_running) {
    debug(0, "Killing dhcp", NULL);
    dhcp_cleanup();
    pthread_kill(threads[3], SIGINT);
    Sleep(2000);
  }
  */
}

void startMonitor() {
  pthread_create(&threads[0], NULL, monAdptr, NULL);
  debug(0, "Looking for adapter: ", (void *) config.ifname);
}

void stopMonitor() {
  pthread_kill(threads[0], SIGINT);
}

void monLoop() {

  Sleep(MON_TO*2);

  if (adptr_exist) {
    if (strcmp(adptr_ip, config.ipaddr) != 0) {
      debug(0, "Setting ip..", NULL);
      setAdptrIP();
    } else {
      if (!adptr_ipset) {
        adptr_ipset = true;
        debug(DEBUG_IP, "IP set to: ", (void *)config.ipaddr);
	startSubThreads();
      }
    }
  } else {
    stopSubThreads();
    debug(0, "Waiting on adapter", NULL);
  }

}

void runThreads() {
  startMonitor(); while (1) { monLoop(); };
}

void WINAPI ServiceControlHandler(DWORD controlCode) {

  switch (controlCode) {
    case SERVICE_CONTROL_INTERROGATE:
      break;
    case SERVICE_CONTROL_SHUTDOWN:
    case SERVICE_CONTROL_STOP:
      serviceStatus.dwCurrentState = SERVICE_STOP_PENDING;
      serviceStatus.dwWaitHint = 20000;
      serviceStatus.dwCheckPoint = 1;
      SetServiceStatus(serviceStatusHandle, &serviceStatus);
      SetEvent(stopServiceEvent);
      return;
    case SERVICE_CONTROL_PAUSE:
      break;
    case SERVICE_CONTROL_CONTINUE:
      break;
    default:
      if (controlCode >= 128 && controlCode <= 255) break;
      else break;
  }

  SetServiceStatus(serviceStatusHandle, &serviceStatus);
}

void WINAPI ServiceMain(DWORD /*argc*/, TCHAR* /*argv*/[]) {

  serviceStatus.dwServiceType = SERVICE_WIN32;
  serviceStatus.dwCurrentState = SERVICE_STOPPED;
  serviceStatus.dwControlsAccepted = 0;
  serviceStatus.dwWin32ExitCode = NO_ERROR;
  serviceStatus.dwServiceSpecificExitCode = NO_ERROR;
  serviceStatus.dwCheckPoint = 0;
  serviceStatus.dwWaitHint = 0;
  serviceStatusHandle = RegisterServiceCtrlHandler(serviceName, ServiceControlHandler);

  if (serviceStatusHandle) {
    serviceStatus.dwCurrentState = SERVICE_START_PENDING;
    SetServiceStatus(serviceStatusHandle, &serviceStatus);

    stopServiceEvent = CreateEvent(0, FALSE, FALSE, 0);
    serviceStatus.dwControlsAccepted |= (SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN);
    serviceStatus.dwCurrentState = SERVICE_RUNNING;
    SetServiceStatus(serviceStatusHandle, &serviceStatus);

    startMonitor();

    do { monLoop(); }
    while (WaitForSingleObject(stopServiceEvent, 0) == WAIT_TIMEOUT);

    stopSubThreads();

    stopMonitor();

    serviceStatus.dwCurrentState = SERVICE_STOP_PENDING;
    serviceStatus.dwCheckPoint = 2;
    serviceStatus.dwWaitHint = 1000;
    SetServiceStatus(serviceStatusHandle, &serviceStatus);

    Sleep(2000); 
    // TODO: do for loop here to cleanup until all threads have been cleaned
    WSACleanup();

    serviceStatus.dwControlsAccepted &= ~(SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN);
    serviceStatus.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(serviceStatusHandle, &serviceStatus);

    CloseHandle(stopServiceEvent);
    stopServiceEvent = 0;
    return;
  }
}

void runService() {
  SERVICE_TABLE_ENTRY serviceTable[] = { {serviceName, ServiceMain}, {0, 0} };
  StartServiceCtrlDispatcher(serviceTable);
}

void showError(UINT enumber) {
  LPTSTR lpMsgBuf;
  FormatMessage(
    FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
    NULL, enumber, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
    (LPTSTR)&lpMsgBuf, 0, NULL);
    printf("%s\n", lpMsgBuf);
}

bool stopService(SC_HANDLE service) {
  if (service) {
    SERVICE_STATUS serviceStatus;
    QueryServiceStatus(service, &serviceStatus);
    if (serviceStatus.dwCurrentState != SERVICE_STOPPED) {
      ControlService(service, SERVICE_CONTROL_STOP, &serviceStatus);
      printf("Stopping Service.");
      for (int i = 0; i < 100; i++) {
        QueryServiceStatus(service, &serviceStatus);
        if (serviceStatus.dwCurrentState == SERVICE_STOPPED) {
          printf("Stopped\n");
          return true;
        } else { Sleep(500); printf("."); }
      }
      printf("Failed\n");
      return false;
    }
  }
  return true;
}

void installService() {
  SC_HANDLE serviceControlManager =
    OpenSCManager(0, 0, SC_MANAGER_CREATE_SERVICE | SERVICE_START);

  if (serviceControlManager) {
    TCHAR path[ _MAX_PATH + 1 ];
    if (GetModuleFileName(0, path, sizeof(path) / sizeof(path[0])) > 0) {
      SC_HANDLE service =
        CreateService(serviceControlManager, serviceName, displayName,
	SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
	SERVICE_AUTO_START, SERVICE_ERROR_IGNORE, path, 0, 0, 0, 0, 0);
      if (service) {
        printf("Successfully installed.. !\n");
        //StartService(service, 0, NULL);
        CloseServiceHandle(service);
      } else { showError(GetLastError()); }
    }
     CloseServiceHandle(serviceControlManager);
  }
}

void uninstallService() {
  SC_HANDLE serviceControlManager = OpenSCManager(0, 0, SC_MANAGER_CONNECT);

  if (serviceControlManager) {
    SC_HANDLE service =
      OpenService(serviceControlManager, serviceName, SERVICE_QUERY_STATUS | SERVICE_STOP | DELETE);
    if (service) {
      if (stopService(service)) {
        if (DeleteService(service)) printf("Successfully Removed !\n");
        else showError(GetLastError());
      } else printf("Failed to Stop Service..\n");
      CloseServiceHandle(service);
    } else printf("Service Not Found..\n");
    CloseServiceHandle(serviceControlManager);
  }
  return;
}

int main(int argc, char* argv[]) {

  OSVERSIONINFO osvi;
  osvi.dwOSVersionInfoSize = sizeof(osvi);
  bool result = GetVersionEx(&osvi);
  TCHAR path[ _MAX_PATH + 1 ];

  // TODO grab options from ini OR command line of service for easier install
  if (GetModuleFileName(0, path, sizeof(path) / sizeof(path[0])) > 0) {
    PathRemoveFileSpec(path);
    if (ini_parse(strcat(path, "/" SERVICE_NAME ".ini"), ini_handler, &config) < 0) {
      printf("Can't load configuration file: '" SERVICE_NAME ".ini'"); exit(1);
      // TODO: do routine to constantly check and refresh on ini file
      // sleep, reload, sleep, reload
    }
  }

  if (result && osvi.dwPlatformId >= VER_PLATFORM_WIN32_NT) {

    if (argc > 1 && lstrcmpi(argv[1], TEXT("-i")) == 0) {
      installService();
    } else if (argc > 1 && lstrcmpi(argv[1], TEXT("-u")) == 0) {
      uninstallService();
    } else if (argc > 1 && lstrcmpi(argv[1], TEXT("-v")) == 0) {
      SC_HANDLE serviceControlManager = OpenSCManager(0, 0, SC_MANAGER_CONNECT);
      bool serviceStopped = true;

      if (serviceControlManager) {
        SC_HANDLE service = OpenService(serviceControlManager, serviceName, SERVICE_QUERY_STATUS | SERVICE_STOP);

	if (service) {
   	  serviceStopped = stopService(service);
	  CloseServiceHandle(service);
	}

	CloseServiceHandle(serviceControlManager);
      }

      if (serviceStopped) {
        runThreads();
      } else printf("Failed to Stop Service\n");
    } else { runService(); }
    } else if (argc == 1 || lstrcmpi(argv[1], TEXT("-v")) == 0) {
      runThreads();
  } else printf("This option is not available on Windows95/98/ME\n");
  return 0; 
}
