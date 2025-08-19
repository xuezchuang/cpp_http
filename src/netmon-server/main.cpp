// main.cpp
#include <drogon/drogon.h>
#include <json/json.h>
#include <mutex>
#include <tuple>
#include <vector>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <unordered_map>
#include <map>
#include <chrono>
#include <iomanip>
#include <sstream>

using namespace drogon;
namespace fs = std::filesystem;
std::mutex cache_lock;

std::unordered_map<std::string, std::string> mac_ip_map;
using TrafficEntry = std::tuple<std::string, long long, long long>;        // ts, up, down
std::unordered_map<std::string, std::vector<TrafficEntry>> traffic_cache;

std::unordered_map<std::string, std::string> name_map = {
	{"54:92:09:7c:45:74", "Huawei Router"},
	{"06:3b:62:cf:69:2d", "snowsome"},
	{"92:b4:59:1d:a4:ff", "snowsome"},
	{"fe:aa:68:f0:32:7f", "gugu"},
	{"f2:84:38:a6:fc:33", "yueguang"},
	{"50:a0:09:8a:2b:83", "MiAiSoundbox"},
	{"9e:dd:2d:3b:0e:37", "dalianmao"},
	{"c8:5e:a9:52:f7:9c", "DESKTOP-5158P17"},
	{"54:f2:9f:2f:7c:f0", "MiAiTV"},
	{"52:23:40:01:f7:c2", "ipad"},
};

static std::unordered_set<std::string> TRACKED_HOSTS{
	"gugu", "yueguang", "snowsome", "dalianmao"   // 按需修改
};

using CpuEntry = std::tuple<std::string, double, Json::Value>;

std::vector<CpuEntry> cpu_cache;
std::vector<std::tuple<std::string, Json::Value>> temp_cache;

static std::mutex cache_mutex;

constexpr int ONLINE_TIMEOUT = 300;  // 秒

// 策略阈值（和你的 Python 对齐）
static constexpr int GRACE_NO_TRAFFIC_SEC = 30;   // 流量静默宽限
static constexpr int FAIL_THRESH = 2;    // 连续失败次数阈值（达阈值判离线并推送）
static constexpr int POLL_SEC = 10;   // presence 检测轮询周期（外层循环会用 60s 起线程，这里保持 10s 逻辑）
static constexpr int ONLINE_EXPIRE_SEC = 600;  // 用于清理在线态（与之前一致）

static std::unordered_map<std::string, int> fail_counts; // name -> 连续失败次数
static std::unordered_map<std::string, std::chrono::system_clock::time_point> last_seen;    // name -> 最近一次确认在线
static std::unordered_map<std::string, std::chrono::system_clock::time_point> offline_since;// name -> 首次离线时间

// 推送配置
static const std::string RELAY_HOST = "https://wecom.snowsome.com";
static const std::string RELAY_PATH = "/api/relay";
static const std::string SECRET = "1qnQiwDZxsgJsYKDR4ybtGm0urLYxNBMdXlCMMuovA4";

struct IpStat {
	long long up{ 0 };
	long long down{ 0 };
	double    last{ 0.0 };   // epoch seconds
};

std::unordered_map<std::string, IpStat> ip_stats; // ip -> {up,down,last}

// ===== 小工具：把 "YYYY-MM-DD HH:MM:SS" 变为 "YYYY-MM-DDTHH:MM:SS" =====
static inline std::string to_iso_ts(std::string ts_raw)
{
	std::replace(ts_raw.begin(), ts_raw.end(), ' ', 'T');
	return ts_raw;
}

// ===== 配置 =====
static const std::string HISTORY_DIR = "/var/log/netmon/history";
static const size_t MAX_RECORDS = 200000;   // 按需改

// ===== 小工具 =====

static constexpr const char* DATE_FMT = "%Y-%m-%d %H:%M:%S";
// 解析 "YYYY-mm-dd HH:MM:SS" -> time_t（失败返回 -1）
static inline std::time_t parseTs(const std::string& ts)
{
	std::tm tm{};
	std::istringstream iss(ts);
	iss >> std::get_time(&tm, DATE_FMT);
	if (!iss.fail())
		return std::mktime(&tm); // 本地时区
	return static_cast<std::time_t>(-1);
}

// 安全读取 query 参数（默认值）
static inline std::string getQuery(const drogon::HttpRequestPtr& req, const std::string& key, const std::string& def = "")
{
	auto v = req->getParameter(key);
	return v.empty() ? def : v;
}

// 简单字符串转小写
static inline std::string toLower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return s;
}

// 近似四舍五入到 n 位小数
static inline double roundTo(double x, int n)
{
	double p = std::pow(10.0, n);
	return std::round(x * p) / p;
}

static inline std::string now_ts_raw()
{
	using clock = std::chrono::system_clock;
	auto t = clock::to_time_t(clock::now());
	std::tm tm{};
	localtime_r(&t, &tm);
	std::ostringstream oss;
	oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
	return oss.str();
}
static inline std::string today_yyyymmdd()
{
	using clock = std::chrono::system_clock;
	auto t = clock::to_time_t(clock::now());
	std::tm tm{};
	localtime_r(&t, &tm);
	std::ostringstream oss;
	oss << std::put_time(&tm, "%Y%m%d");
	return oss.str();
}
static inline void ensure_dir(const std::string& dir)
{
	std::error_code ec;
	std::filesystem::create_directories(dir, ec);
}
static inline std::string join_with_bar(const std::vector<std::string>& v)
{
	std::ostringstream oss;
	for (size_t i = 0; i < v.size(); ++i)
	{
		if (i) oss << " | ";
		oss << v[i];
	}
	return oss.str();
}
static inline std::string exec_shell(const std::string &cmd)
{
	std::string out;
	FILE* pipe = popen(cmd.c_str(), "r");
	if (!pipe) return out;
	char buf[4096];
	while (fgets(buf, sizeof(buf), pipe)) out.append(buf);
	pclose(pipe);
	return out;
}
static inline std::vector<std::string> kv_fragments_from_json_object(const Json::Value& obj)
{
	std::vector<std::string> frags;
	if (!obj.isObject()) return frags;
	auto members = obj.getMemberNames();
	frags.reserve(members.size());
	for (const auto& k : members)
	{
		std::ostringstream oss;
		oss << k << ":" << obj[k].asDouble();
		frags.push_back(oss.str());
	}
	return frags;
}

static inline std::string trim(const std::string& s)
{
	size_t b = s.find_first_not_of(" \t\r\n");
	if (b == std::string::npos) return {};
	size_t e = s.find_last_not_of(" \t\r\n");
	return s.substr(b, e - b + 1);
}

static inline std::vector<std::string> split(const std::string& s, char delim)
{
	std::vector<std::string> out;
	std::string item;
	std::istringstream iss(s);
	while (std::getline(iss, item, delim)) out.emplace_back(item);
	return out;
}

static std::string dayStringOffset(int daysBack)
{
	using namespace std::chrono;
	auto now = system_clock::now();
	auto t = now - hours(24 * daysBack);
	std::time_t tt = system_clock::to_time_t(t);
	std::tm tm{};
#if defined(_WIN32)
	localtime_s(&tm, &tt);
#else
	localtime_r(&tt, &tm);
#endif
	std::ostringstream oss;
	oss << std::put_time(&tm, "%Y%m%d");
	return oss.str();
}

static inline int run_cmd_quiet(const std::string& cmd)
{
	// 用 system 最简；如需更安全可用 fork+exec/popen
	return std::system((cmd + " 2>/dev/null >/dev/null").c_str());
}

