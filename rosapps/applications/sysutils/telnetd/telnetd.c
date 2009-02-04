/*
 * File: TelnetD.c
 *
 * Abstract: a simple telnet 'daemon' for Windows hosts.
 *
 * Compiled & run successfully using MSVC 5.0 under Windows95 (requires 
 * Winsock2 update) and Windows98 and MSVC 6.0 under WindowsNT4
 *
 * Compiler options : no special options needed
 * Linker options   : add wsock32.lib or ws2_32.lib
 *
 * Written by fred.van.lieshout 'at' zonnet.nl
 * Use freely, no copyrights.
 * Use Linux.
 *
 * TODO: 
 * - access control
 * - will/won't handshake
 * - (run as) Windows NT service
 */

#include <stdio.h> 
#include <windows.h>  

/*
** macro definitions
*/
#define TELNET_PORT      (23)

#define BUFSIZE        (4096)  
#define USERID_SIZE      (64)
#define CTRLC             (3)
#define BS                (8)
#define CR               (13)
#define LF               (10)
#define DEL             (127)

#define IAC             (255)
#define DONT            (254)
#define WONT            (253)
#define DO              (252)
#define WILL            (251)
#define ECHO            (1)

#define HANDSHAKE_TIMEOUT (3)

/*
** types
*/

typedef struct client_s
{
  char     userID[USERID_SIZE];
  int      socket;
  BOOLEAN  bTerminate;
  BOOLEAN  bReadFromPipe;
  BOOLEAN  bWriteToPipe;
  HANDLE   hProcess;
  DWORD    dwProcessId;
  HANDLE   hChildStdinWr;   
  HANDLE   hChildStdoutRd;
} client_t;

typedef enum
{
  NoEcho = 0,
  Echo = 1,
  Password = 2
} EchoMode;

/*
** Local data
*/

static BOOLEAN bShutdown = 0;
static BOOLEAN bSocketInterfaceInitialised = 0;

static int sock;

/*
** Forward function declarations
*/
static BOOL WINAPI Cleanup(DWORD dwControlType);
static void WaitForConnect(void);
static BOOLEAN StartSocketInterface(void);
static void CreateSocket(void);
static void UserLogin(int client_socket);
static DWORD WINAPI UserLoginThread(LPVOID);
static int DoTelnetHandshake(int sock);
static int ReceiveLine(int sock, char *buffer, int len, EchoMode echo);
static void RunShell(client_t *client); 
//static BOOL CreateChildProcess(const char *); 
static DWORD WINAPI MonitorChildThread(LPVOID);
static DWORD WINAPI WriteToPipeThread(LPVOID); 
static DWORD WINAPI ReadFromPipeThread(LPVOID); 
static void TerminateShell(client_t *client);
static VOID ErrorExit(LPTSTR); 


/*
** main
*/
DWORD telnetd_main() 
{
  SetConsoleCtrlHandler(Cleanup, 1);

  if (!StartSocketInterface()) {
    ErrorExit("Unable to start socket interface\n");
  }

  CreateSocket();

  while(!bShutdown) {
    WaitForConnect();
  }

  WSACleanup();
  return 0;
}

/*
** Cleanup
*/
static BOOL WINAPI Cleanup(DWORD dwControlType)
{
  if (bSocketInterfaceInitialised) {
    printf("Cleanup...\n");
    WSACleanup();
  }
  return 0;
}

/*
** StartSocketInterface
*/
static BOOLEAN StartSocketInterface(void)
{
  WORD    wVersionRequested;
  WSADATA wsaData;
  int     err; 

  wVersionRequested = MAKEWORD( 2, 0 ); 
  err = WSAStartup(wVersionRequested, &wsaData);
  if (err != 0) {
    printf("requested winsock version not supported\n");
    return 0;
  } 

  bSocketInterfaceInitialised = 1; /* for ErrorExit function */

  if ( wsaData.wVersion != wVersionRequested) {
    printf("requested winsock version not supported\n");
    return 0;
  }
  printf("TelnetD, using %s\n", wsaData.szDescription);
  return 1;
}

