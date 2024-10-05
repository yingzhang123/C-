#include "HttpConnection.h"
#include "LogicSystem.h"
HttpConnection::HttpConnection(boost::asio::io_context& ioc)
	: _socket(ioc) {
}

// ��ʼ�����������ϵ����ݽ������󣬲������첽 HTTP �����ȡ
void HttpConnection::Start()
{
    // ��ȡ��ǰ����� shared_ptr���Ա�֤�������첽�����ڼ䲻�ᱻ����
    auto self = shared_from_this();

    // �첽��ȡ HTTP ����
    // _socket��������ͻ���ͨ�ŵ� socket
    // _buffer�������������ڴ洢��ȡ������
    // _request�����ڱ�������� HTTP ������Ϣ
    // �ص�����������ȡ������ɻ����ʱ����
    http::async_read(
        _socket,           // ����ͨ�ŵ� socket
        _buffer,           // �����������ڽ�������
        _request,          // �����ȡ���� HTTP ��������
        [self](beast::error_code ec, std::size_t bytes_transferred) {
            // �쳣����������ִ������������Ϣ������
            try {
                // ����ڶ�ȡ�����з������󣬴�ӡ������Ϣ������
                if (ec) {
                    std::cout << "http read err is " << ec.what() << std::endl;
                    return;  // ֱ�ӷ��أ����ٴ�������
                }

                // ����ɹ���ȡ�����ݣ�����ʵ�ʴ�����ֽ��������ܲ���Ҫ����
				// ����δʹ�õ� bytes_transferred ��������ȡ���ֽ���������һЩ����£��ֽ������ܲ���Ҫ�������ʹ�� boost::ignore_unused() ������������Ա�����������档
                boost::ignore_unused(bytes_transferred);

                // ������յ��� HTTP ���󣬽�����������Ӧ         ���� HandleReq() ����������յ��� HTTP ��������������������������Ӧ����Ӧ��Ϣ��
                self->HandleReq();

                // ����Ƿ��г�ʱ���ر�δ��ʱ��Ӧ������
                self->CheckDeadline();
            }
            catch (std::exception& exp) {
                // ����ڴ���������鳬ʱ�����з����쳣����������쳣��Ϣ
                std::cout << "exception is " << exp.what() << std::endl;
            }
        }
    );
}


//char תΪ16����
unsigned char ToHex(unsigned char x)
{
	return  x > 9 ? x + 55 : x + 48;
}

//16����תΪchar
unsigned char FromHex(unsigned char x)
{
	unsigned char y;
	if (x >= 'A' && x <= 'Z') y = x - 'A' + 10;
	else if (x >= 'a' && x <= 'z') y = x - 'a' + 10;
	else if (x >= '0' && x <= '9') y = x - '0';
	else assert(0);
	return y;
}

std::string UrlEncode(const std::string& str)
{
	std::string strTemp = "";
	size_t length = str.length();
	for (size_t i = 0; i < length; i++)
	{
		//�ж��Ƿ�������ֺ���ĸ����
		if (isalnum((unsigned char)str[i]) ||
			(str[i] == '-') ||
			(str[i] == '_') ||
			(str[i] == '.') ||
			(str[i] == '~'))
			strTemp += str[i];
		else if (str[i] == ' ') //Ϊ���ַ�
			strTemp += "+";
		else
		{
			//�����ַ���Ҫ��ǰ��%���Ҹ���λ�͵���λ�ֱ�תΪ16����
			strTemp += '%';
			strTemp += ToHex((unsigned char)str[i] >> 4);
			strTemp += ToHex((unsigned char)str[i] & 0x0F);
		}
	}
	return strTemp;
}

std::string UrlDecode(const std::string& str)
{
	std::string strTemp = "";
	size_t length = str.length();
	for (size_t i = 0; i < length; i++)
	{
		//��ԭ+Ϊ��
		if (str[i] == '+') strTemp += ' ';
		//����%������������ַ���16����תΪchar��ƴ��
		else if (str[i] == '%')
		{
			assert(i + 2 < length);
			unsigned char high = FromHex((unsigned char)str[++i]);
			unsigned char low = FromHex((unsigned char)str[++i]);
			strTemp += high * 16 + low;
		}
		else strTemp += str[i];
	}
	return strTemp;
}

void HttpConnection::PreParseGetParam() {
    // ��ȡ URI���������л�ȡĿ��·����������ѯ������
    auto uri = _request.target();

    // ���� '?' ��λ�ã���ʾ��ѯ�ַ����Ŀ�ʼ������Ҳ��� '?'��˵��û�в�ѯ����
    auto query_pos = uri.find('?');
    
    // ���û�в�ѯ�ַ�����ֱ�ӽ� URI ���� _get_url����ʾ�����·������
    if (query_pos == std::string::npos) {
        _get_url = uri;  // ���������� URI ��Ϊ����� URL
        return;  // û�в�ѯ����������
    }

    // ����� URL ��·�����֣��� '?' ֮ǰ�Ĳ��֣�
    _get_url = uri.substr(0, query_pos);

    // ��ȡ��ѯ�ַ������֣��� '?' ֮��Ĳ��֣�
    std::string query_string = uri.substr(query_pos + 1);
    std::string key;
    std::string value;
    size_t pos = 0;

    // ������ѯ�ַ��������� '&' �ָ������
    while ((pos = query_string.find('&')) != std::string::npos) {
        // ȡ��һ����ֵ�ԣ�ֱ����һ�� '&' ��λ�ã�
        auto pair = query_string.substr(0, pos);
        size_t eq_pos = pair.find('=');  // ���� '=' �ָ��������� key �� value
        
        // ����ҵ��� '='������ֵ�Էֱ𱣴�
        if (eq_pos != std::string::npos) {
            // ������ UrlDecode �������� URL �еı���ת��
            key = UrlDecode(pair.substr(0, eq_pos));  // ���� key
            value = UrlDecode(pair.substr(eq_pos + 1));  // ���� value
            _get_params[key] = value;  // ����ֵ�Դ��� _get_params
        }

        // �Ƴ��Ѵ���ļ�ֵ�ԣ���������ʣ�µĲ�ѯ�ַ���
        query_string.erase(0, pos + 1);
    }

    // �����ѯ�ַ��������һ����ֵ�ԣ����ʣ�µĲ���û�� '&' �ָ�����
    if (!query_string.empty()) {
        size_t eq_pos = query_string.find('=');  // ���� '=' �ָ���
        if (eq_pos != std::string::npos) {
            key = UrlDecode(query_string.substr(0, eq_pos));  // ���� key
            value = UrlDecode(query_string.substr(eq_pos + 1));  // ���� value
            _get_params[key] = value;  // ����ֵ�Դ��� _get_params
        }
    }
}