static inline std::string nowDate()
{
	using namespace std::chrono;
	auto now = system_clock::now();
	std::time_t tt = system_clock::to_time_t(now);
	std::tm tm{};
#if defined(_WIN32)
	localtime_s(&tm, &tt);
#else
	localtime_r(&tt, &tm);
#endif
	char buf[32];
	std::strftime(buf, sizeof(buf), "%Y%m%d", &tm);
	return std::string(buf);
}

static inline std::string nowTs()
{
	using namespace std::chrono;
	auto now = system_clock::now();
	std::time_t tt = system_clock::to_time_t(now);
	std::tm tm{};
#if defined(_WIN32)
	localtime_s(&tm, &tt);
#else
	localtime_r(&tt, &tm);
#endif
	char buf[32];
	std::strftime(buf, sizeof(buf), DATE_FMT, &tm);
	return std::string(buf);
}

static inline void trimVectorToMax(std::vector<TrafficEntry>& v, size_t maxKeep)
{
	if (v.size() > maxKeep)
	{
		v.erase(v.begin(), v.begin() + (v.size() - maxKeep));
	}
}

static inline bool parseTrafficLine(const std::string& line,
	std::string& ts, long long& up, long long& down)
{
	auto first = line.find(',');
	if (first == std::string::npos) return false;
	auto second = line.find(',', first + 1);
	if (second == std::string::npos) return false;

	ts = line.substr(0, first);
	try
	{
		up = std::stoll(line.substr(first + 1, second - first - 1));
		down = std::stoll(line.substr(second + 1));
	}
	catch (...) { return false; }
	return true;
}

static const std::unordered_set<std::string> PRESENT_STATES = {
	"REACHABLE", "STALE", "DELAY", "PROBE"
};


static std::optional<std::tuple<std::string, long long, long long>> lastChangeFromCsv(const std::string& path)
{
	std::ifstream ifs(path);
	if (!ifs) return std::nullopt;

	std::string line, lastTs;
	long long lastUp = -1, lastDown = -1;
	bool havePrev = false;

	std::tuple<std::string, long long, long long> lastChange;

	while (std::getline(ifs, line))
	{
		if (line.empty()) continue;
		std::string ts; long long up = 0, down = 0;
		if (!parseTrafficLine(line, ts, up, down)) continue;

		if (!havePrev)
		{
			// 第一行不算“变化”，只建立基准
			lastTs = ts; lastUp = up; lastDown = down;
			havePrev = true;
			continue;
		}

		if (up != lastUp || down != lastDown)
		{
			lastChange = std::make_tuple(ts, up, down);
			lastTs = ts; lastUp = up; lastDown = down;
		}
		else
		{
			// 没变化就只更新基准时间（可选，不影响“变化”记录）
			lastTs = ts;
		}
	}

	if (havePrev)
	{
		// 如果从未出现“变化”，Python 里的 _last_change_from_csv 通常会返回 None；
		// 这里仅当确实记录过“变化”时才返回。
		// 判断方式：看我们是否给 lastChange 赋过值。
		// 粗暴法：初始化一个标志。
		// 这里用 up/down 初始 -1 的特征来判断是否有实际赋值：
		const auto& [_ts, _up, _down] = lastChange;
		if (!std::get<0>(lastChange).empty() || _up != 0 || _down != 0)
		{
			return lastChange;
		}
	}
	return std::nullopt;
}

std::unordered_map<std::string, std::string> get_ip_mac_map()
{
	std::unordered_map<std::string, std::string> ip_mac;

	try
	{
		std::string out = exec_shell("ip neigh");

		std::istringstream iss(out);
		std::string line;
		while (std::getline(iss, line))
		{
			if (line.empty()) continue;
			std::istringstream ls(line);
			std::vector<std::string> parts;
			std::string tok;
			while (ls >> tok) parts.emplace_back(std::move(tok));

			if (parts.size() >= 5)
			{
				std::string ip = parts[0];
				std::string mac = toLower(parts[4]);
				ip_mac[ip] = std::move(mac);
			}
		}
	}
	catch (...)
	{
		// 与 Python 一样，异常忽略
	}

	return ip_mac;
}

static inline double toEpoch(const std::string& ts)
{
	std::tm tm{};
	std::istringstream iss(ts);
	iss >> std::get_time(&tm, DATE_FMT);
	if (iss.fail()) return -1.0;
	std::time_t t = std::mktime(&tm); // 本地时区
	if (t == static_cast<std::time_t>(-1)) return -1.0;
	return static_cast<double>(t);
}

// ====== _init_ip_stats_from_history：从今天/昨天恢复 ip_stats ======
void init_ip_stats_from_history()
{
	// 1) 取 “当前” IP->MAC，建 mac->ip 映射（小写 MAC）
	std::unordered_map<std::string, std::string> ip_mac;
	try { ip_mac = get_ip_mac_map(); }
	catch (...) {}

	std::unordered_map<std::string, std::string> mac_to_ip;
	for (const auto& kv : ip_mac)
	{
		const std::string& ip = kv.first;
		const std::string& mac = kv.second;
		if (!mac.empty()) mac_to_ip[toLower(mac)] = ip;
	}

	// 2) 今天/昨天两个目录
	auto dayStr = [](int offsetDays)->std::string
		{
			using namespace std::chrono;
			auto now = system_clock::now() + hours(24 * offsetDays);
			std::time_t tt = system_clock::to_time_t(now);
			std::tm tm{};
#if defined(_WIN32)
			localtime_s(&tm, &tt);
#else
			localtime_r(&tt, &tm);
#endif
			char buf[16];
			std::strftime(buf, sizeof(buf), "%Y%m%d", &tm);
			return std::string(buf);
		};

	std::vector<std::string> day_list{
		dayStr(0),   // today
		dayStr(-1),  // yesterday
	};

	// 3) 聚合临时结果：ip -> (up, down, last_epoch)
	std::unordered_map<std::string, std::tuple<long long, long long, double>> tmp_stats;

	for (const auto& day : day_list)
	{
		fs::path log_dir = fs::path(HISTORY_DIR) / ("speed-" + day);
		if (!fs::exists(log_dir) || !fs::is_directory(log_dir)) continue;

		for (const auto& ent : fs::directory_iterator(log_dir))
		{
			if (!ent.is_regular_file()) continue;
			const auto& p = ent.path();
			if (p.extension() != ".csv") continue;

			std::string mac = toLower(p.stem().string());
			if (mac == "__router__") continue;

			auto last = lastChangeFromCsv(p.string());
			if (!last.has_value()) continue;

			const auto& [ts, up, down] = *last;
			double epoch = toEpoch(ts);
			if (epoch < 0) continue;

			// 映射到 IP
			auto itIP = mac_to_ip.find(mac);
			if (itIP == mac_to_ip.end()) continue;
			const std::string& ip = itIP->second;

			auto itOld = tmp_stats.find(ip);
			if (itOld == tmp_stats.end() || epoch > std::get<2>(itOld->second))
			{
				tmp_stats[ip] = std::make_tuple(up, down, epoch);
			}
		}
	}

	if (tmp_stats.empty()) return;

	// 4) 回写到 ip_stats（加锁）
	{
		std::lock_guard<std::mutex> lk(cache_lock);
		for (const auto& kv : tmp_stats)
		{
			const std::string& ip = kv.first;
			const auto& [up, down, last_epoch] = kv.second;
			ip_stats[ip] = IpStat{ up, down, last_epoch };
		}
	}
}

std::vector<std::string> get_current_online_ips()
{
	// 冷启动：如果 ip_stats 还空，尝试从日志恢复
	bool emptyNow = false;
	{
		std::lock_guard<std::mutex> lk(cache_lock);
		emptyNow = ip_stats.empty();
	}
	if (emptyNow)
	{
		try { init_ip_stats_from_history(); }
		catch (...) {}
	}

	const double nowEpoch = []()
		{
			using namespace std::chrono;
			return duration_cast<std::chrono::seconds>(
				system_clock::now().time_since_epoch()).count();
		}();

	std::vector<std::string> online;
	{
		std::lock_guard<std::mutex> lk(cache_lock);
		for (const auto& kv : ip_stats)
		{
			const std::string& ip = kv.first;
			const IpStat& st = kv.second;
			if (nowEpoch - st.last <= static_cast<double>(ONLINE_TIMEOUT))
			{
				online.emplace_back(ip);
			}
		}
	}
	return online;
}

