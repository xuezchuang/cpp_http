// main.cpp
#include <drogon/drogon.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <iomanip>

// ================== 配置（与你的 Python 完全一致） ==================
static const std::string TOKEN = "tRyFzOuWv3";
static const std::string ENCODING_AES_KEY = "okLaSE5i2rWrCvUgtwK9OnsXHbvWNnhGPXKToLVFzRf"; // 43 chars, base64 key (no '=' at end)
static const std::string CORP_ID = "ww3d5a9af1b074dbb3";

static const std::string CORP_SECRET_MAIN = "bbJWt-3zKqClynjHG8vGuUaVl5zRI51RHqv-LXhyQeI";
static const int AGENT_ID_MAIN = 1000002;
static const std::string TO_USER_MAIN = "XueZhenChuang";

static const std::string RELAY_SECRET = "1qnQiwDZxsgJsYKDR4ybtGm0urLYxNBMdXlCMMuovA4";
static const int AGENT_ID_RELAY = 1000003;
static const std::string TO_USER_RELAY = "@all";

static const std::string LOG_BASE_DIR = "/var/log/netmon/speed";
// ===================================================================

using namespace drogon;

// ---- 小工具 ----
static inline std::string trim(const std::string& s)
{
	size_t b = s.find_first_not_of(" \t\r\n");
	if (b == std::string::npos) return "";
	size_t e = s.find_last_not_of(" \t\r\n");
	return s.substr(b, e - b + 1);
}

static inline std::string stripCData(const std::string& s)
{
	std::string t = trim(s);
	const std::string L = "<![CDATA[";
	const std::string R = "]]>";
	if (t.size() >= L.size() + R.size()
		&& t.rfind(L, 0) == 0
		&& t.substr(t.size() - R.size()) == R)
	{
		return t.substr(L.size(), t.size() - L.size() - R.size());
	}
	return t;
}

static inline std::string toLower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
	return s;
}

static std::string sha1Hex(const std::string& data)
{
	unsigned char md[SHA_DIGEST_LENGTH];
	SHA1(reinterpret_cast<const unsigned char*>(data.data()), data.size(), md);
	std::ostringstream oss;
	for (auto c : md) oss << std::hex << std::setw(2) << std::setfill('0') << (int)c;
	return oss.str();
}

static std::string calcSignature(const std::string& token,
	const std::string& timestamp,
	const std::string& nonce,
	const std::string& msg)
{
	std::vector<std::string> v{ token, timestamp, nonce, msg };
	std::sort(v.begin(), v.end());
	std::string joined = v[0] + v[1] + v[2] + v[3];
	return sha1Hex(joined);
}

static bool base64Decode(const std::string& b64, std::string& out)
{
	try
	{
		out = drogon::utils::base64Decode(b64);
		return true;
	}
	catch (...)
	{
		return false;
	}
}

// PKCS7 去填充
static bool pkcs7Unpad(std::string& buf)
{
	if (buf.empty()) return false;
	unsigned char p = static_cast<unsigned char>(buf.back());
	if (p == 0 || p > 32) return false;
	if (buf.size() < p) return false;
	// 校验每个尾字节
	for (size_t i = buf.size() - p; i < buf.size(); ++i)
	{
		if (static_cast<unsigned char>(buf[i]) != p) return false;
	}
	buf.resize(buf.size() - p);
	return true;
}

// AES-256-CBC 解密（iv = key前16字节）
static bool aes256cbcDecrypt(const std::string& key32, const std::string& cipherRaw, std::string& plain)
{
	if (key32.size() != 32) return false;
	unsigned char iv[16];
	memcpy(iv, key32.data(), 16);

	EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
	if (!ctx) return false;

	bool ok = false;
	do
	{
		if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr,
			reinterpret_cast<const unsigned char*>(key32.data()), iv) != 1) break;

		plain.resize(cipherRaw.size() + EVP_CIPHER_block_size(EVP_aes_256_cbc()));
		int outlen1 = 0;
		if (EVP_DecryptUpdate(ctx,
			reinterpret_cast<unsigned char*>(&plain[0]), &outlen1,
			reinterpret_cast<const unsigned char*>(cipherRaw.data()), (int)cipherRaw.size()) != 1) break;

		int outlen2 = 0;
		if (EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(&plain[0]) + outlen1, &outlen2) != 1) break;
		plain.resize(outlen1 + outlen2);
		ok = true;
	} while (false);

	EVP_CIPHER_CTX_free(ctx);
	return ok;
}

