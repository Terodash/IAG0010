#include "stdafx.h"
#include "Winsock2.h" // necessary for sockets, Windows.h is not needed.
#include "mswsock.h"
#include "process.h"  // necessary for threading
#include "time.h"

#pragma warning(disable : 4996)

//
// Global variables
//
TCHAR CommandBuf[81];
HANDLE hCommandGot;       // event "the user has typed a command"
HANDLE hStopCommandGot;   // event "the main thread has recognized that it was the stop command"
HANDLE hCommandProcessed; // event "the main thread has finished the processing of command"
HANDLE hReadKeyboard;     // keyboard reading thread handle
HANDLE hStdIn;			  // stdin standard input stream handle
WSADATA WsaData;          // filled during Winsock initialization 
DWORD Error;
SOCKET hClientSocket = INVALID_SOCKET;
sockaddr_in ClientSocketInfo;
HANDLE hReceiveNet;       // TCP/IP info reading thread handle
BOOL SocketError;
BOOL conAccepted = FALSE;
BOOL measStarted = FALSE;
BOOL hRDataProcessed = FALSE;
_TCHAR FilePath[2048];
HANDLE hOutputFile;

//
// Prototypes
//
unsigned int __stdcall ReadKeyboard(void* pArguments); //reads keyboard
unsigned int __stdcall ReceiveNet(void* pArguments); //receives data from server
const char * parseData(char *data); //parses received data, outputs to console and writes to file
void sendCommand(SOCKET hClientSocket, wchar_t *command); //creates package and sends command to server
void evalResponse(WSABUF DataBuf); //evaluates the server's response, and acts accordingly
BOOL estCon(); //establishes connection with server
void createFile(); //creates the file 
int writeToFile(char * data, HANDLE hOutputFile); //writes chunks of data to file