// Python: scan_all_ip_mac(online_only=True) -> Dict[ip, mac]
std::unordered_map<std::string, std::string> scan_all_ip_mac(bool online_only = true)
{
	std::unordered_map<std::string, std::string> ip_mac;

	try
	{
		// 等价于：subprocess.check_output(["ip", "neigh"], text=True)
		std::string out = exec_shell("ip neigh");

		std::istringstream iss(out);
		std::string line;
		while (std::getline(iss, line))
		{
			// 拆空白
			std::istringstream ls(line);
			std::vector<std::string> parts;
			std::string tok;
			while (ls >> tok) parts.emplace_back(std::move(tok));
			if (parts.size() < 5) continue;

			// parts[0] = IP, parts[4] = MAC, 最后一个为状态
			const std::string& ip = parts[0];
			const std::string  state = parts.back();

			// 仅统计 192.168.20.* 且状态在 PRESENT_STATES
			if (ip.rfind("192.168.20.", 0) != 0) continue;
			if (PRESENT_STATES.find(state) == PRESENT_STATES.end()) continue;

			// 有的行是 "... lladdr <mac> <STATE>"，此时 parts[4] 就是 MAC
			std::string mac = toLower(parts[4]);
			ip_mac[ip] = std::move(mac);
		}
	}
	catch (...)
	{
		// 静默与 Python 一致
	}

	if (!online_only)
	{
		return ip_mac;
	}

	// 过滤：只保留当前在线 IP
	std::unordered_map<std::string, std::string> filtered;
	try
	{
		std::vector<std::string> online = get_current_online_ips();
		std::unordered_set<std::string> online_set(online.begin(), online.end());
		for (auto& kv : ip_mac)
		{
			if (online_set.find(kv.first) != online_set.end())
			{
				filtered.emplace(kv.first, kv.second);
			}
		}
	}
	catch (...)
	{
		// 如果取在线列表失败，就退化为原始表
		return ip_mac;
	}

	return filtered;
}

static std::unordered_map<std::string, long long>
parseNftSetBytes(const std::string& setName)
{
	std::unordered_map<std::string, long long> results;

	// 等价于：nft list set ip netmon up/down
	std::string output = exec_shell("nft list set ip netmon " + setName);
	if (output.empty()) return results;

	// 抽出 elements { ... } 块
	bool in_elements = false;
	std::vector<std::string> element_lines;
	std::istringstream iss(output);
	std::string line;

	while (std::getline(iss, line))
	{
		// 去掉首尾空白
		auto ltrim = [](std::string& s)
			{
				s.erase(s.begin(), std::find_if(s.begin(), s.end(),
					[](unsigned char ch) { return !std::isspace(ch); }));
			};
		auto rtrim = [](std::string& s)
			{
				s.erase(std::find_if(s.rbegin(), s.rend(),
					[](unsigned char ch) { return !std::isspace(ch); }).base(), s.end());
			};
		ltrim(line); rtrim(line);

		if (!in_elements)
		{
			// 行里出现 "elements" 且带 "{"，开始收集
			auto pos = line.find("elements");
			auto brace = line.find('{');
			if (pos != std::string::npos && brace != std::string::npos)
			{
				in_elements = true;
				// 取 { 后面部分
				std::string after = line.substr(brace + 1);
				element_lines.push_back(after);
			}
		}
		else
		{
			// 收集直到遇到 '}'
			auto brace = line.find('}');
			if (brace != std::string::npos)
			{
				element_lines.push_back(line.substr(0, brace));
				break;
			}
			element_lines.push_back(line);
		}
	}

	if (element_lines.empty()) return results;

	// 把逗号替换成换行，方便逐条解析
	std::string element_text;
	for (auto& s : element_lines)
	{
		element_text += s;
		element_text.push_back(' ');
	}
	std::replace(element_text.begin(), element_text.end(), ',', '\n');

	std::istringstream el(element_text);
	std::string entry;
	while (std::getline(el, entry))
	{
		// 拆空白
		std::istringstream es(entry);
		std::vector<std::string> parts;
		std::string tok;
		while (es >> tok) parts.emplace_back(std::move(tok));
		if (parts.size() < 6) continue;

		// 过滤出看起来像 IPv4 的（含三个点）
		if (std::count(parts[0].begin(), parts[0].end(), '.') != 3) continue;

		const std::string& ip = parts[0];

		// 在 tokens 里找 "bytes <number>"
		for (size_t i = 0; i + 1 < parts.size(); ++i)
		{
			if (parts[i] == "bytes")
			{
				try
				{
					long long bytes = std::stoll(parts[i + 1]);
					results[ip] = bytes;
				}
				catch (...)
				{
					// 忽略解析失败
				}
				break;
			}
		}
	}

	return results;
}

// 对外提供：与 Python 的 get_nft_bytes() 对齐
std::unordered_map<std::string, long long> get_nft_bytes_up() { return parseNftSetBytes("up"); }
std::unordered_map<std::string, long long> get_nft_bytes_down() { return parseNftSetBytes("down"); }


// 根据设备名（name）在邻居表中查 IP：name_map 为 MAC->Name
static std::string ipByName(const std::unordered_map<std::string, std::string>& ip_mac,
	const std::string& name)
{
	// 反查：找出某个 MAC 的名字等于 name 的 ip
	for (const auto& kv : ip_mac)
	{
		const std::string& ip = kv.first;
		const std::string& mac = kv.second;
		auto it = name_map.find(mac);
		if (it != name_map.end() && it->second == name)
		{
			return ip;
		}
	}
	return {};
}

// 进行一次“存在性复核”：如果 flow_recent 为真，直接认为 present；否则用 arping/ping 试探
static bool isPresent(const std::string& ip, bool flow_recent)
{
	if (flow_recent) return true;

	// 优先 arping（更快发现二层），失败就用 ping
	int rc = std::system((std::string("arping -q -c 1 -w 1 ") + ip + " >/dev/null 2>&1").c_str());
	if (rc == 0) return true;

	rc = std::system((std::string("ping -c 1 -W 1 ") + ip + " >/dev/null 2>&1").c_str());
	return rc == 0;
}

static long long readCounterFile(const std::string& path)
{
	std::ifstream ifs(path);
	long long v = 0;
	if (ifs)
	{
		ifs >> v;
	}
	return v;
}

// 返回 (tx_bytes, rx_bytes)
std::pair<long long, long long> get_ppp0_bytes()
{
	// 也可把接口名做成可配置
	const std::string base = "/sys/class/net/ppp0/statistics/";
	long long tx = readCounterFile(base + "tx_bytes");
	long long rx = readCounterFile(base + "rx_bytes");
	return { tx, rx };
}

// 发送企业微信消息（经你的转发服务）
static void send_wechat_msg(const std::string& text)
{
	try
	{
		auto client = drogon::HttpClient::newHttpClient(RELAY_HOST);
		Json::Value body;
		body["secret"] = SECRET;
		body["text"] = text;

		auto req = drogon::HttpRequest::newHttpJsonRequest(body);
		req->setPath(RELAY_PATH);
		req->setMethod(drogon::Post);

		// 同步发送，5秒超时
		auto [result, resp] = client->sendRequest(req, 5.0);
		if (result != drogon::ReqResult::Ok || !resp || resp->getStatusCode() >= 400)
		{
			LOG_WARN << "[presence] send_wechat_msg failed: result=" << static_cast<int>(result)
				<< " code=" << (resp ? resp->getStatusCode() : drogon::kUnknown);
		}
	}
	catch (const std::exception& e)
	{
		LOG_WARN << "[presence] send_wechat_msg exception: " << e.what();
	}
}

