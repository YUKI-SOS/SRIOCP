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
	delete m_pSendOverlapped;
	m_pRecvOverlapped = nullptr;
	m_pSendOverlapped = nullptr;

	delete m_pRecvBuff;
	delete m_pSendBuff;
	m_pRecvBuff = nullptr;
	m_pSendBuff = nullptr;

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

	return true;
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

bool CConnection::PostRecv()
{
	DWORD dwFlags = 0;
	DWORD dwBytes = 0;

	m_pRecvOverlapped->wsabuff.len = m_pRecvBuff->GetReservedBytes();
	m_pRecvOverlapped->wsabuff.buf = m_pRecvBuff->GetReadPtr();
	m_pRecvOverlapped->eIoType = IOType::RECV;
	m_pRecvOverlapped->dwIndex = m_dwConnectionIndex;

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
	return true;
}

bool CConnection::PushSend(char* pMsg, DWORD dwBytes)
{

	return true;
}

bool CConnection::SendBuff()
{
	m_pSendBuff->Lock();
	
	DWORD dwBytes = 0;
	ZeroMemory(&m_pSendOverlapped, sizeof(OVERLAPPED));
	m_pSendOverlapped->wsabuff.len = m_pSendBuff->GetReservedBytes();
	m_pSendOverlapped->wsabuff.buf = m_pSendBuff->GetReadPtr();
	m_pSendOverlapped->eIoType = IOType::SEND;
	m_pSendOverlapped->dwIndex = m_dwConnectionIndex;

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
			std::cout << "WSASend fail" << std::endl;
			return false;
		}
	}

	printf("sendbytes = %d \n", dwBytes);

	m_pSendBuff->UnLock();

	return true;
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
