#include "LogicSystem.h"
#include "HttpConnection.h"
#include "VerifyGrpcClient.h"
#include "RedisMgr.h"
#include "MysqlMgr.h"
#include "StatusGrpcClient.h"

LogicSystem::LogicSystem() {
	
	// ע�� GET ���� "/get_test" �Ĵ����߼�
	RegGet("/get_test", [](std::shared_ptr<HttpConnection> connection) {
		// ��ͻ��˷���һ���򵥵� GET ������Ӧ
		beast::ostream(connection->_response.body()) << "receive get_test req " << std::endl;
		int i = 0;
		// ������ѯ�������������Ӧ����
		for (auto& elem : connection->_get_params) {
			i++;
			beast::ostream(connection->_response.body()) << "param" << i << " key is " << elem.first;
			beast::ostream(connection->_response.body()) << ", " << " value is " << elem.second << std::endl;
		}
	});

	// ע�� POST ���� "/test_procedure" �Ĵ����߼�
	RegPost("/test_procedure", [](std::shared_ptr<HttpConnection> connection) {
		// ��ȡ POST �������ַ���
		auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());
		std::cout << "receive body is " << body_str << std::endl;
		// ������Ӧ��������Ϊ JSON
		connection->_response.set(http::field::content_type, "text/json");
		Json::Value root, src_root;
		Json::Reader reader;
		// �����������е� JSON ����
		bool parse_success = reader.parse(body_str, src_root);
		if (!parse_success) {
			std::cout << "Failed to parse JSON data!" << std::endl;
			// �������ʧ�ܣ����ش�����Ϣ
			root["error"] = ErrorCodes::Error_Json;
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}

		// ��� JSON ���Ƿ���� "email" �ֶ�
		if (!src_root.isMember("email")) {
			std::cout << "Failed to parse JSON data!" << std::endl;
			root["error"] = ErrorCodes::Error_Json;
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}

		// �����ݿ�ִ�й��̲�ѯ�����ؽ��
		auto email = src_root["email"].asString();
		int uid = 0;
		std::string name = "";
		MysqlMgr::GetInstance()->TestProcedure(email, uid, name);
		std::cout << "email is " << email << std::endl;

		// ������Ӧ JSON
		root["error"] = ErrorCodes::Success;
		root["email"] = src_root["email"];
		root["name"] = name;
		root["uid"] = uid;
		std::string jsonstr = root.toStyledString();
		beast::ostream(connection->_response.body()) << jsonstr;
		return true;
	});

	// ע�� POST ���� "/get_varifycode" �Ĵ����߼�
	RegPost("/get_varifycode", [](std::shared_ptr<HttpConnection> connection) {

		// ��ȡ�������е����ݲ�����ת��Ϊ�ַ�����ʽ
		auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());
		std::cout << "receive body is " << body_str << std::endl;

		// ���� HTTP ��Ӧ����������Ϊ JSON ��ʽ
		connection->_response.set(http::field::content_type, "text/json");

		// ���� JSON �������ڽ����������е� JSON ����
		Json::Value root, src_root;
		Json::Reader reader;

		// �����������е� JSON ����
		bool parse_success = reader.parse(body_str, src_root);
		if (!parse_success) {
			// �������ʧ�ܣ����ش�����Ϣ
			root["error"] = ErrorCodes::Error_Json;  // ������룺JSON ����ʧ��
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;  // ������Ӧ����������
		}

		// �ӽ������� JSON ��������ȡ�û��������ַ
		auto email = src_root["email"].asString();
		// ͨ�� gRPC �ͻ��˵��û�ȡ��֤��Ľӿ�
		GetVarifyRsp rsp = VerifyGrpcClient::GetInstance()->GetVarifyCode(email);

		// ��ӡ�����ַ�Թ�����
		std::cout << "email is " << email << std::endl;
		// �� gRPC ��Ӧ�еĴ�����洢�� JSON ��Ӧ��
		root["error"] = rsp.error();
		// �������е������ַҲ���� JSON ��Ӧ��
		root["email"] = src_root["email"];

		// ����Ӧ����� JSON ��ʽ��д�� HTTP ��Ӧ����
		std::string jsonstr = root.toStyledString();
		beast::ostream(connection->_response.body()) << jsonstr;

		return true;  // ������Ӧ����������
	});

	// ע���û�ע���߼� POST ���� "/user_register"
	RegPost("/user_register", [](std::shared_ptr<HttpConnection> connection) {
		// ��ȡ������ JSON ������
		auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());
		std::cout << "receive body is " << body_str << std::endl;
		connection->_response.set(http::field::content_type, "text/json");
		Json::Value root, src_root;
		Json::Reader reader;
		bool parse_success = reader.parse(body_str, src_root);
		if (!parse_success) {
			root["error"] = ErrorCodes::Error_Json;
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}

		// ��֤�����ȷ�������Ƿ�ƥ��
		auto pwd = src_root["passwd"].asString();
		auto confirm = src_root["confirm"].asString();
		if (pwd != confirm) {
			root["error"] = ErrorCodes::PasswdErr;
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}

		// ��� Redis �е���֤���Ƿ����
		std::string varify_code;
		bool b_get_varify = RedisMgr::GetInstance()->Get(CODEPREFIX + src_root["email"].asString(), varify_code);
		if (!b_get_varify || varify_code != src_root["varifycode"].asString()) {
			root["error"] = ErrorCodes::VarifyCodeErr;
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}

		// ���û���Ϣע�ᵽ MySQL ���ݿ���
		auto email = src_root["email"].asString();
		auto name = src_root["user"].asString();
		auto icon = src_root["icon"].asString();
		int uid = MysqlMgr::GetInstance()->RegUser(name, email, pwd, icon);
		if (uid == 0 || uid == -1) {
			root["error"] = ErrorCodes::UserExist;
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}

		// ���سɹ���ע����Ϣ
		root["error"] = 0;
		root["uid"] = uid;
		std::string jsonstr = root.toStyledString();
		beast::ostream(connection->_response.body()) << jsonstr;
		return true;
	});

	// ע�� POST ���� "/reset_pwd" �Ĵ����߼�
	RegPost("/reset_pwd", [](std::shared_ptr<HttpConnection> connection) {

		// �� HTTP ����������ȡ���ݲ�����ת��Ϊ�ַ���
		auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());
		std::cout << "receive body is " << body_str << std::endl;
		
		// ���� HTTP ��Ӧ����������Ϊ JSON ��ʽ
		connection->_response.set(http::field::content_type, "text/json");

		// ���� JSON �������ڽ����������е� JSON ����
		Json::Value root;
		Json::Reader reader;
		Json::Value src_root;

		// ���Խ����������е� JSON ����
		bool parse_success = reader.parse(body_str, src_root);
		if (!parse_success) {
			// �������ʧ�ܣ����� JSON ��ʽ�Ĵ�����Ϣ
			std::cout << "Failed to parse JSON data!" << std::endl;
			root["error"] = ErrorCodes::Error_Json;  // ������룺JSON ����ʧ��
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;  // ������Ӧ����������
		}

		// �ӽ������� JSON ��������ȡ email���û�����������
		auto email = src_root["email"].asString();
		auto name = src_root["user"].asString();
		auto pwd = src_root["passwd"].asString();

		// �� Redis �в����� email ��������֤��
		std::string varify_code;
		bool b_get_varify = RedisMgr::GetInstance()->Get(CODEPREFIX + src_root["email"].asString(), varify_code);
		if (!b_get_varify) {
			// �����֤���ѹ��ڻ򲻴��ڣ�������֤����ڵĴ�����Ϣ
			std::cout << " get varify code expired" << std::endl;
			root["error"] = ErrorCodes::VarifyExpired;  // ������룺��֤�����
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;  // ������Ӧ����������
		}

		// ����û��ṩ����֤���Ƿ���ȷ
		if (varify_code != src_root["varifycode"].asString()) {
			// �����֤�벻ƥ�䣬������֤�����Ĵ�����Ϣ
			std::cout << " varify code error" << std::endl;
			root["error"] = ErrorCodes::VarifyCodeErr;  // ������룺��֤�����
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;  // ������Ӧ����������
		}

		// ��֤�û����������Ƿ�ƥ��
		bool email_valid = MysqlMgr::GetInstance()->CheckEmail(name, email);
		if (!email_valid) {
			// ����û��������䲻ƥ�䣬���ش�����Ϣ
			std::cout << " user email not match" << std::endl;
			root["error"] = ErrorCodes::EmailNotMatch;  // ������룺�������û�����ƥ��
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;  // ������Ӧ����������
		}

		// �����û�����
		bool b_up = MysqlMgr::GetInstance()->UpdatePwd(name, pwd);
		if (!b_up) {
			// �����������ʧ�ܣ����ش�����Ϣ
			std::cout << " update pwd failed" << std::endl;
			root["error"] = ErrorCodes::PasswdUpFailed;  // ������룺�������ʧ��
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;  // ������Ӧ����������
		}

		// �ɹ��������룬���سɹ�����Ӧ��Ϣ
		std::cout << "succeed to update password " << pwd << std::endl;
		root["error"] = 0;  // ������� 0 ��ʾ�ɹ�
		root["email"] = email;  // �����û�������
		root["user"] = name;  // �����û���
		root["passwd"] = pwd;  // ���ظ��º������
		root["varifycode"] = src_root["varifycode"].asString();  // ������֤��
		std::string jsonstr = root.toStyledString();

		// ������õ� JSON ��Ӧ����д�� HTTP ��Ӧ����
		beast::ostream(connection->_response.body()) << jsonstr;

		return true;  // ������Ӧ����������
	});

	// ע�� POST ���� "/user_login" �Ĵ����߼�
	RegPost("/user_login", [](std::shared_ptr<HttpConnection> connection) {
		
		// �� HTTP ����������ȡ���ݲ�����ת��Ϊ�ַ�����ʽ
		auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());
		std::cout << "receive body is " << body_str << std::endl;
		
		// ���� HTTP ��Ӧ���ݵ�����Ϊ JSON ��ʽ
		connection->_response.set(http::field::content_type, "text/json");

		// ���� JSON �������ڽ����������е� JSON ����
		Json::Value root;
		Json::Reader reader;
		Json::Value src_root;

		// �����������е� JSON ���ݣ����洢�� src_root ������
		bool parse_success = reader.parse(body_str, src_root);
		if (!parse_success) {
			// �������ʧ�ܣ����� JSON ��ʽ�Ĵ�����Ϣ�������ô������
			std::cout << "Failed to parse JSON data!" << std::endl;
			root["error"] = ErrorCodes::Error_Json; // ����������Ϊ JSON ����ʧ��
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;  // ��������������
		}

		// �ӽ������� JSON ��������ȡ email ������
		auto email = src_root["email"].asString();
		auto pwd = src_root["passwd"].asString();
		
		// ���� UserInfo �������ڴ洢�����ݿ��ȡ���û���Ϣ
		UserInfo userInfo;

		// ��ѯ���ݿ⣬�ж��û�����������Ƿ�ƥ��
		bool pwd_valid = MysqlMgr::GetInstance()->CheckPwd(email, pwd, userInfo);
		if (!pwd_valid) {
			// ����û��������벻ƥ�䣬���ش�����Ϣ�����ô������
			std::cout << " user pwd not match" << std::endl;
			root["error"] = ErrorCodes::PasswdInvalid; // ����������Ϊ������Ч
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;  // ��������������
		}

		// ͨ�� gRPC ���ã���ѯ StatusServer �����Ի�ȡ���ʵ����������������Ϣ
		auto reply = StatusGrpcClient::GetInstance()->GetChatServer(userInfo.uid);
		if (reply.error()) {
			// ��� gRPC ����ʧ�ܣ����ش�����Ϣ�����ô������
			std::cout << " grpc get chat server failed, error is " << reply.error() << std::endl;
			root["error"] = ErrorCodes::RPCFailed; // ����������Ϊ gRPC ����ʧ��
			std::string jsonstr = root.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;  // ��������������
		}

		// �ɹ���ѯ���û���Ϣ�ͷ�����������Ϣ
		std::cout << "succeed to load userinfo uid is " << userInfo.uid << std::endl;

		// ���� JSON ��ʽ����Ӧ����
		root["error"] = 0;  // ������� 0 ��ʾ�ɹ�
		root["email"] = email;  // �����û�����
		root["uid"] = userInfo.uid;  // �����û� ID
		root["token"] = reply.token();  // �����û��������֤����
		root["host"] = reply.host();  // ���������������������ַ
		root["port"] = reply.port();  // ��������������Ķ˿ں�
		
		// ������õ� JSON ��ʽ�ַ���д�� HTTP ��Ӧ����
		std::string jsonstr = root.toStyledString();
		beast::ostream(connection->_response.body()) << jsonstr;

		return true;  // ��������������
	});

}

void LogicSystem::RegGet(std::string url, HttpHandler handler) {
	_get_handlers.insert(make_pair(url, handler));
}

void LogicSystem::RegPost(std::string url, HttpHandler handler) {
	_post_handlers.insert(make_pair(url, handler));
}

LogicSystem::~LogicSystem() {

}

bool LogicSystem::HandleGet(std::string path, std::shared_ptr<HttpConnection> con) {
	if (_get_handlers.find(path) == _get_handlers.end()) {
		return false;
	}

	_get_handlers[path](con);
	return true;
}

bool LogicSystem::HandlePost(std::string path, std::shared_ptr<HttpConnection> con) {
	if (_post_handlers.find(path) == _post_handlers.end()) {
		return false;
	}

	_post_handlers[path](con);
	return true;
}