// ====== 主函数：加载近 7 天历史 ======
void loadRecentHistory(int days = 7)
{
	for (int i = 0; i < days; ++i)
	{
		const std::string day = dayStringOffset(i);

		// ---- 各主机 traffic ----
		{
			fs::path log_dir = fs::path(HISTORY_DIR) / ("speed-" + day);
			if (fs::exists(log_dir) && fs::is_directory(log_dir))
			{
				for (const auto& entry : fs::directory_iterator(log_dir))
				{
					if (!entry.is_regular_file()) continue;
					const auto& p = entry.path();
					if (p.extension() != ".csv") continue;

					std::string key = toLower(p.stem().string()); // 去掉 .csv
					std::ifstream ifs(p);
					if (!ifs)
					{
						LOG_WARN << "Failed to open traffic file: " << p.string();
						continue;
					}
					std::string line;
					while (std::getline(ifs, line))
					{
						line = trim(line);
						if (line.empty()) continue;
						// 期望: ts,up,down
						auto cols = split(line, ',');
						if (cols.size() != 3) continue;
						try
						{
							const std::string& ts = cols[0];
							long long up = std::stoll(cols[1]);
							long long down = std::stoll(cols[2]);
							{
								std::scoped_lock lk(cache_lock);
								traffic_cache[key].emplace_back(ts, up, down);
							}
						}
						catch (...)
						{
							continue;
						}
					}
				}
			}
		}

		// ---- CPU 历史 ----
		{
			fs::path cpu_path = fs::path(HISTORY_DIR) / ("cpu-" + day + ".csv");
			if (fs::exists(cpu_path))
			{
				std::ifstream ifs(cpu_path);
				if (!ifs)
				{
					LOG_WARN << "Failed to open CPU file: " << cpu_path.string();
				}
				else
				{
					std::string line;
					while (std::getline(ifs, line))
					{
						line = trim(line);
						if (line.empty()) continue;
						// 期望: ts,usage,top(含逗号) —— 只切两段
						// 用手动找第一个逗号和第二个逗号来模拟 Python split(",", 2)
						auto firstComma = line.find(',');
						if (firstComma == std::string::npos) continue;
						auto secondComma = line.find(',', firstComma + 1);
						if (secondComma == std::string::npos) continue;

						std::string ts = line.substr(0, firstComma);
						std::string usage = line.substr(firstComma + 1, secondComma - firstComma - 1);
						std::string top = line.substr(secondComma + 1);

						double u = 0.0;
						try { u = std::stod(trim(usage)); }
						catch (...) { continue; }

						std::vector<std::string> top_list;
						top = trim(top);
						if (!top.empty())
						{
							// 拆分 " | "
							// 简单做法：先按 '|' 再 trim，再把空的过滤掉
							std::vector<std::string> parts = split(top, '|');
							for (auto& s : parts)
							{
								s = trim(s);
								if (!s.empty()) top_list.emplace_back(std::move(s));
							}
						}

						{
							std::scoped_lock lk(cache_lock);

							// 把 top_list -> Json 数组
							Json::Value topJson(Json::arrayValue);
							topJson.resize(0);               // 可省，确保是 array
							for (const auto& s : top_list)
							{
								topJson.append(s);           // Json::Value 可由 std::string 隐式构造
							}

							cpu_cache.emplace_back(std::move(ts), u, std::move(topJson));
						}

					}
				}
			}
		}

		// ---- 温度历史 ----
		{
			fs::path temp_path = fs::path(HISTORY_DIR) / ("temp-" + day + ".csv");
			if (fs::exists(temp_path))
			{
				std::ifstream ifs(temp_path);
				if (!ifs)
				{
					LOG_WARN << "Failed to open temp file: " << temp_path.string();
				}
				else
				{
					std::string line;
					while (std::getline(ifs, line))
					{
						line = trim(line);
						if (line.empty()) continue;

						// Python: ts, *pairs = line.split(",", 1)
						auto comma = line.find(',');
						if (comma == std::string::npos)
						{
							// 只有 ts
							std::scoped_lock lk(cache_lock);
							Json::Value obj(Json::objectValue);
							temp_cache.emplace_back(line, std::move(obj));
							continue;
						}

						std::string ts = line.substr(0, comma);
						std::string pairs = line.substr(comma + 1);

						Json::Value temps(Json::objectValue);   // JSON 对象，等价于 Python 的 dict
						if (!pairs.empty())
						{
							// 形如： "sda:48,nvme0:50"
							auto kvs = split(pairs, ',');
							for (auto& kv : kvs)
							{
								kv = trim(kv);
								if (kv.empty()) continue;

								auto colon = kv.find(':');
								if (colon == std::string::npos) continue;

								std::string k = trim(kv.substr(0, colon));
								std::string v = trim(kv.substr(colon + 1));
								if (k.empty() || v.empty()) continue;

								try
								{
									temps[k] = std::stod(v);    // 存成 JSON number
								}
								catch (...)
								{
									continue;
								}
							}
						}

						{
							std::scoped_lock lk(cache_lock);
							temp_cache.emplace_back(std::move(ts), std::move(temps));
						}

					}
				}
			}
		}
	}

	LOG_INFO << "Recent history loaded (last " << days << " days). "
		<< "traffic_keys=" << traffic_cache.size()
		<< " cpu_count=" << cpu_cache.size()
		<< " temp_count=" << temp_cache.size();
}

void record_traffic_once()
{
	std::unordered_map<std::string, std::string> ip_mac;
	try
	{
		ip_mac = scan_all_ip_mac(/*online_only=*/true);
	}
	catch (...)
	{
		// 忽略异常，继续后续步骤
	}

	for (const auto& kv : ip_mac)
	{
		const std::string& ip = kv.first;
		if (ip.rfind("192.168.20.", 0) == 0)
		{
			// nft add element ip netmon clients { ip }
			run_cmd_quiet(std::string("nft add element ip netmon clients { ") + ip + " }");
		}
	}

	// 3) 读取各 IP 的累计字节 & ppp0 自身累计字节
	std::unordered_map<std::string, long long> up_map, down_map;
	long long tx2 = 0, rx2 = 0;
	try { up_map = get_nft_bytes_up(); }
	catch (...) {}
	try { down_map = get_nft_bytes_down(); }
	catch (...) {}
	try
	{
		auto p = get_ppp0_bytes();
		tx2 = p.first; rx2 = p.second;
	}
	catch (...) {}

	// 4) 时间与日志目录
	const std::string timestamp = nowTs();
	const std::string today = nowDate();
	fs::path log_dir = fs::path(HISTORY_DIR) / ("speed-" + today);
	std::error_code ec;
	fs::create_directories(log_dir, ec); // 忽略错误

	// 5) 记录路由器（软路由自身）
	{
		// 内存
		{
			std::lock_guard<std::mutex> lk(cache_lock);
			auto& vec = traffic_cache["__router__"];
			vec.emplace_back(timestamp, tx2, rx2);
			trimVectorToMax(vec, MAX_RECORDS);
		}
		// CSV 追加
		std::ofstream ofs((log_dir / "__router__.csv").string(), std::ios::app);
		if (ofs) ofs << timestamp << ',' << tx2 << ',' << rx2 << '\n';
	}

	// 6) 记录每个在线主机（按 MAC 为 key，小写）
	for (const auto& kv : ip_mac)
	{
		const std::string& ip = kv.first;
		std::string mac = toLower(kv.second);

		auto itu = up_map.find(ip);
		auto itd = down_map.find(ip);
		if (itu == up_map.end() || itd == down_map.end()) continue;

		long long up_val = itu->second;
		long long down_val = itd->second;

		// 内存 + mac->ip 映射
		{
			std::lock_guard<std::mutex> lk(cache_lock);
			auto& vec = traffic_cache[mac];
			vec.emplace_back(timestamp, up_val, down_val);
			trimVectorToMax(vec, MAX_RECORDS);

			mac_ip_map[mac] = ip; // ★ 更新 MAC -> IP
		}

		// 逐文件 CSV 追加
		std::ofstream ofs((log_dir / (mac + ".csv")).string(), std::ios::app);
		if (ofs) ofs << timestamp << ',' << up_val << ',' << down_val << '\n';
	}

}

