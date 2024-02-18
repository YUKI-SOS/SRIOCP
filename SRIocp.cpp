#include "pchh.h"
#include "CIocp.h"

CIocp::CIocp() :m_listenSocket(NULL), m_socket(NULL), completionPort(NULL),
bIsWorkerThread(false), m_pMainConnection(this),
isConnected(false), writeBuff(NULL),ilWriteBuffPos(-1), readBuff(NULL),
recvBuff(NULL), recvBuffPos(0),ioBuffPos(0), readBuffPos(0), sendBuffPos(0)
{
}

CIocp::~CIocp()
{
	if(m_BufferSwapLock != nullptr)
		delete[] m_BufferSwapLock;
	if(m_ThreadIdArr != nullptr)
		delete[] m_ThreadIdArr;
	if(recvBuff != nullptr)
		delete[] recvBuff;

	if (readBuff != nullptr) 
	{
		readBuff->resize(0);
		delete readBuff;
	}
	if (writeBuff != nullptr)
	{
		writeBuff->resize(0);
		delete writeBuff;
	}
	
}

bool CIocp::InitSocket(CSType csType, UINT port)
{
	int retVal;
	WSADATA wsaData;

	m_csType = csType;

	isConnected = true;

	//버퍼 할당
	readBuff = new std::vector<PacketInfo>;
	writeBuff = new std::vector<PacketInfo>;

	sendBuff = new std::vector<PacketInfo>;
	//sendBuff->resize(1);

	readBuff->resize(1);
	writeBuff->resize(1);
	//윈속 초기화
	if ((retVal = WSAStartup(MAKEWORD(2, 2), &wsaData)) != 0)
	{
		std::cout << "WSAStartup fail" << std::endl;
		return false;
	}

	//iocp객체 생성
	//CreateIoCompletionPort마지막 인자가 0이면 cpu 코어 개수만큼 스레드 이용
	if ((completionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0)) == NULL)
	{
		std::cout << "CreateIoCompletionPort fail" << std::endl;
		return false;
	}


	//워커스레드 생성
	CreateWorkerThread();

	//패킷 처리 스레드 생성
	//CreatePacketProcessThread();

	//클라이언트도 ConnectEx 등의 함수의 포인터를 얻기 위해 임의의 소켓이 필요함.
	if ((m_listenSocket = WSASocket(AF_INET,
		SOCK_STREAM,
		IPPROTO_TCP,
		NULL,
		0,
		WSA_FLAG_OVERLAPPED
	)) == INVALID_SOCKET)
	{
		std::cout << "WSASocket fail" << std::endl;
		return false;
	}

	//AcceptEx 함수 쓸 수 있도록 등록
	lpfnAcceptEx = NULL;
	GUID guidAcceptEx = WSAID_ACCEPTEX;
	DWORD dwBytes = 0;
	
	if (WSAIoctl(m_listenSocket,
		SIO_GET_EXTENSION_FUNCTION_POINTER,
		&guidAcceptEx,
		sizeof(GUID),
		&lpfnAcceptEx,
		sizeof(LPFN_ACCEPTEX),
		&dwBytes,
		NULL,
		NULL) == SOCKET_ERROR)
	{
		std::cout << "AcceptEx WsaIoctl Error:" << WSAGetLastError() << std::endl;
	}


	//DisconnectEx 함수 쓸 수 있도록 등록
	lpfnDisconnectEx = NULL;
	GUID guidDiconnectEx = WSAID_DISCONNECTEX;
	dwBytes = 0;

	if (WSAIoctl(m_listenSocket,
		SIO_GET_EXTENSION_FUNCTION_POINTER,
		&guidDiconnectEx,
		sizeof(GUID),
		&lpfnDisconnectEx,
		sizeof(LPFN_DISCONNECTEX),
		&dwBytes,
		NULL,
		NULL) == SOCKET_ERROR)
	{
		std::cout << "DisConnectEx WsaIoctl Error:" << WSAGetLastError() << std::endl;
	}

	//ConnectEx 함수 쓸 수 있도록 등록
	lpfnConnectEx = NULL;
	GUID guidConnectEx = WSAID_CONNECTEX;
	dwBytes = 0;

	if (WSAIoctl(m_listenSocket,
		SIO_GET_EXTENSION_FUNCTION_POINTER,
		&guidConnectEx,
		sizeof(GUID),
		&lpfnConnectEx,
		sizeof(LPFN_CONNECTEX),
		&dwBytes,
		NULL,
		NULL) == SOCKET_ERROR)
	{
		std::cout << "ConnectEx WsaIoctl Error:" << WSAGetLastError() << std::endl;
	}

	//아래로는 서버 초기화
	if (csType == CSType::CLIENT) return true;

	//listen 소켓 iocp 연결
	if (CreateIoCompletionPort((HANDLE)m_listenSocket,
		completionPort,
		(ULONG_PTR)this,
		0) == NULL)
	{
		std::cout << "listensocket iocp fail" << std::endl;
	}

	//bind
	SOCKADDR_IN serverAddr;
	ZeroMemory(&serverAddr, sizeof(serverAddr));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serverAddr.sin_port = htons(port);

	//TCP홀펀칭 이미 사용중인 포트에 다른 소켓 강제 바인딩 
	SetReuseSocketOpt(m_listenSocket);

	if (bind(m_listenSocket,
		(PSOCKADDR)&serverAddr,
		sizeof(serverAddr)) == SOCKET_ERROR)
	{
		std::cout << "bind fail" << WSAGetLastError() << std::endl;
		return false;
	}

	//listen
	if (listen(m_listenSocket, 5) == SOCKET_ERROR)
	{
		std::cout << "listen fail" << std::endl;
	}

	//포트를 0으로 바인드 했을 경우 할당해준 포트를 알아낸다. 
	SOCKADDR_IN sin;
	socklen_t len = sizeof(sin);
	if (getsockname(m_listenSocket, (SOCKADDR*)&sin, &len) != -1) 
	{
		bindPort = ntohs(sin.sin_port);
		printf("port number %d\n", ntohs(sin.sin_port));
	}

	return true;

}

