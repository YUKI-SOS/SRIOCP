#include "CConnection.h"
#include "SRIocp.h"

CConnection::CConnection()
{
	m_pNetwork = nullptr;
	m_dwConnectionIndex = -1;
	m_socket = INVALID_SOCKET;
	m_ConnectionStatus = false;

	memset(m_szIP, 0 , sizeof(m_szIP));
	memset(m_AddrBuf, 0, sizeof(m_AddrBuf));
	m_dwRemotePort = -1;

	m_pRecvOverlapped = nullptr;
	m_pSendOverlapped = nullptr;

	m_pRecvBuff = nullptr;
	m_pSendBuff = nullptr;

	m_dwSendWait = 0;
}

CConnection::~CConnection()
{
	m_pNetwork = nullptr;
	m_dwConnectionIndex = -1;
	m_socket = INVALID_SOCKET;
	m_ConnectionStatus = false;

	memset(m_szIP, 0, sizeof(m_szIP));
	memset(m_AddrBuf, 0, sizeof(m_AddrBuf));
	m_dwRemotePort = -1;

	delete m_pRecvOverlapped;
	m_pRecvOverlapped = nullptr;
	delete m_pSendOverlapped;
	m_pSendOverlapped = nullptr;

	delete m_pRecvBuff;
	m_pRecvBuff = nullptr;
	delete m_pSendBuff;
	m_pSendBuff = nullptr;

	m_dwSendWait = 0;

}

bool CConnection::Initialize(DWORD dwConnectionIndex, SOCKET socket, DWORD dwRecvRingBuffSize, DWORD dwSendRingBuffSize)
{
	m_dwConnectionIndex = dwConnectionIndex;
	m_socket = socket;

	m_pRecvBuff = new SRRecvRingBuffer();
	m_pRecvBuff->Initialize(dwRecvRingBuffSize);
	
	m_pSendBuff = new SRSendRingBuffer();
	m_pSendBuff->Initialize(dwSendRingBuffSize);

	m_pRecvOverlapped = new OverlappedEX;
	m_pRecvOverlapped->dwIndex = dwConnectionIndex;

	m_pSendOverlapped = new OverlappedEX;
	m_pSendOverlapped->dwIndex = dwConnectionIndex;

	m_dwSendWait = 0;

	return true;
}

int CConnection::SetAcceptContextOpt()
{
	int iRet = setsockopt(m_socket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char*)&(m_pNetwork->m_ListenSocket), sizeof(SOCKET));
	
	if(iRet == SOCKET_ERROR)
	{
		printf("AcceptEX SocketOption Fail WSAGetLastError = %d\n", WSAGetLastError());
	};

	return iRet;
}

int CConnection::SetConnectContextOpt()
{
	int iRet = setsockopt(m_socket, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0);

	if (iRet == SOCKET_ERROR)
	{
		printf("ConnectEX SocketOption Fail WSAGetLastError = %d\n", WSAGetLastError());
	};

	return iRet;
}

bool CConnection::CloseSocket()
{
	//진짜로 소켓을 초기화 하는 처리.
	//기본적으로 소켓을 재활용하기 때문에 비정상 적인 상황이나 종료 시 사용한다. 

	if (m_socket != INVALID_SOCKET)
	{
		closesocket(m_socket);
		m_socket = INVALID_SOCKET;
	}

	return true;
}

bool CConnection::PrepareRecv()
{
	DWORD dwFlags = 0;
	DWORD dwBytes = 0;

	m_pRecvOverlapped->wsabuff.len = m_pRecvBuff->GetReservedBytes();
	m_pRecvOverlapped->wsabuff.buf = m_pRecvBuff->GetReadPtr();
	m_pRecvOverlapped->eIoType = IOType::RECV;
	m_pRecvOverlapped->dwIndex = m_dwConnectionIndex;

	IncreaseRecvRef();

	if (WSARecv(m_socket,
		&m_pRecvOverlapped->wsabuff,
		1,
		&dwBytes,
		&dwFlags,
		(LPOVERLAPPED)&m_pRecvOverlapped->overlapped,
		NULL) == SOCKET_ERROR)
	{
		if (WSAGetLastError() != ERROR_IO_PENDING)
		{
			printf("WSARecv Fail. WSAGetLastError = %d \n", WSAGetLastError());
			DecreaseRecvRef();
			return false;
		}
	}

	return true;
}

void CConnection::RecvProcess(DWORD dwRecvBytes, char** ppMsg, DWORD* pdwMsgBytes, DWORD* pdwMsgNum)
{
	m_pRecvBuff->RecvProcess(dwRecvBytes, ppMsg, pdwMsgBytes, pdwMsgNum);
}

void CConnection::CheckReset()
{
	m_pRecvBuff->CheckReset();
}