void record_cpu_once()
{
	try
	{
		// loadavg(1m)
		double loads[3] = { 0,0,0 };
		if (getloadavg(loads, 3) == -1) return;
		const double load1 = loads[0];

		// core 数
		long core_count = sysconf(_SC_NPROCESSORS_ONLN);
		if (core_count <= 0) core_count = 1;

		// usage 百分比，限制到两位小数
		double usage = std::min(100.0, (load1 / static_cast<double>(core_count)) * 100.0);
		usage = std::round(usage * 100.0) / 100.0;

		// top list: 去掉表头，取 5 行
		// 输出形如：
		// COMMAND %CPU
		// ffmpeg  123.4
		// ...
		std::string top_out = exec_shell("ps -eo comm,%cpu --sort=-%cpu | head -n 6");
		std::vector<std::string> top_list;
		{
			std::istringstream iss(top_out);
			std::string line;
			bool first = true;
			while (std::getline(iss, line))
			{
				if (line.empty()) continue;
				if (first) { first = false; continue; } // skip header
				// trim
				auto l = line.find_first_not_of(" \t\r\n");
				auto r = line.find_last_not_of(" \t\r\n");
				if (l == std::string::npos) continue;
				top_list.emplace_back(line.substr(l, r - l + 1));
			}
		}

		// 转 tops 为 Json::arrayValue
		Json::Value tops(Json::arrayValue);
		for (auto& s : top_list)
		{
			Json::Value item;
			// 你 Python 里是 "comm %cpu" 的原始行，这里保持一致
			item = s;
			tops.append(item);
		}

		// 时间戳
		const std::string timestamp = now_ts_raw();

		// 追加到缓存并截断
		{
			std::lock_guard<std::mutex> lk(cache_mutex);
			cpu_cache.emplace_back(timestamp, usage, tops);
			if (cpu_cache.size() > MAX_RECORDS)
			{
				cpu_cache.erase(cpu_cache.begin(),
					cpu_cache.end() - static_cast<std::ptrdiff_t>(MAX_RECORDS));
			}
		}

		// 追加到 CSV
		ensure_dir(HISTORY_DIR);
		const std::string today = today_yyyymmdd();
		const std::string csv_path = HISTORY_DIR + "/cpu-" + today + ".csv";
		std::ofstream ofs(csv_path, std::ios::app);
		if (ofs)
		{
			ofs << timestamp << "," << usage << ",";
			ofs << join_with_bar(top_list) << "\n";
		}
	}
	catch (...)
	{
		// 静默
	}
}

void record_temp_once()
{
	try
	{
		Json::Value temps(Json::objectValue);

		// === sensors -u：CPU (Package id 0 -> temp1_input)，NVMe (Composite -> temp1_input) ===
		try
		{
			std::string out = exec_shell("sensors -u 2>/dev/null");
			if (!out.empty())
			{
				std::istringstream iss(out);
				std::string line;

				bool in_pkg = false;           // 是否处于 “Package id 0:” 小节
				bool in_composite = false;     // 是否处于 “Composite:” 小节
				std::string pending_nvme;      // 最近的 nvme 节名（如 nvme0/nvme1），用于给 Composite 赋名

				auto trim = [](const std::string& s)->std::string
					{
						auto l = s.find_first_not_of(" \t\r\n");
						if (l == std::string::npos) return {};
						auto r = s.find_last_not_of(" \t\r\n");
						return s.substr(l, r - l + 1);
					};

				while (std::getline(iss, line))
				{
					std::string s = trim(line);
					if (s.empty()) continue;

					// 顶层节名（如 coretemp-isa-0000 / nvme-pci-0500）
					if (line.size() && line[0] != ' ' && s.find(':') == std::string::npos)
					{
						// 离开任何小节
						in_pkg = false;
						in_composite = false;
						// 记录最近出现过的 nvme 设备名（如果像 "nvme-pci-0500"，我们希望键名更亲和）
						pending_nvme.clear();
						continue;
					}

					// 子节标题：例如 "Package id 0:"、"Core 0:"、"Composite:"
					if (s.back() == ':' && s.find(':') == s.size() - 1)
					{
						std::string title = s.substr(0, s.size() - 1);
						// 标记是否进入 CPU 包温小节
						in_pkg = (title == "Package id 0");
						// 标记是否进入 Composite 小节，并尽量保留 nvme 名
						in_composite = (title == "Composite");

						// 尝试从上一行或更早的输出里找 nvme 名（本块简单策略：若上一顶层节包含 "nvme" 关键字，则命名为 "nvme"）
						if (in_composite && pending_nvme.empty())
						{
							pending_nvme = "NVMe";
						}
						continue;
					}

					// 记录 nvme 名：任何含 nvme 的标题行（如 "nvme-pci-0500"）
					if (s.find("nvme") != std::string::npos && s.find(':') == std::string::npos)
					{
						// 取第一个空格前的 token 或整段
						auto pos = s.find(' ');
						pending_nvme = (pos == std::string::npos) ? s : s.substr(0, pos);
						// 防止太长，简单规整一下（可选）
						if (pending_nvme.rfind("nvme", 0) == std::string::npos) pending_nvme = "NVMe";
						continue;
					}

					// 键值行：形如 "temp1_input: 55.000"
					if (s.rfind("temp1_input:", 0) == 0)
					{
						// 解析数字
						auto p = s.find(':');
						if (p != std::string::npos)
						{
							std::string vstr = trim(s.substr(p + 1));
							try
							{
								double v = std::stod(vstr);

								if (in_pkg)
								{
									temps["CPU"] = v;
									in_pkg = false; // 取到一个就够了
								}
								else if (in_composite)
								{
									std::string key = !pending_nvme.empty() ? pending_nvme : "NVMe";
									temps[key] = v;
									in_composite = false;
								}
							}
							catch (...) {}
						}
					}
				}
			}
		}
		catch (...) {}

		// === smartctl：优先 JSON 解析 /dev/sda ===
		try
		{
			std::string js = exec_shell("smartctl -A -j /dev/sda 2>/dev/null");
			if (!js.empty())
			{
				Json::Value root;
				Json::CharReaderBuilder rb;
				std::string errs;
				std::istringstream iss(js);
				if (Json::parseFromStream(rb, iss, &root, &errs))
				{
					double tempC = std::numeric_limits<double>::quiet_NaN();

					// 1) 顶层温度（smartctl JSON 提供，最可靠）
					if (root.isMember("temperature") && root["temperature"].isMember("current") &&
						root["temperature"]["current"].isNumeric())
					{
						tempC = root["temperature"]["current"].asDouble();
					}

					// 2) 回退：在 ATA SMART 表里找 190/194 或按名称匹配
					if (!std::isfinite(tempC) &&
						root.isMember("ata_smart_attributes") &&
						root["ata_smart_attributes"].isMember("table") &&
						root["ata_smart_attributes"]["table"].isArray())
					{
						const auto& tbl = root["ata_smart_attributes"]["table"];
						for (const auto& item : tbl)
						{
							if (!item.isMember("id") || !item["id"].isInt()) continue;
							int id = item["id"].asInt();
							std::string name = item.isMember("name") && item["name"].isString()
								? item["name"].asString() : "";

							// 190 = Airflow_Temperature_Cel, 194 = Temperature_Celsius
							if (id == 190 || id == 194 ||
								name == "Airflow_Temperature_Cel" || name == "Temperature_Celsius")
							{
								if (item.isMember("raw") && item["raw"].isMember("value") &&
									item["raw"]["value"].isNumeric())
								{
									tempC = item["raw"]["value"].asDouble();
									break;
								}
							}
						}
					}

					if (std::isfinite(tempC))
					{
						temps["sda"] = tempC;  // ★ 关键：写入 map，后面的落盘逻辑才能触发
					}
				}
			}

		}
		catch (...) {}

		// === 写入缓存与 CSV（与你现有逻辑一致） ===
		if (!temps.empty())
		{
			const std::string timestamp = now_ts_raw();
			const std::string today = today_yyyymmdd();

			{
				std::lock_guard<std::mutex> lk(cache_mutex);
				temp_cache.emplace_back(timestamp, temps);
				if (temp_cache.size() > MAX_RECORDS)
				{
					temp_cache.erase(
						temp_cache.begin(),
						temp_cache.end() - static_cast<std::ptrdiff_t>(MAX_RECORDS)
					);
				}
			}

			try
			{
				ensure_dir(HISTORY_DIR);
				const std::string csv_path = HISTORY_DIR + "/temp-" + today + ".csv";
				std::ofstream ofs(csv_path, std::ios::app);
				if (ofs)
				{
					// 以 k:v 形式拼接
					auto keys = temps.getMemberNames();
					std::ostringstream payload;
					for (size_t i = 0; i < keys.size(); ++i)
					{
						if (i) payload << ",";
						payload << keys[i] << ":" << temps[keys[i]].asDouble();
					}
					ofs << timestamp << "," << payload.str() << "\n";
				}
			}
			catch (...) {}
		}
	}
	catch (...)
	{
		// 保持与 Python 语义一致：静默
	}
}


