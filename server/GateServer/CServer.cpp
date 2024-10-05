#include "CServer.h"
#include <iostream>
#include "HttpConnection.h"
#include "AsioIOServicePool.h"

// ���캯������ʼ�� CServer ���󣬴�����������_acceptor��������ָ���˿��ϵ���������
CServer::CServer(boost::asio::io_context& ioc, unsigned short& port) 
    : _ioc(ioc),  // ��ʼ�� io_context������ I/O ����
      _acceptor(ioc, tcp::endpoint(tcp::v4(), port))  // ��ʼ�� acceptor������ TCP v4 ����
{
}

// ��������������ʼ�첽��������
void CServer::Start() {
    // ���� this ָ��Ĺ���ָ�룬ȷ������� self ���첽��������Ȼ��Ч   ȷ�����첽���������У�CServer ���󲻻ᱻ���١�
    auto self = shared_from_this();

    // �� IO ������л�ȡһ�� io_context ʵ�������������ӵĴ���
    auto& io_context = AsioIOServicePool::GetInstance()->GetIOService();
    
    // ����һ�� HttpConnection �������ڴ���������           ����һ���µ� HttpConnection ����������ÿ�������ӡ�
    std::shared_ptr<HttpConnection> new_con = std::make_shared<HttpConnection>(io_context);

    // �첽���������ӣ��ȴ��ͻ��˵���������                �첽�صȴ������ӣ����ܺ�ִ�лص�����
    _acceptor.async_accept(new_con->GetSocket(),                                           //  ָ���������ӵ�Ŀ�� socket���������Ӻ�Ὣ�ͻ������ӵ� socket ���� HttpConnection �� socket��
        [self, new_con](beast::error_code ec) {                                            // ������������Ļص�����
            try {
                // �����������ʱ�����������¿�ʼ�����µ�����
                if (ec) {
                    self->Start();
                    return;
                }

                // ����������ӳɹ�����ʼ����������
                new_con->Start();
                
                // ���������µ��������󣬱��ַ�������������
                self->Start();
            }
            catch (std::exception& exp) {
                // ���񲢴����쳣����ӡ������Ϣ������������������
                std::cout << "exception is " << exp.what() << std::endl;
                self->Start();
            }
    });
}