//****************************************************************************************************************
//                                 MAIN THREAD
//****************************************************************************************************************
int _tmain(int argc, _TCHAR* argv[])
{
	//_tprintf(_T("\tIN:main\n"));
	//
	// Initializations for multithreading
	//
	if (argc != 0) {
		memcpy(FilePath, argv[1], wcslen(argv[1])*sizeof(wchar_t));
		_tprintf(_T("%s\n"), FilePath);
		createFile();
	}

	if (!(hCommandGot = CreateEvent(NULL, TRUE, FALSE, NULL)) ||
		!(hStopCommandGot = CreateEvent(NULL, TRUE, FALSE, NULL)) ||
		!(hCommandProcessed = CreateEvent(NULL, TRUE, TRUE, NULL)))
	{
		_tprintf(_T("CreateEvent() failed, error %d\n"), GetLastError());
		return 1;
	}
	//
	// Prepare keyboard, start the thread
	//
	hStdIn = GetStdHandle(STD_INPUT_HANDLE);
	if (hStdIn == INVALID_HANDLE_VALUE)
	{
		_tprintf(_T("GetStdHandle() failed, error %d\n"), GetLastError());
		return 1;
	}
	if (!SetConsoleMode(hStdIn, ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT))
	{
		_tprintf(_T("SetConsoleMode() failed, error %d\n"), GetLastError());
		return 1;
	}
	if (!(hReadKeyboard = (HANDLE)_beginthreadex(NULL, 0, &ReadKeyboard, NULL, 0, NULL)))
	{
		_tprintf(_T("Unable to create keyboard thread\n"));
		return 1;
	}
	//
	// Main processing loop
	//
	while (TRUE)
	{
		//_tprintf(_T("\tIN:main\n"));
		if (WaitForSingleObject(hCommandGot, INFINITE) != WAIT_OBJECT_0)
		{ // Wait until the command has arrived (i.e. until CommandGot is signaled)
			_tprintf(_T("WaitForSingleObject() failed, error %d\n"), GetLastError());
			goto out;
		}
		ResetEvent(hCommandGot); // CommandGot back to unsignaled
		if (!_tcsicmp(CommandBuf, _T("exit"))) // Case-insensitive comparation
		{
			SetEvent(hStopCommandGot); // To force the other threads to quit
			break;
		}
		else if (!_tcsicmp(CommandBuf, _T("connect")) && !conAccepted) {
			if (!estCon()) {
				_tprintf(_T("Connection could not be established. Check if the server is running and try again.\n"));
				SocketError = FALSE;
			}
			else {
				_tprintf(_T("Connection established\n"));
			}
			SetEvent(hCommandProcessed);
		}
		else if (!wcscmp(CommandBuf, _T("Start"))) // Case-insensitive comparation
		{
			if (conAccepted && !measStarted) {
				_tprintf(_T("Entered command: %s\n"), CommandBuf);
				sendCommand(hClientSocket, CommandBuf);
				SetEvent(hCommandProcessed);
				measStarted = TRUE;
			}
			else {
				_tprintf(_T("Connection has not been established yet. Type \"connect\" to connect.\n"));
				SetEvent(hCommandProcessed);
			}
		}
		else if (!wcscmp(CommandBuf, _T("Break"))) // Case-insensitive comparation
		{
			if (conAccepted && measStarted) {
				_tprintf(_T("Entered command: %s\n"), CommandBuf);
				sendCommand(hClientSocket, CommandBuf);
				SetEvent(hCommandProcessed);
				measStarted = FALSE;
			}
			else if (conAccepted) {
				_tprintf(_T("Measurements have not begun yet. Type \"Start\" to start measurements.\n"));
				SetEvent(hCommandProcessed);
			}
			else {
				_tprintf(_T("Connection has not been established yet. Type \"connect\" to connect.\n"));
				SetEvent(hCommandProcessed);
			}
		}
		else if (!wcscmp(CommandBuf, _T("Stop"))) // Case-insensitive comparation
		{
			if (conAccepted) {
				_tprintf(_T("Entered command: %s\n"), CommandBuf);
				sendCommand(hClientSocket, CommandBuf);
				SetEvent(hCommandProcessed);
				measStarted = FALSE;
			}
			else {
				_tprintf(_T("Connection has not been established yet. Type \"connect\" to connect.\n"));
				SetEvent(hCommandProcessed);
			}
		}
		else if (!wcscmp(CommandBuf, _T("Ready"))) // Case-insensitive comparation
		{
			if (conAccepted && measStarted) {
				_tprintf(_T("Entered command: %s\n"), CommandBuf);
				sendCommand(hClientSocket, CommandBuf);
				SetEvent(hCommandProcessed);
			}
			else if (conAccepted) {
				_tprintf(_T("Measurements have not begun yet. Type \"Start\" to start measurements.\n"));
				SetEvent(hCommandProcessed);
			}
			else {
				_tprintf(_T("Connection has not been established yet. Type \"connect\" to connect.\n"));
				SetEvent(hCommandProcessed);
			}
		}
		else
		{
			_tprintf(_T("Command \"%s\" not recognized\n"), CommandBuf);
			SetEvent(hCommandProcessed); // To allow the keyboard reading thread to continue
		}
	}
	//
	// Shut down
	//
out:
	if (hReadKeyboard)
	{
		WaitForSingleObject(hReadKeyboard, INFINITE); // Wait until the end of keyboard thread
		CloseHandle(hReadKeyboard);
	}
	if (hReceiveNet)
	{
		WaitForSingleObject(hReceiveNet, INFINITE); // Wait until the end of receive thread
		CloseHandle(hReceiveNet);
	}
	if (hClientSocket != INVALID_SOCKET)
	{
		if (shutdown(hClientSocket, SD_RECEIVE) == SOCKET_ERROR)
		{
			if ((Error = WSAGetLastError()) != WSAENOTCONN) // WSAENOTCONN means that the connection was not established,
															// so the shut down was senseless
				_tprintf(_T("shutdown() failed, error %d\n"), WSAGetLastError());
		}
		closesocket(hClientSocket);
	}
	WSACleanup(); // clean Windows sockets support
	CloseHandle(hStopCommandGot);
	CloseHandle(hCommandGot);
	CloseHandle(hCommandProcessed);
	CloseHandle(hOutputFile);
	return 0;
}