// ===== 后台线程：固定间隔循环采样 =====
void start_background_record_thread(int interval_sec)
{
	if (interval_sec <= 0) interval_sec = 5;
	std::thread([interval_sec]()
		{
			// 守护线程
			for (;;)
			{
				record_traffic_once();
				record_cpu_once();
				record_temp_once();
				std::this_thread::sleep_for(std::chrono::seconds(interval_sec));
			}
		}).detach();
}

// ====== 路由函数（非 lambda）======

// GET /
void Index(const HttpRequestPtr& req,
	std::function<void(const HttpResponsePtr&)>&& cb)
{
	// 直接回 index.html；你已放在 www 下
	auto resp = HttpResponse::newFileResponse("www/index.html");
	// 可选：设定 content-type（drogon 会自动猜测，这里显式一下）
	resp->setContentTypeCode(CT_TEXT_HTML);
	cb(resp);
}

// GET /api/cpu-history
void CpuHistoryApi(const HttpRequestPtr& req,
	std::function<void(const HttpResponsePtr&)>&& cb)
{
	// ---- 参数 ----
	auto range_arg = req->getParameter("range");
	if (range_arg.empty()) range_arg = "1h";
	int offset = 0;
	try
	{
		auto offStr = req->getParameter("offset");
		if (!offStr.empty()) offset = std::stoi(offStr);
	}
	catch (...) { offset = 0; }

	// 分钟映射
	int minutes = 60;
	if (range_arg == "5m") minutes = 5;
	else if (range_arg == "1h") minutes = 60;
	else if (range_arg == "6h") minutes = 360;
	else if (range_arg == "1d") minutes = 1440;
	else if (range_arg == "1w") minutes = 10080;

	const int interval = 5; // 每 5 秒一条
	int raw_count = std::max(1, (minutes * 60) / interval);

	// 降采样
	int downsample_step = 1;
	if (minutes > 360)   downsample_step = 2;
	if (minutes > 1440)  downsample_step = 4;
	if (minutes > 3000)  downsample_step = 10;
	if (minutes > 7000)  downsample_step = 20;
	const int step = std::max(1, downsample_step);

	// ---- 切片 ----
	std::vector<CpuEntry> sliced;  // ★ 关键
	{
		std::lock_guard<std::mutex> lk(cache_mutex); // 或统一用 cache_lock
		const int total = static_cast<int>(cpu_cache.size());

		int end_index = total - offset * raw_count;
		if (end_index < 0) end_index = 0;
		if (end_index > total) end_index = total;

		const int start_index = std::max(0, end_index - raw_count);

		if (start_index < end_index)
		{
			sliced.assign(cpu_cache.begin() + start_index,
				cpu_cache.begin() + end_index);
		}
	}

	// ---- 组装 JSON ----
	Json::Value timestamps(Json::arrayValue);
	Json::Value values(Json::arrayValue);
	Json::Value top_map(Json::objectValue);
	Json::Value series(Json::objectValue);
	Json::Value series_list(Json::arrayValue);
	Json::Value points(Json::arrayValue);

	for (size_t i = 0; i < sliced.size(); i += static_cast<size_t>(step))
	{
		const auto& row = sliced[i];
		const std::string& ts_raw = std::get<0>(row);
		const double usage = std::get<1>(row);
		const Json::Value& tops = std::get<2>(row); // 可能是 null / array

		std::string ts_iso = to_iso_ts(ts_raw);

		timestamps.append(ts_iso);
		values.append(usage);

		Json::Value pt(Json::arrayValue);
		pt.append(ts_iso);
		pt.append(usage);
		points.append(pt);

		if (!tops.isNull() && tops.isArray())
		{
			top_map[ts_iso] = tops;
			top_map[ts_raw] = tops;
		}
	}

	series["CPU"] = values;

	Json::Value one(Json::objectValue);
	one["name"] = "CPU";
	one["type"] = "line";
	one["data"] = values;
	series_list.append(one);

	Json::Value root(Json::objectValue);
	root["timestamps"] = timestamps;
	root["values"] = values;
	root["series"] = series;
	root["top_map"] = top_map;
	root["series_list"] = series_list;
	root["points"] = points;
	root["x"] = timestamps;
	root["y"] = values;

	auto resp = HttpResponse::newHttpJsonResponse(root);
	cb(resp);
}

