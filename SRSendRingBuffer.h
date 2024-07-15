#pragma once
#include <Windows.h>

//���� �����忡�� �뺸 �Ϸ� �ޱ� ���� Send �� �� ������ ���� �ʿ��ϴ�. 
//

class SRSendRingBuffer 
{
public:
	SRSendRingBuffer();
	~SRSendRingBuffer();

public:
	bool Initialize(DWORD dwSize);
	bool Push(char* pMsg, DWORD dwLength); //�����ۿ� ����
	bool PostSend(DWORD dwLength); //�Ϸ� �뺸 �� read ����

	char* GetReadPtr();
	DWORD GetReservedBytes();
	DWORD GetUsageBytes();

	void Lock();
	void UnLock();

private:
	char* m_pBuffer; //������ ���� ������
	char* m_pReadPos; //send: ó���� ������ ��ġ. ���� ���� �Ǵ� �Ϸ� �뺸 �Ŀ� ����.
	char* m_pWritePos; //send: ��û�� �� ���� �����ؾ��� ��ġ.

	DWORD m_dwBufferSize; //������ �� ������
	DWORD m_dwUsageBytes; //�����ۿ� ���� ������. �޾Ƽ� �����ۿ� ���̸� �ø���, ��Ŷ���� �ν��ؼ� �����Ϸ��ϸ� ���δ�.
	DWORD m_dwReserveBytes; //�����ۿ� ���� �뷮. 
	
	SRWLOCK m_sendLock;
};