void CIocp::InitSocketOption(SOCKET socket)
{
	SetLingerOpt(socket);
	SetNagleOffOpt(socket);
}

void CIocp::SetReuseSocketOpt(SOCKET socket)
{
	//socket Reuse Option. SO_REUSEADDR은 서버 소켓에서만 time_wait를 재활용 할 수 있는 것 같다.
	int option = 1;
	if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, (char*)&option, sizeof(int)) == SOCKET_ERROR)
	{
		std::cout << "Set Socket option ReuseAddr error :" << WSAGetLastError() << std::endl;
	};
}

void CIocp::SetLingerOpt(SOCKET socket)
{
	//onoff 0 - default 소켓버퍼에 남은 데이터를 전부 보내고 종료하는 정상종료
	//onoff 1 linger 0 - close 즉시 리턴하고 소켓버퍼에 남은 데이터를 버리는 비정상종료.
	//onoff 1 linger 1 - 지정시간동안 대기한 뒤 소켓버퍼에 남은 데이터를 전부 보내보고 다 보내면 정상종료 하며 리턴 못 보내면 비정상종료 에러와 함께 리턴.
	LINGER linger = { 0,0 };
	linger.l_onoff = 1;
	linger.l_linger = 0;

	setsockopt(socket, SOL_SOCKET, SO_LINGER, (CHAR*)&linger, sizeof(linger));
}

void CIocp::SetNagleOffOpt(SOCKET socket)
{
	int nagleOpt = 1; //1 비활성화 0 활성화
	setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, (const char*)&nagleOpt, sizeof(nagleOpt));
}

bool CIocp::CreateWorkerThread()
{
	HANDLE threadHandle; //워커스레드 핸들
	DWORD threadID;

	bIsWorkerThread = true;

	//cpu 개수 확인
	GetSystemInfo(&sysInfo);

	dwLockNum = sysInfo.dwNumberOfProcessors * 2;
	//SRWLock 생성 및 초기화.
	m_BufferSwapLock = new SRWLOCK[dwLockNum];
	for (DWORD i = 0; i < dwLockNum; i++) 
	{
		InitializeSRWLock(m_BufferSwapLock + i);
	}
	//스레드 아이디 배열에 저장
	m_ThreadIdArr = new DWORD[dwLockNum];

	//(CPU 개수 * 2)개의 워커 스레드 생성
	for (DWORD i = 0; i < sysInfo.dwNumberOfProcessors * 2; i++)
	{
		if ((threadHandle = (HANDLE)_beginthreadex(NULL,
			0,
			&WorkerThread,
			this,
			0,
			(unsigned int*)&m_ThreadIdArr[i])) == NULL)
		{
			std::cout << "CreateWorkerThread fail" << std::endl;
			return false;
		}

		CloseHandle(threadHandle);
	}

	return true;
}

