#include "SRIocp.h"

//전역 변수 초기화
AcceptFunc CIocp::g_pOnAcceptFunc = NULL;
ConnectFunc CIocp::g_pOnConnectFunc = NULL;
CloseFunc CIocp::g_pOnCloseFunc = NULL;
RecvFunc CIocp::g_pOnRecvFunc = NULL;


CIocp::CIocp()
{
	m_ListenSocket = NULL;
	m_socket = NULL;
	m_CompletionPort = NULL;
	m_bWorkerThreadLive = false;
	m_pMainConnection = this;
	m_isConnected = false;
	m_pWriteBuff = NULL;
	m_uInterLockWriteBuffPos = -1;
	m_pReadBuff = NULL;
	m_pTempBuff = NULL;
	m_uTempBuffPos = 0;
	m_uIoPos = 0;
	m_uReadBuffPos = 0;
	m_uSendBuffPos = 0;

	m_dwLockNum = 0;
}


CIocp::~CIocp()
{
	if (m_pBufferSwapLock != nullptr)
		delete[] m_pBufferSwapLock;
	if (m_pThreadIdArr != nullptr)
		delete[] m_pThreadIdArr;
	if (m_pTempBuff != nullptr)
		delete[] m_pTempBuff;

	if (m_pReadBuff != nullptr)
	{
		m_pReadBuff->resize(0);
		delete m_pReadBuff;
	}
	if (m_pWriteBuff != nullptr)
	{
		m_pWriteBuff->resize(0);
		delete m_pWriteBuff;
	}

}

bool CIocp::InitConnectionList(UINT nCount)
{
	for (int i = 0; i < nCount; i++)
	{
		CIocp* child = new CIocp;
		child->m_pTempBuff = new char[BUFFSIZE];
		ZeroMemory(child->m_pTempBuff, BUFFSIZE);
		child->m_pMainConnection = this;
		child->m_uConnectionIndex = i;
		m_ConnectionList.push_back(child);
	}

	m_nChildSockNum = nCount;

	return false;
}

