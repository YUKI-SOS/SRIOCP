#include "SRIocp.h"
#include "CConnection.h"

//전역 변수 초기화
AcceptFunc CIocp::g_pOnAcceptFunc = NULL;
ConnectFunc CIocp::g_pOnConnectFunc = NULL;
CloseFunc CIocp::g_pOnCloseFunc = NULL;
RecvFunc CIocp::g_pOnRecvFunc = NULL;


CIocp::CIocp()
{
	m_ListenSocket = NULL;
	m_CompletionPort = NULL;
	m_bWorkerThreadLive = false;
	m_ilWriteQueuePos = -1;
	//m_pTempBuff = NULL;
	//m_uTempBuffPos = 0;
	//m_uIoPos = 0;
	m_dwReadQueuePos = 0;
	m_dwSendQueuePos = 0;

	m_dwLockNum = 0;
}


CIocp::~CIocp()
{
	if (m_pBufferSwapLock != nullptr)
		delete[] m_pBufferSwapLock;
	if (m_pThreadIdArr != nullptr)
		delete[] m_pThreadIdArr;
	//if (m_pTempBuff != nullptr)
		//delete[] m_pTempBuff;

}

bool CIocp::InitConnectionList(DWORD dwCount)
{
	m_ConnectionList.resize(dwCount);

	for (DWORD i = 0; i < dwCount; i++)
	{
		CConnection* pConnection = new CConnection;
		SOCKET socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_IP,	NULL, 0, WSA_FLAG_OVERLAPPED);
		pConnection->Initialize(i, socket, RECV_RING_BUFFER_MAX, SEND_RING_BUFFER_MAX);
		pConnection->SetNetwork(this);
		m_ConnectionList[i] = pConnection;
	}

	m_dwConnectionMax = dwCount;

	return false;
}

bool CIocp::GetIoExFuncPointer()
{
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
		printf("AcceptEx WsaIoctl Error. WSAGetLastError = %d \n", WSAGetLastError());
		return false;
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
		printf("DisConnectEx WsaIoctl Error. WSAGetLastError = %d \n", WSAGetLastError());
		return false;
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
		printf("ConnectEx WsaIoctl Error. WSAGetLastError = %d \n", WSAGetLastError());
		return false;
	}

	return true;
}