//Overlapped IO 작업 완료 통보를 받아 처리하는 워커 스레드
unsigned __stdcall CIocp::WorkerThread(LPVOID CompletionPortObj)
{
	CIocp* arg = (CIocp*)CompletionPortObj;
	HANDLE completionport = arg->completionPort;
	//SOCKET socket;
	ULONG_PTR key = NULL;
	LPOVERLAPPED lpOverlapped = NULL;
	DWORD transferredBytes = 0;

	DWORD dwLockIndex = 0;
	DWORD currentThreadId = GetCurrentThreadId();

	//스레드 아이디를 비교해서 스레드가 가질 락 인덱스를 가진다.
	for (DWORD i = 0; i < arg->dwLockNum; i++) 
	{
		if (arg->m_ThreadIdArr[i] == currentThreadId)
			dwLockIndex = i;
	}
	

	while (arg->bIsWorkerThread)
	{
		if (GetQueuedCompletionStatus(completionport, //CompletionPort 핸들
			&transferredBytes,				//비동기 입출력 작업으로 전송된 바이트
			(PULONG_PTR)&key,			//CreateIoCompletionPort함수 호출시 전달한 세번째 인자가 여기 저장
			&lpOverlapped,			//비동기 입출력 함수 호출 시 전달한 오버랩 구조체 주소값.
			INFINITE) == 0)
		{
			CIocp* piocp = (CIocp*)key;
			IODATA* pioData = (IODATA*)lpOverlapped;
			std::cout << pioData->connectSocket << "Socket " << "GetQueuedCompletionStatus fail : " << WSAGetLastError() << std::endl;
			//piocp->OnClose();
			arg->CloseSocket(piocp->m_socket);
			continue;
		}


		if (key == 0)
		{
			break;
		}

		CIocp* piocp = (CIocp*)key;
		IODATA* pioData = (IODATA*)lpOverlapped;
		//std::cout << "trnasferredbytes = " << transferredBytes << std::endl;
		//std::cout << "iotype = " << (int)pioData->ioType << std::endl;
		//DWORD currentThreadId = GetCurrentThreadId();
		if (pioData->ioType == IOType::ACCEPT) { std::cout << currentThreadId << "번 스레드 iotype = accept\n"; }
		else if (pioData->ioType == IOType::CONNECT) { std::cout << currentThreadId << "번 스레드 iotype = connect\n"; }
		else if (pioData->ioType == IOType::DISCONNECT) { std::cout << currentThreadId << "번 스레드 iotype = disconnect\n"; }
		//else if (pioData->ioType == IOType::RECV) { std::cout << currentThreadId << "번 스레드 iotype = recv\n"; }
		//else if (pioData->ioType == IOType::SEND) { std::cout << currentThreadId << "번 스레드 iotype = send\n"; }
		//std::cout << currentThreadId << "번 스레드 iotype = " << (int)pioData->ioType << std::endl;

		

		if (pioData->ioType == IOType::CONNECT) 
		{
			if (setsockopt(pioData->connectSocket, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0) == SOCKET_ERROR) 
			{
				std::cout << "Set Socket option Update Connect Context Error :" << WSAGetLastError() << std::endl;
			};
			std::cout << pioData->connectSocket << "Socket Connected\n";

			piocp->isConnected = true;
			arg->OnConnect(piocp->m_socket);

			CIocp* pConnection = arg->GetConnection(pioData->connectSocket);
			arg->RecvSet(pConnection);
			continue;
		}
		if (pioData->ioType == IOType::DISCONNECT)
		{
			//서버는 ReAccecptEx하면서 클라이언트는 소켓을 다시 할당하면서 InitConnectPool에서 isConnected를 false처리하기 때문에 여기서 하지 않는다.
			//클라는 isConnected인 소켓이 없으면 다시 소켓을 커넥션 수 만큼 만들기 때문에 판단하기 위해서 false로 만들지 않는다.
			//piocp->isConnected = false;
			std::string sSocket = std::to_string(piocp->m_socket);
			std::cout << "Closing socket " + sSocket << std::endl;
			piocp->OnClose();
			if (arg->m_csType == CSType::SERVER)
				arg->ReAcceptSocket(piocp->m_socket);
			continue;
		}
		//GetQueuedCompletionStatus 해서 가져오는데 성공했는데 전달받은 패킷이 0이면 접속이 끊긴 것으로.
		if (transferredBytes == 0 && pioData->ioType != IOType::ACCEPT && pioData->ioType != IOType::CONNECT)
		{
			//piocp->OnClose();
			std::cout << "Enter 0byte\n";
			arg->CloseSocket(piocp->m_socket);
			
			continue;
		}


		//recv send 구분
		//비동기 입출력에서 오버랩구조체를 인자로 전달할 때 오버랩구조체를 멤버로 가진 구조체를 오버랩으로 캐스팅해서 보내고
		//GetQueuedCompletionStatus에서 받은 오버랩 구조체를 다시 원래 구조체로 캐스팅하면 다른 멤버도 받아올 수 있다.
		//그런 방법으로 IOType Enum을 끼어넣어서 받아와서 구분짓는다.
		//GetQueuedCompletionStatus 에 들어오는 key 값에다가 객체 주소를 넘겨받아서 가져온다. 

		if (pioData->ioType == IOType::ACCEPT)
		{
			if (setsockopt(pioData->connectSocket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char*)&(arg->m_listenSocket), sizeof(SOCKET)) == SOCKET_ERROR) 
			{
				std::cout << "Set Socket Option Update Accept Context Error : " << WSAGetLastError() << std::endl;
			};
			SOCKADDR_IN* sockAddr = NULL;
			int addrlen = sizeof(SOCKADDR);
			SOCKADDR_IN* remoteAddr = NULL;
			int remoteaddrlen = sizeof(SOCKADDR_IN);
			GetAcceptExSockaddrs(pioData->Buff,
				0,
				sizeof(SOCKADDR_IN) + 16,
				sizeof(SOCKADDR_IN) + 16,
				(SOCKADDR**)&sockAddr,
				&addrlen,
				(SOCKADDR**)&remoteAddr,
				&remoteaddrlen);

			std::string sSocket = std::to_string(pioData->connectSocket);
			//std::string sAddr = inet_ntoa(sockAddr->sin_addr);
			//std::string sPort = std::to_string(ntohs(sockAddr->sin_port));
			std::string sRemoteAddr = inet_ntoa(remoteAddr->sin_addr);
			std::string sRemotePort = std::to_string(ntohs(remoteAddr->sin_port));

			static int a = 1;
			std::cout << "접속 개수= " << a << "소켓 넘버=" + sSocket + "클라이언트 접속:IP 주소=" + sRemoteAddr + "포트 번호=" + sRemotePort << std::endl;
			a++;

			piocp->OnAccept(pioData->connectSocket);

			CIocp* pConnection = arg->GetConnection(pioData->connectSocket);
			pConnection->isConnected = true;
			pConnection->remotePort = ntohs(remoteAddr->sin_port);
			pConnection->remoteIP = sRemoteAddr;
			arg->RecvSet(pConnection);

			continue;
		}

		else if (pioData->ioType == IOType::RECV)
		{
			//std::cout << *(int*)(pioData->Buff + 4) << "번 패킷 " << transferredBytes << "바이트 수신" << std::endl;

			piocp->ioBuffPos = transferredBytes;

			PacketInfo packetInfo;
			packetInfo.connection = piocp;
			while (1) 
			{
				//리시브 버퍼에 남은 패킷이 있으면 iobuffer에 있는 것이 패킷의 시작부분이 아니라고 봄.
				if(piocp->recvBuffPos > 0)
				{
					//리시브버퍼에서 4바이트 읽은 크기가 리시브 버퍼와 io버퍼의 크기보다 크면 쪼개져서 덜 받은 패킷으로 판단.
					if (*(int*)piocp->recvBuff > piocp->recvBuffPos + piocp->ioBuffPos) 
					{
						memcpy(piocp->recvBuff + piocp->recvBuffPos, pioData->Buff, piocp->ioBuffPos);
						piocp->recvBuffPos += piocp->ioBuffPos;
						piocp->ioBuffPos = 0;
						goto MAKEPACKETEND;
					}
					//짤려서 뒤에 들어온 패킷부분을 리시브 버퍼에 이어준다.
					memcpy(piocp->recvBuff + piocp->recvBuffPos, pioData->Buff, *(int*)piocp->recvBuff - piocp->recvBuffPos);
					//패킷을 만들어서 라이트버퍼에 넣어준다.
					memcpy(packetInfo.Buff, piocp->recvBuff, *(int*)piocp->recvBuff);
					arg->PushWriteBuffer(&packetInfo, dwLockIndex);
					//io버퍼에서 리시브 버퍼로 넘겨준 만큼 땡겨준다.
					memmove(pioData->Buff, pioData->Buff + *(int*)piocp->recvBuff - piocp->recvBuffPos, sizeof(pioData->Buff) - (*(int*)piocp->recvBuff - piocp->recvBuffPos));
					piocp->ioBuffPos -= *(int*)piocp->recvBuff - piocp->recvBuffPos;
					//리시브 버퍼를 비워준다.
					ZeroMemory(piocp->recvBuff, _msize(piocp->recvBuff));
					piocp->recvBuffPos = 0;
				}

				//패킷의 사이즈가 io버퍼 위치보다 크면 뒤에 더 받을 패킷이 있다고 보고 리시브 버퍼에 불완전한 패킷 저장.
				if (piocp->ioBuffPos < *(int*)pioData->Buff)
				{
					//리시브 버퍼에 불완전 패킷 저장.
					memcpy(piocp->recvBuff + piocp->recvBuffPos, pioData->Buff, piocp->ioBuffPos);
					piocp->recvBuffPos += piocp->ioBuffPos;
					//불완전한 패킷 보내고 나머지를 땡긴다.
					memmove(pioData->Buff, pioData->Buff + piocp->ioBuffPos, sizeof(pioData->Buff) - piocp->ioBuffPos);
					//땡긴 나머지 부분을 0으로 채워준다.
					ZeroMemory(pioData->Buff + (sizeof(pioData->Buff) - piocp->ioBuffPos), piocp->ioBuffPos);
					piocp->ioBuffPos = 0;
					goto MAKEPACKETEND;
				}

				//패킷이 시작부분이라고 보고.
				int packetSize = *(int*)pioData->Buff;

				if(packetSize == 0)
					goto MAKEPACKETEND;

				//패킷을 만들어서 라이트 버퍼에 저장. 
				memcpy(packetInfo.Buff, pioData->Buff, packetSize);
				arg->PushWriteBuffer(&packetInfo, dwLockIndex);
				piocp->ioBuffPos -= packetSize;
				memmove(pioData->Buff, pioData->Buff + packetSize, sizeof(pioData->Buff) - packetSize);
				ZeroMemory(pioData->Buff + (sizeof(pioData->Buff) - packetSize), packetSize);
			}

			MAKEPACKETEND:
			arg->RecvSet(piocp);
		}

		//비동기 송신 이후 송신했다는 결과를 통지받을 뿐
		else if (pioData->ioType == IOType::SEND)
		{
			//std::cout << *(int*)(pioData->Buff + 4) << "번 패킷 " << transferredBytes << "바이트 송신" << std::endl;
			delete pioData;
		}

	}
	//DWORD currentThreadId = GetCurrentThreadId();
	CString ds;
	ds.Format(L"%d 스레드 종료\n", currentThreadId);
	OutputDebugStringW(ds);
	_endthreadex(0);

	return 0;
}