// 企业微信消息体解密：返回 msg（中间那段）
static bool wecomDecryptMsg(const std::string& encodingAesKey43,
	const std::string& corpId,
	const std::string& cipherB64,
	std::string& outMsg)
{
	// 1) 解 key
	std::string key;
	if (!base64Decode(encodingAesKey43 + "=", key)) return false;
	if (key.size() != 32) return false;

	// 2) 解密数据
	std::string cipherRaw;
	if (!base64Decode(cipherB64, cipherRaw)) return false;

	std::string plain;
	if (!aes256cbcDecrypt(key, cipherRaw, plain)) return false;
	if (!pkcs7Unpad(plain)) 
	{

	}

	// 格式: 16字节随机 + 4字节网络序长度 + msg + corpId
	if (plain.size() < 20) return false;
	const char* p = plain.data();
	uint32_t msgLenNet = 0;
	memcpy(&msgLenNet, p + 16, 4);
	uint32_t msgLen = ntohl(msgLenNet);

	if (plain.size() < 16 + 4 + msgLen) return false;
	outMsg.assign(p + 16 + 4, msgLen);

	std::string corp(plain.data() + 16 + 4 + msgLen, plain.size() - (16 + 4 + msgLen));
	if (corp != corpId)
	{
		// 企业ID不匹配
		return false;
	}
	return true;
}

// 极简 XML 取值（只取第一个标签，足够应付企业微信的简单格式）
static std::string getXmlTag(const std::string& xml, const std::string& tag)
{
	std::string lt = "<" + tag + ">";
	std::string lt2 = "<" + tag + "><![CDATA[";
	std::string rt = "</" + tag + ">";
	auto p = xml.find(lt2);
	if (p != std::string::npos)
	{
		p += lt2.size();
		auto q = xml.find("]]>", p);
		if (q != std::string::npos)
		{
			return xml.substr(p, q - p);
		}
	}
	p = xml.find(lt);
	if (p != std::string::npos)
	{
		p += lt.size();
		auto q = xml.find(rt, p);
		if (q != std::string::npos)
		{
			return xml.substr(p, q - p);
		}
	}
	return "";
}

// 速度格式化（单位：KB/s 输入）
static std::string formatSpeed(double kbps)
{
	std::ostringstream oss;
	if (kbps >= 1024.0)
	{
		oss << std::fixed << std::setprecision(2) << (kbps / 1024.0) << " MB/s";
	}
	else
	{
		oss << std::fixed << std::setprecision(2) << kbps << " KB/s";
	}
	return oss.str();
}

// 今日/近7日 报表
static std::string getTodayReport(bool down)
{
	auto now = trantor::Date::now();
	auto tm = now.after(0).tmStruct();
	//char ymd[32];
	//snprintf(ymd, sizeof(ymd), "%04d%02d%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);

	char ymd[9];
	strftime(ymd, sizeof(ymd), "%Y%m%d", &tm);

	std::string prefix = down ? "speed-down-" : "speed-up-";
	std::string path = LOG_BASE_DIR + "/" + prefix + ymd + ".csv";

	std::ifstream fin(path);
	if (!fin.good())
	{
		return down ? "📅 No download speed records today." : "📅 No upload speed records today.";
	}
	std::vector<std::string> lines;
	std::string line;
	while (std::getline(fin, line)) if (!trim(line).empty()) lines.push_back(line);
	if (lines.empty()) return "📅 No data today.";

	std::ostringstream oss;
	oss << (down ? "📥 今日下载速度记录：" : "📤 今日上传速度记录：") << "\n";
	oss << std::left << std::setw(20) << "Time" << std::right << std::setw(12) << "Speed" << "\n";
	oss << std::string(34, '-') << "\n";

	int start = std::max<int>(0, (int)lines.size() - 10);
	for (int i = start; i < (int)lines.size(); ++i)
	{
		auto& l = lines[i];
		auto pos = l.find(',');
		if (pos == std::string::npos) continue;
		std::string ts = l.substr(0, pos);
		std::string val = l.substr(pos + 1);
		// 取时分秒
		if (ts.size() >= 19) ts = ts.substr(11, 8);
		try
		{
			double v = std::stod(val);
			oss << std::left << std::setw(20) << ts
				<< std::right << std::setw(12) << formatSpeed(v) << "\n";
		}
		catch (...) {}
	}
	return oss.str();
}