void TempHistoryApi(const HttpRequestPtr& req,
	std::function<void(const HttpResponsePtr&)>&& cb)
{
	// ---- 参数 ----
	auto range_arg = req->getParameter("range");
	if (range_arg.empty()) range_arg = "1h";
	int offset = 0;
	try
	{
		auto offStr = req->getParameter("offset");
		if (!offStr.empty()) offset = std::stoi(offStr);
	}
	catch (...) { offset = 0; }

	// 分钟映射
	int minutes = 60;
	if (range_arg == "5m") minutes = 5;
	else if (range_arg == "1h") minutes = 60;
	else if (range_arg == "6h") minutes = 360;
	else if (range_arg == "1d") minutes = 1440;
	else if (range_arg == "1w") minutes = 10080;

	// 记录粒度 & 采样数
	const int interval = 5; // 每5秒一条
	int raw_count = std::max(1, (minutes * 60) / interval);

	// 降采样
	int downsample_step = 1;
	if (minutes > 360)   downsample_step = 2;
	if (minutes > 1440)  downsample_step = 4;
	if (minutes > 3000)  downsample_step = 10;
	if (minutes > 7000)  downsample_step = 20;
	const int step = std::max(1, downsample_step);

	// ---- 切片 ----
	std::vector<std::tuple<std::string, Json::Value>> sliced;
	{
		std::lock_guard<std::mutex> lk(cache_mutex);
		const int total = static_cast<int>(temp_cache.size());
		int end_index = total - offset * raw_count;
		if (end_index < 0) end_index = 0;
		const int start_index = std::max(0, end_index - raw_count);
		if (start_index < end_index && start_index < total)
		{
			sliced.assign(temp_cache.begin() + start_index,
				temp_cache.begin() + std::min(end_index, total));
		}
	}

	// ---- 组装 JSON（对齐：缺失填 null）----
	Json::Value timestamps(Json::arrayValue);        // ["YYYY-MM-DD HH:MM:SS", ...]
	Json::Value series(Json::objectValue);           // { name: [v1, null, v3, ...], ... }

	// 便于取长度
	auto get_len = [&](const Json::Value& arr) -> int { return static_cast<int>(arr.size()); };

	for (size_t i = 0; i < sliced.size(); i += static_cast<size_t>(step))
	{
		const auto& row = sliced[i];
		const std::string& ts = std::get<0>(row);      // 原样返回（不转 ISO）
		const Json::Value& temps = std::get<1>(row);   // object: name -> value(double)

		// 追加时间戳
		timestamps.append(ts);

		// 本次样本序号（从 0 开始），也是目标长度-1
		const int t_index = get_len(timestamps) - 1;

		// 先给已有的所有 series 在这一时刻填 null 占位
		auto members_existing = series.getMemberNames();
		for (const auto& name : members_existing)
		{
			series[name].append(Json::nullValue);
		}

		// 写入本次出现的传感器值
		if (temps.isObject())
		{
			auto sensors = temps.getMemberNames();
			for (const auto& name : sensors)
			{
				// 新出现的传感器：需要补齐前面所有时刻的 null
				if (!series.isMember(name))
				{
					series[name] = Json::Value(Json::arrayValue);
					// 补齐到与 timestamps 长度一致（此刻还没写入当前值，所以需要填充 t_index 个 null）
					for (int k = 0; k < t_index; ++k) series[name].append(Json::nullValue);
					// 由于上面“给所有已有 series 先 append null”，新 series 没跟到这一位，
					// 下面直接 append 值即可。
				}
				else
				{
					// 覆盖刚才 append 的 null：先 pop，再写值（保持与 Python 逻辑一致）
					if (get_len(series[name]) == get_len(timestamps))
					{
						series[name].removeIndex(get_len(series[name]) - 1, nullptr);
					}
				}

				// 写入当前值
				double v = temps[name].asDouble();
				series[name].append(v);
			}
		}

		// 确保所有 series 的长度 == timestamps 长度（已在上面流程保证）
		// 若某些传感器本次没有值，它们保持 null（对齐）
	}

	// 返回空结构也要是对象/数组，避免前端 Object.keys 报错
	Json::Value root(Json::objectValue);
	root["timestamps"] = timestamps;                 // []
	root["series"] = series.isObject() ? series : Json::Value(Json::objectValue);

	auto resp = HttpResponse::newHttpJsonResponse(root);
	cb(resp);
}

// 注册： app().registerHandler("/api/traffic-history", apiTrafficHistory, {drogon::Get});
static void apiTrafficHistory(const drogon::HttpRequestPtr& req,
	std::function<void(const drogon::HttpResponsePtr&)>&& callback)
{
	// 读取参数
	std::string macOrName = getQuery(req, "mac", /*def*/"");
	std::string rangeArg = getQuery(req, "range", "1h");
	int offset = 0;
	try { offset = std::stoi(getQuery(req, "offset", "0")); }
	catch (...) { offset = 0; }
	if (macOrName.empty())
	{
		auto resp = drogon::HttpResponse::newHttpJsonResponse(Json::Value{
			{"error", "missing mac or name"}
			});
		resp->setStatusCode(drogon::k400BadRequest);
		return callback(resp);
	}

	// range -> minutes
	static const std::unordered_map<std::string, int> rangeMap{
		{"5m", 5}, {"1h", 60}, {"6h", 360}, {"1d", 1440}, {"1w", 10080}
	};
	int minutes = 60;
	if (auto it = rangeMap.find(rangeArg); it != rangeMap.end()) minutes = it->second;

	const int interval = 5;                         // 每 5 秒一条
	const int raw_count = std::max(1, (minutes * 60) / interval);

	// 降采样步长
	int downsample_step = 1;
	if (minutes > 360)   downsample_step = 2;
	if (minutes > 1440)  downsample_step = 4;
	if (minutes > 3000)  downsample_step = 10;
	if (minutes > 7000)  downsample_step = 20;

	// key 归一化 & 反查 name->mac
	std::string key = toLower(macOrName);
	std::vector<TrafficEntry> full_list;

	{
		std::lock_guard<std::mutex> lk(cache_lock);

		// 直接按 MAC 查
		auto it = traffic_cache.find(key);
		if (it == traffic_cache.end())
		{
			// 反查 name -> mac
			// Python: reverse_map = {v.lower(): k for k, v in name_map.items()}
			std::string macKey = key;
			for (const auto& kv : name_map) // kv.first: mac, kv.second: name
			{
				if (toLower(kv.second) == key) { macKey = toLower(kv.first); break; }
			}
			auto it2 = traffic_cache.find(macKey);
			if (it2 != traffic_cache.end())
				full_list = it2->second;
		}
		else
		{
			full_list = it->second;
		}
	}

	// 切片
	std::vector<TrafficEntry> sliced;
	{
		const int total = static_cast<int>(full_list.size());
		int end_index = std::clamp(total - offset * raw_count, 0, total);
		int start_index = std::max(0, end_index - raw_count);
		if (start_index < end_index)
		{
			sliced.assign(full_list.begin() + start_index,
				full_list.begin() + end_index);
		}
	}

	// 计算速率
	Json::Value records(Json::arrayValue);
	if (!sliced.empty() && downsample_step > 0)
	{
		for (int i = downsample_step; i < static_cast<int>(sliced.size()); i += downsample_step)
		{
			const auto& [t0, up0, dn0] = sliced[i - downsample_step];
			const auto& [t1, up1, dn1] = sliced[i];

			std::time_t tt0 = parseTs(t0);
			std::time_t tt1 = parseTs(t1);
			if (tt0 == static_cast<std::time_t>(-1) || tt1 == static_cast<std::time_t>(-1))
				continue;

			double dt = std::difftime(tt1, tt0);
			if (dt <= 0.0) continue;

			double up_speed = roundTo((static_cast<double>(up1 - up0)) / dt, 2);
			double down_speed = roundTo((static_cast<double>(dn1 - dn0)) / dt, 2);

			Json::Value row(Json::objectValue);
			row["time"] = t1;
			row["upload"] = up_speed;
			row["download"] = down_speed;
			records.append(std::move(row));
		}
	}

	Json::Value out(Json::objectValue);
	out["records"] = std::move(records);

	auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
	resp->setStatusCode(drogon::k200OK);
	callback(resp);
}

// 注册： app().registerHandler("/api/all-hosts", apiAllHosts, {drogon::Get});
static void apiAllHosts(const drogon::HttpRequestPtr& req,
	std::function<void(const drogon::HttpResponsePtr&)>&& callback)
{
	Json::Value hosts(Json::arrayValue);

	{
		std::lock_guard<std::mutex> lk(cache_lock);

		// 遍历 traffic_cache 的 key（MAC）
		for (const auto& kv : traffic_cache)
		{
			const std::string& mac = kv.first;

			Json::Value obj(Json::objectValue);
			if (mac == "__router__")
			{
				obj["mac"] = "__router__";
				obj["name"] = "本机（软路由）";
				obj["ip"] = "127.0.0.1";
			}
			else
			{
				// name: 优先从 name_map 取，没有就回退为 mac
				std::string name = mac;
				if (auto itn = name_map.find(mac); itn != name_map.end())
					name = itn->second;

				// ip: 优先从 mac_ip_map 取，没有就 "N/A"
				std::string ip = "N/A";
				if (auto iti = mac_ip_map.find(mac); iti != mac_ip_map.end())
					ip = iti->second;

				obj["mac"] = mac;
				obj["name"] = name;
				obj["ip"] = ip;
			}

			hosts.append(std::move(obj));
		}
	}

	Json::Value out(Json::objectValue);
	out["hosts"] = std::move(hosts);

	auto resp = drogon::HttpResponse::newHttpJsonResponse(out);
	resp->setStatusCode(drogon::k200OK);
	callback(resp);
}


