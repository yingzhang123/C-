#pragma once
#include "const.h"
#include "Singleton.h"
#include "ConfigMgr.h"
#include <grpcpp/grpcpp.h> 
#include "message.grpc.pb.h"
#include "message.pb.h"

using grpc::Channel;
using grpc::Status;
using grpc::ClientContext;

using message::GetChatServerReq;
using message::GetChatServerRsp;
using message::LoginRsp;
using message::LoginReq;
using message::StatusService;

// StatusConPool �ࣺgRPC ���ӳأ����ڹ����� StatusService::Stub ����
class StatusConPool {
public:
    /**
     * @brief ���캯������ʼ�����ӳأ�����ָ�������� gRPC ���Ӳ��������
     * 
     * @param poolSize ���ӳصĴ�С
     * @param host gRPC ��������������ַ
     * @param port gRPC �������Ķ˿�
     */
    StatusConPool(size_t poolSize, std::string host, std::string port)
        : poolSize_(poolSize), host_(host), port_(port), b_stop_(false) {
        
        // ���� poolSize_ �� gRPC ���Ӳ�����������Ӷ���
        for (size_t i = 0; i < poolSize_; ++i) {
            // ���� gRPC ���ӣ�ʹ�ò���ȫ��ͨ��ƾ֤��InsecureChannelCredentials��
            std::shared_ptr<Channel> channel = grpc::CreateChannel(host + ":" + port,
                grpc::InsecureChannelCredentials());

            // �� gRPC ����� Stub ����������Ӷ���
            connections_.push(StatusService::NewStub(channel));
        }
    }

    /**
     * @brief ����������ȷ���ڶ�������ʱ�ر����ӳ�
     */
    ~StatusConPool() {
        // ������ȷ���̰߳�ȫ
        std::lock_guard<std::mutex> lock(mutex_);
        Close();  // �ر����ӳ�
        // ������Ӷ���
        while (!connections_.empty()) {
            connections_.pop();
        }
    }

    /**
     * @brief �����ӳ��л�ȡһ�� gRPC ����
     * 
     * @return std::unique_ptr<StatusService::Stub> ��ȡ���� gRPC Stub ����
     */
    std::unique_ptr<StatusService::Stub> getConnection() {
        std::unique_lock<std::mutex> lock(mutex_);
        // �ȴ����ӳ����п������ӣ������ӳ���ֹͣ
        cond_.wait(lock, [this] {
            if (b_stop_) {
                return true;
            }
            return !connections_.empty();  // ֻҪ�п������ӾͲ��ٵȴ�
        });
        // ������ӳ���ֹͣ��ֱ�ӷ��ؿ�ָ��
        if (b_stop_) {
            return nullptr;
        }
        // ��ȡһ�� gRPC ���Ӳ��Ӷ������Ƴ�
        auto context = std::move(connections_.front());
        connections_.pop();
        return context;
    }

    /**
     * @brief �� gRPC ���ӹ黹�����ӳ�
     * 
     * @param context Ҫ�黹�� gRPC Stub ����
     */
    void returnConnection(std::unique_ptr<StatusService::Stub> context) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (b_stop_) {
            return;  // ������ӳ���ֹͣ�����ٹ黹����
        }
        // ���������·������
        connections_.push(std::move(context));
        cond_.notify_one();  // ֪ͨ�ȴ��е��߳����µ����ӿ���
    }

    /**
     * @brief �ر����ӳأ�ֹͣ�������Ӳ���
     */
    void Close() {
        b_stop_ = true;           // ������ӳ���ֹͣ
        cond_.notify_all();       // �������еȴ��е��߳�
    }

private:
    std::atomic<bool> b_stop_;   // ������ӳ��Ƿ���ֹͣ
    size_t poolSize_;            // ���ӳش�С
    std::string host_;           // gRPC ��������������ַ
    std::string port_;           // gRPC �������Ķ˿�
    std::queue<std::unique_ptr<StatusService::Stub>> connections_; // �洢 gRPC ���ӵĶ���
    std::mutex mutex_;           // ��������ȷ���̰߳�ȫ
    std::condition_variable cond_; // ���������������߳�ͬ��
};


class StatusGrpcClient :public Singleton<StatusGrpcClient>
{
	friend class Singleton<StatusGrpcClient>;
public:
	~StatusGrpcClient() {

	}
	GetChatServerRsp GetChatServer(int uid);
	LoginRsp Login(int uid, std::string token);
private:
	StatusGrpcClient();
	std::unique_ptr<StatusConPool> pool_;
	
};



