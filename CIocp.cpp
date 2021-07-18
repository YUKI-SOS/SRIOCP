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

	//���� �Ҵ�
	readBuff = new std::vector<PacketInfo>;
	writeBuff = new std::vector<PacketInfo>;

	sendBuff = new std::vector<PacketInfo>;
	//sendBuff->resize(1);

	readBuff->resize(1);
	writeBuff->resize(1);
	//���� �ʱ�ȭ
	if ((retVal = WSAStartup(MAKEWORD(2, 2), &wsaData)) != 0)
	{
		std::cout << "WSAStartup fail" << std::endl;
		return false;
	}

	//iocp��ü ����
	//CreateIoCompletionPort������ ���ڰ� 0�̸� cpu �ھ� ������ŭ ������ �̿�
	if ((completionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0)) == NULL)
	{
		std::cout << "CreateIoCompletionPort fail" << std::endl;
		return false;
	}


	//��Ŀ������ ����
	CreateWorkerThread();

	//��Ŷ ó�� ������ ����
	//CreatePacketProcessThread();

	//Ŭ���̾�Ʈ�� ConnectEx ���� �Լ��� �����͸� ��� ���� ������ ������ �ʿ���.
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

	//AcceptEx �Լ� �� �� �ֵ��� ���
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


	//DisconnectEx �Լ� �� �� �ֵ��� ���
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

	//ConnectEx �Լ� �� �� �ֵ��� ���
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

	//�Ʒ��δ� ���� �ʱ�ȭ
	if (csType == CSType::CLIENT) return true;

	//listen ���� iocp ����
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

	//TCPȦ��Ī �̹� ������� ��Ʈ�� �ٸ� ���� ���� ���ε� 
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

	//��Ʈ�� 0���� ���ε� ���� ��� �Ҵ����� ��Ʈ�� �˾Ƴ���. 
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
	//socket Reuse Option. SO_REUSEADDR�� ���� ���Ͽ����� time_wait�� ��Ȱ�� �� �� �ִ� �� ����.
	int option = 1;
	if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, (char*)&option, sizeof(int)) == SOCKET_ERROR)
	{
		std::cout << "Set Socket option ReuseAddr error :" << WSAGetLastError() << std::endl;
	};
}

void CIocp::SetLingerOpt(SOCKET socket)
{
	//onoff 0 - default ���Ϲ��ۿ� ���� �����͸� ���� ������ �����ϴ� ��������
	//onoff 1 linger 0 - close ��� �����ϰ� ���Ϲ��ۿ� ���� �����͸� ������ ����������.
	//onoff 1 linger 1 - �����ð����� ����� �� ���Ϲ��ۿ� ���� �����͸� ���� �������� �� ������ �������� �ϸ� ���� �� ������ ���������� ������ �Բ� ����.
	LINGER linger = { 0,0 };
	linger.l_onoff = 1;
	linger.l_linger = 0;

	setsockopt(socket, SOL_SOCKET, SO_LINGER, (CHAR*)&linger, sizeof(linger));
}

void CIocp::SetNagleOffOpt(SOCKET socket)
{
	int nagleOpt = 1; //1 ��Ȱ��ȭ 0 Ȱ��ȭ
	setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, (const char*)&nagleOpt, sizeof(nagleOpt));
}