/*
** CreateSocket
*/
static void CreateSocket(void)
{
   struct sockaddr_in sa;

   sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
   if (sock < 0) {
     ErrorExit("Cannot create socket");
   }

   memset(&sa, 0, sizeof(sa));
   sa.sin_family = AF_INET;
   sa.sin_addr.s_addr = INADDR_ANY;
   sa.sin_port = htons(TELNET_PORT);
   if (bind(sock, (struct sockaddr*) &sa, sizeof(sa)) != 0) {
      ErrorExit("Cannot bind address to socket");
   }
}

/*
** WaitForConnect
*/
static void WaitForConnect(void)
{
  struct sockaddr_in sa;
  int new_sock;

  if (listen(sock, 1) < 0) {
     ErrorExit("Cannot listen on socket");
  }

  if ((new_sock = accept(sock, (struct sockaddr*) &sa, NULL)) < 0) {
    fprintf(stderr, "Failed to accept incoming call\n");
  } else {
    printf("user connected on socket %d, port %d, address %lx\n", new_sock,
                                       htons(sa.sin_port), sa.sin_addr.s_addr);
    UserLogin(new_sock);
  }
}


/*
** Function: UserLogin
*/
static void UserLogin(int client_socket)
{
  DWORD      threadID;
  client_t  *client = malloc(sizeof(client_t));

  if (client == NULL) {
    ErrorExit("failed to allocate memory for client");
  }

  client->socket = client_socket;
  CreateThread(NULL, 0, UserLoginThread, client, 0, &threadID);
}

/*
** Function: UserLoginThread
*/
static DWORD WINAPI UserLoginThread(LPVOID data)
{
  client_t  *client = (client_t *) data;
  char       welcome[256];
  char       hostname[64] = "Unknown";
  char      *pwdPrompt = "\r\npass:";
  //char      *logonPrompt = "\r\nLogin OK, please wait...";
  //char      *byebye = "\r\nWrong! bye bye...\r\n";
  char       userID[USERID_SIZE];
  char       password[USERID_SIZE];
  int        received;
  char      *terminator;

  if (DoTelnetHandshake(client->socket)) {
    closesocket(client->socket);
    free(client);
    return 0;
  }

  gethostname(hostname, sizeof(hostname));
  sprintf(welcome, "\r\nWelcome to %s, please identify yourself\r\n\r\nuser:", hostname);

  if (send(client->socket, welcome, strlen(welcome), 0) < 0) {   
    closesocket(client->socket);
    free(client);
    return 0;
  }
  received = ReceiveLine(client->socket, userID, sizeof(userID), Echo );
  if (received < 0) {
    closesocket(client->socket);
    free(client);
    return 0;
  } else if (received) {
    if ((terminator = strchr(userID, CR)) != NULL) {
      *terminator = '\0';
    }
  }

  if (send(client->socket, pwdPrompt, strlen(pwdPrompt), 0) < 0) {   
    closesocket(client->socket);
    free(client);
    return 0;
  }
  received = ReceiveLine(client->socket, password, sizeof(password), Password );

#if 0
  if (received < 0) {
    closesocket(client->socket);
    free(client);
    return 0;
  } else if (received) {
    if ((terminator = strchr(password, CR)) != NULL) {
      *terminator = '\0';
    }
  }
#endif

  /* TODO: do authentication here */

  
  printf("User '%s' logged on\n", userID);
#if 0
  strcpy(client->userID, userID);
  if (send(client->socket, logonPrompt, strlen(logonPrompt), 0) < 0) {   
    closesocket(client->socket);
    free(client);
    return 0;
  }
#endif
  RunShell(client);
  return 0;
}