bool CConnection::Send(char* pMsg, DWORD dwBytes)
{
	m_pSendBuff->Lock();
	
	bool ret = false;
	
	ret = PushSend(pMsg, dwBytes);

	if (ret == false) 
	{
		m_pSendBuff->UnLock();
		return false;
	}
	
	//send에 대한 통보가 오지 않아 send할 수 없는 상태면 보내지 않는다. 
	if (m_dwSendWait == TRUE)
	{
		m_pSendBuff->UnLock();
		return true;
	}

	ret = SendBuff();
	
	if (dwBytes != m_pSendBuff->GetUsageBytes())
	{
		printf("Send dwbytes = %d usageBytes = %d \n", dwBytes, m_pSendBuff->GetUsageBytes());
		__debugbreak();
	}

	if (ret == false) 
	{
		m_pSendBuff->UnLock();
		return false;
	}
	
	//send 했으면 통보가 오기 전 까지 커넥션이 send할 수 없도록 한다.
	InterlockedExchange(&m_dwSendWait, TRUE);

	m_pSendBuff->UnLock();

	return true;
}

bool CConnection::PushSend(char* pMsg, DWORD dwBytes)
{
	return m_pSendBuff->Push(pMsg, dwBytes);
}

bool CConnection::SendBuff()
{
	DWORD dwBytes = 0;
	memset(m_pSendOverlapped, 0, sizeof(OVERLAPPED));
	m_pSendOverlapped->wsabuff.len = m_pSendBuff->GetUsageBytes();
	m_pSendOverlapped->wsabuff.buf = m_pSendBuff->GetReadPtr();
	m_pSendOverlapped->eIoType = IOType::SEND;
	m_pSendOverlapped->dwIndex = m_dwConnectionIndex;

	IncreaseSendRef();

	if (WSASend(m_socket,
		&m_pSendOverlapped->wsabuff,
		1,
		&dwBytes,
		0,
		(LPOVERLAPPED)&m_pSendOverlapped->overlapped,
		NULL) == SOCKET_ERROR)
	{
		if (WSAGetLastError() != ERROR_IO_PENDING)
		{
			printf("WSASend Fail. WSAGetLastError = %d \n", WSAGetLastError());
			DecreaseSendRef();
			return false;
		}
	}

#ifdef __DEV_LOG__
	printf("SendBuff readptr = %p UsageBytes = %d  dwBytes = %d \n", m_pSendBuff->GetReadPtr(), m_pSendBuff->GetUsageBytes(), dwBytes);
#endif

	return true;
}

void CConnection::LockSend()
{
	m_pSendBuff->Lock();
}

void CConnection::UnLockSend()
{
	m_pSendBuff->UnLock();
}

CIocp* CConnection::GetNetwork()
{
	return m_pNetwork;
}

void CConnection::SetNetwork(CIocp* pNetwork)
{
	m_pNetwork = pNetwork;
}

SOCKET CConnection::GetSocket()
{
	return m_socket;
}

bool CConnection::GetConnectionStaus()
{
	return m_ConnectionStatus;
}

void CConnection::SetConnectionStatus(bool status)
{
	m_ConnectionStatus = status;
}

void CConnection::SetRemoteIP(char* szIP, DWORD dwLength)
{
	memcpy(m_szIP, szIP, dwLength);
}

bool CConnection::GetPeerName(char* pAddress, DWORD* pPort)
{
	SOCKADDR_IN addr;
	int addrlen = sizeof(SOCKADDR_IN);
	getpeername(m_socket, (sockaddr*)&addr, &addrlen);

	char* p = inet_ntoa(addr.sin_addr);

	memcpy(pAddress, p, strlen(p));
	*pPort = ntohs(addr.sin_port);

	return true;
}

char* CConnection::GetAddrBuff()
{
	return m_AddrBuf;
}

DWORD CConnection::GetRemotePort()
{
	return m_dwRemotePort;
}

void CConnection::SetRemotePort(DWORD dwPort)
{
	m_dwRemotePort = dwPort;
}

OverlappedEX* CConnection::GetRecvOverlapped()
{
	return m_pRecvOverlapped;
}

OverlappedEX* CConnection::GetSendOverlapped()
{
	return m_pSendOverlapped;
}

SRRecvRingBuffer* CConnection::GetRecvRingBuff()
{
	return m_pRecvBuff;
}

SRSendRingBuffer* CConnection::GetSendRingBuff()
{
	return m_pSendBuff;
}

DWORD CConnection::GetAcceptRefCount()
{
	return m_dwAcceptRefCount;
}

DWORD CConnection::GetConnectRefCount()
{
	return m_dwConnectRefCount;
}

DWORD CConnection::GetRecvRefCount()
{
	return m_dwRecvRefCount;
}

DWORD CConnection::GetSendRefCount()
{
	return m_dwSendRefCount;
}

void CConnection::IncreaseAcceptRef()
{
	InterlockedIncrement(&m_dwAcceptRefCount);
}

void CConnection::DecreaseAcceptRef()
{
	InterlockedDecrement(&m_dwAcceptRefCount);
}

void CConnection::IncreaseConnectRef()
{
	InterlockedIncrement(&m_dwConnectRefCount);
}

void CConnection::DecreaseConnectRef()
{
	InterlockedDecrement(&m_dwConnectRefCount);
}

void CConnection::IncreaseRecvRef()
{
	InterlockedIncrement(&m_dwRecvRefCount);
}

void CConnection::DecreaseRecvRef()
{
	InterlockedDecrement(&m_dwRecvRefCount);
}

void CConnection::IncreaseSendRef()
{
	InterlockedIncrement(&m_dwSendRefCount);
}

void CConnection::DecreaseSendRef()
{
	InterlockedDecrement(&m_dwSendRefCount);
}