inline void refreshOnlineIpsOnce()
{
	// now (epoch secs)
	const double now = []()
		{
			using namespace std::chrono;
			return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
		}();

	// up/down 映射：{ ip -> bytes }
	std::unordered_map<std::string, long long> up, down;
	try { up = get_nft_bytes_up(); }
	catch (...) {}
	try { down = get_nft_bytes_down(); }
	catch (...) {}

	// 做 keys 的并集
	std::unordered_set<std::string> keys;
	keys.reserve(up.size() + down.size());
	for (auto& kv : up)   keys.insert(kv.first);
	for (auto& kv : down) keys.insert(kv.first);

	// 更新 ip_stats；并清理超时项
	{
		std::lock_guard<std::mutex> lk(cache_lock);

		// 更新/写入
		for (const auto& ip : keys)
		{
			const long long u = (up.count(ip) ? up[ip] : 0LL);
			const long long d = (down.count(ip) ? down[ip] : 0LL);

			auto it = ip_stats.find(ip);
			if (it == ip_stats.end())
			{
				ip_stats.emplace(ip, IpStat{ u, d, now });
			}
			else
			{
				IpStat& st = it->second;
				if (u != st.up || d != st.down)
				{
					st.last = now;   // 有变化才刷新 last
				}
				st.up = u;
				st.down = d;
			}
		}

		// 过期清理（now - last >= 600）
		for (auto it = ip_stats.begin(); it != ip_stats.end(); )
		{
			if (now - it->second.last >= ONLINE_EXPIRE_SEC)
			{
				it = ip_stats.erase(it);
			}
			else
			{
				++it;
			}
		}
	}
}

static void presenceMonitorOnce()
{
	using clock = std::chrono::system_clock;
	const auto now_tp = clock::now();

	// 1) 从“流量变化”拿在线 IP 集
	std::unordered_set<std::string> flow_online_ips;
	try
	{
		auto v = get_current_online_ips();
		flow_online_ips.insert(v.begin(), v.end());
	}
	catch (...) {}

	// 2) 一份“当前邻居表” IP->MAC
	std::unordered_map<std::string, std::string> ip_mac;
	try { ip_mac = get_ip_mac_map(); }
	catch (...) {}

	// 3) 在线名字集合（ip -> mac -> name），没名字就用 ip
	std::unordered_set<std::string> online_names;
	for (const auto& ip : flow_online_ips)
	{
		std::string name = ip;
		auto itMac = ip_mac.find(ip);
		if (itMac != ip_mac.end())
		{
			auto itName = name_map.find(itMac->second);
			if (itName != name_map.end()) name = itName->second;
		}
		online_names.insert(std::move(name));
	}

	// 4) 逐个跟踪目标判断状态变更
	for (const auto& name : TRACKED_HOSTS)
	{
		if (online_names.count(name))
		{
			// 直接在线：清空失败计数，更新 last_seen
			fail_counts[name] = 0;
			last_seen[name] = now_tp;

			// 如果先前处于“离线计时”，则判定“回家了”
			auto itOff = offline_since.find(name);
			if (itOff != offline_since.end())
			{
				auto left_tp = itOff->second;
				offline_since.erase(itOff);

				auto mins_total = std::chrono::duration_cast<std::chrono::minutes>(now_tp - left_tp).count();
				long long hours = mins_total / 60;
				long long mins = mins_total % 60;

				std::ostringstream oss;
				oss << "🏠 " << name << " 回到家了，离开了 " << hours << " 小时 " << mins << " 分钟";
				send_wechat_msg(oss.str());
			}
			continue;
		}

		// 不在“流量在线集合”中，看看是否在宽限期内
		auto itLast = last_seen.find(name);
		auto delta_sec = itLast == last_seen.end()
			? 0
			: std::chrono::duration_cast<std::chrono::seconds>(now_tp - itLast->second).count();

		if (delta_sec <= GRACE_NO_TRAFFIC_SEC)
		{
			// 宽限期内先不判离线
			continue;
		}

		// 做一次复核（邻居表找 IP；如果 flow_recent 或探测成功则视为 present）
		std::string ip = ipByName(ip_mac, name);
		bool flow_recent = !ip.empty() && (flow_online_ips.count(ip) > 0);

		if (!ip.empty() && isPresent(ip, flow_recent))
		{
			// 复核在线：更新 last_seen，清零失败；如之前在离线计时，推“回家了”
			last_seen[name] = now_tp;
			fail_counts[name] = 0;

			auto itOff = offline_since.find(name);
			if (itOff != offline_since.end())
			{
				auto left_tp = itOff->second;
				offline_since.erase(itOff);

				auto mins_total = std::chrono::duration_cast<std::chrono::minutes>(now_tp - left_tp).count();
				long long hours = mins_total / 60;
				long long mins = mins_total % 60;

				std::ostringstream oss;
				oss << "🏠 " << name << " 回到家了，离开了 " << hours << " 小时 " << mins << " 分钟";
				send_wechat_msg(oss.str());
			}
		}
		else
		{
			// 认为一次失败
			int& fc = fail_counts[name];
			fc += 1;
			if (fc >= FAIL_THRESH && offline_since.find(name) == offline_since.end())
			{
				offline_since[name] = now_tp;
				send_wechat_msg(std::string("🚪 ") + name + " 离开了家");
			}
		}
	}
}

void start_background_RefreshLoop(int presence_interval_sec = 10)
{
	if (presence_interval_sec <= 0) presence_interval_sec = 10;
	const int refresh_interval_sec = 60;

	std::thread([presence_interval_sec, refresh_interval_sec]()
		{
			using clock = std::chrono::steady_clock;
			using seconds = std::chrono::seconds;

			auto now = clock::now();
			auto nextPresence = now;                               // 立刻跑一次 presence
			auto nextRefresh = now + seconds(refresh_interval_sec);

			for (;;)
			{
				now = clock::now();

				// 处理“错过的tick”（避免长时间阻塞后漏跑）
				while (now >= nextPresence)
				{
					presenceMonitorOnce();
					nextPresence += seconds(presence_interval_sec);
				}
				while (now >= nextRefresh)
				{
					refreshOnlineIpsOnce();
					nextRefresh += seconds(refresh_interval_sec);
				}

				// 休眠到下一个最近的任务
				auto wakeAt = std::min(nextPresence, nextRefresh);
				std::this_thread::sleep_until(wakeAt);
			}
		}).detach();
}

int main()
{
	loadRecentHistory();

	start_background_record_thread(5);
	start_background_RefreshLoop(10);

	// 指定静态资源根目录；这样 /static/... 等会直接从 www 下取
	drogon::app().setDocumentRoot("./www");

	// 路由注册（函数指针，不用 lambda）
	drogon::app()
		.registerHandler("/", &Index, { Get })
		.registerHandler("/api/traffic-history", &apiTrafficHistory, { Get })
		.registerHandler("/api/cpu-history", &CpuHistoryApi, { Get })
		.registerHandler("/api/temp-history", &TempHistoryApi, { Get })
		.registerHandler("/api/all-hosts", &apiAllHosts, { Get })
		.addListener("0.0.0.0", 35811)
		.setLogLevel(trantor::Logger::kInfo)
		.run();

	return 0;
}
