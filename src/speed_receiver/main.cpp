#include <drogon/drogon.h>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <fstream>

using namespace drogon;

// === 配置常量 ===
static const std::string LOG_BASE_DIR = "/var/log/netmon/speed";
static const std::string SECRET_TOKEN = "MySuperSecret123";

static std::string toUpper(const std::string& s)
{
	std::string out = s;
	std::transform(out.begin(), out.end(), out.begin(),
		[](unsigned char c) { return std::toupper(c); });
	return out;
}


// 小工具：时间格式化
static std::string nowFmt(const char* fmt)
{
	using namespace std::chrono;
	auto t = system_clock::to_time_t(system_clock::now());
	std::tm tm{};
	localtime_r(&t, &tm);
	std::ostringstream oss;
	oss << std::put_time(&tm, fmt);
	return oss.str();
}

// 统一返回 JSON: ok
static HttpResponsePtr jsonOk(const std::string& received)
{
	Json::Value j;
	j["status"] = "ok";
	j["received"] = received;
	j["timestamp"] = nowFmt("%FT%T"); // ISO-ish
	auto resp = HttpResponse::newHttpJsonResponse(j);
	resp->setStatusCode(k200OK);
	resp->addHeader("Content-Type", "application/json; charset=utf-8");
	return resp;
}

// 统一返回 JSON: error
static HttpResponsePtr jsonError(const std::string& msg,
	const std::string& raw,
	HttpStatusCode code = k400BadRequest)
{
	Json::Value j;
	j["status"] = "error";
	j["msg"] = msg;
	j["debug_raw"] = raw;
	auto resp = HttpResponse::newHttpJsonResponse(j);
	resp->setStatusCode(code);
	resp->addHeader("Content-Type", "application/json; charset=utf-8");
	return resp;
}

int main(int argc, char* argv[])
{
	// 路由：POST /api/report-speed
	app().registerHandler(
		"/api/report-speed",
		[](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback)
		{
			try
			{
				// Drogon 会在 content-type 为 application/x-www-form-urlencoded 或 multipart 时
				// 自动把 POST 表单解析到 parameters；同时也会解析 URL 查询参数。
				const auto& params = req->getParameters();
				std::string data = req->getParameter("data");   // 可能为空
				std::string token = req->getParameter("token");  // 可能为空

				// 兜底：若客户端没正确设 content-type，可手动从原始 body 里解析
				if (data.empty() || token.empty())
				{
					// 原始 body 是 urlencoded 的话，这样也能拆出来
					std::string body(req->getBody());
					if (data.empty())
					{
						auto it = body.find("data=");
						if (it != std::string::npos)
						{
							auto tail = body.substr(it + 5);
							auto amp = tail.find('&');
							auto enc = tail.substr(0, amp == std::string::npos ? tail.size() : amp);
							data = drogon::utils::urlDecode(enc);
						}
					}
					if (token.empty())
					{
						auto it = req->getBody().find("token=");
						if (it != std::string::npos)
						{
							auto tail = req->getBody().substr(it + 6);
							auto amp = tail.find('&');
							auto enc = tail.substr(0, amp == std::string::npos ? tail.size() : amp);
							token = drogon::utils::urlDecode(enc);
						}
					}
				}

				// token 校验
				if (token != SECRET_TOKEN)
				{
					callback(jsonError("Invalid token", std::string(req->getBody()), k403Forbidden));
					return;
				}

				if (data.empty())
				{
					callback(jsonError("Missing 'data' parameter", std::string(req->getBody()), k400BadRequest));
					return;
				}

				// 解析 data: "<speed>,<up/down>"
				// 去空白
				auto trim = [](std::string s)
					{
						auto issp = [](int c) { return std::isspace(c); };
						s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](int c) { return !issp(c); }));
						s.erase(std::find_if(s.rbegin(), s.rend(), [&](int c) { return !issp(c); }).base(), s.end());
						return s;
					};
				data = trim(data);

				auto commaPos = data.find(',');
				if (commaPos == std::string::npos)
				{
					callback(jsonError("Expected format: <speed>,<up/down>", data, k400BadRequest));
					return;
				}

				std::string speedStr = trim(data.substr(0, commaPos));
				std::string dir = trim(data.substr(commaPos + 1));

				if (!(dir == "up" || dir == "down"))
				{
					callback(jsonError("Expected format: <speed>,<up/down>", data, k400BadRequest));
					return;
				}

				char* endp = nullptr;
				errno = 0;
				double speed = std::strtod(speedStr.c_str(), &endp);
				if (endp == speedStr.c_str() || errno != 0)
				{
					callback(jsonError("Speed must be a number", data, k400BadRequest));
					return;
				}

				// 生成文件路径
				std::string nowHuman = nowFmt("%Y-%m-%d %H:%M:%S");
				std::string today = nowFmt("%Y%m%d");
				std::string filename = "speed-" + dir + "-" + today + ".csv";
				std::filesystem::path logDir(LOG_BASE_DIR);
				std::filesystem::path logPath = logDir / filename;

				// 确保目录存在
				std::error_code ec;
				std::filesystem::create_directories(logDir, ec);
				if (ec)
				{
					callback(jsonError("Failed to create log directory: " + ec.message(), "", k500InternalServerError));
					return;
				}

				// 追加写入
				{
					std::ofstream ofs(logPath, std::ios::app);
					if (!ofs)
					{
						callback(jsonError("Failed to open log file for append", "", k500InternalServerError));
						return;
					}
					ofs << nowHuman << "," << std::fixed << std::setprecision(2) << speed << "\n";
				}

				std::ostringstream oss;
				oss << "[" << toUpper(dir) << "] " << nowHuman
					<< " " << std::fixed << std::setprecision(2) << speed << " KB/s";

				LOG_INFO << oss.str();

				callback(jsonOk(data));
			}
			catch (const std::exception& e)
			{
				callback(jsonError(e.what(), "", k500InternalServerError));
			}
		},
		{ Post }  // 只允许 POST
	);

	// 监听与运行
	app()
		.addListener("0.0.0.0", 35810)
		.run();

	return 0;
}