bool CIocp::InitSocket(ECSType csType, UINT port)
{
	int retVal;
	WSADATA wsaData;

	m_eCSType = csType;

	m_isConnected = true;

	//버퍼 할당
	m_pReadBuff = new std::vector<PacketInfo>;
	m_pWriteBuff = new std::vector<PacketInfo>;

	m_pSendBuff = new std::vector<PacketInfo>;
	//sendBuff->resize(1);

	m_pReadBuff->resize(1);
	m_pWriteBuff->resize(1);
	//윈속 초기화
	if ((retVal = WSAStartup(MAKEWORD(2, 2), &wsaData)) != 0)
	{
		std::cout << "WSAStartup fail" << std::endl;
		return false;
	}

	//iocp객체 생성
	//CreateIoCompletionPort마지막 인자가 0이면 cpu 코어 개수만큼 스레드 이용
	if ((m_CompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0)) == NULL)
	{
		std::cout << "CreateIoCompletionPort fail" << std::endl;
		return false;
	}


	//워커스레드 생성
	CreateWorkerThread();

	//클라이언트도 ConnectEx 등의 함수의 포인터를 얻기 위해 임의의 소켓이 필요함.
	if ((m_ListenSocket = WSASocket(AF_INET,
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

	if (WSAIoctl(m_ListenSocket,
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

	if (WSAIoctl(m_ListenSocket,
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

	if (WSAIoctl(m_ListenSocket,
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
	if (csType == ECSType::CLIENT) return true;

	//listen 소켓 iocp 연결
	if (CreateIoCompletionPort((HANDLE)m_ListenSocket,
		m_CompletionPort,
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
	SetReuseSocketOpt(m_ListenSocket);

	if (bind(m_ListenSocket,
		(PSOCKADDR)&serverAddr,
		sizeof(serverAddr)) == SOCKET_ERROR)
	{
		std::cout << "bind fail" << WSAGetLastError() << std::endl;
		return false;
	}

	//listen
	if (listen(m_ListenSocket, 5) == SOCKET_ERROR)
	{
		std::cout << "listen fail" << std::endl;
	}

	//포트를 0으로 바인드 했을 경우 할당해준 포트를 알아낸다. 
	SOCKADDR_IN sin;
	socklen_t len = sizeof(sin);
	if (getsockname(m_ListenSocket, (SOCKADDR*)&sin, &len) != -1)
	{
		m_nBindPort = ntohs(sin.sin_port);
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

	m_bWorkerThreadLive = true;

	//cpu 개수 확인
	SYSTEM_INFO sysInfo;
	GetSystemInfo(&sysInfo);

	m_dwLockNum = sysInfo.dwNumberOfProcessors * 2;
	//SRWLock 생성 및 초기화.
	m_pBufferSwapLock = new SRWLOCK[m_dwLockNum];
	for (DWORD i = 0; i < m_dwLockNum; i++)
	{
		InitializeSRWLock(m_pBufferSwapLock + i);
	}
	//스레드 아이디 배열에 저장
	m_pThreadIdArr = new DWORD[m_dwLockNum];

	//(CPU 개수 * 2)개의 워커 스레드 생성
	for (DWORD i = 0; i < sysInfo.dwNumberOfProcessors * 2; i++)
	{
		if ((threadHandle = (HANDLE)_beginthreadex(NULL,
			0,
			&WorkerThread,
			this,
			0,
			(unsigned int*)&m_pThreadIdArr[i])) == NULL)
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
	HANDLE completionport = arg->m_CompletionPort;
	//SOCKET socket;
	ULONG_PTR key = NULL;
	LPOVERLAPPED lpOverlapped = NULL;
	DWORD transferredBytes = 0;

	DWORD dwLockIndex = 0;
	DWORD currentThreadId = GetCurrentThreadId();

	//스레드 아이디를 비교해서 스레드가 가질 락 인덱스를 가진다.
	for (DWORD i = 0; i < arg->m_dwLockNum; i++)
	{
		if (arg->m_pThreadIdArr[i] == currentThreadId)
			dwLockIndex = i;
	}


	while (arg->m_bWorkerThreadLive)
	{
		if (GetQueuedCompletionStatus(completionport, //CompletionPort 핸들
			&transferredBytes,				//비동기 입출력 작업으로 전송된 바이트
			(PULONG_PTR)&key,			//CreateIoCompletionPort함수 호출시 전달한 세번째 인자가 여기 저장
			&lpOverlapped,			//비동기 입출력 함수 호출 시 전달한 오버랩 구조체 주소값.
			INFINITE) == 0)
		{
			CIocp* piocp = (CIocp*)key;
			IODATA* pioData = (IODATA*)lpOverlapped;
			std::cout << pioData->socket << "Socket " << "GetQueuedCompletionStatus fail : " << WSAGetLastError() << std::endl;
			
			arg->CloseSocket(piocp->m_uConnectionIndex);
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
			if (setsockopt(pioData->socket, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0) == SOCKET_ERROR)
			{
				std::cout << "Set Socket option Update Connect Context Error :" << WSAGetLastError() << std::endl;
			};
			std::cout << pioData->socket << "Socket Connected\n";

			piocp->m_isConnected = true;
			//arg->OnConnect(piocp->m_Socket);
			OnConnect(pioData->uIndex);

			CIocp* pConnection = arg->GetConnection(pioData->uIndex);
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
			//piocp->OnClose();
			OnClose(pioData->uIndex);

			if (arg->m_eCSType == ECSType::SERVER)
				arg->ReAcceptSocket(pioData->uIndex);
			continue;
		}
		//GetQueuedCompletionStatus 해서 가져오는데 성공했는데 전달받은 패킷이 0이면 접속이 끊긴 것으로.
		if (transferredBytes == 0 
			&& pioData->ioType != IOType::ACCEPT 
			&& pioData->ioType != IOType::CONNECT)
		{
			//piocp->OnClose();
			std::cout << "Enter 0byte\n";
			arg->CloseSocket(piocp->m_uConnectionIndex);

			continue;
		}


		//recv send 구분
		//비동기 입출력에서 오버랩구조체를 인자로 전달할 때 오버랩구조체를 멤버로 가진 구조체를 오버랩으로 캐스팅해서 보내고
		//GetQueuedCompletionStatus에서 받은 오버랩 구조체를 다시 원래 구조체로 캐스팅하면 다른 멤버도 받아올 수 있다.
		//그런 방법으로 IOType Enum을 끼어넣어서 받아와서 구분짓는다.
		//GetQueuedCompletionStatus 에 들어오는 key 값에다가 객체 주소를 넘겨받아서 가져온다. 

		if (pioData->ioType == IOType::ACCEPT)
		{
			if (setsockopt(pioData->socket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char*)&(arg->m_ListenSocket), sizeof(SOCKET)) == SOCKET_ERROR)
			{
				std::cout << "Set Socket Option Update Accept Context Error : " << WSAGetLastError() << std::endl;
			};
			SOCKADDR_IN* sockAddr = NULL;
			int addrlen = sizeof(SOCKADDR);
			SOCKADDR_IN* remoteAddr = NULL;
			int remoteaddrlen = sizeof(SOCKADDR_IN);
			GetAcceptExSockaddrs(pioData->buff,
				0,
				sizeof(SOCKADDR_IN) + 16,
				sizeof(SOCKADDR_IN) + 16,
				(SOCKADDR**)&sockAddr,
				&addrlen,
				(SOCKADDR**)&remoteAddr,
				&remoteaddrlen);

			std::string sSocket = std::to_string(pioData->socket);
			//std::string sAddr = inet_ntoa(sockAddr->sin_addr);
			//std::string sPort = std::to_string(ntohs(sockAddr->sin_port));
			std::string sRemoteAddr = inet_ntoa(remoteAddr->sin_addr);
			std::string sRemotePort = std::to_string(ntohs(remoteAddr->sin_port));

			static int a = 1;
			std::cout << "접속 개수= " << a << "소켓 넘버=" + sSocket + "클라이언트 접속:IP 주소=" + sRemoteAddr + "포트 번호=" + sRemotePort << std::endl;
			a++;

			//piocp->OnAccept(pioData->connectSocket);
			CIocp::OnAccept(pioData->uIndex);

			CIocp* pConnection = arg->GetConnection(pioData->uIndex);
			pConnection->m_isConnected = true;
			pConnection->m_uRemotePort = ntohs(remoteAddr->sin_port);
			pConnection->m_szRemoteIP = sRemoteAddr;
			arg->RecvSet(pConnection);

			continue;
		}

		else if (pioData->ioType == IOType::RECV)
		{
			//std::cout << *(int*)(pioData->Buff + 4) << "번 패킷 " << transferredBytes << "바이트 수신" << std::endl;

			piocp->m_uIoPos = transferredBytes;

			PacketInfo packetInfo;
			packetInfo.pConnection = piocp;
			while (1)
			{
				//임시 버퍼에 남은 패킷이 있으면 iobuffer에 있는 것이 패킷의 시작부분이 아니라고 봄.
				if (piocp->m_uTempBuffPos > 0)
				{
					//임시 버퍼에서 4바이트 읽은 크기가 임시 버퍼와 io버퍼의 크기보다 크면 쪼개져서 덜 받은 패킷으로 판단.
					if (*(int*)piocp->m_pTempBuff > piocp->m_uTempBuffPos + piocp->m_uIoPos)
					{
						memcpy(piocp->m_pTempBuff + piocp->m_uTempBuffPos, pioData->buff, piocp->m_uIoPos);
						piocp->m_uTempBuffPos += piocp->m_uIoPos;
						piocp->m_uIoPos = 0;
						goto MAKEPACKETEND;
					}
					//짤려서 뒤에 들어온 패킷부분을 임시 버퍼에 이어준다.
					memcpy(piocp->m_pTempBuff + piocp->m_uTempBuffPos, pioData->buff, *(int*)piocp->m_pTempBuff - piocp->m_uTempBuffPos);
					//패킷을 만들어서 라이트버퍼에 넣어준다.
					memcpy(packetInfo.Buff, piocp->m_pTempBuff, *(int*)piocp->m_pTempBuff);
					arg->PushWriteBuffer(&packetInfo, dwLockIndex);
					//io버퍼에서 임시 버퍼로 넘겨준 만큼 땡겨준다.
					memmove(pioData->buff, pioData->buff + *(int*)piocp->m_pTempBuff - piocp->m_uTempBuffPos, sizeof(pioData->buff) - (*(int*)piocp->m_pTempBuff - piocp->m_uTempBuffPos));
					piocp->m_uIoPos -= *(int*)piocp->m_pTempBuff - piocp->m_uTempBuffPos;
					//임시 버퍼를 비워준다.
					ZeroMemory(piocp->m_pTempBuff, _msize(piocp->m_pTempBuff));
					piocp->m_uTempBuffPos = 0;
				}

				//패킷의 사이즈가 io버퍼 위치보다 크면 뒤에 더 받을 패킷이 있다고 보고 임시 버퍼에 불완전한 패킷 저장.
				if (piocp->m_uIoPos < *(int*)pioData->buff)
				{
					//임시 버퍼에 불완전 패킷 저장.
					memcpy(piocp->m_pTempBuff + piocp->m_uTempBuffPos, pioData->buff, piocp->m_uIoPos);
					piocp->m_uTempBuffPos += piocp->m_uIoPos;
					//불완전한 패킷 보내고 나머지를 땡긴다.
					memmove(pioData->buff, pioData->buff + piocp->m_uIoPos, sizeof(pioData->buff) - piocp->m_uIoPos);
					//땡긴 나머지 부분을 0으로 채워준다.
					ZeroMemory(pioData->buff + (sizeof(pioData->buff) - piocp->m_uIoPos), piocp->m_uIoPos);
					piocp->m_uIoPos = 0;
					goto MAKEPACKETEND;
				}

				//패킷이 시작부분이라고 보고.
				int packetSize = *(int*)pioData->buff;

				if (packetSize == 0)
					goto MAKEPACKETEND;

				//패킷을 만들어서 라이트 버퍼에 저장. 
				memcpy(packetInfo.Buff, pioData->buff, packetSize);
				arg->PushWriteBuffer(&packetInfo, dwLockIndex);
				piocp->m_uIoPos -= packetSize;
				memmove(pioData->buff, pioData->buff + packetSize, sizeof(pioData->buff) - packetSize);
				ZeroMemory(pioData->buff + (sizeof(pioData->buff) - packetSize), packetSize);
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
		if (this->m_uReadBuffPos >= this->GetReadContainerSize() && this->GetWriteContainerSize() != 0)
		{
			this->SwapRWBuffer();
		}

		//리드버퍼 다 처리했으면 스킵.
		if (this->m_uReadBuffPos >= this->GetReadContainerSize()) break;

		//std::cout << "read버퍼 size = " << this->GetReadContainerSize() << std::endl;

		/*PacketInfo packetInfo = (*this->m_pReadBuff)[this->m_uReadBuffPos];

		if (packetInfo.pConnection == NULL)
			continue;*/

		PacketInfo* packetInfo = &(*this->m_pReadBuff)[this->m_uReadBuffPos];


		/*char log[24];
		sprintf_s(log, "%d 패킷 처리\n", *(int*)(packetInfo.Buff + 4));
		OutputDebugStringA(log);*/
		OnRecv(packetInfo);
		//packetInfo.pConnection->OnReceive();
		this->m_uReadBuffPos++;
	}
}
void CIocp::SendPacketProcess()
{
	while (1)
	{
		if (m_uSendBuffPos >= m_pSendBuff->size())
		{
			m_pSendBuff->clear();
			this->m_uSendBuffPos = 0;
			break;
		}

		PacketInfo sendPacket = (*m_pSendBuff)[m_uSendBuffPos];

		if (sendPacket.pConnection == NULL)
			continue;

		CIocp* pIocp = sendPacket.pConnection;
		int size = *(int*)sendPacket.Buff;
		pIocp->Send(pIocp->m_uConnectionIndex, sendPacket.Buff, size);

		this->m_uSendBuffPos++;
	}
}
void CIocp::SwapRWBuffer()
{
	for (DWORD i = 0; i < m_dwLockNum; i++)
	{
		AcquireSRWLockExclusive(m_pBufferSwapLock + i);
	}

	//std::cout << "스왑 전 read버퍼 size = " << GetReadContainerSize() << " write버퍼 size = " << GetWriteContainerSize() << std::endl;
	m_pReadBuff->clear();

	auto tempBuff = m_pWriteBuff;
	m_pWriteBuff = m_pReadBuff;
	m_pReadBuff = tempBuff;
	//std::cout << "스왑 후 read버퍼 size = " << GetReadContainerSize() << " write버퍼 size = " << GetWriteContainerSize() << std::endl;
	InterlockedExchange(&m_uInterLockWriteBuffPos, -1);
	m_uReadBuffPos = 0;

	for (DWORD i = 0; i < m_dwLockNum; i++)
	{
		ReleaseSRWLockExclusive(m_pBufferSwapLock + i);
	}
}

void CIocp::PushWriteBuffer(PacketInfo* packetInfo, DWORD dwLockIndex)
{
	//큐에 인터락의 위치가 할당되어 있지 않다면 크기 2배 증가.
	//resize는 메모리 크기와 동시에 요소들의 초기화도 일어남.
	AcquireSRWLockExclusive(m_pBufferSwapLock + dwLockIndex);

	//인터락을 통해 원자적으로 크기 증가
	ULONG buffPos = InterlockedIncrement(&m_uInterLockWriteBuffPos);

	if (buffPos >= m_pWriteBuff->size())
	{
		if (m_pWriteBuff->size() == 0)
			m_pWriteBuff->resize(1);

		m_pWriteBuff->resize(m_pWriteBuff->size() * 2);
	}

	(*m_pWriteBuff)[buffPos] = *packetInfo;

	//std::cout << *(int*)(packetInfo->Buff + 4) << "번 패킷 라이트버퍼에 씀" << std::endl;

	ReleaseSRWLockExclusive(m_pBufferSwapLock + dwLockIndex);
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
		pioData->socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_IP,
			NULL, 0, WSA_FLAG_OVERLAPPED);
		pioData->dataBuff.len = 0;
		pioData->dataBuff.buf = NULL;
		pioData->ioType = IOType::ACCEPT;
		flags = 0;
		recvBytes = 0;

		CIocp* pConnection = m_ConnectionList[i];
		if (pConnection == NULL) return false;
		pConnection->m_isConnected = false;
		pConnection->m_socket = pioData->socket;
		pConnection->m_pIoData = pioData;

		pioData->uIndex = pConnection->m_uConnectionIndex;

		InitSocketOption(pConnection->m_socket);

		if (AcceptEx(m_ListenSocket,
			pioData->socket,
			pioData->buff,
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
		if ((CreateIoCompletionPort((HANDLE)pioData->socket,
			m_CompletionPort,
			(ULONG_PTR)pConnection,
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
		pioData->socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_IP,
			NULL, 0, WSA_FLAG_OVERLAPPED);
		pioData->dataBuff.len = 0;
		pioData->dataBuff.buf = NULL;
		pioData->ioType = IOType::CONNECT;
		flags = 0;
		recvBytes = 0;

		CString ds;
		ds.Format(L"새로 연결한 소켓%d\n", pioData->socket);
		OutputDebugString(ds);

		CIocp* pConnection = m_ConnectionList[i];
		pConnection->m_socket = pioData->socket;
		pConnection->m_pIoData = pioData;
		pConnection->m_isConnected = false;
		pConnection->m_pMainConnection = this;

		pioData->uIndex = pConnection->m_uConnectionIndex;

		InitSocketOption(pConnection->m_socket);

		//TCP홀펀칭 이미 사용중인 포트에 다른 소켓 강제 바인딩 
		SetReuseSocketOpt(pConnection->m_socket);

		//ConnectEx용 bind
		if (bind(pConnection->m_socket, (PSOCKADDR)&addr,
			sizeof(addr)) == SOCKET_ERROR)
		{
			std::cout << "ConnectEx bind fail" << std::endl;
			return false;
		}

		//소켓과 iocp 연결
		if ((CreateIoCompletionPort((HANDLE)pioData->socket,
			m_CompletionPort,
			(ULONG_PTR)pConnection,
			0)) == NULL)
		{
			std::cout << "CreateIoCompletionPort bind error" << std::endl;
			return false;
		}

	}
	return true;
}

bool CIocp::ReAcceptSocket(UINT uIndex)
{
	//DWORD flags;
	DWORD recvBytes;

	CIocp* pConnection = GetConnection(uIndex);
	pConnection->m_isConnected = false;
	pConnection->m_pIoData->ioType = IOType::ACCEPT;
	if (AcceptEx(m_ListenSocket,
		pConnection->m_socket,
		pConnection->m_pIoData->buff,
		0,
		sizeof(SOCKADDR_IN) + 16,
		sizeof(SOCKADDR_IN) + 16,
		&recvBytes,
		(LPOVERLAPPED)pConnection->m_pIoData) == FALSE)
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

void CIocp::CloseSocket(UINT uIndex)
{
	//TF_DISCONNECT넣으면 10022 WSAEINVAL 오류 바인딩 실패. 이미 bind된 소켓에 바인드하거나 주소체계가 일관적이지 않을 때
	//lpfnDisconnectEx(socket, NULL, TF_DISCONNECT | TF_REUSE_SOCKET, NULL);

	CIocp* pIocp = GetConnection(uIndex);

	IODATA* pioData = pIocp->m_pIoData;
	ZeroMemory(&pioData->overlapped, sizeof(OVERLAPPED));
	pioData->socket = pIocp->m_socket;
	pioData->dataBuff.len = 0;
	pioData->dataBuff.buf = pioData->buff;
	pioData->ioType = IOType::DISCONNECT;
	pioData->uIndex = uIndex;

	if (lpfnDisconnectEx(pIocp->m_socket, (LPOVERLAPPED)pioData, TF_REUSE_SOCKET, NULL) == FALSE)
	{
		if (WSAGetLastError() != ERROR_IO_PENDING)
		{
			std::cout << pIocp->m_socket << "Socket " << "DisconnectEx Error : " << WSAGetLastError() << std::endl;
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

	CIocp* pConnection = GetNoneConnectConnection();
	if (pConnection == NULL)
	{
		//다시 커넥트 소켓풀 초기화
		if (!InitConnectPool(m_nChildSockNum))
		{
			std::cout << "re_InitConnectPool fail" << std::endl;
		};
		OutputDebugString(L"커넥션풀 초기화");
		pConnection = GetNoneConnectConnection();
	}

	//어차피 해당 타입이 다른 프로세스로 넘어가진 않는다. 
	//받는 쪽 워커스레드에서 Accept하면 recv를 다시 연결해 주되 IOType::CONNECT으로 해서
	//커넥트 시에 발생하는 0바이트 패킷을 연결이 끊어주는 패킷과 구분해주도록 한다.
	pConnection->m_pIoData->ioType = IOType::CONNECT;

	if (lpfnConnectEx(pConnection->m_socket,
		(SOCKADDR*)&sockAddr,
		sizeof(sockAddr),
		NULL,
		0,
		NULL,
		(LPOVERLAPPED)pConnection->m_pIoData) == FALSE)
	{
		if (WSAGetLastError() != ERROR_IO_PENDING)
		{
			std::cout << "ConnectEx fail: " << WSAGetLastError() << std::endl;
			return NULL;
		}
	};

	return pConnection->m_socket;
}

bool CIocp::RecvSet(CIocp* pConnection)
{
	DWORD flags;
	DWORD recvBytes;

	//오버랩IO를 위해 구조체 세팅
	IODATA* pioData = pConnection->m_pIoData;
	if (pioData == NULL) return false;
	ZeroMemory(pioData, sizeof(IODATA));
	pioData->socket = pConnection->m_socket;
	pioData->dataBuff.len = BUFFSIZE;
	pioData->dataBuff.buf = pioData->buff;
	pioData->ioType = IOType::RECV;
	pioData->uIndex = pConnection->m_uConnectionIndex;
	flags = 0;
	recvBytes = 0;

	if (WSARecv(pConnection->m_socket,
		&pioData->dataBuff,
		1,
		&recvBytes,
		&flags,
		(LPOVERLAPPED)pioData,
		NULL) == SOCKET_ERROR )
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

CIocp* CIocp::GetEmptyConnection()
{
	for (auto it = m_ConnectionList.begin(); it != m_ConnectionList.end(); ++it)
	{
		if ((*it)->m_socket == NULL)
		{
			return (*it);
		}
	}
	return NULL;
}

CIocp* CIocp::GetConnection(UINT uIndex)
{
	return m_ConnectionList[uIndex];
}



CIocp* CIocp::GetNoneConnectConnection()
{
	for (auto it = m_ConnectionList.begin(); it != m_ConnectionList.end(); ++it)
	{
		if ((*it)->m_isConnected == false)
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


bool CIocp::Send(UINT uIndex, void* lpBuff, int nBuffSize)
{
	DWORD currentThreadId = GetCurrentThreadId();
	CString ds;
	ds.Format(L"%d 쓰기 스레드 확인차\n", currentThreadId);
	//OutputDebugString(ds);

	CIocp* pIocp = GetConnection(uIndex);

	DWORD sendbytes = 0;
	IODATA* pioData = new IODATA;
	ZeroMemory(&pioData->overlapped, sizeof(OVERLAPPED));
	pioData->socket = pIocp->m_socket;
	pioData->dataBuff.len = nBuffSize;
	pioData->dataBuff.buf = pioData->buff;
	memcpy(pioData->dataBuff.buf, lpBuff, nBuffSize);
	pioData->ioType = IOType::SEND;
	pioData->uIndex = uIndex;

	if (WSASend(pIocp->m_socket,
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
	pckInfo.pConnection = this;
	memcpy(pckInfo.Buff, lpBuff, nBuffSize);

	m_pMainConnection->m_pSendBuff->push_back(pckInfo);

}


void CIocp::StopThread()
{
	m_bWorkerThreadLive = false;
	for (int i = 0; i < m_dwLockNum; i++)
	{
		if (PostQueuedCompletionStatus(m_CompletionPort, 0, (ULONG_PTR)0, 0) == 0)
		{
			std::cout << "PostQueuedCompletionStatus fail" << std::endl;
		};
	}
}

UINT CIocp::GetThreadLockNum()
{
	DWORD currentThreadId = GetCurrentThreadId();

	//스레드 아이디를 비교해서 스레드 순서에 따라 락 인덱스를 얻는다.
	for (DWORD i = 0; i < m_pMainConnection->m_dwLockNum; i++)
	{
		if (m_pMainConnection->m_pThreadIdArr[i] == currentThreadId)
		{
			return i;
		}
	}
}

UINT CIocp::GetWriteContainerSize()
{
	UINT Count = 0;

	for (int i = 0; i < m_pWriteBuff->size(); i++)
	{
		if ((*m_pWriteBuff)[i].pConnection != NULL)
			Count++;
	}
	return Count;
}

UINT CIocp::GetReadContainerSize()
{
	UINT Count = 0;

	if (m_pReadBuff->size() == 0)
		return Count;

	for (int i = 0; i < m_pReadBuff->size(); i++)
	{
		if ((*m_pReadBuff)[i].pConnection != NULL)
			Count++;
	}
	return Count;
}