void CIocp::PacketProcess()
{
	while (1)
	{
		//리드 버퍼 다 처리했고, 라이트버퍼에 남아있으면 스왑.
		if (this->readBuffPos >= this->GetReadContainerSize() && this->GetWriteContainerSize() != 0)
		{
			this->SwapRWBuffer();
		}

		//리드버퍼 다 처리했으면 스킵.
		if (this->readBuffPos >= this->GetReadContainerSize()) break;

		//std::cout << "read버퍼 size = " << this->GetReadContainerSize() << std::endl;

		PacketInfo packetInfo = (*this->readBuff)[this->readBuffPos];

		if (packetInfo.connection == NULL)
			continue;

		/*char log[24];
		sprintf_s(log, "%d 패킷 처리\n", *(int*)(packetInfo.Buff + 4));
		OutputDebugStringA(log);*/
		packetInfo.connection->OnReceive();
		this->readBuffPos++;
	}
}
void CIocp::SendPacketProcess()
{
	while (1)
	{
		if (sendBuffPos >= sendBuff->size())
		{
			sendBuff->clear();
			this->sendBuffPos = 0;
			break;
		}

		PacketInfo sendPacket = (*sendBuff)[sendBuffPos];

		if (sendPacket.connection == NULL)
			continue;

		CIocp* connection = sendPacket.connection;
		int size = *(int*)sendPacket.Buff;
		connection->Send(sendPacket.Buff, size);

		this->sendBuffPos++;
	}
}
void CIocp::SwapRWBuffer()
{
	for (DWORD i = 0; i < dwLockNum; i++) 
	{
		AcquireSRWLockExclusive(m_BufferSwapLock + i);
	}

	//std::cout << "스왑 전 read버퍼 size = " << GetReadContainerSize() << " write버퍼 size = " << GetWriteContainerSize() << std::endl;
	readBuff->clear();

	auto tempBuff = writeBuff;
	writeBuff = readBuff;
	readBuff = tempBuff;
	//std::cout << "스왑 후 read버퍼 size = " << GetReadContainerSize() << " write버퍼 size = " << GetWriteContainerSize() << std::endl;
	InterlockedExchange(&ilWriteBuffPos, -1);
	readBuffPos = 0;

	for (DWORD i = 0; i < dwLockNum; i++)
	{
		ReleaseSRWLockExclusive(m_BufferSwapLock + i);
	}
}