bool CIocp::CreateWorkerThread()
{
	HANDLE threadHandle; //��Ŀ������ �ڵ�
	DWORD threadID;

	bIsWorkerThread = true;

	//cpu ���� Ȯ��
	GetSystemInfo(&sysInfo);

	dwLockNum = sysInfo.dwNumberOfProcessors * 2;
	//SRWLock ���� �� �ʱ�ȭ.
	m_BufferSwapLock = new SRWLOCK[dwLockNum];
	for (DWORD i = 0; i < dwLockNum; i++) 
	{
		InitializeSRWLock(m_BufferSwapLock + i);
	}
	//������ ���̵� �迭�� ����
	m_ThreadIdArr = new DWORD[dwLockNum];

	//(CPU ���� * 2)���� ��Ŀ ������ ����
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

//Overlapped IO �۾� �Ϸ� �뺸�� �޾� ó���ϴ� ��Ŀ ������
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

	//������ ���̵� ���ؼ� �����尡 ���� �� �ε����� ������.
	for (DWORD i = 0; i < arg->dwLockNum; i++) 
	{
		if (arg->m_ThreadIdArr[i] == currentThreadId)
			dwLockIndex = i;
	}
	

	while (arg->bIsWorkerThread)
	{
		if (GetQueuedCompletionStatus(completionport, //CompletionPort �ڵ�
			&transferredBytes,				//�񵿱� ����� �۾����� ���۵� ����Ʈ
			(PULONG_PTR)&key,			//CreateIoCompletionPort�Լ� ȣ��� ������ ����° ���ڰ� ���� ����
			&lpOverlapped,			//�񵿱� ����� �Լ� ȣ�� �� ������ ������ ����ü �ּҰ�.
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
		if (pioData->ioType == IOType::ACCEPT) { std::cout << currentThreadId << "�� ������ iotype = accept\n"; }
		else if (pioData->ioType == IOType::CONNECT) { std::cout << currentThreadId << "�� ������ iotype = connect\n"; }
		else if (pioData->ioType == IOType::DISCONNECT) { std::cout << currentThreadId << "�� ������ iotype = disconnect\n"; }
		//else if (pioData->ioType == IOType::RECV) { std::cout << currentThreadId << "�� ������ iotype = recv\n"; }
		//else if (pioData->ioType == IOType::SEND) { std::cout << currentThreadId << "�� ������ iotype = send\n"; }
		//std::cout << currentThreadId << "�� ������ iotype = " << (int)pioData->ioType << std::endl;

		

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
			//������ ReAccecptEx�ϸ鼭 Ŭ���̾�Ʈ�� ������ �ٽ� �Ҵ��ϸ鼭 InitConnectPool���� isConnected�� falseó���ϱ� ������ ���⼭ ���� �ʴ´�.
			//Ŭ��� isConnected�� ������ ������ �ٽ� ������ Ŀ�ؼ� �� ��ŭ ����� ������ �Ǵ��ϱ� ���ؼ� false�� ������ �ʴ´�.
			//piocp->isConnected = false;
			std::string sSocket = std::to_string(piocp->m_socket);
			std::cout << "Closing socket " + sSocket << std::endl;
			piocp->OnClose();
			if (arg->m_csType == CSType::SERVER)
				arg->ReAcceptSocket(piocp->m_socket);
			continue;
		}
		//GetQueuedCompletionStatus �ؼ� �������µ� �����ߴµ� ���޹��� ��Ŷ�� 0�̸� ������ ���� ������.
		if (transferredBytes == 0 && pioData->ioType != IOType::ACCEPT && pioData->ioType != IOType::CONNECT)
		{
			//piocp->OnClose();
			std::cout << "Enter 0byte\n";
			arg->CloseSocket(piocp->m_socket);
			
			continue;
		}


		//recv send ����
		//�񵿱� ����¿��� ����������ü�� ���ڷ� ������ �� ����������ü�� ����� ���� ����ü�� ���������� ĳ�����ؼ� ������
		//GetQueuedCompletionStatus���� ���� ������ ����ü�� �ٽ� ���� ����ü�� ĳ�����ϸ� �ٸ� ����� �޾ƿ� �� �ִ�.
		//�׷� ������� IOType Enum�� ����־ �޾ƿͼ� �������´�.
		//GetQueuedCompletionStatus �� ������ key �����ٰ� ��ü �ּҸ� �Ѱܹ޾Ƽ� �����´�. 

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
			std::cout << "���� ����= " << a << "���� �ѹ�=" + sSocket + "Ŭ���̾�Ʈ ����:IP �ּ�=" + sRemoteAddr + "��Ʈ ��ȣ=" + sRemotePort << std::endl;
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
			//std::cout << *(int*)(pioData->Buff + 4) << "�� ��Ŷ " << transferredBytes << "����Ʈ ����" << std::endl;

			piocp->ioBuffPos = transferredBytes;

			PacketInfo packetInfo;
			packetInfo.connection = piocp;
			while (1) 
			{
				//���ú� ���ۿ� ���� ��Ŷ�� ������ iobuffer�� �ִ� ���� ��Ŷ�� ���ۺκ��� �ƴ϶�� ��.
				if(piocp->recvBuffPos > 0)
				{
					//���ú���ۿ��� 4����Ʈ ���� ũ�Ⱑ ���ú� ���ۿ� io������ ũ�⺸�� ũ�� �ɰ����� �� ���� ��Ŷ���� �Ǵ�.
					if (*(int*)piocp->recvBuff > piocp->recvBuffPos + piocp->ioBuffPos) 
					{
						memcpy(piocp->recvBuff + piocp->recvBuffPos, pioData->Buff, piocp->ioBuffPos);
						piocp->recvBuffPos += piocp->ioBuffPos;
						piocp->ioBuffPos = 0;
						goto MAKEPACKETEND;
					}
					//©���� �ڿ� ���� ��Ŷ�κ��� ���ú� ���ۿ� �̾��ش�.
					memcpy(piocp->recvBuff + piocp->recvBuffPos, pioData->Buff, *(int*)piocp->recvBuff - piocp->recvBuffPos);
					//��Ŷ�� ���� ����Ʈ���ۿ� �־��ش�.
					memcpy(packetInfo.Buff, piocp->recvBuff, *(int*)piocp->recvBuff);
					arg->PushWriteBuffer(&packetInfo, dwLockIndex);
					//io���ۿ��� ���ú� ���۷� �Ѱ��� ��ŭ �����ش�.
					memmove(pioData->Buff, pioData->Buff + *(int*)piocp->recvBuff - piocp->recvBuffPos, sizeof(pioData->Buff) - (*(int*)piocp->recvBuff - piocp->recvBuffPos));
					piocp->ioBuffPos -= *(int*)piocp->recvBuff - piocp->recvBuffPos;
					//���ú� ���۸� ����ش�.
					ZeroMemory(piocp->recvBuff, _msize(piocp->recvBuff));
					piocp->recvBuffPos = 0;
				}

				//��Ŷ�� ����� io���� ��ġ���� ũ�� �ڿ� �� ���� ��Ŷ�� �ִٰ� ���� ���ú� ���ۿ� �ҿ����� ��Ŷ ����.
				if (piocp->ioBuffPos < *(int*)pioData->Buff)
				{
					//���ú� ���ۿ� �ҿ��� ��Ŷ ����.
					memcpy(piocp->recvBuff + piocp->recvBuffPos, pioData->Buff, piocp->ioBuffPos);
					piocp->recvBuffPos += piocp->ioBuffPos;
					//�ҿ����� ��Ŷ ������ �������� �����.
					memmove(pioData->Buff, pioData->Buff + piocp->ioBuffPos, sizeof(pioData->Buff) - piocp->ioBuffPos);
					//���� ������ �κ��� 0���� ä���ش�.
					ZeroMemory(pioData->Buff + (sizeof(pioData->Buff) - piocp->ioBuffPos), piocp->ioBuffPos);
					piocp->ioBuffPos = 0;
					goto MAKEPACKETEND;
				}

				//��Ŷ�� ���ۺκ��̶�� ����.
				int packetSize = *(int*)pioData->Buff;

				if(packetSize == 0)
					goto MAKEPACKETEND;

				//��Ŷ�� ���� ����Ʈ ���ۿ� ����. 
				memcpy(packetInfo.Buff, pioData->Buff, packetSize);
				arg->PushWriteBuffer(&packetInfo, dwLockIndex);
				piocp->ioBuffPos -= packetSize;
				memmove(pioData->Buff, pioData->Buff + packetSize, sizeof(pioData->Buff) - packetSize);
				ZeroMemory(pioData->Buff + (sizeof(pioData->Buff) - packetSize), packetSize);
			}

			MAKEPACKETEND:
			arg->RecvSet(piocp);
		}

		//�񵿱� �۽� ���� �۽��ߴٴ� ����� �������� ��
		else if (pioData->ioType == IOType::SEND)
		{
			//std::cout << *(int*)(pioData->Buff + 4) << "�� ��Ŷ " << transferredBytes << "����Ʈ �۽�" << std::endl;
			delete pioData;
		}

	}
	//DWORD currentThreadId = GetCurrentThreadId();
	CString ds;
	ds.Format(L"%d ������ ����\n", currentThreadId);
	OutputDebugStringW(ds);
	_endthreadex(0);

	return 0;
}

