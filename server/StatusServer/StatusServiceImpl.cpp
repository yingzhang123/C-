#include "StatusServiceImpl.h"
#include "ConfigMgr.h"
#include "const.h"
#include "RedisMgr.h"
#include <climits>

// ����һ��ȫ��Ψһ��ʶ�� (UUID) ������ת��Ϊ�ַ�����
std::string generate_unique_string() {
    // ����UUID����ʹ���������������Ψһ��UUID
    boost::uuids::uuid uuid = boost::uuids::random_generator()();

    // ��UUID����ת��Ϊ�ַ�����ʾ��ʽ
    std::string unique_string = to_string(uuid);

    return unique_string; // �������ɵ�Ψһ�ַ���
}

// gRPC��������ȡ������С�������������Ϣ
Status StatusServiceImpl::GetChatServer(ServerContext* context, const GetChatServerReq* request, GetChatServerRsp* reply)
{
    std::string prefix("llfc status server has received :  ");
    
    // �ӷ������б��л�ȡ��ǰ������С�����������
    const auto& server = getChatServer();
    
    // ������Ӧ�е���������������Ͷ˿���Ϣ
    reply->set_host(server.host);
    reply->set_port(server.port);
    
    // ���óɹ�״̬��
    reply->set_error(ErrorCodes::Success);
    
    // ����Ψһ���û� token�����������õ���Ӧ��
    reply->set_token(generate_unique_string());
    
    // �����ɵ� token ���� Redis����¼���û��� token
    insertToken(request->uid(), reply->token());
    
    // ���سɹ�״̬
    return Status::OK;
}


// ���캯������ʼ�������������Ϣ
StatusServiceImpl::StatusServiceImpl()
{
    // ��ȡ�����ļ��е������������Ϣ
    auto& cfg = ConfigMgr::Inst();
    
    // �������л�ȡ����������������б�
    auto server_list = cfg["chatservers"]["Name"];

    std::vector<std::string> words;
    std::stringstream ss(server_list);
    std::string word;

    // ����������������ַ��������ŷָ�������words����
    while (std::getline(ss, word, ',')) {
        words.push_back(word);
    }

    // ����������������������ƣ����ط�������������Ϣ
    for (auto& word : words) {
        if (cfg[word]["Name"].empty()) {
            continue; // �������������Ϊ�գ��������÷�����
        }

        // ���� ChatServer �������������������˿ں�����
        ChatServer server;
        server.port = cfg[word]["Port"];
        server.host = cfg[word]["Host"];
        server.name = cfg[word]["Name"];
        
        // ��������������������ӳ���
        _servers[server.name] = server;
    }
}

// �ӷ������б��л�ȡ��ǰ������С�����������
ChatServer StatusServiceImpl::getChatServer() {
    // ���������������б�ķ��ʣ���ֹ��������
    std::lock_guard<std::mutex> guard(_server_mtx);

    // �����һ���������Ǹ�����С�ķ�����
    auto minServer = _servers.begin()->second;
    
    // �� Redis �л�ȡ�÷�������������
    auto count_str = RedisMgr::GetInstance()->HGet(LOGIN_COUNT, minServer.name);   //�����ֶ�
    
    if (count_str.empty()) {
        // ��� Redis ��û�и÷���������������Ϣ��Ĭ������Ϊ���ֵ
        minServer.con_count = INT_MAX;
    } else {
        // ���������ַ���ת��Ϊ����
        minServer.con_count = std::stoi(count_str);
    }

    // �����������б�Ѱ����������С�ķ�����
    for (auto& server : _servers) {
        if (server.second.name == minServer.name) {
            continue; // ������ǰ��С���ط�����
        }

        // �� Redis ��ȡ������������������
        auto count_str = RedisMgr::GetInstance()->HGet(LOGIN_COUNT, server.second.name);
        
        if (count_str.empty()) {
            // ��������������ڣ�����Ϊ���ֵ
            server.second.con_count = INT_MAX;
        } else {
            // ���������ַ���ת��Ϊ����
            server.second.con_count = std::stoi(count_str);
        }

        // �����ǰ��������������С�ڵ�ǰ��С����������������������С������
        if (server.second.con_count < minServer.con_count) {
            minServer = server.second;
        }
    }

    // ���ظ�����С�ķ�����
    return minServer;
}


// gRPC�����������û���¼�߼�
Status StatusServiceImpl::Login(ServerContext* context, const LoginReq* request, LoginRsp* reply)
{
    // ��ȡ�����е��û�ID��token
    auto uid = request->uid();
    auto token = request->token();

    // �����û� token �� Redis �еļ�
    std::string uid_str = std::to_string(uid);
    std::string token_key = USERTOKENPREFIX + uid_str;
    
    // �� Redis �л�ȡ���û��� token
    std::string token_value = "";
    bool success = RedisMgr::GetInstance()->Get(token_key, token_value);
    
    if (!success) {
        // ��� Redis �в����ڸ��û��� token���򷵻���Ч�û�ID����
        reply->set_error(ErrorCodes::UidInvalid);
        return Status::OK;
    }

    // ��� token ��ƥ�䣬������Ч token ����
    if (token_value != token) {
        reply->set_error(ErrorCodes::TokenInvalid);
        return Status::OK;
    }

    // ��� token ƥ�䣬���سɹ��������û�ID�� token ����
    reply->set_error(ErrorCodes::Success);
    reply->set_uid(uid);
    reply->set_token(token);
    return Status::OK;
}


// ���û��� token ���� Redis����¼��¼״̬
void StatusServiceImpl::insertToken(int uid, std::string token)
{
    // �����û� token �� Redis �еļ�
    std::string uid_str = std::to_string(uid);
    std::string token_key = USERTOKENPREFIX + uid_str;
    
    // �� token �洢�� Redis ��
    RedisMgr::GetInstance()->Set(token_key, token);
}