void CIocp::PushWriteBuffer(PacketInfo* packetInfo, DWORD dwLockIndex)
{
	//큐에 인터락의 위치가 할당되어 있지 않다면 크기 2배 증가.
	//deque의 경우 메모리가 연속적으로 할당되어있지 않기 때문에 reserve 함수는 없음.
	//resize는 메모리 크기와 동시에 요소들의 초기화도 일어남.
	AcquireSRWLockExclusive(m_BufferSwapLock + dwLockIndex);
	
	//인터락을 통해 원자적으로 크기 증가
	ULONG buffPos = InterlockedIncrement(&ilWriteBuffPos);

	if (buffPos >= writeBuff->size())
	{
		if (writeBuff->size() == 0)
			writeBuff->resize(1);

		writeBuff->resize(writeBuff->size() * 2);
	}

	(*writeBuff)[buffPos] = *packetInfo;

	//std::cout << *(int*)(packetInfo->Buff + 4) << "번 패킷 라이트버퍼에 씀" << std::endl;
	
	ReleaseSRWLockExclusive(m_BufferSwapLock + dwLockIndex);
}

bool CIocp::InitAcceptPool(UINT num)
{
	DWORD flags;
	DWORD recvBytes;

	for (int i = 0; i < num; i++)
	{
		//오버랩IO를 위해 구조체 세팅
		IODATA* pioData = new IODATA;
		if (pioData == NULL) return false;
		ZeroMemory(pioData, sizeof(IODATA));
		pioData->connectSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_IP,
			NULL, 0, WSA_FLAG_OVERLAPPED);
		pioData->dataBuff.len = 0;
		pioData->dataBuff.buf = NULL;
		pioData->ioType = IOType::ACCEPT;
		flags = 0;
		recvBytes = 0;

		CIocp* pClient = GetEmptyConnection();
		if (pClient == NULL) return false;
		pClient->isConnected = false;
		pClient->m_socket = pioData->connectSocket;
		pClient->m_ioData = pioData;

		InitSocketOption(pClient->m_socket);

		if (AcceptEx(m_listenSocket,
			pioData->connectSocket,
			pioData->Buff,
			0,
			sizeof(SOCKADDR_IN) + 16,
			sizeof(SOCKADDR_IN) + 16,
			&recvBytes,
			(LPOVERLAPPED)pioData) == FALSE)
		{
			if (WSAGetLastError() != ERROR_IO_PENDING)
			{
				std::cout << "AcceptEx fail" << WSAGetLastError() << std::endl;
			}

		}

		////소켓과 iocp 연결
		if ((CreateIoCompletionPort((HANDLE)pioData->connectSocket,
			completionPort,
			(ULONG_PTR)pClient,
			0)) == NULL)
		{
			std::cout << "CreateIoCompletionPort bind error" << std::endl;
		}
	}

	return true;
}