// ���� HTTP ����
void HttpConnection::HandleReq() {
    // ���� HTTP ��Ӧ�İ汾�ţ�������� HTTP �汾����һ��
    _response.version(_request.version());
    
    // ����Ϊ�����ӣ�������������󲻱������ӣ�HTTP/1.0 Ĭ�϶����ӣ�
    _response.keep_alive(false);
    
    // ���� GET ����
    if (_request.method() == http::verb::get) {
        // ���� GET ����� URL ����
        PreParseGetParam();
        
        // ���� GET �����߼���ͨ�� LogicSystem ִ����Ӧ�Ĵ���
        // LogicSystem::HandleGet ������������� URL �������ҵ���߼�
        bool success = LogicSystem::GetInstance()->HandleGet(_get_url, shared_from_this());

        // �������ʧ�ܣ����� 404 Not Found ����
        if (!success) {
            _response.result(http::status::not_found);  // ������Ӧ״̬Ϊ 404
            _response.set(http::field::content_type, "text/plain");  // ������Ӧ��������Ϊ���ı�
            beast::ostream(_response.body()) << "url not found\r\n";  // ��Ӧ����Ϊ "url not found"
            WriteResponse();  // ������Ӧ
            return;  // ��������
        }

        // �������ɹ������� 200 OK
        _response.result(http::status::ok);  // ������Ӧ״̬Ϊ 200 OK
        _response.set(http::field::server, "GateServer");  // ���÷�������ʶ
        WriteResponse();  // ������Ӧ
        return;  // ��������
    }

    // ���� POST ����
    if (_request.method() == http::verb::post) {
        // ���� POST �����߼���ͨ�� LogicSystem ִ����Ӧ�Ĵ���
        // LogicSystem::HandlePost ������������� URL �������ҵ���߼�
        bool success = LogicSystem::GetInstance()->HandlePost(_request.target(), shared_from_this());

        // �������ʧ�ܣ����� 404 Not Found ����
        if (!success) {
            _response.result(http::status::not_found);  // ������Ӧ״̬Ϊ 404
            _response.set(http::field::content_type, "text/plain");  // ������Ӧ��������Ϊ���ı�
            beast::ostream(_response.body()) << "url not found\r\n";  // ��Ӧ����Ϊ "url not found"
            WriteResponse();  // ������Ӧ
            return;  // ��������
        }

        // �������ɹ������� 200 OK
        _response.result(http::status::ok);  // ������Ӧ״̬Ϊ 200 OK
        _response.set(http::field::server, "GateServer");  // ���÷�������ʶ
        WriteResponse();  // ������Ӧ
        return;  // ��������
    }
}


void HttpConnection::CheckDeadline() {
    // ��ȡ��ǰ����� shared_ptr���Է�ֹ�������첽�����ڼ䱻����
    auto self = shared_from_this();

    // �첽�ȴ���ʱ�������¼�
    deadline_.async_wait(
        // �ص��������ڶ�ʱ������ʱִ��
        [self](beast::error_code ec) {
            // ��鶨ʱ���Ƿ񴥷�ʱû�д�����
            if (!ec) {
                // �����ʱ��������û��ȡ�����ر� socket��ȡ���κ�δ��ɵĲ���
                self->_socket.close(ec);
            }
        }
    );
}


void HttpConnection::WriteResponse() {
    // ��ȡ��ǰ����� shared_ptr��ȷ���������첽�����ڼ䲻�ᱻ����
    auto self = shared_from_this();

    // ���� HTTP ��Ӧ���ݵĳ��ȣ�����Ӧ��Ĵ�С���ֽ���������Ϊ Content-Length
    _response.content_length(_response.body().size());

    // �첽���� HTTP ��Ӧ
    http::async_write(
        _socket,  // ����ͨ�ŵ� socket
        _response,  // Ҫ���͵� HTTP ��Ӧ��Ϣ
        // �ص�������д������ɺ�ִ��
        [self](beast::error_code ec, std::size_t) {
            // �ر� socket ��д�ˣ���ʾ���ٷ������ݣ������Ͳ�����ɣ�
            self->_socket.shutdown(tcp::socket::shutdown_send, ec);
            
            // ȡ����ʱ������ֹ������鳬ʱ�������Ѵ�����ϣ�
            self->deadline_.cancel();
        });
}