void CIocp::PacketProcess()
{
	while (1)
	{
		//���� ���� �� ó���߰�, ����Ʈ���ۿ� ���������� ����.
		if (this->readBuffPos >= this->GetReadContainerSize() && this->GetWriteContainerSize() != 0)
		{
			this->SwapRWBuffer();
		}

		//������� �� ó�������� ��ŵ.
		if (this->readBuffPos >= this->GetReadContainerSize()) break;

		//std::cout << "read���� size = " << this->GetReadContainerSize() << std::endl;

		PacketInfo packetInfo = (*this->readBuff)[this->readBuffPos];

		if (packetInfo.connection == NULL)
			continue;

		/*char log[24];
		sprintf_s(log, "%d ��Ŷ ó��\n", *(int*)(packetInfo.Buff + 4));
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

	//std::cout << "���� �� read���� size = " << GetReadContainerSize() << " write���� size = " << GetWriteContainerSize() << std::endl;
	readBuff->clear();

	auto tempBuff = writeBuff;
	writeBuff = readBuff;
	readBuff = tempBuff;
	//std::cout << "���� �� read���� size = " << GetReadContainerSize() << " write���� size = " << GetWriteContainerSize() << std::endl;
	InterlockedExchange(&ilWriteBuffPos, -1);
	readBuffPos = 0;

	for (DWORD i = 0; i < dwLockNum; i++)
	{
		ReleaseSRWLockExclusive(m_BufferSwapLock + i);
	}
}

void CIocp::PushWriteBuffer(PacketInfo* packetInfo, DWORD dwLockIndex)
{
	//ť�� ���Ͷ��� ��ġ�� �Ҵ�Ǿ� ���� �ʴٸ� ũ�� 2�� ����.
	//deque�� ��� �޸𸮰� ���������� �Ҵ�Ǿ����� �ʱ� ������ reserve �Լ��� ����.
	//resize�� �޸� ũ��� ���ÿ� ��ҵ��� �ʱ�ȭ�� �Ͼ.
	AcquireSRWLockExclusive(m_BufferSwapLock + dwLockIndex);
	
	//���Ͷ��� ���� ���������� ũ�� ����
	ULONG buffPos = InterlockedIncrement(&ilWriteBuffPos);

	if (buffPos >= writeBuff->size())
	{
		if (writeBuff->size() == 0)
			writeBuff->resize(1);

		writeBuff->resize(writeBuff->size() * 2);
	}

	(*writeBuff)[buffPos] = *packetInfo;

	//std::cout << *(int*)(packetInfo->Buff + 4) << "�� ��Ŷ ����Ʈ���ۿ� ��" << std::endl;
	
	ReleaseSRWLockExclusive(m_BufferSwapLock + dwLockIndex);
}

bool CIocp::InitAcceptPool(UINT num)
{
	DWORD flags;
	DWORD recvBytes;

	for (int i = 0; i < num; i++)
	{
		//������IO�� ���� ����ü ����
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

		////���ϰ� iocp ����
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
		//������IO�� ���� ����ü ����
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
		ds.Format(L"���� ������ ����%d\n", pioData->connectSocket);
		OutputDebugString(ds);

		CIocp* pClient = pConnectionList[i];
		pClient->m_socket = pioData->connectSocket;
		pClient->m_ioData = pioData;
		pClient->isConnected = false;
		pClient->m_pMainConnection = this;

		InitSocketOption(pClient->m_socket);

		//TCPȦ��Ī �̹� ������� ��Ʈ�� �ٸ� ���� ���� ���ε� 
		SetReuseSocketOpt(pClient->m_socket);

		//ConnectEx�� bind
		if (bind(pClient->m_socket, (PSOCKADDR)&addr,
			sizeof(addr)) == SOCKET_ERROR) 
		{
			std::cout << "ConnectEx bind fail" << std::endl;
			return false;
		}

		//���ϰ� iocp ����
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
	//TF_DISCONNECT������ 10022 WSAEINVAL ���� ���ε� ����. �̹� bind�� ���Ͽ� ���ε��ϰų� �ּ�ü�谡 �ϰ������� ���� ��
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

	//gethostbyname�� deprecated. getaddrinfo�� ��� �Ἥ domain���� ���� ip�� ��´�. 
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

	//���⼭ ȣ��Ʈ�� ip��°� ���ʿ��� ����. ������ �ű���.
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
	//inet_addr�� deprecated�Ǿ� inet_pton(AF_INET, char_str, &(sockAddr.sin_addr.s_addr)); ��ü�ؾ� ������...
	sockAddr.sin_addr.s_addr = inet_addr(conAddr);
	sockAddr.sin_port = htons(port);

	delete[] szHost;

	CIocp* pClient = GetNoneConnectConnection();
	if (pClient == NULL)
	{
		//�ٽ� Ŀ��Ʈ ����Ǯ �ʱ�ȭ
		if (!InitConnectPool(m_ChildSockNum))
		{
			std::cout << "re_InitConnectPool fail" << std::endl;
		};
		OutputDebugString(L"Ŀ�ؼ�Ǯ �ʱ�ȭ");
		pClient = GetNoneConnectConnection();
	}

	//������ �ش� Ÿ���� �ٸ� ���μ����� �Ѿ�� �ʴ´�. 
	//�޴� �� ��Ŀ�����忡�� Accept�ϸ� recv�� �ٽ� ������ �ֵ� IOType::CONNECT���� �ؼ�
	//Ŀ��Ʈ �ÿ� �߻��ϴ� 0����Ʈ ��Ŷ�� ������ �����ִ� ��Ŷ�� �������ֵ��� �Ѵ�.
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

	//������IO�� ���� ����ü ����
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

	//������IO�� ���� ����ü ����
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
	ds.Format(L"%d ���� ������ Ȯ����\n", currentThreadId);
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

	//std::cout << *(int*)(pioData->dataBuff.buf + 4) << "�� ��Ŷ " << sendbytes << "byte send" << std::endl;
	return true;
}

void CIocp::SendToBuff(void* lpBuff, int nBuffSize)
{
	//Recv�� ������� �޵��� ����ȭ�߱� ������ Send���� ������ �� ���ʿ�.
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

	//������ ���̵� ���ؼ� ������ ������ ���� �� �ε����� ��´�.
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