bool CIocp::InitConnectPool(UINT num)
{
	//bind
	SOCKADDR_IN addr;
	ZeroMemory(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	DWORD flags;
	DWORD recvBytes;

	for (int i = 0; i < num; i++)
	{
		//오버랩IO를 위해 구조체 세팅
		IODATA* pioData = new IODATA;
		if (pioData == NULL) return false;
		ZeroMemory(pioData, sizeof(IODATA));
		pioData->connectSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_IP,
			NULL, 0, WSA_FLAG_OVERLAPPED);
		pioData->dataBuff.len = 0;
		pioData->dataBuff.buf = NULL;
		pioData->ioType = IOType::CONNECT;
		flags = 0;
		recvBytes = 0;

		CString ds;
		ds.Format(L"새로 연결한 소켓%d\n", pioData->connectSocket);
		OutputDebugString(ds);

		CIocp* pClient = pConnectionList[i];
		pClient->m_socket = pioData->connectSocket;
		pClient->m_ioData = pioData;
		pClient->isConnected = false;
		pClient->m_pMainConnection = this;

		InitSocketOption(pClient->m_socket);

		//TCP홀펀칭 이미 사용중인 포트에 다른 소켓 강제 바인딩 
		SetReuseSocketOpt(pClient->m_socket);

		//ConnectEx용 bind
		if (bind(pClient->m_socket, (PSOCKADDR)&addr,
			sizeof(addr)) == SOCKET_ERROR) 
		{
			std::cout << "ConnectEx bind fail" << std::endl;
			return false;
		}

		//소켓과 iocp 연결
		if ((CreateIoCompletionPort((HANDLE)pioData->connectSocket,
			completionPort,
			(ULONG_PTR)pClient,
			0)) == NULL)
		{
			std::cout << "CreateIoCompletionPort bind error" << std::endl;
			return false;
		}

	}
	return true;
}

bool CIocp::ReAcceptSocket(SOCKET socket)
{
	//DWORD flags;
	DWORD recvBytes;

	CIocp* pClient = GetConnection(socket);
	pClient->isConnected = false;
	pClient->m_ioData->ioType = IOType::ACCEPT;
	if (AcceptEx(m_listenSocket,
		socket,
		pClient->m_ioData->Buff,
		0,
		sizeof(SOCKADDR_IN) + 16,
		sizeof(SOCKADDR_IN) + 16,
		&recvBytes,
		(LPOVERLAPPED)pClient->m_ioData) == FALSE)
	{
		if (WSAGetLastError() != ERROR_IO_PENDING)
		{
			std::cout << "ReAcceptEx fail: " << WSAGetLastError() << std::endl;
			return false;
		}
	}
	std::cout << socket << "REAcceptEx succ" << std::endl;
	return true;
}

void CIocp::CloseSocket(SOCKET socket)
{
	//TF_DISCONNECT넣으면 10022 WSAEINVAL 오류 바인딩 실패. 이미 bind된 소켓에 바인드하거나 주소체계가 일관적이지 않을 때
	//lpfnDisconnectEx(socket, NULL, TF_DISCONNECT | TF_REUSE_SOCKET, NULL);
	
	IODATA* pioData = new IODATA;
	ZeroMemory(&pioData->overLapped, sizeof(OVERLAPPED));
	pioData->connectSocket = m_socket;
	pioData->dataBuff.len = 0;
	pioData->dataBuff.buf = pioData->Buff;
	pioData->ioType = IOType::DISCONNECT;

	if (lpfnDisconnectEx(socket, (LPOVERLAPPED)pioData, TF_REUSE_SOCKET, NULL) == FALSE) 
	{
		if (WSAGetLastError() != ERROR_IO_PENDING) 
		{
			std::cout << socket << "Socket " <<"DisconnectEx Error : " << WSAGetLastError() << std::endl;
			return;
		}
	};

	std::cout << "Call DisconnectEx" << std::endl;
	//TransmitFile(socket, NULL, 0, 0, (LPOVERLAPPED)pClient->m_ioData, NULL, TF_DISCONNECT | TF_REUSE_SOCKET);
	//shutdown(socket, SD_BOTH);
	//closesocket(socket);
}

