#include <drogon/drogon.h>
#include <json/json.h>
using namespace drogon;

// ---- 用户数据 & 查找 ----
struct User { const char* name; const char* pass; const char* token; };
static User kUsers[] = {
	{"admin","pass0","admin_token"},
	{"user1","pass1","user1_token"},
	{"user2","pass2","user2_token"},
	{nullptr,nullptr,nullptr},
};

static User* findByNamePass(const std::string& n, const std::string& p)
{
	for (auto* u = kUsers; u->name; ++u) if (n == u->name && p == u->pass) return u;
	return nullptr;
}
static User* findByToken(const std::string& t)
{
	for (auto* u = kUsers; u->name; ++u) if (t == u->token) return u;
	return nullptr;
}

#include <drogon/utils/Utilities.h>   // base64Decode

static User* getUserFromReq(const HttpRequestPtr& req)
{
	// 1) X-Token
	if (auto tok = req->getHeader("x-token"); !tok.empty())
	{
		if (auto* u = findByToken(tok)) return u;
	}

	// 2) Authorization: Basic base64(user:pass)
	if (auto auth = req->getHeader("authorization"); !auth.empty())
	{
		// 兼容大小写：getHeader 已大小写不敏感，这里只判断前缀
		const std::string prefix = "Basic ";
		if (auth.size() > prefix.size() &&
			strncasecmp(auth.data(), prefix.c_str(), prefix.size()) == 0)
		{
			auto decoded = drogon::utils::base64Decode(auth.substr(prefix.size()));
			auto pos = decoded.find(':');
			if (pos != std::string::npos)
			{
				auto name = decoded.substr(0, pos);
				auto pass = decoded.substr(pos + 1);
				if (auto* u = findByNamePass(name, pass)) return u;
			}
		}
	}

	// 3) query/form 参数（GET ?user=..&pass=.. 或 POST x-www-form-urlencoded）
	{
		auto name = req->getParameter("user");
		auto pass = req->getParameter("pass");
		auto tok = req->getParameter("token");
		if (!tok.empty())
		{
			if (auto* u = findByToken(tok)) return u;
		}
		if (!name.empty() && !pass.empty())
		{
			if (auto* u = findByNamePass(name, pass)) return u;
		}
	}

	// 4) JSON body
	if (auto json = req->getJsonObject())
	{
		auto name = (*json)["user"].asString();
		auto pass = (*json)["pass"].asString();
		auto tok = (*json)["token"].asString();
		if (!tok.empty())
		{
			if (auto* u = findByToken(tok)) return u;
		}
		if (!name.empty() && !pass.empty())
		{
			if (auto* u = findByNamePass(name, pass)) return u;
		}
	}

	return nullptr;
}


int main()
{
	app().addListener("0.0.0.0", 35812);
	
	app().setDocumentRoot("layuimini");
	//app().setDocumentRoot("web_root");
	app().setLogLevel(trantor::Logger::kInfo);

	app().registerHandler(
		"/api/login",
		[](const HttpRequestPtr& req,
			std::function<void(const HttpResponsePtr&)>&& callback)
		{
			if (auto* u = getUserFromReq(req))
			{
				Json::Value out; out["user"] = u->name; out["token"] = u->token;
				callback(HttpResponse::newHttpJsonResponse(out));
			}
			else
			{
				auto r = HttpResponse::newHttpResponse();
				r->setStatusCode(k401Unauthorized);
				r->setBody("Unauthorized\n");
				callback(r);
			}
		},
		{ Get, Post }
	);

	app().registerHandler(
		"/api/data",
		[](const HttpRequestPtr& req,
			std::function<void(const HttpResponsePtr&)>&& callback)
		{
			if (!getUserFromReq(req))
			{
				auto r = HttpResponse::newHttpResponse();
				r->setStatusCode(k403Forbidden);
				r->setBody("Denied\n");
				callback(r);
				return;
			}
			Json::Value out; out["text"] = "Hello!"; out["data"] = "somedata";
			callback(HttpResponse::newHttpJsonResponse(out));
		},
		{ Get, Post }
	);

	// 允许未登录访问：/api/init.json -> 从 layuimini/api/init.json 返回文件
	app().registerHandler(
		"/api/init.json",
		[](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)> &&cb){
			auto resp = HttpResponse::newFileResponse("layuimini/api/init.json");
			resp->setContentTypeCode(CT_APPLICATION_JSON);
			cb(resp);
		},
		{Get}
	);

	// 需要鉴权的接口：/api/data
	app().registerHandler(
		"/api/data",
		[](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)> &&cb){
			if (!getUserFromReq(req)) {
				auto r = HttpResponse::newHttpResponse();
				r->setStatusCode(k403Forbidden);
				r->setBody("Denied\n");
				cb(r);
				return;
			}
			Json::Value j; j["text"]="Hello!"; j["data"]="somedata";
			cb(HttpResponse::newHttpJsonResponse(j));
		},
		{Get, Post}
	);

	// 兜底规则：未登录访问 /api/* 其他路径，一律 403（保持和 mongoose 一致）
	app().registerHandler(
		R"(^/api/.*$)",
		[](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)> &&cb){
			if (!getUserFromReq(req)) {
				auto r = HttpResponse::newHttpResponse();
				r->setStatusCode(k403Forbidden);
				r->setBody("Denied\n");
				cb(r);
				return;
			}
			// 已登录但没有更具体路由，返回 404
			cb(HttpResponse::newNotFoundResponse());
		},
		{Get, Post}
	);


	app().run();
	return 0;
}