/*
** Function: DoTelnetHandshake
*/
static int DoTelnetHandshake(int sock)
{
  int retval;
  int received;
  fd_set set;
  struct timeval timeout = { HANDSHAKE_TIMEOUT, 0 };
  unsigned char will_echo[3] = { IAC, WILL, ECHO };
  unsigned char client_reply[256];

  if (send(sock, (const char *) will_echo, sizeof(will_echo), 0) < 0) {   
    return -1;
  }

  /* Now wait for client response (and ignore it) */
  FD_ZERO(&set);
  FD_SET(sock, &set);

  do {
    retval = select(0, &set, NULL, NULL, &timeout);
    /* check for error */
    if (retval < 0) {
      return -1;
      /* check for timeout */
    } else if (retval == 0) {
      return 0;
    }
    /* no error and no timeout, we have data in our sock */
    received = recv(sock, (char *) client_reply, sizeof(client_reply), 0);
    if (received <= 0) {
     return -1;
    }
  } while (retval);

  return 0;
}

/*
** Function: ReceiveLine
**
** Abstract: receive until timeout or CR
** In      : sock, len
** Out     : buffer
** Result  : int
** Pre     : 'sock' must be valid socket
** Post    : (result = the number of bytes read into 'buffer')
**           OR (result = -1 and error)
*/
static int ReceiveLine(int sock, char *buffer, int len, EchoMode echo)
{
  int            i = 0;
  int            retval;
  fd_set         set;
  struct timeval timeout = { 0, 100000 };
  char           del[3] = { BS, ' ', BS };
  char           asterisk[1] = { '*' };

  FD_ZERO(&set);
  FD_SET(sock, &set);

  memset(buffer, '\0', len);

  do {
    /* When we're in echo mode, we do not need a timeout */
    retval = select(0, &set, NULL, NULL, (echo ? NULL : &timeout) );
    /* check for error */
    if (retval < 0) {
      return -1;
      /* check for timeout */
    } else if (retval == 0) {
      /* return number of characters received so far */
      return i;
    }
    /* no error and no timeout, we have data in our sock */
    if (recv(sock, &buffer[i], 1, 0) <= 0) {
      return -1;
    }
    if ((buffer[i] == '\0') || (buffer[i] == LF)) {
      /* ignore null characters and linefeeds from DOS telnet clients */
      buffer[i] = '\0';
    } else if ((buffer[i] == DEL) || (buffer[i] == BS)) {
      /* handle delete and backspace */
      buffer[i] = '\0';
      if (echo) {
	      if (i > 0) {
          i--;
          buffer[i] = '\0';
          if (send(sock, del, sizeof(del), 0) < 0) {
            return -1;
          }
        }
      } else {
        buffer[i] = BS;  /* Let shell process handle it */
	      i++;
      }
    } else {
      /* echo typed characters */
      if (echo == Echo && send(sock, &buffer[i], 1, 0) < 0) {
        return -1;
      } else if (echo == Password && send(sock, asterisk, sizeof(asterisk), 0) < 0) {
        return -1;
      }
      if (buffer[i] == CR) {
        i++;
        buffer[i] = LF; /* append LF for DOS command processor */
        i++;
        return i;
      }

      i++;
    }    
  } while (i < len);

  return i;
}

