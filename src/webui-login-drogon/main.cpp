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

// ---- 从请求中获取用户（先 Header: X-Token；再 JSON: user/pass 或 token）----
static User* getUserFromReq(const HttpRequestPtr& req)
{
	auto tok = req->getHeader("x-token");
	if (!tok.empty()) if (auto* u = findByToken(tok)) return u;

	if (auto json = req->getJsonObject())
	{
		auto n = (*json)["user"].asString();
		auto p = (*json)["pass"].asString();
		if (!n.empty() && !p.empty()) if (auto* u = findByNamePass(n, p)) return u;
		auto t2 = (*json)["token"].asString();
		if (!t2.empty()) if (auto* u = findByToken(t2)) return u;
	}
	return nullptr;
}

int main()
{
	app().addListener("0.0.0.0", 35812);
	// 运行目录在 build/，指回源码里的 layuimini
	app().setDocumentRoot("../src/layuimini");
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


	app().run();
	return 0;
}
