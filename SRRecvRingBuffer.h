#pragma once
#include <Windows.h>

//recv의 경우. WSARecv 걸어놓으면 그걸 gqcs에서 완료통보를 받을 수 있는 스레드는 하나 뿐이다.
//완료 통보를 받아서 다시 WSARecv를 걸어놓는다.
//별도로 락이 필요 없다.

//1. 리시브 예약할 때 모자라면 순환해서 버퍼의 시작으로 남은 데이터 옮기고 다시 예약. 
//2. 모자라면 남은 공간만큼만 예약 걸고 특정 사이즈 보다 조금만 남으면 순환해서 다시 예약. 
//2번 방법으로 순환하기로 선택. 

#define RESET_RESERVE_SIZE 100

class SRRecvRingBuffer 
{
public:
	SRRecvRingBuffer();
	~SRRecvRingBuffer();

	int Initialize(DWORD dwSize);
	void RecvProcess(DWORD dwRecvBytes, char** ppMsg, DWORD* pdwMsgBytes, DWORD* pdwMsgNum);
	void CheckReset();

	char* GetReadPtr();
	DWORD GetReservedBytes();

private:
	char* m_pBuffer; //버퍼의 시작 포인터
	char* m_pReadPos; //완료 통보 후 패킷으로 완성이 안되고 남아있는 데이터의 시작 위치
	char* m_pWritePos; //리시브 예약 걸 위치
	DWORD m_dwReservedBytes; //링버퍼의 가용 용량
	DWORD m_dwUsageBytes; //링버퍼에 남은 데이터. 받아서 링버퍼에 쌓이면 올리고, 패킷으로 인식해서 조립완료하면 줄인다.
	DWORD m_dwBufferTotalSize; //링버퍼 총 사이즈
};