/*
** Function: RunShell
*/
static void RunShell(client_t *client) 
{ 
   DWORD                 threadID;
   HANDLE                hChildStdinRd;
   HANDLE                hChildStdinWr;
   HANDLE                hChildStdoutRd;
   HANDLE                hChildStdoutWr;
   STARTUPINFO           si;
   PROCESS_INFORMATION   piProcInfo;
   SECURITY_ATTRIBUTES   saAttr;

   const char *name = "c:\\windows\\system32\\cmd.exe";
   const char *cmd = NULL;
   //const char *name = "d:\\cygwin\\bin\\bash.exe";
   //const char *cmd = "d:\\cygwin\\bin\\bash.exe --login -i";
   
   saAttr.nLength = sizeof(SECURITY_ATTRIBUTES); 
   saAttr.bInheritHandle = TRUE; 
   saAttr.lpSecurityDescriptor = NULL; 
   
   // Create a pipe for the child process's STDOUT.  
   if (! CreatePipe(&hChildStdoutRd, &hChildStdoutWr, &saAttr, 0)) 
      ErrorExit("Stdout pipe creation failed\n");  

   if (! CreatePipe(&hChildStdinRd, &hChildStdinWr, &saAttr, 0)) 
      ErrorExit("Stdin pipe creation failed\n");  


   client->bTerminate = FALSE;
   client->bWriteToPipe = TRUE;
   client->bReadFromPipe = TRUE;
   client->hChildStdinWr = hChildStdinWr;   
   client->hChildStdoutRd = hChildStdoutRd;


   // Create the child process (the shell)
   printf("Creating child process...\n");

   ZeroMemory( &si, sizeof(STARTUPINFO) );
   si.cb = sizeof(STARTUPINFO);  

   si.dwFlags = STARTF_USESTDHANDLES;
   si.hStdInput = hChildStdinRd;
   si.hStdOutput = hChildStdoutWr;
   si.hStdError = hChildStdoutWr;

   //si.dwFlags |= STARTF_USESHOWWINDOW;
   //si.wShowWindow = SW_SHOW;

   if (!CreateProcess((LPSTR) name,              // executable module
                      (LPSTR) cmd,               // command line 
                      NULL,                      // process security attributes 
                      NULL,                      // primary thread security attributes 
                      TRUE,                      // handles are inherited 
                      DETACHED_PROCESS +         // creation flags 
                      CREATE_NEW_PROCESS_GROUP,
                      NULL,                      // use parent's environment 
                      NULL,                      // use parent's current directory 
                      &si,                       // startup info
                      &piProcInfo)) {
     ErrorExit("Create process failed");
   }

   client->hProcess = piProcInfo.hProcess;
   client->dwProcessId = piProcInfo.dwProcessId;

   printf("New child created (groupid=%lu)\n", client->dwProcessId);

   // No longer need these in the parent...
   if (!CloseHandle(hChildStdoutWr)) 
     ErrorExit("Closing handle failed");  
   if (!CloseHandle(hChildStdinRd)) 
     ErrorExit("Closing handle failed");  

   CreateThread(NULL, 0, WriteToPipeThread, client, 0, &threadID);
   CreateThread(NULL, 0, ReadFromPipeThread, client, 0, &threadID);
   CreateThread(NULL, 0, MonitorChildThread, client, 0, &threadID);
} 


/*
** Function: MonitorChildThread
**
** Abstract: Monitor the child (shell) process
*/
static DWORD WINAPI MonitorChildThread(LPVOID data)
{
  DWORD exitCode;
  client_t *client = (client_t *) data;

  printf("Monitor thread running...\n");

  WaitForSingleObject(client->hProcess, INFINITE);

  GetExitCodeProcess(client->hProcess, &exitCode);
  printf("Child process terminated with code %lx\n", exitCode);

  /* signal the other threads to give up */
  client->bTerminate = TRUE;

  Sleep(500);

  CloseHandle(client->hChildStdoutRd);
  CloseHandle(client->hChildStdinWr);       
  CloseHandle(client->hProcess);

  closesocket(client->socket);

  printf("Waiting for all threads to give up..\n");

  while (client->bWriteToPipe || client->bReadFromPipe) {
    printf(".");
    fflush(stdout);
    Sleep(1000);
  }

  printf("Cleanup for user '%s'\n", client->userID);
  free(client);
  return 0;
}

/*
** Function: WriteToPipeThread
** 
** Abstract: read data from the telnet client socket
**           and pass it on to the shell process.
*/
static DWORD WINAPI WriteToPipeThread(LPVOID data)
{
  int       iRead;
  DWORD     dwWritten;
  CHAR      chBuf[BUFSIZE];
  client_t *client = (client_t *) data;

  while (!client->bTerminate) {
    iRead = ReceiveLine(client->socket, chBuf, BUFSIZE, FALSE);
    if (iRead < 0) {
      printf("Client disconnect\n");
      break;
    } else if (iRead > 0) {
      if (strchr(chBuf, CTRLC)) {
        GenerateConsoleCtrlEvent(CTRL_C_EVENT, client->dwProcessId);
      }
      if (send(client->socket, chBuf, iRead, 0) < 0) {
		 printf("error writing to socket\n");
         break;    
	  }
      if (! WriteFile(client->hChildStdinWr, chBuf, (DWORD) iRead, &dwWritten, NULL)) {
        printf("Error writing to pipe\n");
        break;
      }
    }
  }

  if (!client->bTerminate) {
    TerminateShell(client);
  }

  printf("WriteToPipeThread terminated\n");

  client->bWriteToPipe = FALSE;
  return 0;
}