//**************************************************************************************************************
//                          KEYBOARD READING THREAD
//**************************************************************************************************************
unsigned int __stdcall ReadKeyboard(void* pArguments)
{
	//_tprintf(_T("\tIN:ReadKeyboard\n"));
	DWORD nReadChars;
	HANDLE KeyboardEvents[2];
	KeyboardEvents[1] = hCommandProcessed;
	KeyboardEvents[0] = hStopCommandGot;
	DWORD WaitResult;
	//
	// Reading loop
	//
	while (TRUE)
	{
		//_tprintf(_T("\tIN:ReadKeyboard\n"));
		WaitResult = WaitForMultipleObjects(2, KeyboardEvents,
			FALSE, // wait until one of the events becomes signaled
			INFINITE);
		// Waiting until hCommandProcessed or hStopCommandGot becomes signaled. Initially hCommandProcessed
		// is signaled, so at the beginning WaitForMultipleObjects() returns immediately with WaitResult equal
		// with WAIT_OBJECT_0 + 1.
		if (WaitResult == WAIT_OBJECT_0)
			return 0;  // Stop command, i.e. hStopCommandGot is signaled
		else if (WaitResult == WAIT_OBJECT_0 + 1)
		{ // If the signaled event is hCommandProcessed, the WaitResult is WAIT_OBJECT_0 + 1
			_tprintf(_T("Insert command\n"));
			if (!ReadConsole(hStdIn, CommandBuf, 80, &nReadChars, NULL))
			{ // The problem is that when we already are in this function, the only way to leave it
			  // is to type something and then press ENTER. So we cannot step into this function at any moment.
			  // WaitForMultipleObjects() prevents it.
				_tprintf(_T("ReadConsole() failed, error %d\n"), GetLastError());
				return 1;
			}
			CommandBuf[nReadChars - 2] = 0; // The command in buf ends with "\r\n", we have to get rid of them
			ResetEvent(hCommandProcessed);
			// Set hCommandProcessed to non-signaled. Therefore WaitForMultipleObjects() blocks the keyboard thread.
			// When the main thread has ended the analyzing of command, it sets hCommandprocessed or hStopCommandGot
			// to signaled and the keyboard thread can continue.
			SetEvent(hCommandGot);
			// Set hCommandGot event to signaled. Due to that WaitForSingleObject() in the main thread
			// returns, the waiting stops and the analyzing of inserted command may begin
		}
		else
		{	// waiting failed
			_tprintf(_T("WaitForMultipleObjects()failed, error %d\n"), GetLastError());
			return 1;
		}
	}
	return 0;
}

