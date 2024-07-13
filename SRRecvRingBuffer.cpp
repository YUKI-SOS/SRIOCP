#include "SRRecvRingBuffer.h"
#include <stdio.h>

SRRecvRingBuffer::SRRecvRingBuffer()
{
	m_pBuffer = nullptr;
	m_pReadPos = nullptr;
	m_pWritePos = nullptr;
	m_dwUsageBytes = 0;
	m_dwReservedBytes = 0;
	m_dwBufferTotalSize = 0;
}

SRRecvRingBuffer::~SRRecvRingBuffer()
{

}

int SRRecvRingBuffer::Initialize(DWORD dwSize)
{
	m_dwBufferTotalSize = dwSize;

	m_pBuffer = new char[m_dwBufferTotalSize];
	if (m_pBuffer == nullptr)
	{
		printf("%s %d Alloc Fail\n", __FUNCTION__, __LINE__);
		return false;
	}
	memset(m_pBuffer, 0, dwSize);

	m_pReadPos = m_pBuffer;
	m_pWritePos = m_pBuffer;

	m_dwUsageBytes = 0;
	m_dwReservedBytes = dwSize;

	return 0;
}

void SRRecvRingBuffer::RecvProcess(DWORD dwRecvBytes, char** ppMsg, DWORD* pdwMsgBytes, DWORD* pdwMsgNum)
{
	//링버퍼에서 받은 데이터로 패킷을 만들어내는 함수.
	DWORD dwPaketLength = 0;
	*pdwMsgBytes = 0;
	*pdwMsgNum = 0;

	m_dwReservedBytes -= dwRecvBytes;
	m_dwUsageBytes += dwRecvBytes;

	//부족하면 부족한 만큼만 걸고 받은 다음 리셋해서 다시 건다.
	//넘치는지 체크는 의미 없음. 받을 수 있는 만큼만 걸어두니까.
	
	//패킷 시작 위치 넘겨주고 리시브 라이트 큐(백버퍼)에 카피할 때 이용.
	(*ppMsg) = m_pReadPos;

	while (true)
	{
		//최소한의 패킷도 될 수 없는 데이터만 남으면 탈출. 
		if (m_dwUsageBytes < 4)
			return;

		//사이즈를 얻는다.
		dwPaketLength = *(DWORD*)m_pReadPos;

		//패킷이 안되고 남아있는 용량이 패킷 길이보다 작으면 패킷 못만드는 상태임으로 탈출
		if (m_dwUsageBytes < dwPaketLength)
			return;

		//읽은 패킷 사이즈 만큼 리드 증가
		m_pReadPos += dwPaketLength;
		(*pdwMsgBytes) += dwPaketLength;
		(*pdwMsgNum)++;

		//완성된 패킷 만큼 남은 데이터 감소
		m_dwUsageBytes -= dwPaketLength;
	}

}

void SRRecvRingBuffer::CheckReset()
{
	//가용 용량이 리셋하기로 정해둔 사이즈보다 작으면 맨 앞으로 땡긴다.
	if (m_dwReservedBytes < RESET_RESERVE_SIZE) 
	{
		m_dwReservedBytes = m_dwBufferTotalSize - m_dwUsageBytes; //버퍼의 가용 용량 재계산.
		memcpy(m_pBuffer, m_pReadPos, m_dwUsageBytes); //처리 못하고 남아있는 데이터 버퍼의 시작으로 카피
		m_pReadPos = m_pBuffer; //리드를 버퍼의 시작으로 설정
		m_pWritePos = m_pBuffer + m_dwUsageBytes; //라이트를 남은 데이터를 버퍼의 시작으로 카피한 이후로 설정
	}
}

char* SRRecvRingBuffer::GetReadPtr()
{
	return m_pReadPos;
}

DWORD SRRecvRingBuffer::GetReservedBytes()
{
	return m_dwReservedBytes;
}