static std::string get7DaysReport(bool down)
{
	std::ostringstream oss;
	bool any = false;
	for (int i = 0; i < 7; ++i)
	{
		auto day = trantor::Date::now().after(-i * 24 * 3600);
		auto tm = day.tmStruct();
		//char ymd[9], y[5], m[3], d[3];
		//char ymd[32], y[16], m[8], d[8];
		//snprintf(y, sizeof(y), "%04d", tm.tm_year + 1900);
		//snprintf(m, sizeof(m), "%02d", tm.tm_mon + 1);
		//snprintf(d, sizeof(d), "%02d", tm.tm_mday);
		//snprintf(ymd, sizeof(ymd), "%s%s%s", y, m, d);
		char ymd[9];
		strftime(ymd, sizeof(ymd), "%Y%m%d", &tm);

		std::string prefix = down ? "speed-down-" : "speed-up-";
		std::string path = LOG_BASE_DIR + "/" + prefix + ymd + ".csv";
		std::ifstream fin(path);
		if (!fin.good()) continue;

		any = true;

		std::ostringstream date_oss;
		date_oss << std::put_time(&tm, "%Y-%m-%d");

		oss << "\n📅 " << date_oss.str()
			<< (down ? " 下载记录：" : " 上传记录：") << "\n";

		//oss << "\n📅 " << y << "-" << m << "-" << d << (down ? " 下载记录：" : " 上传记录：") << "\n";

		oss << std::left << std::setw(20) << "Time" << std::right << std::setw(12) << "Speed" << "\n";
		oss << std::string(34, '-') << "\n";

		std::string line;
		while (std::getline(fin, line))
		{
			if (trim(line).empty()) continue;
			auto pos = line.find(',');
			if (pos == std::string::npos) continue;
			std::string ts = line.substr(0, pos);
			std::string val = line.substr(pos + 1);
			if (ts.size() >= 19) ts = ts.substr(11, 8);
			try
			{
				double v = std::stod(val);
				oss << std::left << std::setw(20) << ts
					<< std::right << std::setw(12) << formatSpeed(v) << "\n";
			}
			catch (...) {}
		}
	}
	if (!any)
	{
		return down ? "📆 No download speed data in the past 7 days." :
			"📆 No upload speed data in the past 7 days.";
	}
	return std::string(down ? "📆 近7日下载速度记录：" : "📆 近7日上传速度记录：") + "\n" + oss.str();
}

static bool parseJsonLoose(const std::string& s, Json::Value& out)
{
	Json::CharReaderBuilder b; b["collectComments"] = false;
	std::string errs;
	std::unique_ptr<Json::CharReader> r(b.newCharReader());
	return r->parse(s.data(), s.data() + s.size(), &out, &errs);
}

static bool parseJsonLoose(std::string_view sv, Json::Value& out)
{
	if (sv.empty()) return false;
	Json::CharReaderBuilder b; b["collectComments"] = false;
	std::string errs;
	std::unique_ptr<Json::CharReader> r(b.newCharReader());
	const char* begin = sv.data();
	const char* end = sv.data() + sv.size();
	return r->parse(begin, end, &out, &errs) && errs.empty();
}