//********************************************************************************************************************
//                          TCP/IP INFO RECEIVING THREAD
//********************************************************************************************************************
unsigned int __stdcall ReceiveNet(void* pArguments)
{
	//_tprintf(_T("\tIN:ReceiveNet\n"));
	//
	// Preparations
	//
	WSABUF DataBuf;  // Buffer for received data is a structure
	char ArrayInBuf[2048];
	DataBuf.buf = &ArrayInBuf[0];
	DataBuf.len = 2048;
	DWORD nReceivedBytes = 0, ReceiveFlags = 0;
	HANDLE NetEvents[2];
	NetEvents[0] = hStopCommandGot;
	WSAOVERLAPPED Overlapped;
	memset(&Overlapped, 0, sizeof Overlapped);
	Overlapped.hEvent = NetEvents[1] = WSACreateEvent(); // manual and nonsignaled
	DWORD Result, Error;
	//
	// Receiving loop
	//
	while (TRUE)
	{
		//_tprintf(_T("\tIN:ReceiveNet\n"));
		Result = WSARecv(hClientSocket,
			&DataBuf,
			1,  // no comments here
			&nReceivedBytes,
			&ReceiveFlags, // no comments here
			&Overlapped,
			NULL);  // no comments here
		if (Result == SOCKET_ERROR)
		{  // Returned with socket error, let us examine why
			if ((Error = WSAGetLastError()) != WSA_IO_PENDING)
			{  // Unable to continue, for example because the server has closed the connection
				_tprintf(_T("WSARecv() failed, error %d\n"), Error);
				conAccepted = FALSE;
				goto out;
			}
			DWORD WaitResult = WSAWaitForMultipleEvents(2, NetEvents, FALSE, WSA_INFINITE, FALSE); // wait for data
			switch (WaitResult) // analyse why the waiting ended
			{
			case WAIT_OBJECT_0:
				// Waiting stopped because hStopCommandGot has become signaled, i.e. the user has decided to exit
				conAccepted = FALSE;
				goto out;
			case WAIT_OBJECT_0 + 1:
				// Waiting stopped because Overlapped.hEvent is now signaled, i.e. the receiving operation has ended. 
				// Now we have to see how many bytes we have got.
				WSAResetEvent(NetEvents[1]); // to be ready for the next data package
				if (WSAGetOverlappedResult(hClientSocket, &Overlapped, &nReceivedBytes, FALSE, &ReceiveFlags))
				{
					_tprintf(_T("%d bytes received\n"), nReceivedBytes);
					//_tprintf(_T("Received response: %hs\n"), DataBuf.buf + sizeof(int));
						   // Here should follow the processing of received data
					evalResponse(DataBuf);

					break;
				}
				else
				{	// Fatal problems
					_tprintf(_T("WSAGetOverlappedResult() failed, error %d\n"), GetLastError());
					conAccepted = FALSE;
					goto out;
				}
			default: // Fatal problems
				_tprintf(_T("WSAWaitForMultipleEvents() failed, error %d\n"), WSAGetLastError());
				conAccepted = FALSE;
				goto out;
			}
		}
		else
		{  // Returned immediately without socket error
			if (!nReceivedBytes)
			{  // When the receiving function has read nothing and returned immediately, the connection is off  
				_tprintf(_T("Server has closed the connection\n"));
				conAccepted = FALSE;
				goto out;
			}
			else
			{
				_tprintf(_T("%d bytes received\n"), nReceivedBytes);
				// Here should follow the processing of received data
				evalResponse(DataBuf);
			}
		}
	}
out:
	WSACloseEvent(NetEvents[1]);
	return 0;
}