SOCKET CIocp::Connect(LPCTSTR lpszHostAddress, UINT port)
{
	wchar_t* wszHost;
	char* szHost;
	int len;

	wszHost = (LPTSTR)lpszHostAddress;
	len = WideCharToMultiByte(CP_ACP, 0, wszHost, -1, NULL, 0, NULL, NULL);
	szHost = new char[len + 1];
	WideCharToMultiByte(CP_ACP, 0, wszHost, -1, szHost, len, 0, 0);

	//gethostbyname이 deprecated. getaddrinfo를 대신 써서 domain으로 부터 ip를 얻는다. 
	/*char host[20];
	gethostname(host, 20);
	hostent* hent = gethostbyname(host);
	in_addr addr;
	addr.s_addr = *(ULONG*)*hent->h_addr_list;
	char* address = inet_ntoa(addr);*/
	
	//test
	/*SOCKADDR_IN ssockAddr;
	ZeroMemory(&ssockAddr, sizeof(ssockAddr));
	ssockAddr.sin_family = AF_INET;
	ssockAddr.sin_addr.s_addr = inet_addr(szHost);
	ssockAddr.sin_port = 80;
	char szBuffer1[128];
	char szBuffer2[128];

	ZeroMemory(szBuffer1, sizeof(szBuffer1));
	ZeroMemory(szBuffer2, sizeof(szBuffer2));
	getnameinfo((const SOCKADDR*)&ssockAddr, sizeof(ssockAddr), szBuffer1, sizeof(szBuffer1), szBuffer2, sizeof(szBuffer2), NI_NUMERICSERV);
	hostent* hent = gethostbyname(szBuffer1);
	in_addr addr;
	addr.s_addr = *(ULONG*)*hent->h_addr_list;
	char* address = inet_ntoa(addr);

	ADDRINFO* pAddrInfotest = NULL;
	ADDRINFO stAddrInfotest = { 0, };
	//stAddrInfotest.ai_family = AF_UNSPEC;
	stAddrInfotest.ai_family = AF_INET;
	stAddrInfotest.ai_socktype = SOCK_STREAM;
	stAddrInfotest.ai_protocol = IPPROTO_TCP;
	if (getaddrinfo(szBuffer1, NULL, &stAddrInfotest, &pAddrInfotest) != 0)
	{
		int a = WSAGetLastError();
	}
	sockaddr_in* temptest = (sockaddr_in*)pAddrInfotest->ai_addr;
	char* conAddrtest = inet_ntoa(temptest->sin_addr);*/
	//testend

	//여기서 호스트로 ip얻는건 불필요한 과정. 밖으로 옮기자.
	ADDRINFO* pAddrInfo = NULL;
	ADDRINFO stAddrInfo = { 0, };
	//stAddrInfo.ai_family = AF_UNSPEC;
	stAddrInfo.ai_family = AF_INET;
	stAddrInfo.ai_socktype = SOCK_STREAM;
	stAddrInfo.ai_protocol = IPPROTO_TCP;

	getaddrinfo(szHost, NULL, &stAddrInfo, &pAddrInfo);
	if (pAddrInfo == NULL) 
	{
		std::cout << "domain convert address fail" << std::endl;
		delete[] szHost;
		return NULL;
	}
	sockaddr_in* temp = (sockaddr_in*)pAddrInfo->ai_addr;
	char* conAddr = inet_ntoa(temp->sin_addr);

	SOCKADDR_IN sockAddr;
	ZeroMemory(&sockAddr, sizeof(sockAddr));
	sockAddr.sin_family = AF_INET;
	//inet_addr이 deprecated되어 inet_pton(AF_INET, char_str, &(sockAddr.sin_addr.s_addr)); 대체해야 하지만...
	sockAddr.sin_addr.s_addr = inet_addr(conAddr);
	sockAddr.sin_port = htons(port);

	delete[] szHost;

	CIocp* pClient = GetNoneConnectConnection();
	if (pClient == NULL)
	{
		//다시 커넥트 소켓풀 초기화
		if (!InitConnectPool(m_ChildSockNum))
		{
			std::cout << "re_InitConnectPool fail" << std::endl;
		};
		OutputDebugString(L"커넥션풀 초기화");
		pClient = GetNoneConnectConnection();
	}

	//어차피 해당 타입이 다른 프로세스로 넘어가진 않는다. 
	//받는 쪽 워커스레드에서 Accept하면 recv를 다시 연결해 주되 IOType::CONNECT으로 해서
	//커넥트 시에 발생하는 0바이트 패킷을 연결이 끊어주는 패킷과 구분해주도록 한다.
	pClient->m_ioData->ioType = IOType::CONNECT;

	if (lpfnConnectEx(pClient->m_socket,
		(SOCKADDR*)&sockAddr,
		sizeof(sockAddr),
		NULL,
		0,
		NULL,
		(LPOVERLAPPED)pClient->m_ioData) == FALSE) 
	{
		if (WSAGetLastError() != ERROR_IO_PENDING)
		{
			std::cout << "ConnectEx fail: " << WSAGetLastError() << std::endl;
			return NULL;
		}
	};

	return pClient->m_socket;
}

bool CIocp::RecvSet(CIocp* pClient)
{
	DWORD flags;
	DWORD recvBytes;

	//오버랩IO를 위해 구조체 세팅
	IODATA* pioData = pClient->m_ioData;
	if (pioData == NULL) return false;
	ZeroMemory(pioData, sizeof(IODATA));
	pioData->connectSocket = pClient->m_socket;
	pioData->dataBuff.len = BUFFSIZE;
	pioData->dataBuff.buf = pioData->Buff;
	pioData->ioType = IOType::RECV;
	flags = 0;
	recvBytes = 0;

	if ((WSARecv(pClient->m_socket,
		&pioData->dataBuff,
		1,
		&recvBytes,
		&flags,
		(LPOVERLAPPED)pioData,
		NULL)) == SOCKET_ERROR)
	{
		if (WSAGetLastError() != ERROR_IO_PENDING)
		{
			int a = WSAGetLastError();
			std::cout << "WSARecv fail: " << WSAGetLastError() << std::endl;
			return false;
		}
	}
	//std::cout << "Recv bind" << std::endl;
	return true;
}