// 发送企业微信文本消息（获取 token -> 发送），异步执行，不阻塞 HTTP 响应
static void sendWecomTextAsync(const std::string& corpSecret,
	int agentId,
	const std::string& toUser,
	const std::string& content)
{
	if (trim(content).empty())
	{
		LOG_WARN << "Message empty, skip sending";
		return;
	}

	auto cli = HttpClient::newHttpClient("https://qyapi.weixin.qq.com");
	// 1) gettoken
	auto tokenReq = HttpRequest::newHttpRequest();
	tokenReq->setMethod(Get);
	tokenReq->setPath("/cgi-bin/gettoken");
	tokenReq->setParameter("corpid", CORP_ID);
	tokenReq->setParameter("corpsecret", corpSecret);
	//tokenReq->setTimeout(5.0);
	
	cli->sendRequest(tokenReq, [cli, agentId, toUser, content](ReqResult r, const HttpResponsePtr& resp)
		{
			if (r != drogon::ReqResult::Ok || !resp) { /* 打日志并 return */ return; }
			if (resp->getStatusCode() != drogon::k200OK)
			{
				LOG_ERROR << "status=" << resp->getStatusCode() << " body=" << resp->getBody();
				return;
			}
			Json::Value j;
			if (!parseJsonLoose(resp->getBody(), j))
			{
				LOG_ERROR << "token parse json failed: " << resp->getBody();
				return;
			}

			auto access_token = j.get("access_token", "").asString();
			if (access_token.empty())
			{
				LOG_ERROR << "access_token missing: " << resp->body();
				return;
			}
			// 2) /message/send
			Json::Value payload(Json::objectValue);
			payload["touser"] = toUser;
			payload["msgtype"] = "text";
			payload["agentid"] = agentId;

			Json::Value text(Json::objectValue);
			text["content"] = content;
			payload["text"] = text;

			payload["safe"] = 0;

			auto sendReq = HttpRequest::newHttpJsonRequest(payload);
			sendReq->setMethod(Post);
			sendReq->setPath("/cgi-bin/message/send");
			sendReq->setParameter("access_token", access_token);

			cli->sendRequest(sendReq, [](ReqResult r2, const HttpResponsePtr& resp2)
				{
					if (r2 != ReqResult::Ok || !resp2)
					{
						LOG_ERROR << "send message failed";
						return;
					}
					Json::Value j2;
					try { j2 = *resp2->getJsonObject(); }
					catch (...) { LOG_ERROR << "send json parse error"; return; }
					int errcode = j2.get("errcode", -1).asInt();
					if (errcode == 0) LOG_INFO << "✅ Message sent";
					else LOG_ERROR << "❌ Send failed: " << resp2->body();
				},5);
		},5);
}

