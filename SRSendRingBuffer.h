#pragma once
#include <Windows.h>

//���� �����忡�� Send �� �� ������ ���� �ʿ��ϴ�. 
//�������� ���� ����Ʈ ��ġ���� ��Ű���� �� �ܿ��� send �뺸 �Ϸ� ������ �ٽ� send�� �� ������ �ؾ� �Ѵ�.
//�Ϸ� ������ �����ۿ� push�� �س��� �뺸 �Ǹ� ���� �͵��� ������. 

class SRSendRingBuffer 
{
public:
	SRSendRingBuffer();
	~SRSendRingBuffer();

public:
	bool Initialize(DWORD dwSize);
	void Recycle();
	bool Push(char* pMsg, DWORD dwLength); //�����ۿ� ����
	void PostSend(DWORD dwLength); //�Ϸ� �뺸 �� read ����

	char* GetReadPtr();
	DWORD GetReservedBytes();
	DWORD GetUsageBytes();

	void Lock();
	void UnLock();

private:
	char* m_pBuffer; //������ ���� ������
	char* m_pReadPos; //send: ó���� ������ ��ġ. ���� ���� �Ǵ� �Ϸ� �뺸 �Ŀ� ����.
	char* m_pWritePos; //send: ��û�� �� ���� �����ؾ��� ��ġ.

	DWORD m_dwTotalBytes; //������ �� ������
	DWORD m_dwUsageBytes; //�����ۿ� ���� ������. �޾Ƽ� �����ۿ� ���̸� �ø���, ��Ŷ���� �ν��ؼ� �����Ϸ��ϸ� ���δ�.
	DWORD m_dwReserveBytes; //�����ۿ� ���� �뷮. 
	
	SRWLOCK m_sendLock;
};