#pragma once
#include <Windows.h>

//여러 스레드에서 Send 할 수 있으니 락이 필요하다. 
//링버퍼의 리드 라이트 위치등을 지키려면 락 외에도 send 통보 완료 전까지 다시 send할 수 없도록 해야 한다.
//완료 전에는 링버퍼에 push만 해놓고 통보 되면 쌓인 것들을 보낸다. 

class SRSendRingBuffer 
{
public:
	SRSendRingBuffer();
	~SRSendRingBuffer();

public:
	bool Initialize(DWORD dwSize);
	void Recycle();
	bool Push(char* pMsg, DWORD dwLength); //링버퍼에 쓰기
	void PostSend(DWORD dwLength); //완료 통보 후 read 증가

	char* GetReadPtr();
	DWORD GetReservedBytes();
	DWORD GetUsageBytes();

	void Lock();
	void UnLock();

private:
	char* m_pBuffer; //버퍼의 시작 포인터
	char* m_pReadPos; //send: 처리를 시작할 위치. 보낸 이후 또는 완료 통보 후에 증가.
	char* m_pWritePos; //send: 요청할 때 쓰기 시작해야할 위치.

	DWORD m_dwTotalBytes; //링버퍼 총 사이즈
	DWORD m_dwUsageBytes; //링버퍼에 남은 데이터. 받아서 링버퍼에 쌓이면 올리고, 패킷으로 인식해서 조립완료하면 줄인다.
	DWORD m_dwReserveBytes; //링버퍼에 남은 용량. 
	
	SRWLOCK m_sendLock;
};