BOOL estCon() {
	//_tprintf(_T("\tIN:estCon\n"));
	//
	// Initializations for socket
	//
	if (Error = WSAStartup(MAKEWORD(2, 0), &WsaData)) // Initialize Windows socket support
	{
		_tprintf(_T("WSAStartup() failed, error %d\n"), Error);
		SocketError = TRUE;
		SetEvent(hCommandProcessed);
		return FALSE;
	}
	else if ((hClientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == INVALID_SOCKET)
	{
		_tprintf(_T("socket() failed, error %d\n"), WSAGetLastError());
		SocketError = TRUE;
		SetEvent(hCommandProcessed);
		return FALSE;
	}
	//
	// Connect client to server
	//
	if (!SocketError)
	{
		ClientSocketInfo.sin_family = AF_INET;
		ClientSocketInfo.sin_addr.s_addr = inet_addr("127.0.0.1");
		ClientSocketInfo.sin_port = htons(1234);  // port number is selected just for example
		if (connect(hClientSocket, (SOCKADDR*)&ClientSocketInfo, sizeof(ClientSocketInfo)) == SOCKET_ERROR)
		{
			_tprintf(_T("Unable to connect to server, error %d\n"), WSAGetLastError());
			SocketError = TRUE;
			SetEvent(hCommandProcessed);
			return FALSE;
		}
	}
	//
	// Start net thread
	//
	if (!SocketError)
	{
		if (!(hReceiveNet = (HANDLE)_beginthreadex(NULL, 0, &ReceiveNet, NULL, 0, NULL)))
		{
			_tprintf(_T("Unable to create socket receiving thread\n"));
			SetEvent(hCommandProcessed);
			return FALSE;
		}
	}
	return TRUE;
}

void sendCommand(SOCKET hClientSocket, wchar_t *command) {
	//_tprintf(_T("\tIN:sendCommand: \"%s\"\n"), command);
	char temp[2048];
	int length = sizeof(int) + (wcslen(command) + 1) * sizeof(wchar_t);
	memcpy(temp, &length, sizeof(int));
	memcpy(temp + sizeof(int), command, (wcslen(command) + 1) * sizeof(wchar_t));
	send(hClientSocket, temp, length, 0);
	_tprintf(_T("Sending command: \"%s\"\n"), command);
}

void evalResponse(WSABUF DataBuf) {
	if (!_tcsicmp((const wchar_t *)(DataBuf.buf + sizeof(int)), _T("Identify"))) {
		sendCommand(hClientSocket, L"coursework");
	}
	else if (!_tcsicmp((const wchar_t *)(DataBuf.buf + sizeof(int)), _T("Accepted"))) {
		_tprintf(_T("Connection accepted\n"));
		conAccepted = TRUE;
	}
	else {
		parseData(DataBuf.buf);
	}
}

void createFile() {
	hOutputFile = CreateFile(FilePath, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hOutputFile == INVALID_HANDLE_VALUE) {
		_tprintf(_T("Unable to create file, error %d\n"), GetLastError());
		exit;
	}
}

int writeToFile(char * data, HANDLE hOutputFile) {
	int length = strlen(data);
	DWORD sucWritten;
	_tprintf(_T("Writing %d bytes to file\n"), length);

	if (!WriteFile(hOutputFile, data, length * sizeof(char), &sucWritten, NULL)) {
		_tprintf(_T("Unable to write to file. Received error %d"), GetLastError());
		return 1;
	}
	if (sucWritten != length) {
		_tprintf(_T("Write failed, only %d bytes written\n"), sucWritten);
	}

	return 0;
}

const char * parseData(char *data) {
	//_tprintf(_T("\tIN:parseData: \n"));
	int pos = 0; //current position in the data package

	int length; //length of the full data package
	//_tprintf(_T("Pos: %d\n"), pos);
	memcpy(&length, data, sizeof(int));
	pos += sizeof(int);
	_tprintf(_T("Total bytes in package: %d\n"), length);

	int cnumber; //number of channels in the data package
	//_tprintf(_T("Pos: %d\n"), pos);
	memcpy(&cnumber, data + pos, sizeof(int));
	pos += sizeof(int);
	_tprintf(_T("Number of channels in package: %d\n"), cnumber);

	char dToWrite[2048]; //used to send strings to be written to file

	time_t now = time(NULL);
	struct tm *t = localtime(&now);
	char currentTime[2048];
	sprintf(currentTime, "Measurement results at %d-%02d-%02d %02d:%02d:%02d",
		t->tm_year + 1900, t->tm_mon, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
	printf(currentTime);
	strcpy(dToWrite, currentTime);
	strcat(dToWrite, "\n");
	writeToFile(dToWrite, hOutputFile);

	for (int i = 0; i < cnumber; i++) {
		int mpnumber; //number of measurement points in the channel
		//_tprintf(_T("Pos: %d\n"), pos);
		memcpy(&mpnumber, data + pos, sizeof(int));
		pos += sizeof(int);
		_tprintf(_T("Number of measurement points in channel: %d\n"), mpnumber);

		char cname[2048]; //name of the current channel
		//_tprintf(_T("Pos: %d\n"), pos);
		memccpy(cname, data + pos, '\0', 15);
		printf("Channel name: %s\n", cname);
		strcpy(dToWrite, cname);
		strcat(dToWrite, ":\n");
		writeToFile(dToWrite, hOutputFile);
		pos += strlen(cname) + 1;

		for (int j = 0; j < mpnumber; j++) {
			char pname[2048]; //name of the current point
			//_tprintf(_T("Pos: %d\n"), pos);
			memccpy(pname, data + pos, '\0', 35);
			printf("Point name: %s\n", pname);
			strcpy(dToWrite, pname);
			strcat(dToWrite, ": ");
			writeToFile(dToWrite, hOutputFile);
			pos += strlen(pname) + 1;

			char stringMeas[2048];

			if (!strcmp(pname, "Input solution flow") ||
				!strcmp(pname, "Input gas flow") ||
				!strcmp(pname, "Output solution flow") ||
				!strcmp(pname, "Input steam flow")) {
				//printf("IF\n");
				double measurement; //the measurement is an integer for given point (only "Level" points are integers)
				//_tprintf(_T("Pos: %d\n"), pos);
				memcpy(&measurement, data + pos, sizeof(double));
				pos += sizeof(double);
				_tprintf(_T("Measurement: %.3f m%c/s\n"), measurement, 252);
				sprintf(stringMeas, "%.3f m^3/s\n", measurement);
				strcpy(dToWrite, stringMeas);
				writeToFile(dToWrite, hOutputFile);
			}
			else if (!strcmp(pname, "Input solution temperature") ||
					 !strcmp(pname, "Input steam temperature")) {
				double measurement; //the measurement is an integer for given point (only "Level" points are integers)
				//_tprintf(_T("Pos: %d\n"), pos);
				memcpy(&measurement, data + pos, sizeof(double));
				pos += sizeof(double);
				_tprintf(_T("Measurement: %.1f %cC\n"), measurement, 248);
				sprintf(stringMeas, "%.1f %cC\n", measurement, 248);
				strcpy(dToWrite, stringMeas);
				writeToFile(dToWrite, hOutputFile);
			}
			else if (!strcmp(pname, "Input solution pressure") ||
					 !strcmp(pname, "Input gas pressure")) {
				double measurement; //the measurement is an integer for given point (only "Level" points are integers)
				//_tprintf(_T("Pos: %d\n"), pos);
				memcpy(&measurement, data + pos, sizeof(double));
				pos += sizeof(double);
				_tprintf(_T("Measurement: %.1f atm\n"), measurement);
				sprintf(stringMeas, "%.1f atm\n", measurement);
				strcpy(dToWrite, stringMeas);
				writeToFile(dToWrite, hOutputFile);
			}
			else if (!strcmp(pname, "Output solution concentration")) { //TODO: ask about the data types!
				int measurement; //the measurement is an integer for given point (only "Level" points are integers)
				//_tprintf(_T("Pos: %d\n"), pos);
				memcpy(&measurement, data + pos, sizeof(int));
				pos += sizeof(int);
				_tprintf(_T("Measurement: %d %%\n"), measurement);
				sprintf(stringMeas, "%d %%\n", measurement);
				strcpy(dToWrite, stringMeas);
				writeToFile(dToWrite, hOutputFile);
			}
			else if (!strcmp(pname, "Level")) {
				//printf("IF\n");
				int measurement; //the measurement is an integer for given point (only "Level" points are integers)
				//_tprintf(_T("Pos: %d\n"), pos);
				memcpy(&measurement, data + pos, sizeof(int));
				pos += sizeof(int);
				_tprintf(_T("Measurement: %d %%\n"), measurement);
				sprintf(stringMeas, "%d %%\n", measurement);
				strcpy(dToWrite, stringMeas);
				writeToFile(dToWrite, hOutputFile);
			}
			else if (!strcmp(pname, "Output solution conductivity")) { //TODO: ask about the data types!
				double measurement; //the measurement is an integer for given point (only "Level" points are integers)
				//_tprintf(_T("Pos: %d\n"), pos);
				memcpy(&measurement, data + pos, sizeof(double));
				pos += sizeof(double);
				_tprintf(_T("Measurement: %.2f S/m\n"), measurement);
				sprintf(stringMeas, "%.2f S/m\n", measurement);
				strcpy(dToWrite, stringMeas);
				writeToFile(dToWrite, hOutputFile);
			}
		}
	}

	_tprintf(_T("\n\n"));
	strcpy(dToWrite, "\n\n");
	writeToFile(dToWrite, hOutputFile);
	sendCommand(hClientSocket, L"Ready");

	return "";
}