bool CIocp::InitSocket(ECSType csType, UINT port)
{
	int retVal;
	WSADATA wsaData;

	m_eCSType = csType;

	//큐 리사이즈
	m_RecvQueue1.resize(RECV_QUEUE_MAX);
	m_RecvQueue2.resize(RECV_QUEUE_MAX);

	m_pReadQueue = &m_RecvQueue1;
	m_pWriteQueue = &m_RecvQueue2;

	m_pSendQueue.resize(SEND_QUEUE_MAX);

	//이벤트 초기화(오토 리셋)
	m_WriteQueueWaitEvent = CreateEvent(NULL, FALSE, FALSE, NULL); 

	//윈속 초기화
	if ((retVal = WSAStartup(MAKEWORD(2, 2), &wsaData)) != 0)
	{
		printf("WSAStartup Fail\n");
		return false;
	}

	//iocp객체 생성
	//CreateIoCompletionPort마지막 인자가 0이면 cpu 코어 개수만큼 스레드 이용
	if ((m_CompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0)) == NULL)
	{
		printf("CreateIoCompletionPort Fail\n");
		return false;
	}

	//워커스레드 생성
	if (CreateWorkerThread() == false) 
	{
		printf("CreateWorkerThread Fail\n");
		return false;
	}

	//클라이언트도 ConnectEx 등의 함수의 포인터를 얻기 위해 임의의 소켓이 필요함.
	if ((m_ListenSocket = WSASocket(AF_INET,
		SOCK_STREAM,
		IPPROTO_TCP,
		NULL,
		0,
		WSA_FLAG_OVERLAPPED
	)) == INVALID_SOCKET)
	{
		printf("WSASocket Fail\n");
		return false;
	}

	if (GetIoExFuncPointer() == false)
	{
		printf("GetIoExFuncPointer Fail\n");
		return false;
	}

	

	//아래로는 서버 초기화
	if (csType == ECSType::CLIENT)
		return true;

	//listen 소켓 iocp 연결
	if (CreateIoCompletionPort((HANDLE)m_ListenSocket,
		m_CompletionPort,
		(ULONG_PTR)this,
		0) == NULL)
	{
		printf("CreateIoCompletionPort Fail\n");
		return false;
	}

	//bind
	SOCKADDR_IN serverAddr;
	ZeroMemory(&serverAddr, sizeof(serverAddr));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serverAddr.sin_port = htons(port);

	//TCP홀펀칭 이미 사용중인 포트에 다른 소켓 강제 바인딩 
	//SetReuseSocketOpt(m_ListenSocket);

	if (bind(m_ListenSocket,
		(PSOCKADDR)&serverAddr,
		sizeof(serverAddr)) == SOCKET_ERROR)
	{
		printf("Bind Fail. WSAGetLastError = %d\n", WSAGetLastError());
		return false;
	}

	//listen
	if (listen(m_ListenSocket, 5) == SOCKET_ERROR)
	{
		printf("Listen Fail\n");
		return false;
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

	//m_dwLockNum = sysInfo.dwNumberOfProcessors * 2;
	m_dwLockNum = 2; //테스트

	//SRWLock 생성 및 초기화.
	m_pBufferSwapLock = new SRWLOCK[m_dwLockNum];
	for (DWORD i = 0; i < m_dwLockNum; i++)
	{
		InitializeSRWLock(m_pBufferSwapLock + i);
	}
	//스레드 아이디 배열에 저장
	m_pThreadIdArr = new DWORD[m_dwLockNum];

	//(CPU 개수 * 2)개의 워커 스레드 생성
	for (DWORD i = 0; i < m_dwLockNum; i++)
	{
		if ((threadHandle = (HANDLE)_beginthreadex(NULL,
			0,
			&WorkerThread,
			this,
			0,
			(unsigned int*)&m_pThreadIdArr[i])) == NULL)
		{
			printf("CreateWorkerThread Fail\n");
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
	ULONG_PTR key = NULL;
	LPOVERLAPPED lpOverlapped = NULL;
	DWORD dwTransferredBytes = 0;

	DWORD dwLockIndex = 0;
	DWORD dwCurrentThreadId = GetCurrentThreadId();

	char* pMsg = nullptr;
	DWORD dwMsgBytes = 0;
	DWORD dwMsgNum = 0;

	//스레드 아이디를 비교해서 스레드가 가질 락 인덱스를 가진다.
	for (DWORD i = 0; i < arg->m_dwLockNum; i++)
	{
		if (arg->m_pThreadIdArr[i] == dwCurrentThreadId)
			dwLockIndex = i;
	}

	while (arg->m_bWorkerThreadLive)
	{
		BOOL bRet = GetQueuedCompletionStatus(completionport, //CompletionPort 핸들
			&dwTransferredBytes,				//비동기 입출력 작업으로 전송된 바이트
			(PULONG_PTR)&key,			//CreateIoCompletionPort함수 호출시 전달한 세번째 인자가 여기 저장
			&lpOverlapped,			//비동기 입출력 함수 호출 시 전달한 오버랩 구조체 주소값.
			INFINITE);
		
		if (lpOverlapped == nullptr) 
		{
			printf("lpOverlapped = nullptr \n");
			continue;
		}

		/*if (key == NULL)
		{
			continue;
		}*/
		
		CIocp* pIocp = (CIocp*)key;
		OverlappedEX* pOverlapped = (OverlappedEX*)lpOverlapped;
		CConnection* pConnection = pIocp->GetConnection(pOverlapped->dwIndex);
		SOCKET socket = pConnection->GetSocket();

		printf("transferredBytes = %d\n", dwTransferredBytes);
		printf("eIoType = %d \n", (int)pOverlapped->eIoType);

		if (bRet == FALSE) 
		{
			printf("Socket = %d GetQueuedCompletionStatus Fail WSAGetLastError = %d \n", socket, WSAGetLastError());
			arg->CloseConnection(pIocp->m_uConnectionIndex);
			continue;
		}	

		
		//GetQueuedCompletionStatus 해서 가져오는데 성공했는데 전달받은 패킷이 0이면 접속이 끊긴 것으로 판단.
		if (dwTransferredBytes == 0
			&& pOverlapped->eIoType != IOType::ACCEPT
			&& pOverlapped->eIoType != IOType::CONNECT)
		{
			printf("dwTransferredBytes = 0 \n");
			arg->CloseConnection(pIocp->m_uConnectionIndex);

			continue;
		}

		printf("CurrentTreadId = %d IoType = %d \n", dwCurrentThreadId, pOverlapped->eIoType);

		if (pOverlapped->eIoType == IOType::CONNECT)
		{
			if (setsockopt(pConnection->GetSocket(), SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0) == SOCKET_ERROR)
			{
				printf("ConnectEX SocketOption Fail WSAGetLastError = %d\n", WSAGetLastError());
			};
			
			printf("Socket = %d Connected \n", socket);

			pConnection->SetConnectionStatus(true);
			OnConnect(pOverlapped->dwIndex);

			pConnection->PostRecv();
			//arg->RecvSet(pConnection);
			continue;
		}
		if (pOverlapped->eIoType == IOType::DISCONNECT)
		{
			//서버는 ReAccecptEx하면서 클라이언트는 소켓을 다시 할당하면서 InitConnectPool에서 isConnected를 false처리하기 때문에 여기서 하지 않는다.
			//클라는 isConnected인 소켓이 없으면 다시 소켓을 커넥션 수 만큼 만들기 때문에 판단하기 위해서 false로 만들지 않는다.
			//piocp->isConnected = false;
			printf("IOType is Disconnect. Socket = %d \n", socket);
			OnClose(pOverlapped->dwIndex);

			if (arg->m_eCSType == ECSType::SERVER)
				arg->ReAcceptSocket(pOverlapped->dwIndex);
			
			continue;
		}

		//recv send 구분
		//비동기 입출력에서 오버랩구조체를 인자로 전달할 때 오버랩구조체를 멤버로 가진 구조체를 오버랩으로 캐스팅해서 보내고
		//GetQueuedCompletionStatus에서 받은 오버랩 구조체를 다시 원래 구조체로 캐스팅하면 다른 멤버도 받아올 수 있다.
		//그런 방법으로 IOType Enum을 끼어넣어서 받아와서 구분짓는다.
		//GetQueuedCompletionStatus 에 들어오는 key 값에다가 객체 주소를 넘겨받아서 가져온다. 
		if (pOverlapped->eIoType == IOType::ACCEPT)
		{
			if (setsockopt(socket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char*)&(arg->m_ListenSocket), sizeof(SOCKET)) == SOCKET_ERROR)
			{
				printf("AcceptEX SocketOption Fail WSAGetLastError = %d\n", WSAGetLastError());
			};

			SOCKADDR_IN* sockAddr = NULL;
			int addrlen = sizeof(SOCKADDR);
			SOCKADDR_IN* remoteAddr = NULL;
			int remoteaddrlen = sizeof(SOCKADDR_IN);
			GetAcceptExSockaddrs(pOverlapped->wsabuff.buf, //커넥션의 m_AddrBuf
				0,
				sizeof(SOCKADDR_IN) + 16,
				sizeof(SOCKADDR_IN) + 16,
				(SOCKADDR**)&sockAddr,
				&addrlen,
				(SOCKADDR**)&remoteAddr,
				&remoteaddrlen);

			//std::string sSocket = std::to_string(pIoData->socket);
			//std::string sAddr = inet_ntoa(sockAddr->sin_addr);
			//std::string sPort = std::to_string(ntohs(sockAddr->sin_port));
			//std::string sRemoteAddr = inet_ntoa(remoteAddr->sin_addr);
			//std::string sRemotePort = std::to_string(ntohs(remoteAddr->sin_port));
			
			char* szRemoteAddr = inet_ntoa(remoteAddr->sin_addr);
			DWORD dwRemotePort = ntohs(remoteAddr->sin_port);

			static int iAcceptCnt = 0;
			iAcceptCnt++;
			printf("Accept Cnt = %d\n", iAcceptCnt);
			printf("Accept Socket = %d ip = %s port = %d \n", socket, inet_ntoa(remoteAddr->sin_addr), ntohs(remoteAddr->sin_port));

			CIocp::OnAccept(pOverlapped->dwIndex);

			pConnection->SetConnectionStatus(true);
			pConnection->SetRemoteIP(szRemoteAddr, ADDR_BUFF_SIZE);
			pConnection->SetRemotePort(dwRemotePort);
			//arg->RecvSet(pConnection);
			pConnection->PostRecv();

			continue;
		}
		else if (pOverlapped->eIoType == IOType::RECV)
		{			

			pConnection->RecvProcess(dwTransferredBytes, &pMsg, &dwMsgBytes, &dwMsgNum);
			pConnection->CheckReset();

			//라이트 큐에 삽입
			pIocp->PushWriteQueue(pOverlapped->dwIndex, pMsg, dwMsgNum, dwMsgBytes, dwLockIndex);
			//Recv 오버랩 재설정
			pConnection->PostRecv();
		}

		/*
		else if (pIoData->ioType == IOType::RECV)
		{
			//std::cout << *(int*)(pioData->Buff + 4) << "번 패킷 " << transferredBytes << "바이트 수신" << std::endl;

			pIocp->m_uIoPos = dwTransferredBytes;

			PacketInfo packetInfo;
			packetInfo.pConnection = pIocp;
			while (1)
			{
				//임시 버퍼에 남은 패킷이 있으면 iobuffer에 있는 것이 패킷의 시작부분이 아니라고 봄.
				if (pIocp->m_uTempBuffPos > 0)
				{
					//임시 버퍼에서 4바이트 읽은 크기가 임시 버퍼와 io버퍼의 크기보다 크면 쪼개져서 덜 받은 패킷으로 판단.
					if (*(int*)pIocp->m_pTempBuff > pIocp->m_uTempBuffPos + pIocp->m_uIoPos)
					{
						memcpy(pIocp->m_pTempBuff + pIocp->m_uTempBuffPos, pIoData->buff, pIocp->m_uIoPos);
						pIocp->m_uTempBuffPos += pIocp->m_uIoPos;
						pIocp->m_uIoPos = 0;
						goto MAKEPACKETEND;
					}
					//짤려서 뒤에 들어온 패킷부분을 임시 버퍼에 이어준다.
					memcpy(pIocp->m_pTempBuff + pIocp->m_uTempBuffPos, pIoData->buff, *(int*)pIocp->m_pTempBuff - pIocp->m_uTempBuffPos);
					//패킷을 만들어서 라이트버퍼에 넣어준다.
					memcpy(packetInfo.buff, pIocp->m_pTempBuff, *(int*)pIocp->m_pTempBuff);
					arg->PushWriteBuffer(&packetInfo, dwLockIndex);
					//io버퍼에서 임시 버퍼로 넘겨준 만큼 땡겨준다.
					memmove(pIoData->buff, pIoData->buff + *(int*)pIocp->m_pTempBuff - pIocp->m_uTempBuffPos, sizeof(pIoData->buff) - (*(int*)pIocp->m_pTempBuff - pIocp->m_uTempBuffPos));
					pIocp->m_uIoPos -= *(int*)pIocp->m_pTempBuff - pIocp->m_uTempBuffPos;
					//임시 버퍼를 비워준다.
					ZeroMemory(pIocp->m_pTempBuff, _msize(pIocp->m_pTempBuff));
					pIocp->m_uTempBuffPos = 0;
				}

				//패킷의 사이즈가 io버퍼 위치보다 크면 뒤에 더 받을 패킷이 있다고 보고 임시 버퍼에 불완전한 패킷 저장.
				if (pIocp->m_uIoPos < *(int*)pIoData->buff)
				{
					//임시 버퍼에 불완전 패킷 저장.
					memcpy(pIocp->m_pTempBuff + pIocp->m_uTempBuffPos, pIoData->buff, pIocp->m_uIoPos);
					pIocp->m_uTempBuffPos += pIocp->m_uIoPos;
					//불완전한 패킷 보내고 나머지를 땡긴다.
					memmove(pIoData->buff, pIoData->buff + pIocp->m_uIoPos, sizeof(pIoData->buff) - pIocp->m_uIoPos);
					//땡긴 나머지 부분을 0으로 채워준다.
					ZeroMemory(pIoData->buff + (sizeof(pIoData->buff) - pIocp->m_uIoPos), pIocp->m_uIoPos);
					pIocp->m_uIoPos = 0;
					goto MAKEPACKETEND;
				}

				//패킷이 시작부분이라고 보고.
				int packetSize = *(int*)pIoData->buff;

				if (packetSize == 0)
					goto MAKEPACKETEND;

				//패킷을 만들어서 라이트 버퍼에 저장. 
				memcpy(packetInfo.buff, pIoData->buff, packetSize);
				arg->PushWriteBuffer(&packetInfo, dwLockIndex);
				pIocp->m_uIoPos -= packetSize;
				memmove(pIoData->buff, pIoData->buff + packetSize, sizeof(pIoData->buff) - packetSize);
				ZeroMemory(pIoData->buff + (sizeof(pIoData->buff) - packetSize), packetSize);
			}

		MAKEPACKETEND:
			arg->RecvSet(pIocp);
		}
		*/
		//비동기 송신 이후 송신했다는 결과를 통지받을 뿐
		else if (pOverlapped->eIoType == IOType::SEND)
		{
			printf("completion %x\n", pOverlapped);
			printf("transferredBytes %d\n", dwTransferredBytes);
			//std::cout << *(int*)(pioData->Buff + 4) << "번 패킷 " << transferredBytes << "바이트 송신" << std::endl;

		}

	}

	char szLog[32];
	memset(szLog, 0, sizeof(szLog));
	sprintf_s(szLog, 32, "%d Tread END\n", dwCurrentThreadId);
	OutputDebugStringA(szLog);
	_endthreadex(0);

	return 0;
}

void CIocp::PacketProcess()
{
	//리드 버퍼 다 처리했고, 라이트버퍼에 남아있으면 스왑.
	/*if (m_dwReadQueuePos >= GetReadContainerSize()
		&& GetWriteContainerSize() != 0)
	{
		SwapRecvQueue();
	}*/

	//리드버퍼 다 처리했으면 탈출
	if (m_dwReadQueuePos >= m_dwReadQueueSize)
		goto SWAPCHECK;

	for (DWORD i = 0; i < m_dwReadQueueSize; i++) 
	{	
		printf("Read Queue Count = %d\n", m_dwReadQueueSize);

		PacketInfo* pPacketInfo = &(*m_pReadQueue)[m_dwReadQueuePos];
		DWORD dwIndex = pPacketInfo->dwIndex;
		CConnection* pConnection = GetConnection(dwIndex);

		if (pConnection->GetConnectionStaus() == false)
			continue;

		/*char szLog[32];
		memset(szLog, 0, sizeof(szLog));
		sprintf_s(szLog, "%d 패킷 처리\n", *(int*)(pPacketInfo->buff + 4));
		OutputDebugStringA(szLog);*/

		OnRecv(pPacketInfo->dwIndex, pPacketInfo->buff, pPacketInfo->dwLength);

		m_dwReadQueuePos++;
	}

	SWAPCHECK:
	if (m_dwWriteQueueSize > 0)
	{
		SwapRecvQueue();
	}
	
}

/*
void CIocp::SendPacketProcess()
{
	while (1)
	{
		if (m_dwSendQueuePos >= m_pSendQueue.size())
		{
			m_pSendQueue.clear();
			this->m_dwSendQueuePos = 0;
			break;
		}

		PacketInfo sendPacket = m_pSendQueue[m_dwSendQueuePos];

		if (sendPacket.pConnection == NULL)
			continue;

		CIocp* pIocp = sendPacket.pConnection;
		int size = *(int*)sendPacket.buff;
		pIocp->Send(pIocp->m_uConnectionIndex, sendPacket.buff, size);

		this->m_dwSendQueuePos++;
	}
}
*/
void CIocp::SwapRecvQueue()
{
	//스왑 중에 라이트 큐에 넣지 못하도록 일괄 락.
	for (DWORD i = 0; i < m_dwLockNum; i++)
	{
		AcquireSRWLockExclusive(m_pBufferSwapLock + i);
	}

	printf("Prev ReadQueueSize = %d WriteQueueSize = %d \n", m_dwReadQueueSize, m_dwWriteQueueSize);

	//리드 큐 다 읽었을 때 스왑이 일어남으로 리드 큐 비워준다. 
	//m_pReadQueue->clear();

	//스왑
	auto tempBuff = m_pWriteQueue;
	m_pWriteQueue = m_pReadQueue;
	m_pReadQueue = tempBuff;

	//라이트큐에 현재까지 쌓은 위치가 사이즈. 스왑 하면서 리드큐에 넣는다.
	m_dwReadQueueSize = m_dwWriteQueueSize;
	m_dwReadQueuePos = 0;
	InterlockedExchange(&m_ilWriteQueuePos, -1); //락으로 쌓여있는 상태니까 여긴 인터락 아니어도 될 거 같긴 한데.
	InterlockedExchange(&m_dwWriteQueueSize, 0);

	printf("After ReadQueueSize = %d WriteQueueSize = %d \n", m_dwReadQueueSize, m_dwWriteQueueSize);

	for (DWORD i = 0; i < m_dwLockNum; i++)
	{
		ReleaseSRWLockExclusive(m_pBufferSwapLock + i);
	}

	SetEvent(m_WriteQueueWaitEvent);
}

/*
void CIocp::PushWriteBuffer(PacketInfo* packetInfo, DWORD dwLockIndex)
{
	//큐에 인터락의 위치가 할당되어 있지 않다면 크기 2배 증가.
	//resize는 메모리 크기와 동시에 요소들의 초기화도 일어남.
	AcquireSRWLockExclusive(m_pBufferSwapLock + dwLockIndex);

	//인터락을 통해 원자적으로 크기 증가
	LONG buffPos = InterlockedIncrement(&m_ilWriteQueuePos);

	if (buffPos >= m_pWriteQueue->size())
	{
		if (m_pWriteQueue->size() == 0)
			m_pWriteQueue->resize(1);

		m_pWriteQueue->resize(m_pWriteQueue->size() * 2);
	}

	(*m_pWriteQueue)[buffPos] = *packetInfo;

	//std::cout << *(int*)(packetInfo->Buff + 4) << "번 패킷 라이트버퍼에 씀" << std::endl;

	ReleaseSRWLockExclusive(m_pBufferSwapLock + dwLockIndex);
}
*/

void CIocp::PushWriteQueue(DWORD dwIndex, char * pMsg, DWORD dwMsgNum, DWORD dwMsgBytes, DWORD dwLockIndex)
{
	//큐에 푸쉬하는 도중 큐 스왑이 일어나지 않기 위한 락
	AcquireSRWLockExclusive(m_pBufferSwapLock + dwLockIndex);

	int iTotalBytes = 0; //완성된 패킷의 헤더에 있는 사이즈를 더한 값

	for(DWORD i = 0; i < dwMsgNum; i++)
	{
		int iBytes = *(DWORD*)pMsg; //패킷 헤더에서 사이즈를 읽는다. 
		iTotalBytes += iBytes;
		DWORD uQueuePos = InterlockedIncrement(&m_ilWriteQueuePos);

		//패킷 하나가 가질 수 있는 사이즈 넘기는지 체크
		if (iBytes > PACKET_BUFF_MAX)
		{
			printf("%s %d", __FUNCTION__, __LINE__);
			__debugbreak();
		}

		//큐 사이즈가 넘치면 큐 스왑이 일어날 때 까지 대기한다.
		if (uQueuePos >= RECV_QUEUE_MAX) 
		{
			printf("RecvQueue OVER. Wait Swap \n");
			WaitForSingleObject(m_WriteQueueWaitEvent, INFINITE);
			//스왑 후 큐 위치를 다시 구한다.
			uQueuePos = InterlockedIncrement(&m_ilWriteQueuePos);
		}

		PacketInfo* pPacketInfo = &(*m_pWriteQueue)[uQueuePos];
		pPacketInfo->dwIndex = dwIndex;
		memcpy(pPacketInfo->buff, pMsg, iBytes);

		pMsg += iBytes;
		InterlockedIncrement(&m_dwWriteQueueSize);
	}

	//패킷 헤더에서의 사이즈와 인자로 받은 사이즈가 다른지 체크
	if (iTotalBytes != dwMsgBytes) 
	{
		printf("%s %d", __FUNCTION__, __LINE__);
		__debugbreak();
	}

	ReleaseSRWLockExclusive(m_pBufferSwapLock + dwLockIndex);
}

bool CIocp::InitAcceptPool(DWORD dwNum)
{
	DWORD dwFlags;
	DWORD dwBytes;

	for (DWORD i = 0; i < dwNum; i++)
	{
		//오버랩IO를 위해 구조체 세팅
		CConnection* pConnection = GetConnection(i);
		OverlappedEX* pOverlapped = pConnection->GetRecvOverlapped();
		SOCKET socket = pConnection->GetSocket();

		if (pOverlapped == nullptr)
			return false;

		ZeroMemory(&pOverlapped->overlapped, sizeof(OVERLAPPED));
		
		pOverlapped->wsabuff.len = ADDR_BUFF_SIZE;
		pOverlapped->wsabuff.buf = pConnection->GetAddrBuff();
		pOverlapped->eIoType = IOType::ACCEPT;
		dwFlags = 0;
		dwBytes = 0;

		pConnection->SetConnectionStatus(false);

		InitSocketOption(socket);

		if (AcceptEx(m_ListenSocket,
			socket,
			pOverlapped->wsabuff.buf,
			0, //Accept 하면서 데이터를 바로 수신하지 않고 연결만 수락하기 위해 0바이트 설정.
			sizeof(SOCKADDR_IN) + 16,
			sizeof(SOCKADDR_IN) + 16,
			&dwBytes,
			(LPOVERLAPPED)pOverlapped) == FALSE)
		{
			if (WSAGetLastError() != ERROR_IO_PENDING)
			{
				printf("AcceptEx Fail. WSAGetLastError = %d \n", WSAGetLastError());
				return false;
			}

		}

		//소켓과 iocp 연결
		if ((CreateIoCompletionPort((HANDLE)socket,
			m_CompletionPort,
			(ULONG_PTR)this,
			0)) == NULL)
		{
			printf("CreateIoCompletionPort bind error \n");
			return false;
		}

	}

	return true;
}

/*
bool CIocp::InitConnectPool(UINT num)
{
	//bind
	SOCKADDR_IN addr;
	ZeroMemory(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	DWORD dwFlags;
	DWORD dwBytes;

	for (int i = 0; i < num; i++)
	{
		//오버랩IO를 위해 구조체 세팅
		IODATA* pioData = new IODATA;
		
		if (pioData == NULL) 
			return false;

		ZeroMemory(pioData, sizeof(IODATA));
		pioData->socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_IP,
			NULL, 0, WSA_FLAG_OVERLAPPED);
		pioData->WSABuff.len = 0;
		pioData->WSABuff.buf = NULL;
		pioData->ioType = IOType::CONNECT;
		dwFlags = 0;
		dwBytes = 0;

		CIocp* pConnection = m_ConnectionList[i];
		pConnection->m_socket = pioData->socket;
		pConnection->m_pIoData = pioData;
		pConnection->m_isConnected = false;
		pConnection->m_pMainConnection = this;

		pioData->dwIndex = pConnection->m_uConnectionIndex;

		InitSocketOption(pConnection->m_socket);

		//TCP홀펀칭 이미 사용중인 포트에 다른 소켓 강제 바인딩 
		//SetReuseSocketOpt(pConnection->m_socket);

		//ConnectEx용 bind
		if (bind(pConnection->m_socket, (PSOCKADDR)&addr,
			sizeof(addr)) == SOCKET_ERROR)
		{
			printf("ConnectEx bind Fail \n");
			return false;
		}

		//소켓과 iocp 연결
		if ((CreateIoCompletionPort((HANDLE)pioData->socket,
			m_CompletionPort,
			(ULONG_PTR)pConnection,
			0)) == NULL)
		{
			printf("CreateIoCompletionPort Bind Error \n");
			return false;
		}

	}
	return true;
}
*/

bool CIocp::ReAcceptSocket(UINT uIndex)
{
	//DWORD flags;
	DWORD dwBytes = 0;

	CConnection* pConnection = GetConnection(uIndex);
	OverlappedEX* pOverlapped = pConnection->GetRecvOverlapped();
	SOCKET socket = pConnection->GetSocket();

	if (pOverlapped == nullptr)
		return false;

	ZeroMemory(&pOverlapped->overlapped, sizeof(OVERLAPPED));

	pOverlapped->wsabuff.len = ADDR_BUFF_SIZE;
	pOverlapped->wsabuff.buf = pConnection->GetAddrBuff();
	pOverlapped->eIoType = IOType::ACCEPT;

	pConnection->SetConnectionStatus(false);

	if (AcceptEx(m_ListenSocket,
		socket,
		pOverlapped->wsabuff.buf,
		0,
		sizeof(SOCKADDR_IN) + 16,
		sizeof(SOCKADDR_IN) + 16,
		&dwBytes,
		(LPOVERLAPPED)pOverlapped) == FALSE)
	{
		if (WSAGetLastError() != ERROR_IO_PENDING)
		{
			printf("ReAcceptEx Fail. WSAGetLastError = %d \n", WSAGetLastError());
			return false;
		}
	}

	printf("uIndex = %d socket = %d ReAccept \n", uIndex, socket);
	return true;
}

bool CIocp::CloseConnection(DWORD dwIndex)
{
	//TF_REUSE_SOCKET 하여 소켓 재활용 한다.
	//비정상 적인 상황이나 종료 시에 CConnection이 가진 CloseSocket 으로 진짜 소켓 해제.
	
	CConnection* pConnection = GetConnection(dwIndex);
	OverlappedEX* pOverlapped = pConnection->GetRecvOverlapped();
	SOCKET socket = pConnection->GetSocket();

	if (pOverlapped == nullptr)
		return false;

	ZeroMemory(&pOverlapped->overlapped, sizeof(OVERLAPPED));

	pOverlapped->wsabuff.len = 0;
	pOverlapped->wsabuff.buf = pConnection->GetRecvRingBuff()->GetReadPtr();
	pOverlapped->eIoType = IOType::DISCONNECT;
	pConnection->SetConnectionStatus(false);

	//TF_DISCONNECT넣으면 10022 WSAEINVAL 오류 바인딩 실패. 
	//lpfnDisconnectEx(socket, NULL, TF_DISCONNECT | TF_REUSE_SOCKET, NULL);

	if (lpfnDisconnectEx(socket,
		(LPOVERLAPPED)pOverlapped,
		TF_REUSE_SOCKET,
		NULL) == FALSE)
	{
		if (WSAGetLastError() != ERROR_IO_PENDING)
		{
			printf("Socket = %d DissconnectEx Error. WSAGetLastError = %d \n", socket, WSAGetLastError());
			return false;
		}
	};

	printf("socket = %d DisconnectEX \n", socket);

	//TransmitFile(socket, NULL, 0, 0, (LPOVERLAPPED)pClient->m_ioData, NULL, TF_DISCONNECT | TF_REUSE_SOCKET);
	//shutdown(socket, SD_BOTH);
	//closesocket(socket);

	return true;
}

SOCKET CIocp::Connect(char* pAddress, UINT port)
{
	//gethostbyname이 deprecated. getaddrinfo를 대신 써서 domain으로 부터 ip를 얻는다. 
	/*
	char host[20];
	gethostname(host, 20);
	hostent* hent = gethostbyname(host);
	in_addr addr;
	addr.s_addr = *(ULONG*)*hent->h_addr_list;
	char* address = inet_ntoa(addr);
	*/

	ADDRINFO* pAddrInfo = nullptr;
	ADDRINFO stAddrInfo = { 0, };
	stAddrInfo.ai_family = AF_INET;
	stAddrInfo.ai_socktype = SOCK_STREAM;
	stAddrInfo.ai_protocol = IPPROTO_TCP;

	getaddrinfo(pAddress, NULL, &stAddrInfo, &pAddrInfo);

	if (pAddrInfo == nullptr)
	{
		std::cout << "domain convert address fail" << std::endl;
		return INVALID_SOCKET;
	}

	sockaddr_in* pSockAddr_in = (sockaddr_in*)pAddrInfo->ai_addr;
	char* pAddr = inet_ntoa(pSockAddr_in->sin_addr);

	SOCKADDR_IN sockAddr;
	ZeroMemory(&sockAddr, sizeof(sockAddr));
	sockAddr.sin_family = AF_INET;
	//inet_addr이 deprecated되어 inet_pton(AF_INET, char_str, &(sockAddr.sin_addr.s_addr));
	sockAddr.sin_addr.s_addr = inet_addr(pAddr);
	sockAddr.sin_port = htons(port);

	CConnection* pConnection = GetFreeConnection();

	if (pConnection == nullptr)
		return INVALID_SOCKET;

	OverlappedEX* pOverlapped = pConnection->GetRecvOverlapped();
	SOCKET socket = pConnection->GetSocket();
	/*
	if (pConnection == NULL)
	{
		//다시 커넥트 소켓풀 초기화
		if (!InitConnectPool(m_dwConnectionMax))
		{
			std::cout << "re_InitConnectPool fail" << std::endl;
		};
		OutputDebugString(L"커넥션풀 초기화");
		pConnection = GetFreeConnection();
	}
	*/

	//어차피 해당 타입이 다른 프로세스로 넘어가진 않는다. 
	//받는 쪽 워커스레드에서 Accept하면 recv를 다시 연결해 주되 IOType::CONNECT으로 해서
	//커넥트 시에 발생하는 0바이트 패킷을 연결이 끊어주는 패킷과 구분해주도록 한다.
	pOverlapped->eIoType = IOType::CONNECT;

	if (lpfnConnectEx(socket,
		(SOCKADDR*)&sockAddr,
		sizeof(sockAddr),
		NULL,
		0,
		NULL,
		(LPOVERLAPPED)pOverlapped) == FALSE)
	{
		if (WSAGetLastError() != ERROR_IO_PENDING)
		{
			printf("ConnectEx Fail. WSAGetLastError = %d \n", WSAGetLastError());
			return NULL;
		}
	};

	return socket;
}

/*
bool CIocp::RecvSet(CConnection* pConnection)
{
	DWORD dwFlags = 0;
	DWORD dwBytes = 0;

	//오버랩IO를 위해 구조체 세팅
	IODATA* pioData = pConnection->m_pIoData;

	if (pioData == NULL) 
		return false;

	ZeroMemory(pioData, sizeof(IODATA));
	pioData->socket = pConnection->m_socket;
	pioData->WSABuff.len = RECV_PACKET_MAX;
	pioData->WSABuff.buf = pioData->buff;
	pioData->ioType = IOType::RECV;
	pioData->dwIndex = pConnection->m_uConnectionIndex;

	if (WSARecv(pConnection->m_socket,
		&pioData->WSABuff,
		1,
		&dwBytes,
		&dwFlags,
		(LPOVERLAPPED)pioData,
		NULL) == SOCKET_ERROR )
	{
		if (WSAGetLastError() != ERROR_IO_PENDING)
		{
			printf("WSARecv Fail. WSAGetLastError = %d \n", WSAGetLastError());
			return false;
		}
	}

	return true;
}
*/

CConnection* CIocp::GetConnection(DWORD dwIndex)
{
	return m_ConnectionList[dwIndex];
}



CConnection* CIocp::GetFreeConnection()
{
	for (DWORD i = 0; i < m_dwConnectionMax; i++) 
	{
		if (m_ConnectionList[i]->GetConnectionStaus() == false)
			return m_ConnectionList[i];
	}
	
	return nullptr;
}

/*
bool CIocp::GetPeerName(char* pAddress, WORD* pPort)
{
	SOCKADDR_IN addr;
	int addrlen = sizeof(SOCKADDR_IN);
	getpeername(m_socket, (sockaddr*)&addr, &addrlen);

	char* p = inet_ntoa(addr.sin_addr);

	memcpy(pAddress, p, strlen(p));
	*pPort = ntohs(addr.sin_port);

	return true;
}
*/

bool CIocp::Send(DWORD dwIndex, char* pMsg, DWORD dwBytes)
{
	DWORD dwCurrentThreadId = GetCurrentThreadId();
	
	CConnection* pConnection = GetConnection(dwIndex);
	bool ret = pConnection->PushSend(pMsg, dwBytes);

	/*DWORD dwBytes = 0;
	IODATA* pioData = new IODATA;
	ZeroMemory(&pioData->overlapped, sizeof(OVERLAPPED));
	pioData->socket = pIocp->m_socket;
	pioData->WSABuff.len = nBuffSize;
	pioData->WSABuff.buf = pioData->buff;
	memcpy(pioData->WSABuff.buf, lpBuff, nBuffSize);
	pioData->ioType = IOType::SEND;
	pioData->dwIndex = uIndex;

	printf("nBuffSize = %d \n", nBuffSize);

	if (WSASend(pIocp->m_socket,
		&pioData->WSABuff,
		1,
		&dwBytes,
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
	
	printf("sendbytes = %d \n", dwBytes);*/

	return ret;
}

/*
void CIocp::SendToBuff(void* lpBuff, int nBuffSize)
{
	//Recv를 순서대로 받도록 동기화했기 때문에 Send에서 별도의 락 불필요.
	PacketInfo PacketInfo;
	PacketInfo.pConnection = this;
	memcpy(PacketInfo.buff, lpBuff, nBuffSize);

	m_pSendQueue.push_back(PacketInfo);

}
*/

void CIocp::StopThread()
{
	m_bWorkerThreadLive = false;
	for (DWORD i = 0; i < m_dwLockNum; i++)
	{
		if (PostQueuedCompletionStatus(m_CompletionPort, 0, (ULONG_PTR)0, 0) == 0)
		{
			printf("PostQueuedCompletionStatus Fail \n");
		};
	}
}

UINT CIocp::GetThreadLockNum()
{
	DWORD currentThreadId = GetCurrentThreadId();

	//스레드 아이디를 비교해서 스레드 순서에 따라 락 인덱스를 얻는다.
	for (DWORD i = 0; i < m_dwLockNum; i++)
	{
		if (m_pThreadIdArr[i] == currentThreadId)
		{
			return i;
		}
	}

	return -1;
}

/*
UINT CIocp::GetWriteContainerSize()
{
	UINT Count = 0;

	for (int i = 0; i < m_pWriteQueue->size(); i++)
	{
		if ((*m_pWriteQueue)[i].pConnection != NULL)
			Count++;
	}
	return Count;
}
*/
/*
UINT CIocp::GetReadContainerSize()
{
	UINT Count = 0;

	if (m_pReadQueue->size() == 0)
		return Count;

	for (int i = 0; i < m_pReadQueue->size(); i++)
	{
		if ((*m_pReadQueue)[i].pConnection != NULL)
			Count++;
	}
	return Count;
}
*/