bool CIocp::RecvSet(CIocp* pClient, IOType ioType)
{
	DWORD flags;
	DWORD recvBytes;

	//오버랩IO를 위해 구조체 세팅
	IODATA* pioData = pClient->m_ioData;
	if (pioData == NULL) return false;
	ZeroMemory(pioData, sizeof(IODATA));
	pioData->connectSocket = pClient->m_socket;
	pioData->dataBuff.len = BUFFSIZE;
	pioData->dataBuff.buf = pioData->Buff;
	pioData->ioType = ioType;
	flags = 0;
	recvBytes = 0;

	if ((WSARecv(pClient->m_socket,
		&pioData->dataBuff,
		1,
		&recvBytes,
		&flags,
		(LPOVERLAPPED)pioData,
		NULL)) == SOCKET_ERROR)
	{
		if (WSAGetLastError() != ERROR_IO_PENDING)
		{
			int a = WSAGetLastError();
			std::cout << "WSARecv fail: " << WSAGetLastError() << std::endl;
			return false;
		}
	}
	std::cout << "Recv Connect bind" << std::endl;
	return true;
}

CIocp* CIocp::GetEmptyConnection()
{
	for (auto it = pConnectionList.begin(); it != pConnectionList.end(); ++it)
	{
		if ((*it)->m_socket == NULL)
		{
			return (*it);
		}
	}
	return NULL;
}

CIocp* CIocp::GetConnection(SOCKET socket)
{
	for (auto it = pConnectionList.begin(); it != pConnectionList.end(); ++it)
	{
		if ((*it)->m_socket == socket)
		{
			return (*it);
		}
	}
	return NULL;
}



CIocp* CIocp::GetNoneConnectConnection()
{
	for (auto it = pConnectionList.begin(); it != pConnectionList.end(); ++it)
	{
		if ((*it)->isConnected == false)
		{
			return (*it);
		}
	}
	return NULL;
}

bool CIocp::GetPeerName(CString& peerAdress, UINT& peerPort)
{
	SOCKADDR_IN addr;
	int addrlen = sizeof(SOCKADDR_IN);
	getpeername(m_socket, (sockaddr*)&addr, &addrlen);

	peerAdress = inet_ntoa(addr.sin_addr);
	peerPort = ntohs(addr.sin_port);

	return false;
}

bool CIocp::Send(void* lpBuff, int nBuffSize)
{
	DWORD currentThreadId = GetCurrentThreadId();
	CString ds;
	ds.Format(L"%d 쓰기 스레드 확인차\n", currentThreadId);
	//OutputDebugString(ds);

	DWORD sendbytes = 0;
	IODATA* pioData = new IODATA;
	ZeroMemory(&pioData->overLapped, sizeof(OVERLAPPED));
	pioData->connectSocket = m_socket;
	pioData->dataBuff.len = nBuffSize;
	pioData->dataBuff.buf = pioData->Buff;
	memcpy(pioData->dataBuff.buf, lpBuff, nBuffSize);
	pioData->ioType = IOType::SEND;

	if (WSASend(m_socket,
		&pioData->dataBuff,
		1,
		&sendbytes,
		0,
		(LPOVERLAPPED)pioData,
		NULL) == SOCKET_ERROR)
	{
		if (WSAGetLastError() != ERROR_IO_PENDING)
		{
			std::cout << "WSASend fail" << std::endl;
			return false;
		}
	}

	//std::cout << *(int*)(pioData->dataBuff.buf + 4) << "번 패킷 " << sendbytes << "byte send" << std::endl;
	return true;
}

void CIocp::SendToBuff(void* lpBuff, int nBuffSize)
{
	//Recv를 순서대로 받도록 동기화했기 때문에 Send에서 별도의 락 불필요.
	PacketInfo pckInfo;
	pckInfo.connection = this;
	memcpy(pckInfo.Buff, lpBuff, nBuffSize);

	m_pMainConnection->sendBuff->push_back(pckInfo);
	
}

PacketInfo CIocp::GetPacket()
{
	return (*readBuff)[readBuffPos];
}



void CIocp::StopThread()
{
	bIsWorkerThread = false;
	for (int i = 0; i < dwLockNum; i++)
	{
		if (PostQueuedCompletionStatus(completionPort, 0, (ULONG_PTR)0, 0) == 0)
		{
			std::cout << "PostQueuedCompletionStatus fail" << std::endl;
		};
	}
}

UINT CIocp::GetThreadLockNum()
{
	DWORD currentThreadId = GetCurrentThreadId();

	//스레드 아이디를 비교해서 스레드 순서에 따라 락 인덱스를 얻는다.
	for (DWORD i = 0; i < m_pMainConnection->dwLockNum; i++)
	{
		if (m_pMainConnection->m_ThreadIdArr[i] == currentThreadId)
		{
			return i;
		}
	}
}

UINT CIocp::GetWriteContainerSize()
{
	UINT Count = 0;

	for (int i = 0; i < writeBuff->size(); i++) 
	{
		if ((*writeBuff)[i].connection != NULL)
			Count++;
	}
	return Count;
}

UINT CIocp::GetReadContainerSize()
{
	UINT Count = 0;

	if (readBuff->size() == 0)
		return Count;

	for (int i = 0; i < readBuff->size(); i++)
	{
		if ((*readBuff)[i].connection != NULL)
			Count++;
	}
	return Count;
}