int main()
{
	// ========== 路由：/api/relay ==========
	app().registerHandler("/api/relay",
		[](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback)
		{
			Json::Value j;
			if (req->getJsonObject()) j = *req->getJsonObject();
			std::string msg = trim(j.get("msg", "").asString());
			std::string secret = trim(j.get("secret", "").asString());

			if (secret != RELAY_SECRET)
			{
				auto r = HttpResponse::newHttpJsonResponse(Json::Value{ {"error","Invalid secret"} });
				r->setStatusCode(k403Forbidden);
				return callback(r);
			}
			if (msg.empty())
			{
				auto r = HttpResponse::newHttpJsonResponse(Json::Value{ {"error","Empty message"} });
				r->setStatusCode(k400BadRequest);
				return callback(r);
			}

			// 异步发送
			sendWecomTextAsync(RELAY_SECRET, AGENT_ID_RELAY, TO_USER_RELAY, msg);
			auto r = HttpResponse::newHttpJsonResponse(Json::Value{ {"status","ok"} });
			callback(r);
		},
		{ Post }
	);

	// ========== 路由：企业微信回调 "/" ==========
	app().registerHandler("/",
		[](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback)
		{
			if (req->method() == Get)
			{
				// 企业微信 URL 验证
				std::string msg_signature = req->getParameter("msg_signature");
				std::string timestamp = req->getParameter("timestamp");
				std::string nonce = req->getParameter("nonce");
				std::string echostr = req->getParameter("echostr");

				// 验签
				std::string sigLocal = calcSignature(TOKEN, timestamp, nonce, echostr);
				if (sigLocal != msg_signature)
				{
					auto r = HttpResponse::newHttpResponse();
					r->setStatusCode(k400BadRequest);
					r->setContentTypeCode(CT_TEXT_PLAIN);
					r->setBody("Signature verification failed");
					return callback(r);
				}
				// 解密 echostr
				std::string plain;
				if (!wecomDecryptMsg(ENCODING_AES_KEY, CORP_ID, echostr, plain))
				{
					auto r = HttpResponse::newHttpResponse();
					r->setStatusCode(k400BadRequest);
					r->setContentTypeCode(CT_TEXT_PLAIN);
					r->setBody("Verification failed: decrypt error");
					return callback(r);
				}
				LOG_INFO << "[OK] WeCom verification passed: " << plain;
				auto r = HttpResponse::newHttpResponse();
				r->setStatusCode(k200OK);
				r->setContentTypeCode(CT_TEXT_PLAIN);
				r->setBody(plain); // 必须回原文
				return callback(r);
			}

			// POST：接收消息
			if (req->method() == Post)
			{
				std::string msg_signature = req->getParameter("msg_signature");
				std::string timestamp = req->getParameter("timestamp");
				std::string nonce = req->getParameter("nonce");
				std::string body(req->getBody());

				// 拿到 <Encrypt> 的内容
				std::string encrypt = getXmlTag(body, "Encrypt");
				if (encrypt.empty())
				{
					auto r = HttpResponse::newHttpResponse();
					r->setStatusCode(k400BadRequest);
					r->setContentTypeCode(CT_TEXT_PLAIN);
					r->setBody("Message error: missing Encrypt");
					return callback(r);
				}

				// 验签：token, timestamp, nonce, encrypt
				std::string sigLocal = calcSignature(TOKEN, timestamp, nonce, encrypt);
				if (sigLocal != msg_signature)
				{
					auto r = HttpResponse::newHttpResponse();
					r->setStatusCode(k400BadRequest);
					r->setBody("Message error: signature mismatch");
					return callback(r);
				}

				// 解密得到完整 XML
				std::string xml;
				if (!wecomDecryptMsg(ENCODING_AES_KEY, CORP_ID, encrypt, xml))
				{
					auto r = HttpResponse::newHttpResponse();
					r->setStatusCode(k400BadRequest);
					r->setBody("Message error: decrypt fail");
					return callback(r);
				}

				LOG_INFO << "✅ Received message: " << xml;

				// 提取字段
				std::string content = trim(getXmlTag(xml, "Content"));
				std::string eventKey = trim(getXmlTag(xml, "EventKey"));
				std::string event = toLower(trim(getXmlTag(xml, "Event")));

				// click 菜单
				if (event == "click" && !eventKey.empty())
				{
					if (eventKey.rfind("#sendmsg#", 0) == 0)
					{
						if (eventKey.find("_0_0") != std::string::npos)       content = "今日上传网速";
						else if (eventKey.find("_0_1") != std::string::npos)  content = "近7天上传网速";
						else if (eventKey.find("_0_2") != std::string::npos)  content = "今日下载网速";
						else if (eventKey.find("_0_3") != std::string::npos)  content = "近7天下载网速";
						else content.clear();
					}
					else
					{
						content = eventKey;
					}
				}
				content = trim(content);
				LOG_INFO << "📝 用户发来内容: [" << content << "]";

				// 四个指令
				std::string reply;
				if (content == "今日上传网速")       reply = getTodayReport(false);
				else if (content == "近7天上传网速") reply = get7DaysReport(false);
				else if (content == "今日下载网速")  reply = getTodayReport(true);
				else if (content == "近7天下载网速") reply = get7DaysReport(true);
				else
				{
					LOG_ERROR << "❌ 未识别的指令，不回复";
					auto r = HttpResponse::newHttpResponse();
					r->setStatusCode(k200OK);
					r->setContentTypeCode(CT_TEXT_PLAIN);
					r->setBody("success");
					return callback(r);
				}

				// 异步发送到主服务账户
				sendWecomTextAsync(CORP_SECRET_MAIN, AGENT_ID_MAIN, TO_USER_MAIN, reply);

				// 企业微信要求返回 "success"
				auto r = HttpResponse::newHttpResponse();
				r->setStatusCode(k200OK);
				r->setContentTypeCode(CT_TEXT_PLAIN);
				r->setBody("success");
				return callback(r);
			}

			auto r = HttpResponse::newHttpResponse();
			r->setStatusCode(k405MethodNotAllowed);
			callback(r);
		},
		{ Get, Post }
	);

	// 监听 127.0.0.1:12346（与你的 Flask 一致）
	app().addListener("127.0.0.1", 12440);
	LOG_INFO << "WeCom relay & callback server started at http://127.0.0.1:12440";
	app().run();
	return 0;
}