/*
** Function: ReadFromPipeThread
**
** Abstract: Read data from the shell's stdout handle and
**           pass it on to the telnet client socket.
*/
static DWORD WINAPI ReadFromPipeThread(LPVOID data) 
{    
  DWORD dwRead;
  DWORD dwAvail;
  CHAR chBuf[BUFSIZE];
  CHAR txBuf[BUFSIZE*2];
  DWORD from,to;
  //char warning[] = "warning: rl_prep_terminal: cannot get terminal settings";

  client_t *client = (client_t *) data;

  while (!client->bTerminate && client->bWriteToPipe) {
    // Since we do not want to block, first peek...
    if (PeekNamedPipe(client->hChildStdoutRd, NULL, 0, NULL, &dwAvail, NULL) == 0) {
      printf("Failed to peek in pipe\n");
      break;
    }
    if (dwAvail) {
      if( ! ReadFile( client->hChildStdoutRd, chBuf, BUFSIZE, &dwRead, NULL) ||
           dwRead == 0) {
        printf("Failed to read from pipe\n");
        break;
      }
	  for (from=0, to=0; from<dwRead; from++, to++) {
        txBuf[to] = chBuf[from];
		if (txBuf[to] == '\n') {
			txBuf[to] = '\r';
			to++;
			txBuf[to] = '\n';
		}
	  }
      if (send(client->socket, txBuf, to, 0) < 0) {
		 printf("error writing to socket\n");
         break;    
	  }
	}
    Sleep(100); /* Hmmm, oh well... what the heck! */
  }

  if (!client->bTerminate) {
    TerminateShell(client);
  }

  printf("ReadFromPipeThread terminated\n");

  client->bReadFromPipe = FALSE;
  return 0;
}

/*
** TerminateShell
*/ 
static void TerminateShell(client_t *client)
{
  DWORD exitCode;
  DWORD dwWritten;
  char stop[] = "\003\r\nexit\r\n"; /* Ctrl-C + exit */

  GetExitCodeProcess(client->hProcess, &exitCode);
  if (exitCode == STILL_ACTIVE) {
    printf("user shell still active, send Ctrl-Break to group-id %lu\n", client->dwProcessId );

    if (!GenerateConsoleCtrlEvent( CTRL_BREAK_EVENT, client->dwProcessId )) {
      printf("Failed to send Ctrl_break\n");
    }

    Sleep(500);

    if (!GenerateConsoleCtrlEvent( CTRL_C_EVENT, client->dwProcessId )) {
      printf("Failed to send Ctrl_C\n");
    }

    Sleep(500);

    if (! WriteFile(client->hChildStdinWr, stop, sizeof(stop), &dwWritten, NULL)) {
        printf("Error writing to pipe\n");
    }

    Sleep(500);

    GetExitCodeProcess(client->hProcess, &exitCode);
    if (exitCode == STILL_ACTIVE) {
      printf("user shell still active, attempt to terminate it now...\n");
      TerminateProcess(client->hProcess, 0);
    }
  }
}

/*
** ErrorExit
*/
static VOID ErrorExit (LPTSTR lpszMessage) 
{ 
   fprintf(stderr, "%s\n", lpszMessage);
   if (bSocketInterfaceInitialised) {
     printf("WSAGetLastError=%d\n", WSAGetLastError());
     WSACleanup();
   }
   ExitProcess(0); 
} 
