#pragma once
#include <string>
#include <iostream>
#include <memory>
#include <grpcpp/grpcpp.h>
#include "message.grpc.pb.h"
#include "const.h"
#include "Singleton.h"
#include "ConfigMgr.h"
using grpc::Channel;
using grpc::Status;
using grpc::ClientContext;

using message::GetVarifyReq;
using message::GetVarifyRsp;
using message::VarifyService;

class RPConPool {
public:
    // ���캯������ʼ�����ӳأ�����ָ�������� gRPC ���Ӳ��������
    RPConPool(size_t poolSize, std::string host, std::string port)
        : poolSize_(poolSize), host_(host), port_(port), b_stop_(false) {
        
        // ���� poolSize_ �� gRPC ���Ӳ�����������Ӷ���
        for (size_t i = 0; i < poolSize_; ++i) {
            // ���� gRPC ���ӣ�ʹ�� insecure channel���Ǽ��ܣ�
            std::shared_ptr<Channel> channel = grpc::CreateChannel(host + ":" + port,
                grpc::InsecureChannelCredentials());
            
            // �� gRPC ����� Stub ������������
            connections_.push(VarifyService::NewStub(channel));
        }
    }

    // ����������ȷ���ڶ�������ʱ�ر����ӳ�
    ~RPConPool() {
        // ������ȷ���̰߳�ȫ
        std::lock_guard<std::mutex> lock(mutex_);
        Close();  // �ر����ӳ�
        // ������Ӷ���
        while (!connections_.empty()) {
            connections_.pop();
        }
    }

    // �����ӳ��л�ȡһ�� gRPC ����
    std::unique_ptr<VarifyService::Stub> getConnection() {
        std::unique_lock<std::mutex> lock(mutex_);
        // �ȴ����ӳ����п������ӣ�������ӳ��ѹرգ���ֱ�ӷ���
        cond_.wait(lock, [this] {
            if (b_stop_) {
                return true;
            }
            return !connections_.empty();  // ֻҪ�п������ӾͲ��ٵȴ�
        });
        // ������ӳ���ֹͣ�����ؿ�ָ��
        if (b_stop_) {
            return nullptr;
        }
        // ��ȡһ�� gRPC ����
        auto context = std::move(connections_.front());
        connections_.pop();
        return context;
    }

    // �� gRPC ���ӹ黹�����ӳ�
    void returnConnection(std::unique_ptr<VarifyService::Stub> context) {
        // ����ȷ���̰߳�ȫ
        std::lock_guard<std::mutex> lock(mutex_);
        if (b_stop_) {
            return;  // ������ӳ���ֹͣ�����ٹ黹����
        }
        // ���������·������
        connections_.push(std::move(context));
        cond_.notify_one();  // ֪ͨ�ȴ��е��߳����µ����ӿ���
    }

    // �ر����ӳأ�ֹͣ�������Ӳ���
    void Close() {
        b_stop_ = true;  // ������ӳ���ֹͣ
        cond_.notify_all();  // �������еȴ��е��߳�
    }

private:
    atomic<bool> b_stop_;   // ���ڱ�����ӳ��Ƿ���ֹͣ
    size_t poolSize_;       // ���ӳش�С
    std::string host_;      // gRPC ��������������ַ
    std::string port_;      // gRPC �������Ķ˿�
    std::queue<std::unique_ptr<VarifyService::Stub>> connections_;     // �洢 gRPC ���ӵĶ���
    std::mutex mutex_;                         // ��������ȷ���̰߳�ȫ
    std::condition_variable cond_;             // ���������������߳�ͬ��
};

class VerifyGrpcClient:public Singleton<VerifyGrpcClient>
{
	friend class Singleton<VerifyGrpcClient>;
public:
	~VerifyGrpcClient() {
		
	}
    GetVarifyRsp GetVarifyCode(std::string email) {
        ClientContext context;           // ���� gRPC �ͻ���������
        GetVarifyRsp reply;              // ���ڴ洢 gRPC ���õ���Ӧ
        GetVarifyReq request;            // ���ڴ洢 gRPC ���õ���������
        request.set_email(email);        // ���������е� email �ֶ�

        // �����ӳ��л�ȡһ�� gRPC ����
        auto stub = pool_->getConnection();
        if (!stub) { // ����Ƿ�ɹ���ȡ����
            GetVarifyRsp error_reply;
            error_reply.set_error(ErrorCodes::RPCFailed); // ���ô������
            return error_reply;
        }

        // ���� gRPC ����� GetVarifyCode ����                                  �˴�ʹ�õ���ͬ����д
        Status status = stub->GetVarifyCode(&context, request, &reply);

        if (status.ok()) {
            // ������óɹ��������ӷ��ص����ӳ�
            pool_->returnConnection(std::move(stub));
            return reply;  // ���ص��ý��
        }
        else {
            // �������ʧ�ܣ������ӷ��ص����ӳأ������ô�����
            pool_->returnConnection(std::move(stub));
            reply.set_error(ErrorCodes::RPCFailed); // ���ô������Ϊ RPC ʧ��
            return reply;
        }
    }



private:
	VerifyGrpcClient();

	std::unique_ptr<RPConPool> pool_;       // ָ�����ӳص�����ָ�룬���ڹ��� gRPC ����
};


