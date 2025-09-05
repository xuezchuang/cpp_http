// admin_server.cpp
#include <drogon/drogon.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <regex>
#include <unordered_map>
#include <ctime>
#include <string>
#include <vector>
#include <shared_mutex>

namespace fs = std::filesystem;
using namespace drogon;

// ===== 可选：md4c 预览（Markdown -> HTML）=====
#ifdef USE_MD4C
#include <md4c-html.h>
static void md4c_out(const MD_CHAR* data, MD_SIZE size, void* ud)
{
	auto* out = static_cast<std::string*>(ud);
	out->append(data, size);
}
#endif


// ====== 基本配置 ======
static const std::string kDocRoot = "./public";       // 线上站点静态文件
static const std::string kAdminRoot = "./hexo-admin/www";   // hexo-admin 前端
static const std::string kContentRoot = "./content";      // Markdown 源目录
static const std::string kAdminToken = "changeme-token"; // 简单令牌（建议用环境变量或 Nginx 限制来源）

static const fs::path kSourceRoot = "/root/cpp_http/src/src/blog/content";
static const fs::path kDraftsDir = kSourceRoot / "_drafts";
static const std::string kSiteURL = "http://example.com/";
static std::mutex g_postsMutex;

struct BlogPost  {
	std::string id;
	std::string relPath;   // _posts 下的相对路径
	std::string title;
	std::string description;
	std::vector<std::string> categories;
	std::vector<std::string> tags;
	std::string raw;       // 完整 markdown（含 front-matter）
	std::string body;
	std::string html;      // 预渲染后的 HTML（你要的）
	std::string date_iso;
	size_t      size{ 0 };
	bool needsRebuild{false}; 
	bool publish{false}; 
	bool draft{ true };
};

// —— 全局内存库 ——
inline std::unordered_map<std::string, BlogPost> g_postsById;          // id -> Post（按值存）
inline std::unordered_map<std::string, std::string> g_idByStem;    // stem -> id（可选）

inline std::unordered_map<std::string, std::vector<std::string>> g_postsByTag;
inline std::unordered_map<std::string, std::vector<std::string>> g_postsByCategory;

inline std::shared_mutex g_storeMutex;

// 把 time_t 转成 "YYYY-MM-DDTHH:MM:SS.000Z"
static inline std::string toIso(std::time_t t)
{
	std::tm tm{};
#ifdef _WIN32
	gmtime_s(&tm, &t);
#else
	gmtime_r(&t, &tm);
#endif
	char buf[64];
	std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S.000Z", &tm);
	return std::string(buf);
}

static inline Json::Value split_to_json_array(const std::string& s)
{
	Json::Value arr(Json::arrayValue);
	std::string token;
	std::istringstream iss(s);
	while (std::getline(iss, token, ','))
	{
		// trim
		size_t a = token.find_first_not_of(" \t\r\n");
		size_t b = token.find_last_not_of(" \t\r\n");
		if (a != std::string::npos) token = token.substr(a, b - a + 1);
		if (!token.empty()) arr.append(token);
	}
	return arr;
}

static inline std::string trim(const std::string& s)
{
	size_t a = s.find_first_not_of(" \t\r\n");
	if (a == std::string::npos) return "";
	size_t b = s.find_last_not_of(" \t\r\n");
	return s.substr(a, b - a + 1);
}
static std::string now_iso_z()
{
	using namespace std::chrono;
	const auto now = system_clock::now();
	const auto t = system_clock::to_time_t(now);
	const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

	std::tm tm{};
	gmtime_r(&t, &tm); // 用 UTC
	std::ostringstream oss;
	oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S") << "."
		<< std::setw(3) << std::setfill('0') << ms.count() << "Z";
	return oss.str();
}
static inline bool starts_with(const std::string& s, const std::string& p)
{
	return s.size() >= p.size() && std::equal(p.begin(), p.end(), s.begin());
}
static inline bool is_space_only(const std::string& s)
{
	for (char c : s) if (!(c == ' ' || c == '\t' || c == '\r' || c == '\n')) return false;
	return true;
}
static inline int leading_indent(const std::string& s)
{
	int n = 0; for (char c : s) { if (c == ' ') { ++n; } else if (c == '\t') { n += 2; } else break; } return n;
}
static inline std::string unquote_if_pair(const std::string& s)
{
	if (s.size() >= 2)
	{
		char a = s.front(), b = s.back();
		if ((a == '"' && b == '"') || (a == '\'' && b == '\'')) return s.substr(1, s.size() - 2);
	}
	return s;
}
// 找到不在引号内的第一个冒号位置
static size_t find_colon_outside_quotes(const std::string& line)
{
	bool inSingle = false, inDouble = false;
	for (size_t i = 0; i < line.size(); ++i)
	{
		char c = line[i];
		if (c == '\'' && !inDouble) inSingle = !inSingle;
		else if (c == '"' && !inSingle) inDouble = !inDouble;
		else if (c == ':' && !inSingle && !inDouble) return i;
	}
	return std::string::npos;
}

static std::string read_block_scalar(std::istringstream& fmss, int baseIndent, bool folded)
{
	std::string out; std::string ln;
	std::vector<std::string> lines;
	// 先窥探下一行的实际缩进作为块体起始缩进
	std::streampos save = fmss.tellg();
	int indent = -1;
	while (true)
	{
		std::streampos p = fmss.tellg();
		if (!std::getline(fmss, ln)) { fmss.clear(); fmss.seekg(p); break; }
		if (ln.find("---") == 0) { fmss.clear(); fmss.seekg(p); break; } // 安全
		if (is_space_only(ln)) { lines.push_back(""); continue; }
		int ind = leading_indent(ln);
		if (ind <= baseIndent) { fmss.clear(); fmss.seekg(p); break; }
		indent = ind; lines.push_back(ln.substr(ind)); break;
	}
	if (indent < 0) return ""; // 空块
	// 继续读后续行，直到缩进回落
	while (true)
	{
		std::streampos p = fmss.tellg();
		if (!std::getline(fmss, ln)) break;
		if (is_space_only(ln)) { lines.push_back(""); continue; }
		int ind = leading_indent(ln);
		if (ind < indent) { fmss.clear(); fmss.seekg(p); break; }
		lines.push_back(ln.substr(indent));
	}
	if (!folded)
	{
		// literal | 原样换行
		for (size_t i = 0; i < lines.size(); ++i)
		{
			out += lines[i];
			if (i + 1 < lines.size()) out += "\n";
		}
	}
	else
	{
		// folded > 折叠：空行->换行，非空行之间->空格
		bool prevBlank = false;
		for (size_t i = 0; i < lines.size(); ++i)
		{
			const std::string& s = lines[i];
			bool blank = s.empty();
			if (blank)
			{
				out += "\n";
			}
			else
			{
				if (!out.empty() && !prevBlank) out += " ";
				out += s;
			}
			prevBlank = blank;
		}
	}
	return out;
}

static inline void strip_utf8_bom(std::string& s)
{
	if (s.size() >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF)
		s.erase(0, 3);
}
static inline std::string to_lf(std::string s)
{
	std::string out; out.reserve(s.size());
	for (size_t i = 0; i < s.size(); ++i)
	{
		if (s[i] == '\r') { if (i + 1 < s.size() && s[i + 1] == '\n') continue; else out.push_back('\n'); }
		else out.push_back(s[i]);
	}
	return out;
}
static inline size_t line_end(const std::string& s, size_t i)
{
	while (i < s.size() && s[i] != '\n') ++i;
	return i;
}
static inline std::string trim_view(const std::string& s, size_t L, size_t R)
{
	while (L < R && (s[L] == ' ' || s[L] == '\t')) ++L;
	while (R > L && (s[R - 1] == ' ' || s[R - 1] == '\t')) --R;
	return s.substr(L, R - L);
}
static inline bool line_is_three_dashes(const std::string& s, size_t lineStart, size_t lineEnd)
{
	size_t a = lineStart, b = lineEnd;
	while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
	while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
	return (b - a == 3 && s[a] == '-' && s[a + 1] == '-' && s[a + 2] == '-');
}
static inline size_t find_block_end_dashes(const std::string& s, size_t from)
{
	size_t pos = from, n = s.size();
	while (pos < n)
	{
		size_t le = line_end(s, pos);
		if (line_is_three_dashes(s, pos, le)) return pos;
		pos = (le < n ? le + 1 : le);
	}
	return std::string::npos;
}

// ====== 工具函数 ======
static bool safeRelPath(const std::string& rel)
{
	// 只允许 a-zA-Z0-9_-. /，禁止 ..、\、绝对路径、连续斜杠
	static const std::regex re("^[A-Za-z0-9_./-]+$");
	if (!std::regex_match(rel, re)) return false;
	if (rel.find("..") != std::string::npos) return false;
#ifdef _WIN32
	if (rel.find('\\') != std::string::npos) return false;
#endif
	if (!rel.empty() && (rel[0] == '/')) return false;
	if (rel.find("//") != std::string::npos) return false;
	return true;
}

static std::string readAll(const fs::path& p)
{
	std::ifstream in(p, std::ios::binary);
	if (!in) return {};
	std::ostringstream ss; ss << in.rdbuf(); return ss.str();
}

static bool writeAllAtomic(const fs::path& p, const std::string& data)
{
	try
	{
		fs::create_directories(p.parent_path());
		auto tmp = p; tmp += ".tmp";
		{
			std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
			if (!out) return false;
			out.write(data.data(), (std::streamsize)data.size());
		}
		fs::rename(tmp, p);
		return true;
	}
	catch (...) { return false; }
}

static std::string escapeHtml(const std::string& s, bool keepNewline = true)
{
	std::string out;
	out.reserve(s.size() * 1.1);
	for (char c : s)
	{
		switch (c)
		{
		case '&':  out += "&amp;";  break;
		case '<':  out += "&lt;";   break;
		case '>':  out += "&gt;";   break;
		case '"':  out += "&quot;"; break;
		case '\'': out += "&#39;";  break;
		case '\n': out += (keepNewline ? "<br/>\n" : "\n"); break;
		case '\r': /* skip or keep */ break;
		default:   out.push_back(c); break;
		}
	}
	return out;
}

// ==== helpers ===============================================================

// 给没有 class 的 <pre><code> 补上 language-plaintext
// 给没有 class 的 <pre><code> 补上 language-plaintext
static inline void ensure_code_class(std::string& html)
{
	static const std::regex re(
		R"(<pre\b[^>]*>\s*<code\b(?![^>]*\bclass\s*=)[^>]*>)",
		std::regex::icase
	);

	std::string out;
	out.reserve(html.size());

	std::sregex_iterator it(html.begin(), html.end(), re), end;
	size_t last = 0;

	for (; it != end; ++it)
	{
		const auto& m = *it;
		size_t start = static_cast<size_t>(m.position());
		size_t len = static_cast<size_t>(m.length());

		// 先把上一个匹配到当前匹配开始之间的内容拷贝出来
		out.append(html, last, start - last);

		// 在匹配到的 <code ...> 里插入 class
		std::string tag = m.str();                 // e.g. "<pre ...><code ...>"
		size_t pos = tag.find("<code");
		if (pos != std::string::npos)
		{
			pos += 5; // 跳过 "<code"
			tag.insert(pos, R"( class="highlight plaintext")");
		}
		out += tag;

		last = start + len;
	}

	// 末尾残余
	out.append(html, last, std::string::npos);
	html.swap(out);
}

// 幂等：跳过已处理的 <pre>，并为新处理的 <pre> 打标记
static inline void wrap_codeblocks_next(std::string& html)
{
	// 仅匹配未打标记的 <pre><code>…</code></pre>
	static const std::regex re(
		R"(<pre(?![^>]*\bdata-copy-wrapped\b)[^>]*>\s*<code\b([^>]*)>([\s\S]*?)</code>\s*</pre>)",
		std::regex::icase
	);

	std::string out;
	out.reserve(html.size() * 11 / 10);

	std::sregex_iterator it(html.begin(), html.end(), re), end;
	size_t last = 0;

	for (; it != end; ++it)
	{
		const auto& m = *it;
		const size_t start = static_cast<size_t>(m.position());

		// 额外保护：匹配片段里若已含 container / copy-btn，则跳过
		{
			std::string_view seg(html.data() + start, static_cast<size_t>(m.length()));
			if (seg.find("highlight-container") != std::string_view::npos ||
				seg.find("copy-btn") != std::string_view::npos)
			{
				continue;
			}
		}

		std::string codeAttr = m[1].str();
		std::string inner = m[2].str();

		std::string lang = "plaintext";
		{
			static const std::regex reLang(R"(highlight ([A-Za-z0-9_+-]+))", std::regex::icase);
			std::smatch lm;
			if (std::regex_search(codeAttr, lm, reLang) && lm.size() >= 2)
				lang = lm[1].str();
			else if (codeAttr.find("class=") == std::string::npos)
				codeAttr += R"( class="highlight plaintext")";
		}

		out.append(html, last, start - last);

		// 只包一层，并给 <pre> 打标记
		out += "<div class=\"highlight-container\">";
		//out += "<figure class=\"highlight " + lang + "\">";
		out += "<pre data-copy-wrapped=\"1\"><code" + codeAttr + ">" + inner + "</code></pre>";
		//out += "</figure>";
		 
		out += R"(<div class="copy-btn"><i class="fa fa-clipboard fa-fw"></i></div>)";
		out += "</div>";

		last = start + static_cast<size_t>(m.length());
	}

	out.append(html, last, std::string::npos);
	html.swap(out);
}

// ==== main ==================================================================
static std::string mdToHtml(const std::string& md)
{
#ifdef USE_MD4C
	std::string out;

	unsigned pf = 0;
#ifdef MD_FLAG_TABLES
	pf |= MD_FLAG_TABLES;
#endif
#ifdef MD_FLAG_TASKLISTS
	pf |= MD_FLAG_TASKLISTS;
#endif
#ifdef MD_FLAG_STRIKETHROUGH
	pf |= MD_FLAG_STRIKETHROUGH;
#endif
#ifdef MD_FLAG_UNDERLINE
	pf |= MD_FLAG_UNDERLINE;
#endif
#ifdef MD_FLAG_PERMISSIVEAUTOLINKS
	pf |= MD_FLAG_PERMISSIVEAUTOLINKS;
#endif
#ifdef MD_FLAG_PERMISSIVEURLAUTOLINKS
	pf |= MD_FLAG_PERMISSIVEURLAUTOLINKS;
#endif
#ifdef MD_FLAG_PERMISSIVEWWWAUTOLINKS
	pf |= MD_FLAG_PERMISSIVEWWWAUTOLINKS;
#endif
#ifdef MD_FLAG_LATEXMATHSPANS
	pf |= MD_FLAG_LATEXMATHSPANS;
#endif
#ifdef MD_FLAG_WIKILINKS
	pf |= MD_FLAG_WIKILINKS;
#endif

	unsigned rf = 0;
#ifdef MD_HTML_FLAG_SKIP_UTF8_BOM
	rf |= MD_HTML_FLAG_SKIP_UTF8_BOM;
#endif
#ifdef MD_HTML_FLAG_SMARTYPANTS
	rf |= MD_HTML_FLAG_SMARTYPANTS;
#endif
#if defined(MD_HTML_FLAG_USE_XHTML)
	rf |= MD_HTML_FLAG_USE_XHTML;
#elif defined(MD_HTML_FLAG_XHTML)
	rf |= MD_HTML_FLAG_XHTML;
#endif

	if (md_html(md.c_str(), (MD_SIZE)md.size(), md4c_out, &out, pf, rf) == 0)
	{
		// 1) 给 <code> 兜底 class
		ensure_code_class(out);
		// 2) 包成 NexT 的 highlight-container + copy-btn 结构
		//wrap_codeblocks_next(out);
		return out;
	}
#endif
	// 渲染失败降级为纯文本
	return std::string("<pre>") + escapeHtml(md, /*keepNewline=*/true) + "</pre>";
}

static const std::string& ensurePostHtml(BlogPost& post)
{
	if (post.html.empty())
	{
		post.html = mdToHtml(post.body);        // 懒渲染一次
	}
	return post.html;
}

// 简易替换
static inline void replace_all(std::string& s, const std::string& from, const std::string& to)
{
	if (from.empty()) return;
	size_t pos = 0;
	while ((pos = s.find(from, pos)) != std::string::npos)
	{
		s.replace(pos, from.size(), to);
		pos += to.size();
	}
}

// 安全 HTML 转义（title/desc 用）
static std::string html_escape(const std::string& in)
{
	std::string out; out.reserve(in.size() * 1.1);
	for (char c : in)
	{
		switch (c)
		{
		case '&': out += "&amp;"; break;
		case '<': out += "&lt;"; break;
		case '>': out += "&gt;"; break;
		case '"': out += "&quot;"; break;
		case '\'': out += "&#39;"; break;
		default: out.push_back(c);
		}
	}
	return out;
}

// ISO 日期 -> 仅日期（YYYY-MM-DD）
static std::string iso_date_only(const std::string& iso)
{
	// 期望形如 2025-08-29T15:11:17.022Z
	auto p = iso.find('T');
	return p == std::string::npos ? iso : iso.substr(0, p);
}

struct SiteConfig {
	std::string siteUrl;    // "http://example.com/"
	std::string siteName;   // "xuezc's home"
	std::string publicRoot; // 例如 "/root/myblog/public"
	std::string author;     // 默认作者

	std::string siteSubtitle;
	std::string authorDesc;
	std::string authorAvatar;
	std::string siteHost;
};


// 简单去标签（取纯文本，用于 slug 和 toc 文本）
static inline std::string strip_tags(std::string s)
{
	static const std::regex re("<[^>]*>");
	return std::regex_replace(s, re, "");
}

// 通用版：英文按 slug 规则；非 ASCII(如中文)原样保留；空白/标点 → '-'；合并多余 '-'
static std::string slugify(const std::string& in)
{
	std::string s; s.reserve(in.size());
	auto is_ascii = [](unsigned char c) { return c < 128; };
	auto is_alnum = [](unsigned char c)
		{
			return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
		};

	// 1) 逐字节处理：ASCII 规则化，非 ASCII 直接保留（UTF-8）
	for (size_t i = 0; i < in.size();)
	{
		unsigned char c = (unsigned char)in[i];
		if (is_ascii(c))
		{
			if (is_alnum(c))
			{
				s.push_back((char)std::tolower(c));
			}
			else if (c == '_' || c == '-' || std::isspace(c))
			{
				s.push_back('-'); // 空白/下划线/短横统一成 '-'
			}
			else
			{
				// 其它 ASCII 标点丢弃为分隔符
				s.push_back('-');
			}
			++i;
		}
		else
		{
			// 复制一个完整的 UTF-8 代码点（2~4 字节）
			int len = 1;
			if ((c & 0xE0) == 0xC0) len = 2;
			else if ((c & 0xF0) == 0xE0) len = 3;
			else if ((c & 0xF8) == 0xF0) len = 4;
			s.append(in, i, (size_t)len);
			i += (size_t)len;
		}
	}

	// 2) 合并多余 '-'，去首尾 '-'
	std::string out; out.reserve(s.size());
	bool lastDash = false;
	for (size_t i = 0; i < s.size();)
	{
		unsigned char c = (unsigned char)s[i];
		if (c == '-')
		{
			if (!lastDash) out.push_back('-');
			lastDash = true;
			++i;
		}
		else
		{
			// 复制 UTF-8 代码点或单字节
			if (c < 128)
			{
				out.push_back((char)c);
				++i;
			}
			else
			{
				int len = 1;
				if ((c & 0xE0) == 0xC0) len = 2;
				else if ((c & 0xF0) == 0xE0) len = 3;
				else if ((c & 0xF8) == 0xF0) len = 4;
				out.append(s, i, (size_t)len);
				i += (size_t)len;
			}
			lastDash = false;
		}
	}
	while (!out.empty() && out.front() == '-') out.erase(out.begin());
	while (!out.empty() && out.back() == '-')  out.pop_back();
	if (out.empty()) out = "section";
	return out;
}


struct TocItem { int level; std::string text; std::string id; };

static inline void generate_toc(std::string& html, std::string& toc_html)
{
	static const std::regex reh(R"(<h([1-6])\b([^>]*)>([\s\S]*?)</h\1>)", std::regex::icase);
	static const std::regex re_id(R"delim(\bid\s*=\s*"([^"]*)")delim", std::regex::icase);

	std::vector<TocItem> items;
	std::unordered_map<std::string, int> used;

	std::string out; out.reserve(html.size() * 11 / 10);
	size_t last = 0;

	for (std::sregex_iterator it(html.begin(), html.end(), reh), end; it != end; ++it)
	{
		const auto& m = *it;
		size_t start = (size_t)m.position();
		size_t len = (size_t)m.length();

		int lvl = std::stoi(m[1].str());       // ← 标题层级（不是编号）
		std::string attrs = m[2].str();
		std::string inner = m[3].str();

		// 取或生成 id
		std::smatch idm; std::string id;
		if (std::regex_search(attrs, idm, re_id) && idm.size() >= 2)
		{
			id = idm[1].str();
		}
		else
		{
			std::string text = strip_tags(inner);
			id = slugify(text);
			int n = used[id]++; if (n > 0) id += "-" + std::to_string(n);
			if (attrs.find("id=") == std::string::npos)
			{
				if (attrs.empty() || attrs.back() != ' ') attrs.push_back(' ');
				attrs += "id=\"" + id + "\"";
			}
		}

		items.push_back(TocItem{ lvl, strip_tags(inner), id });

		// 回写带 id 的 <hN>
		out.append(html, last, start - last);
		out += "<h" + std::to_string(lvl) + " " + attrs + ">";
		out += inner;
		out += "</h" + std::to_string(lvl) + ">";
		last = start + len;
	}
	out.append(html, last, std::string::npos);
	html.swap(out);

	if (items.empty()) { toc_html.clear(); return; }

	// ===== 正确编号：按最顶层开始（通常是最小的那个级别） =====
	int base = items[0].level;
	for (auto& it : items) if (it.level < base) base = it.level;

	int counters[7] = { 0 }; // 1..6 有效
	auto make_number = [&](int L)
		{
			counters[L]++;                         // 当前层级 +1
			for (int j = L + 1; j <= 6; ++j)       // 更深层级清零
				counters[j] = 0;

			std::string num;
			for (int j = base; j <= L; ++j)
			{
				if (counters[j] == 0) continue;
				if (!num.empty()) num.push_back('.');
				num += std::to_string(counters[j]);
			}
			return num + "."; // NexT 的样式里尾部通常有个点
		};

	// 扁平 <ol>，用 nav-level-* 控制缩进（NexT 样式认这个）
	std::string toc; toc.reserve(items.size() * 80);
	toc += "<ol class=\"nav\">";
	for (auto& it : items)
	{
		int L = it.level;
		std::string number = make_number(L);

		toc += "<li class=\"nav-item nav-level-" + std::to_string(L) + "\">";
		toc += "<a class=\"nav-link\" href=\"#" + it.id + "\">";
		toc += "<span class=\"nav-number\">" + number + "</span> ";
		toc += "<span class=\"nav-text\">" + it.text + "</span>";
		toc += "</a>";
		toc += "</li>";
	}
	toc += "</ol>";

	toc_html.swap(toc);
}


// 把 toc_html 塞到模板里的第一个 <div class="post-toc ...">…</div> 中
static inline void inject_toc(std::string& page_html, const std::string& toc_html)
{
	// 匹配：<div class="post-toc ...">……</div>   （非贪婪）
	static const std::regex re(
		R"(<div\s+class="post-toc\b[^"]*"\s*>([\s\S]*?)</div>)",
		std::regex::icase
	);
	std::smatch m;
	if (std::regex_search(page_html, m, re))
	{
		// 组装替换：保留开头 <div ...>，中间换成 toc_html，结尾 </div>
		std::string prefix = page_html.substr(0, (size_t)m.position());
		// 找到开头标签结束位置
		size_t open_end = page_html.find('>', (size_t)m.position());
		std::string open_tag = page_html.substr((size_t)m.position(), open_end - (size_t)m.position() + 1);
		std::string suffix = page_html.substr((size_t)m.position() + (size_t)m.length());
		page_html = prefix + open_tag + toc_html + "</div>" + suffix;
	}
	else
	{
		// 找不到容器：兜底——在侧栏末尾插入
		page_html += "\n<!-- TOC (auto) -->\n<div class=\"post-toc\">" + toc_html + "</div>\n";
	}
}


// 读取模板
static bool read_text_file(const std::string& path, std::string& out)
{
	std::ifstream ifs(path, std::ios::binary);
	if (!ifs) return false;
	std::ostringstream ss; ss << ifs.rdbuf();
	out = ss.str();
	return true;
}

// 用 BlogPost 渲染模板
static std::string render_post_html(const BlogPost& post, const SiteConfig& cfg, const std::string& tpl)
{
	std::string html = tpl;

	// ========= 基础字段 =========
	const std::string title = post.title.empty() ? "Untitled" : post.title;
	const std::string desc = post.description;
	const std::string dateIso = post.date_iso.empty() ? "1970-01-01T00:00:00.000Z" : post.date_iso;
	const std::string dateDay = iso_date_only(dateIso);  // 2025-08-30
	const std::string permalink = cfg.siteUrl + "posts/" + post.id + "/";

	// 更新时间（目前直接复用 date_iso，可拓展）
	const std::string dateUpdatedIso = dateIso;
	const std::string dateUpdatedText = dateDay;

	// ========= 分类 HTML =========
	std::string catsHtml;
	if (!post.categories.empty())
	{
		catsHtml += R"(<span class="post-meta-item"><span class="post-meta-item-icon"><i class="far fa-folder"></i></span>
<span class="post-meta-item-text">分类于</span>)";
		for (size_t i = 0; i < post.categories.size(); ++i)
		{
			if (i) catsHtml += " ，";
			catsHtml += "<span itemprop=\"about\" itemscope itemtype=\"http://schema.org/Thing\"><a href=\"/categories/"
				+ post.categories[i] + "/\" itemprop=\"url\" rel=\"index\"><span itemprop=\"name\">"
				+ html_escape(post.categories[i]) + "</span></a></span>";
		}
		catsHtml += "</span>";
	}

	// ========= 标签 HTML =========
	std::string tagsHtml;
	if (!post.tags.empty())
	{
		tagsHtml += R"(<div class="post-tags">)";
		for (auto& t : post.tags)
		{
			tagsHtml += "<a href=\"/tags/" + t + "/\" rel=\"tag\"># " + html_escape(t) + "</a>";
		}
		tagsHtml += "</div>";
	}
	// ========= 替换占位符 =========
	replace_all(html, "{{TITLE}}", html_escape(title));
	replace_all(html, "{{SITENAME}}", html_escape(cfg.siteName));
	replace_all(html, "{{SITE_SUBTITLE}}", html_escape(cfg.siteSubtitle));
	replace_all(html, "{{DESCRIPTION}}", html_escape(desc));
	replace_all(html, "{{URL}}", permalink);
	replace_all(html, "{{HOSTNAME}}", cfg.siteHost.empty() ? "localhost" : cfg.siteHost);

	replace_all(html, "{{AUTHOR_NAME}}", html_escape(cfg.author));
	replace_all(html, "{{AUTHOR_DESC}}", html_escape(cfg.authorDesc));
	replace_all(html, "{{AUTHOR_AVATAR}}",html_escape(cfg.authorAvatar));

	replace_all(html, "{{DATE_PUBLISHED}}", dateIso);
	replace_all(html, "{{DATE_UPDATED}}", dateUpdatedIso);
	replace_all(html, "{{DATE_TEXT}}", dateDay);
	replace_all(html, "{{DATE_UPDATED_TEXT}}", dateUpdatedText);
	replace_all(html, "{{DATE_UPDATED_TEXT_HIDE}}", ""); // 如果没更新字段, 可以替换成 "display:none"

	replace_all(html, "{{YEAR}}", dateDay.substr(0, 4));
	replace_all(html, "{{CONTENT}}", post.html.empty() ? "<p>(空)</p>" : post.html);

	replace_all(html, "{{CATEGORIES_HTML}}", catsHtml);
	replace_all(html, "{{TAGS_HTML}}", tagsHtml);

	// ========= 占位的其它字段（暂时留空） =========
	replace_all(html, "{{PREV_HTML}}", "");
	replace_all(html, "{{NEXT_HTML}}", "");
	replace_all(html, "{{SIDEBAR_LINKS_HTML}}", "");

	// 站点统计（简单填充）
	replace_all(html, "{{COUNT_POSTS}}", std::to_string(g_postsById.size()));
	replace_all(html, "{{COUNT_CATEGORIES}}", std::to_string(g_postsByCategory.size()));
	replace_all(html, "{{COUNT_TAGS}}", std::to_string(g_postsByTag.size()));

	return html;
}

static bool ensure_static_page(BlogPost& post,
	const SiteConfig& cfg,
	const std::string& tplPath,
	std::string* outPath = nullptr)
{
	const std::string dir = cfg.publicRoot + "/posts/" + post.id;
	const std::string file = dir + "/index.html";

	// 1) 如果已存在且没有“需要重建”的标记，直接用
	std::error_code ec;
	if (!post.needsRebuild && std::filesystem::exists(file, ec))
	{
		if (outPath) *outPath = file;
		return true;
	}

	// 1) 渲染正文 HTML（从 markdown -> HTML）
	ensurePostHtml(post);
	
	// 2) 代码块包装（影响正文 HTML）
	wrap_codeblocks_next(post.html);

	// 3) 生成 TOC：会顺带给 <h1~h6> 补 id（修改 post.html 本身）
	std::string toc_html;
	generate_toc(post.html, toc_html);

	// 4) 读模板 & 渲染整页（把 post.html 填进模板，得到整页 page）
	std::string tpl;
	if (!read_text_file(tplPath, tpl)) return false;
	std::string page = render_post_html(post, cfg, tpl);

	// 5) 把 TOC 注入整页（替换模板里的 <div class="post-toc">…</div>）
	inject_toc(page, toc_html);

	// 6) 落盘（原子写）
	std::filesystem::create_directories(dir, ec);
	// 原子写：先写临时文件再 rename，避免半包文件被读到
	const std::string tmp = file + ".tmp";
	{
		std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
		if (!ofs) return false;
		ofs << page;
	}
	std::filesystem::rename(tmp, file, ec);

	post.needsRebuild = false;      // ✅ 已重建
	if (outPath) *outPath = file;
	return true;
}

// 站点配置（实际放到你的初始化里）
static SiteConfig g_siteCfg{
	/*siteUrl=*/   "http://example.com/",
	/*siteName=*/  "xuezc's home",
	/*publicRoot=*/std::filesystem::absolute("public").string(),
	/*author=*/    "xuezc",
	/*siteSubtitle*/ "首页",
	/*authorDesc*/   "在这里，你会了解更多,更透彻",
	/*authorAvatar*/ "",
	/*siteHost*/     "example.com"
};

static bool authOk(const HttpRequestPtr& req)
{
	// 1) 自定义 Token 头/Cookie
	const std::string expect = kAdminToken; // 例如从环境变量读取
	auto t = req->getHeader("X-Admin-Token");
	if (!t.empty() && t == expect) return true;
	auto ck = req->getCookie("X-Admin-Token");
	if (!ck.empty() && ck == expect) return true;

	// 2) Basic Auth
	auto auth = req->getHeader("Authorization");
	if (auth.rfind("Basic ", 0) == 0)
	{
		auto b64 = auth.substr(6);
		auto dec = drogon::utils::base64Decode(b64);
		auto pos = dec.find(':');
		std::string u = pos == std::string::npos ? dec : dec.substr(0, pos);
		std::string p = pos == std::string::npos ? "" : dec.substr(pos + 1);
		if (u == "admin" && p == "admin") return true;
	}
	return false;
}

static inline std::string toLower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
	return s;
}

static void addCORS(const HttpResponsePtr& rsp)
{
	rsp->addHeader("Access-Control-Allow-Origin", "*");
	rsp->addHeader("Access-Control-Allow-Headers", "Content-Type, X-Admin-Token");
	rsp->addHeader("Access-Control-Allow-Methods", "GET,POST,PUT,DELETE,OPTIONS");
}

static void addNoCache(const HttpResponsePtr& r)
{
	r->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
	r->addHeader("Pragma", "no-cache");
	r->addHeader("Expires", "0");
	r->removeHeader("ETag");
	r->removeHeader("Last-Modified");
}

// 小工具
static HttpResponsePtr fileResp(const fs::path& p, const char* mime = nullptr)
{
	auto r = HttpResponse::newHttpResponse();
	try
	{
		if (!fs::exists(p))
		{
			r->setStatusCode(k404NotFound);
			r->setContentTypeCode(CT_TEXT_PLAIN);
			r->setBody("Not found");
			return r;
		}
		auto fr = HttpResponse::newFileResponse(p.string());
		if (mime) fr->setContentTypeString(mime);
		fr->addHeader("Cache-Control", "public, max-age=3600");
		return fr;
	}
	catch (const std::exception& e)
	{
		r->setStatusCode(k500InternalServerError);
		r->setContentTypeCode(CT_TEXT_PLAIN);
		r->setBody(std::string("fs error: ") + e.what());
		return r;
	}
}

static inline std::time_t filetime_to_time_t(std::filesystem::file_time_type ftime)
{
	using namespace std::chrono;
	const auto sctp = time_point_cast<system_clock::duration>(
		ftime - std::filesystem::file_time_type::clock::now() + system_clock::now()
	);
	return system_clock::to_time_t(sctp);
}
static inline std::string unquote(const std::string& s)
{
	if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\'')))
		return s.substr(1, s.size() - 2);
	return s;
}
// 读整个文件为 string（失败则返回空串）
static inline std::string read_all_text(const std::filesystem::path& p)
{
	std::ifstream ifs(p, std::ios::in | std::ios::binary);
	if (!ifs) return {};
	std::ostringstream oss;
	oss << ifs.rdbuf();
	return oss.str();
}

static bool read_file(const fs::path& p, std::string& out)
{
	std::ifstream ifs(p, std::ios::binary);
	if (!ifs) return false;
	std::ostringstream ss; ss << ifs.rdbuf();
	out = std::move(ss).str();
	return true;
}

static const fs::path kPostsDir = fs::path(kContentRoot) / "_posts";
//static const fs::path kPostsDir = fs::path(kContentRoot) / "_posts";

// CRC16-CCITT (XModem) 常见参数：poly=0x1021, init=0x0000
static uint16_t crc16_ccitt(const std::string& s)
{
	uint16_t crc = 0x0000;
	for (unsigned char b : s)
	{
		crc ^= (uint16_t)b << 8;
		for (int i = 0; i < 8; ++i)
		{
			if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
			else              crc <<= 1;
		}
	}
	return crc;
}

static std::string crc16_abbrlink(const std::string& seed, const char* format /*"dec" or "hex"*/)
{
	uint16_t v = crc16_ccitt(seed);
	if (std::string(format) == "hex")
	{
		char buf[8]; snprintf(buf, sizeof(buf), "%x", (unsigned)v);
		return std::string(buf);
	}
	else
	{ // "dec"
		return std::to_string((unsigned)v);
	}
}

static void parse_yaml_list_inline(const std::string& val, std::vector<std::string>& out)
{
	std::string v = trim(val);
	if (v.empty()) return;
	if (v.front() == '[' && v.back() == ']')
	{
		std::string inner = v.substr(1, v.size() - 2);
		std::vector<std::string> tokens;
		std::string cur; bool inQ = false; char qc = 0;
		for (char c : inner)
		{
			if (inQ) { cur.push_back(c); if (c == qc) inQ = false; }
			else
			{
				if (c == '"' || c == '\'') { inQ = true; qc = c; cur.push_back(c); }
				else if (c == ',') { tokens.push_back(trim(cur)); cur.clear(); }
				else cur.push_back(c);
			}
		}
		if (!cur.empty()) tokens.push_back(trim(cur));
		for (auto& t : tokens) out.push_back(unquote(trim(t)));
	}
	else
	{
		out.push_back(unquote(v));
	}
}
// ================== 仅用 BlogPost 的 FM 解析 ==================
// 返回：是否解析到了 front-matter；若 bodyOut 非空，写入正文（去掉 FM 部分）。
// 规则：若有 abbrlink 则写进 post.id；否则用 id；其它字段同名写入。
static bool parse_front_matter(const std::string& md, BlogPost& post)
{
	std::string src = md;
	strip_utf8_bom(src);
	src = to_lf(src);

	// 定位首个非空行
	size_t pos = 0;
	while (pos < src.size())
	{
		size_t le = line_end(src, pos);
		if (!trim_view(src, pos, le).empty()) break;
		pos = (le < src.size() ? le + 1 : le);
	}
	if (pos >= src.size()) { post.body = src; return false; }
	
	size_t le0 = line_end(src, pos);
	std::string ln0 = trim_view(src, pos, le0);

	std::string fmBuf;
	size_t bodyStart = 0;
	if (ln0 == "---")
	{
		size_t fmStart = (le0 < src.size() ? le0 + 1 : le0);
		size_t endStart = find_block_end_dashes(src, fmStart);
		if (endStart == std::string::npos) { post.body  = src; return false; }
		fmBuf = src.substr(fmStart, endStart - fmStart);
		size_t endLineEnd = line_end(src, endStart);
		bodyStart = (endLineEnd < src.size() ? endLineEnd + 1 : endLineEnd);
	}
	else
	{
		size_t endStart = find_block_end_dashes(src, pos);
		if (endStart == std::string::npos) { post.body  = src; return false; }
		fmBuf = src.substr(pos, endStart - pos);
		size_t endLineEnd = line_end(src, endStart);
		bodyStart = (endLineEnd < src.size() ? endLineEnd + 1 : endLineEnd);
	}

	// 逐行解析 fmBuf
	std::istringstream fmss(fmBuf);
	std::string line;
	while (std::getline(fmss, line))
	{
		if (trim(line).empty()) continue;

		size_t posc = find_colon_outside_quotes(line);
		if (posc == std::string::npos) continue;

		std::string key = trim(line.substr(0, posc));
		std::string val = trim(line.substr(posc + 1));

		// 块标量
		if (val == "|" || val == "|-" || val == ">" || val == ">-")
		{
			bool folded = (val[0] == '>');
			int baseIndent = leading_indent(line);
			std::string block = read_block_scalar(fmss, baseIndent, folded);
			if (key == "description") post.description = block;
			else if (key == "title")  post.title = block;
			else if (key == "id")     post.id = block;           // 临时，若稍后发现 abbrlink 则覆盖
			else if (key == "abbrlink") post.id = block;         // abbrlink 优先
			continue;
		}

		// 空值 → 缩进列表
		if (val.empty())
		{
			std::streampos back = fmss.tellg();
			std::string ln2; std::vector<std::string> items;
			int kIndent = leading_indent(line);
			bool consumed = false;
			while (true)
			{
				std::streampos p2 = fmss.tellg();
				if (!std::getline(fmss, ln2)) break;
				if (trim(ln2).empty()) continue;
				int ind = leading_indent(ln2);
				std::string tln = trim(ln2);
				if (ind <= kIndent || tln.empty() || tln[0] != '-') { fmss.clear(); fmss.seekg(p2); break; }
				std::string item = trim(tln.substr(1));
				item = unquote_if_pair(item);
				items.push_back(item);
				consumed = true;
			}
			if (consumed)
			{
				if (key == "categories") post.categories = std::move(items);
				else if (key == "tags")  post.tags = std::move(items);
				else if (key == "description")
				{
					post.description.clear();
					for (size_t i = 0; i < items.size(); ++i) { if (i) post.description.push_back('\n'); post.description += items[i]; }
				}
				continue;
			}
			else
			{
				fmss.clear(); fmss.seekg(back);
			}
		}

		// 行内列表 / 普通键值
		if (key == "categories") { post.categories.clear(); parse_yaml_list_inline(val, post.categories); }
		else if (key == "tags") { post.tags.clear();       parse_yaml_list_inline(val, post.tags); }
		else if (key == "title") { post.title = unquote_if_pair(val); }
		else if (key == "description") { post.description = unquote_if_pair(val); }
		else if (key == "abbrlink") { post.id = unquote_if_pair(val); }  // abbrlink 优先
		else if (key == "id") { if (post.id.empty()) post.id = unquote_if_pair(val); } // 若无 abbrlink 才用 id
		else if (key == "date") { post.date_iso = unquote_if_pair(val); } // 新增：日期
	}

	post.body  = (bodyStart < src.size() ? src.substr(bodyStart) : std::string());

	post.html = mdToHtml(post.body);

	return true;
}

static inline std::string trim_copy(const std::string& s)
{
	size_t L = 0, R = s.size(); while (L < R && (unsigned char)s[L] <= ' ') ++L; while (R > L && (unsigned char)s[R - 1] <= ' ') --R; return s.substr(L, R - L);
}
static inline std::string lower_ascii(std::string s) { for (auto& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a'); return s; }
static std::string make_taxonomy_id(const std::string& nameRaw, const char* prefix, const char* fmt = "hex")
{
	// 统一规范化，避免 "Graph" 与 "graph" 变两个
	std::string nameNorm = lower_ascii(trim_copy(nameRaw));
	return std::string(prefix) + "_" + crc16_abbrlink(std::string(prefix) + ":" + nameNorm, fmt);
}

// ====== 主函数：初始化全站到内存 ======
bool initPosts()
{
	// 扫描目录 & 组装新容器（构建完成后再一次性交换，避免中途读访问看到半成品）
	std::unordered_map<std::string, BlogPost > postsById;
	std::unordered_map<std::string, std::string> idByStem;
	postsById.reserve(256);
	idByStem.reserve(256);

	std::unordered_map<std::string, std::vector<std::string>> postsByTag;
	std::unordered_map<std::string, std::vector<std::string>> postsByCategory;
	postsByTag.reserve(256); postsByCategory.reserve(256);

	if (!fs::exists(kPostsDir))
	{
		// 目录都没有：也算成功，置空即可
	}
	else
	{
		for (auto& e : fs::recursive_directory_iterator(kPostsDir))
		{
			if (!e.is_regular_file()) continue;
			auto ext = e.path().extension().string();
			if (ext != ".md" && ext != ".markdown") continue;

			// 读文件
			std::string md;
			if (!read_file(e.path(), md)) continue;

			// 先构造 BlogPost，解析 front-matter 直接写入 post 字段
			BlogPost p;
			p.draft = false;
			p.publish = true;
			p.raw = md;  // 保留原文
			std::string body; // 如需用到正文，可接住
			parse_front_matter(p.raw, p); // 会填充 p.id(若存在 abbrlink/id)、title/desc/tags/categories
			

			// 文章 id：优先采用 front-matter 里得到的 p.id，否则生成 abbrlink（dec/hex 二选一）
			const std::string stem = e.path().stem().string();
			if (p.id.empty())
			{
				const std::string seed = !p.title.empty() ? p.title : stem;
				p.id = crc16_abbrlink(seed, "dec");  // 你可以改成 "hex"
			}
			// 极少碰撞兜底
			std::string chosen = p.id;
			int salt = 0;
			while (postsById.find(chosen) != postsById.end())
			{
				++salt;
				chosen = crc16_abbrlink(p.id + "#" + std::to_string(salt), "dec");
			}
			p.id = chosen;

			// 路径/时间/大小
			p.relPath = fs::relative(e.path(), kPostsDir).generic_string();
			if (p.date_iso.empty())
			{
				try
				{
					auto ft = fs::last_write_time(e.path());
					auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
						ft - fs::file_time_type::clock::now()
						+ std::chrono::system_clock::now());
					std::time_t mt = std::chrono::system_clock::to_time_t(sctp);
					p.date_iso = toIso(mt);   // 你已有的 toIso() 函数
				}
				catch (...) {}
			}

			// 放入主表 & stem 索引
			postsById.emplace(p.id, p);            // 按值存；如要避免拷贝可 emplace(p.id, std::move(p))
			idByStem.emplace(stem, p.id);

			// === 构建 taxonomy 全局字典 + 反向索引 ===
			// p.tags / p.categories 目前是“名字”，我们这里给它们生成稳定 tagId/catId
			for (const auto& tagName : p.tags)
			{
				if (trim_copy(tagName).empty()) continue;
				std::string tagId = make_taxonomy_id(tagName, "tag", "hex");  // 例如 tag_a8f1
				postsByTag[tagId].push_back(p.id);               // 反向索引：该标签下的文章
			}
			for (const auto& catName : p.categories)
			{
				if (trim_copy(catName).empty()) continue;
				std::string catId = make_taxonomy_id(catName, "cat", "hex");  // 例如 cat_b91e
				postsByCategory[catId].push_back(p.id);
			}
		}
	}

	// 一次性替换到全局（写锁保护）
	{
		std::unique_lock lk(g_storeMutex);
		g_postsById.swap(postsById);
		g_idByStem.swap(idByStem);
		g_postsByTag.swap(postsByTag);
		g_postsByCategory.swap(postsByCategory);
	}
	return true;
}
// 收集 tags / categories（名字→名字）
static void collect_tags_categories(Json::Value& cats, Json::Value& tags)
{
	// categories
	for (const auto& [catName, posts] : g_postsByCategory)
	{
		cats[catName] = catName;   // key = 名字，value = 名字
	}
	// tags
	for (const auto& [tagName, posts] : g_postsByTag)
	{
		tags[tagName] = tagName;
	}
}

static const fs::path CONTENT_ROOT = fs::current_path() / "content";

static inline fs::path absPathFromRel(const std::string& relPath)
{
	return fs::weakly_canonical(CONTENT_ROOT / relPath);
}

static drogon::HttpResponsePtr jsonError(int code, const std::string& msg)
{
	Json::Value j;
	j["ok"] = false;
	j["error"] = msg;
	auto resp = drogon::HttpResponse::newHttpJsonResponse(j);
	resp->setStatusCode(static_cast<drogon::HttpStatusCode>(code));
	return resp;
}

static drogon::HttpResponsePtr jsonOk(const Json::Value& payload)
{
	Json::Value j = payload;
	j["ok"] = true;
	auto resp = drogon::HttpResponse::newHttpJsonResponse(j);
	resp->setStatusCode(drogon::k200OK);
	return resp;
}

static std::string moveRelPathBetween(const std::string& relPath,
	const std::string& fromDir, // "_drafts/"
	const std::string& toDir)   // "_posts/"
{
	// 基于当前 relPath 替换前缀
	if (!starts_with(relPath, fromDir))
		return {}; // 表示方向不合法

	std::string tail = relPath.substr(fromDir.size()); // 去掉前缀后的部分
	return toDir + tail; // 保持文件名不变
}

static drogon::HttpResponsePtr doMoveAndUpdate(BlogPost& post,
	const std::string& fromDir,
	const std::string& toDir)
{
	// 计算新旧路径
	const std::string& rel = fromDir + post.relPath;
	auto newRel = toDir + post.relPath;

	fs::path src = absPathFromRel(rel);
	fs::path dst = absPathFromRel(newRel);

	// 基础存在性检查
	if (!fs::exists(src))
	{
		return jsonError(404, "源文件不存在: " + src.string());
	}
	if (fs::exists(dst))
	{
		// 目标已存在，返回冲突。你也可以改成自动加后缀避免冲突。
		return jsonError(409, "目标已存在: " + dst.string());
	}

	// 确保目标父目录存在
	try
	{
		fs::create_directories(dst.parent_path());
	}
	catch (const std::exception& e)
	{
		return jsonError(500, std::string("创建目录失败: ") + e.what());
	}

	// 执行移动（重命名）
	try
	{
		fs::rename(src, dst);
	}
	catch (const std::exception& e)
	{
		return jsonError(500, std::string("移动文件失败: ") + e.what());
	}

	// 更新内存对象
	//post.relPath = newRel;
	post.needsRebuild = true; // 提示后续重建

	Json::Value payload;
	payload["id"] = post.id;
	payload["published"] = post.publish;
	payload["isDraft"] = post.draft;

	return jsonOk(payload);
}

static void handlePublish(const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& cb,const std::string& id)
{
	std::lock_guard<std::mutex> lk(g_postsMutex);
	auto it = g_postsById.find(id);
	if (it == g_postsById.end())
	{
		cb(jsonError(404, "未找到文章 id=" + id));
		return;
	}
	auto& post = it->second;
	post.draft = false;
	post.publish = true;
	cb(doMoveAndUpdate(post, "_drafts/", "_posts/"));
}

static void handleUnpublish(const drogon::HttpRequestPtr& req,std::function<void(const drogon::HttpResponsePtr&)>&& cb,const std::string& id)
{

	std::lock_guard<std::mutex> lk(g_postsMutex);
	auto it = g_postsById.find(id);
	if (it == g_postsById.end())
	{
		cb(jsonError(404, "未找到文章 id=" + id));
		return;
	}
	auto& post = it->second;
	post.draft = true;
	post.publish = false;
	cb(doMoveAndUpdate(post, "_posts/", "_drafts/"));
}

// ====== 主程序 ======
int main()
{
	initPosts();

    // 1) 路由前拦截 /admin/** 的静态资源（仅 GET/HEAD），但**跳过 /admin/api/****
    app().registerPreRoutingAdvice(
        [](const HttpRequestPtr& req, AdviceCallback&& cb, AdviceChainCallback&& next) {
            const auto& p = req->path();
            const auto m  = req->method();

            // 只处理 GET/HEAD 的静态访问
            if (m != Get && m != Head) { next(); return; }

            // ✳️ 不拦截你的 API：/admin/api/** 全部放行
            if (p.rfind("/admin/api/", 0) == 0) { next(); return; }

            // /admin 或 /admin/ -> login/index.html
			if (p.rfind("/admin", 0) == 0 && (p == "/admin" || p == "/admin/")) 
			{
				if (!req)
				{
					std::cerr << "[authOk] req == nullptr !!!\n";
					return ;
				}
				std::cerr << "[authOk] req=" << req.get()
					<< " use_count=" << req.use_count() << "\n";

				const bool loggedIn = authOk(req);
				auto file = fs::path(kAdminRoot) / (loggedIn ? "index.html" : "login/index.html");
				auto r = HttpResponse::newFileResponse(file.string());
				r->setContentTypeCode(CT_TEXT_HTML);
				std::cerr << "[HIT] /admin -> " << (loggedIn ? "index.html" : "login/index.html") << "\n";
				cb(r);
				return;
			}

            // /admin/xxx... -> 映射到磁盘 kAdminRoot/xxx...
            if (p.rfind("/admin/", 0) == 0) {
                std::string rel = p.substr(sizeof("/admin/") - 1); // 去掉 "/admin/"
                auto root = fs::weakly_canonical(fs::path(kAdminRoot));
                auto want = fs::weakly_canonical(root / rel);

                // 防目录穿越
                if (want.native().rfind(root.native(), 0) != 0) {
                    auto r = HttpResponse::newHttpResponse();
                    r->setStatusCode(k403Forbidden);
                    r->setContentTypeCode(CT_TEXT_PLAIN);
                    r->setBody("Forbidden");
                    cb(r); return;
                }

                if (fs::is_directory(want)) want /= "index.html";
                if (!fs::exists(want)) {
                    std::cerr << "[MISS] not exists: " << want << "\n";
                    auto r = HttpResponse::newHttpResponse();
                    r->setStatusCode(k404NotFound);
                    r->setContentTypeCode(CT_TEXT_PLAIN);
                    r->setBody("Not found");
                    cb(r); return;
                }

                auto r = HttpResponse::newFileResponse(want.string());
                // 常见 MIME（可选）
                auto ext = toLower(want.extension().string());
                if (ext == ".js")    r->setContentTypeString("application/javascript");
                if (ext == ".css")   r->setContentTypeString("text/css");
                if (ext == ".png")   r->setContentTypeString("image/png");
                if (ext == ".jpg" || ext == ".jpeg") r->setContentTypeString("image/jpeg");
                if (ext == ".gif")   r->setContentTypeString("image/gif");
                if (ext == ".svg")   r->setContentTypeString("image/svg+xml");
                if (ext == ".ttf")   r->setContentTypeString("font/ttf");
                if (ext == ".woff")  r->setContentTypeString("font/woff");
                if (ext == ".woff2") r->setContentTypeString("font/woff2");

                r->addHeader("Cache-Control", "public, max-age=3600");
                std::cerr << "[HIT] " << p << " -> " << want << "\n";
                cb(r);
                return;
            }

			if (p.rfind("/css/normalize.css", 0) == 0 ||
				p.rfind("/css/screen.css", 0) == 0 ||
				p.rfind("/vendor/", 0) == 0 ||     // /vendor/...
				p.rfind("/fonts/", 0) == 0 ||      // /fonts/...
				p == "/bundle.js" || p == "/bundle.css" ||
				p == "/logo.png")
			{
				// 映射到 kAdminRoot 下
				auto rel = p.substr(1); // 去掉开头的 '/'
				auto want = fs::weakly_canonical(fs::path(kAdminRoot) / rel);

				if (!fs::exists(want))
				{
					auto r = HttpResponse::newHttpResponse();
					r->setStatusCode(k404NotFound);
					r->setContentTypeCode(CT_TEXT_PLAIN);
					r->setBody("Not found: " + want.string());
					cb(r);
					return;
				}

				auto r = HttpResponse::newFileResponse(want.string());

				// 简单 MIME
				auto ext = toLower(want.extension().string());
				if (ext == ".js")    r->setContentTypeString("application/javascript");
				if (ext == ".css")   r->setContentTypeString("text/css");
				if (ext == ".png")   r->setContentTypeString("image/png");
				if (ext == ".ttf")   r->setContentTypeString("font/ttf");
				if (ext == ".woff")  r->setContentTypeString("font/woff");
				if (ext == ".woff2") r->setContentTypeString("font/woff2");

				r->addHeader("Cache-Control", "public, max-age=3600");
				cb(r);
				return;
			}

            next();
        }
    );

	// 1) 公开站点静态目录（/ -> ./public）
	app().setDocumentRoot(kDocRoot);

	// 2) 托管 /admin/ 前端（把 hexo-admin 当静态文件）	Drogon 只有一个 documentRoot，这里用一个“兜底路由”来读文件：
	app().registerHandler("/admin", [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb)
		{
			auto p = fs::path(kAdminRoot) / "login/index.html";   // 你原来的入口
			auto r = HttpResponse::newFileResponse(p.string());
			r->setContentTypeCode(CT_TEXT_HTML);
			cb(r);
	
		}, { Get });

	app().registerHandler("/admin", [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb)
		{
			if (req->method() != Post)
			{
				auto r = HttpResponse::newHttpResponse();
				r->setStatusCode(k405MethodNotAllowed);
				return cb(r);
			}

			// 取 Content-Type，兼容 application/json 和 x-www-form-urlencoded
			const auto ctype = req->getHeader("Content-Type");
			std::string user, pass;

			if (ctype.find("application/json") != std::string::npos)
			{
				Json::Value body; Json::CharReaderBuilder rb; std::string errs;
				const auto& s = req->getBody();
				std::unique_ptr<Json::CharReader> rd(rb.newCharReader());
				if (rd->parse(s.data(), s.data() + s.size(), &body, &errs))
				{
					user = body.get("username", "").asString();
					pass = body.get("password", "").asString();
				}
			}
			else
			{
				// 视作表单：application/x-www-form-urlencoded
					// 简单解析 a=b&c=d
				auto decode = [](std::string v) { return drogon::utils::urlDecode(v); };
				std::string s(req->getBody());
				size_t p = 0;
				while (p < s.size())
				{
					auto eq = s.find('=', p);
					auto amp = s.find('&', p);
					if (eq == std::string::npos) break;
					std::string k = s.substr(p, eq - p);
					std::string v = s.substr(eq + 1, (amp == std::string::npos ? s.size() : amp) - (eq + 1));
					k = decode(k); v = decode(v);
					if (k == "username") user = v;
					if (k == "password") pass = v;
					if (amp == std::string::npos) break;
					p = amp + 1;
				}
			}

			// 校验（替换为你的账号逻辑，或用环境变量）
			const char* U = std::getenv("ADMIN_USER");
			const char* P = std::getenv("ADMIN_PASS");
			const std::string okU = U ? U : "admin";
			const std::string okP = P ? P : "admin";

			if (user == okU && pass == okP)
			{
				// 登录成功：下发 cookie/token
				auto r = HttpResponse::newHttpResponse();
				r->setStatusCode(k302Found);
				r->addHeader("Location", "/admin/");

				// 你前端如果读的是这个 cookie，就保持一致；否则改名字
				Cookie ck("X-Admin-Token", "changeme-token");
				ck.setHttpOnly(true);
				ck.setPath("/");
				r->addCookie(ck);

				return cb(r);
			}
			else
			{
				auto r = HttpResponse::newHttpResponse();
				r->setStatusCode(k401Unauthorized);
				r->setContentTypeCode(CT_TEXT_PLAIN);
				r->setBody("bad credentials");
				return cb(r);
			}

		}, { Post });

	// 3) 一个简单的“兼容型” API（路径你可按 hexo-admin 的请求改名）
	// 3.1 预检（CORS）
	app().registerHandler("/admin/api/{1}", [](const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> &&cb, std::string)
		{
			if (req->method() == Options)
			{
				auto r = HttpResponse::newHttpResponse();
				addCORS(r);
				cb(r);
			}
			else
			{
				// 非 OPTIONS 走各自 handler
				auto r = HttpResponse::newHttpResponse();
				r->setStatusCode(k405MethodNotAllowed);
				cb(r);
			}
		}, { Options });

	// 3.2 列出 Markdown（GET /admin/api/list）
	app().registerHandler("/admin/api/pages/list", [](const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> &&cb)
		{
			if (!authOk(req))
			{
				auto r = HttpResponse::newHttpResponse();
				r->setStatusCode(k401Unauthorized);
				cb(r);
				return;
			}

			Json::Value out;
			//out["posts"] = posts;   // 🔑 前端需要的是 posts，不是 items

			auto r = HttpResponse::newHttpJsonResponse(out);
			addCORS(r);
			cb(r);
		}, { Get });

	//4.4
	app().registerHandler("/admin/api/posts/list",[](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb)
		{
			if (!authOk(req))
			{
				auto r = HttpResponse::newHttpResponse();
				r->setStatusCode(k401Unauthorized);
				cb(r);
				return;
			}

			Json::Value arr(Json::arrayValue);
			bool first = true;
			for (const auto& [id, post] : g_postsById)
			{
				Json::Value p(Json::objectValue);
				p["title"] = post.title;
				p["date"] = post.date_iso;
				p["abbrlink"] = post.id;
				p["published"] = post.publish;
				p["isDraft"] = post.draft;
				p["isDiscarded"] = false;

				   
				
				//p["_content"]    = "";                                  // markdown 正文，先空
				//p["source"] = "_posts/" + post.relPath;            // 按以前约定
				//p["raw"] = post.raw.substr(0, 50) + "...";      // 原文，先截断点
				//p["slug"] = "slug-" + id;                        // 随便拼个 slug

				p["_id"] = id;                                  // 直接用 id

				//if (first)
				//{
				//	// 👇 这里就是第一个
				p["content"] = post.html;// ensurePostHtml(post);
				//	first = false; // 之后都不是第一个了
				//}

				

				//p["excerpt"] = "";
				//p["author"]      = "Unknown";  
				//p["description"] = post.description.empty() ? "" : post.description;

				//p["more"] = post.html.empty() ? "<p>preview</p>" : post.html;
				p["path"] = "posts/" + id + "/";
				//p["permalink"] = "http://localhost:4000/" + p["path"].asString();
				// 
				//p["full_source"] = "/abs/path/" + post.relPath;         // 随便拼
				//p["asset_dir"] = "/abs/path/assets/";
				//p["updated"] = post.date_iso;                       // 先复用 date
				//p["comments"] = true;
				//p["layout"] = "post";
				//p["photos"] = Json::Value(Json::arrayValue);
				arr.append(p);
			}

			// ✅ 直接返回“数组”，不包对象
			//auto r = HttpResponse::newHttpJsonResponse(arr);
			//r->setContentTypeCode(CT_APPLICATION_JSON);
			//cb(r);

			Json::StreamWriterBuilder builder;
			builder["emitUTF8"] = true;   // ✅ 保证输出原始 UTF-8 中文
			std::string jsonBody = Json::writeString(builder, arr);

			auto r = HttpResponse::newHttpResponse();
			r->setContentTypeString("application/json; charset=utf-8");  // ✅ 带上 charset
			r->setBody(std::move(jsonBody));
			cb(r);

		}, { Get });

	// 3.3 读取文件（GET /admin/api/get?path=xxx.md）
	app().registerHandler("/admin/api/get", [](const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> &&cb)
		{
			if (!authOk(req)) { auto r = HttpResponse::newHttpResponse(); r->setStatusCode(k401Unauthorized); cb(r); return; }
			auto rel = req->getParameter("path");
			auto r = HttpResponse::newHttpResponse();
			addCORS(r);
			if (!safeRelPath(rel)) { r->setStatusCode(k400BadRequest); r->setBody("bad path"); cb(r); return; }
			fs::path p = fs::path(kContentRoot) / rel;
			std::string content = readAll(p);
			if (content.empty() && !fs::exists(p)) { r->setStatusCode(k404NotFound); r->setBody("not found"); cb(r); return; }
			Json::Value out;
			out["path"] = fs::relative(p, kContentRoot).generic_string();
			out["content"] = content;
			cb(HttpResponse::newHttpJsonResponse(out));
		}, { Get });

	// 3.4 保存/新建（POST /admin/api/save） body: { "path": "...", "content": "..." }
	app().registerHandler("/admin/api/save", [](const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> &&cb)
		{
			if (!authOk(req)) { auto r = HttpResponse::newHttpResponse(); r->setStatusCode(k401Unauthorized); cb(r); return; }
			Json::Value body; Json::CharReaderBuilder rb;
			std::string errs; auto s = req->getBody();
			std::unique_ptr<Json::CharReader> rd(rb.newCharReader());
			if (!rd->parse(s.data(), s.data() + s.size(), &body, &errs) || !body.isMember("path"))
			{
				auto r = HttpResponse::newHttpResponse(); r->setStatusCode(k400BadRequest); r->setBody("bad json"); cb(r); return;
			}
			std::string rel = body["path"].asString();
			std::string content = body.get("content", "").asString();
			auto r = HttpResponse::newHttpResponse(); addCORS(r);
			if (!safeRelPath(rel)) { r->setStatusCode(k400BadRequest); r->setBody("bad path"); cb(r); return; }
			fs::path p = fs::path(kContentRoot) / rel;
			if (!writeAllAtomic(p, content)) { r->setStatusCode(k500InternalServerError); r->setBody("write fail"); cb(r); return; }
			r->setContentTypeCode(CT_APPLICATION_JSON);
			r->setBody("{\"ok\":true}");
			cb(r);
		}, { Post });

	// 3.5 删除（DELETE /admin/api/delete?path=xxx.md）
	app().registerHandler("/admin/api/delete", [](const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> &&cb)
		{
			if (!authOk(req)) { auto r = HttpResponse::newHttpResponse(); r->setStatusCode(k401Unauthorized); cb(r); return; }
			auto rel = req->getParameter("path");
			auto r = HttpResponse::newHttpResponse(); addCORS(r);
			if (!safeRelPath(rel)) { r->setStatusCode(k400BadRequest); r->setBody("bad path"); cb(r); return; }
			fs::path p = fs::path(kContentRoot) / rel;
			std::error_code ec; bool ok = fs::remove(p, ec);
			if (!ok && ec) { r->setStatusCode(k500InternalServerError); r->setBody("delete fail"); cb(r); return; }
			r->setContentTypeCode(CT_APPLICATION_JSON);
			r->setBody("{\"ok\":true}");
			cb(r);
		}, { Delete });

	app().registerHandler("/admin/api/settings/list", [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb)
		{
			Json::Value root;
			root["options"]["overwriteImages"] = true;
			root["options"]["spellcheck"] = true;
			root["options"]["lineNumbers"] = true;
			root["options"]["askImageFilename"] = true;
			root["options"]["imagePath"] = "/images";

			root["editor"]["inputStyle"] = "contenteditable";
			root["editor"]["spellcheck"] = true;
			root["editor"]["lineNumbers"] = true;

			auto resp = HttpResponse::newHttpJsonResponse(root);
			resp->setStatusCode(k200OK);
			cb(resp);
		}, { Get });

	app().registerHandler(
		"/admin/api/tags-categories-and-metadata",
		[](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb)
		{
			Json::Value root;
			root["categories"] = Json::Value(Json::objectValue);
			root["tags"] = Json::Value(Json::objectValue);

			for (const auto& [catName, posts] : g_postsByCategory)
			{
				root["categories"][catName] = catName;   // key=名字, value=名字
			}
			// tags
			for (const auto& [tagName, posts] : g_postsByTag)
			{
				root["tags"][tagName] = tagName;
			}

			// metadata 先固定返回 description
			root["metadata"] = Json::Value(Json::arrayValue);
			root["metadata"].append("description");

			auto resp = HttpResponse::newHttpJsonResponse(root);
			resp->setStatusCode(k200OK);
			cb(resp);
		},
		{ Get });

	app().registerHandler(
		"/admin/api/posts/{1}",
		[](const HttpRequestPtr& req,std::function<void(const HttpResponsePtr&)>&& cb,const std::string& id)
		{
			if (req->method() == Get)
			{
				// 1) 查找文章
				BlogPost* postPtr = nullptr;
				{
#ifdef USE_STORE_LOCK
					std::shared_lock lk(g_storeMutex);
#endif
					auto it = g_postsById.find(id);
					if (it != g_postsById.end())
					{
						// 取可写引用用于懒渲染缓存
						postPtr = &const_cast<BlogPost&>(it->second);
					}
				}
				if (!postPtr)
				{
					auto r = HttpResponse::newHttpResponse();
					r->setStatusCode(k404NotFound);
					cb(r);
					return;
				}
				BlogPost& post = *postPtr;

				// 2) 懒渲染 HTML
				const std::string& html = ensurePostHtml(post);

				// 3) 组装最小可渲染 JSON
				Json::Value p(Json::objectValue);
				p["title"] = post.title.empty() ? "Untitled" : post.title;
				p["date"] = post.date_iso.empty() ? "1970-01-01T00:00:00.000Z" : post.date_iso;
				p["abbrlink"] = post.id;
				p["published"] = post.publish;
				p["isDraft"] = post.draft;
				p["isDiscarded"] = false;
				p["_id"] = post.id;
				p["path"] = "posts/" + post.id + "/";
				p["source"] = "_posts/" + post.relPath;   // 按你旧约定
				p["raw"] = post.raw;                   // 如不需要可去掉
				p["content"] = html;                       // 关键：详情页 HTML

				// tags / categories（名字数组）
				{
					Json::Value tags(Json::arrayValue);
					for (const auto& t : post.tags) tags.append(t);
					p["tags"] = tags;

					Json::Value cats(Json::arrayValue);
					for (const auto& c : post.categories) cats.append(c);
					p["categories"] = cats;
				}

				// 4) 输出 UTF-8 JSON
				Json::StreamWriterBuilder builder;
				builder["emitUTF8"] = true;
				std::string jsonBody = Json::writeString(builder, p);

				auto r = HttpResponse::newHttpResponse();
				r->setContentTypeString("application/json; charset=utf-8");
				r->setBody(std::move(jsonBody));
				cb(r);
			}
			else
			{
				// 1) 找文章
				BlogPost* postPtr = nullptr;
				{
#ifdef USE_STORE_LOCK
					std::shared_lock lk(g_storeMutex);
#endif
					auto it = g_postsById.find(id);
					if (it != g_postsById.end())
					{
						postPtr = &it->second;
					}
				}
				if (!postPtr)
				{
					auto r = HttpResponse::newHttpResponse();
					r->setStatusCode(k404NotFound);
					cb(r);
					return;
				}
				BlogPost& post = *postPtr;

				// 2) 解析请求 JSON
				Json::Value in;
				try
				{
					if (auto jsonPtr = req->getJsonObject()) in = *jsonPtr;
				}
				catch (...) {}

				if (in.isMember("title"))       post.title = in["title"].asString();
				if (in.isMember("description")) post.description = in["description"].asString();
				if (in.isMember("_content"))
				{
					post.body = in["_content"].asString();
					post.html = mdToHtml(post.body);
				}
				//if (in.isMember("content"))		post.html = in["content"].asString();
				if (in.isMember("tags"))
				{
					post.tags.clear();
					for (auto& t : in["tags"]) post.tags.push_back(t.asString());
				}
				if (in.isMember("categories"))
				{
					post.categories.clear();
					for (auto& c : in["categories"]) post.categories.push_back(c.asString());
				}

				// 3) 更新 raw（重新拼 front-matter + body）
				std::ostringstream fm;
				fm << "---\n"
					<< "title: " << post.title << "\n";
				if (!post.description.empty())
					fm << "description: " << post.description << "\n";
				if (!post.tags.empty())
				{
					fm << "tags:\n";
					for (auto& t : post.tags) fm << "- " << t << "\n";
				}
				if (!post.categories.empty())
				{
					fm << "categories:\n";
					for (auto& c : post.categories) fm << "- " << c << "\n";
				}
				fm << "---\n";
				post.raw = fm.str() + post.body;

				// 写回文件
				try
				{
					fs::path absPath = kSourceRoot / post.relPath;
					std::ofstream ofs(absPath, std::ios::binary | std::ios::trunc);
					ofs << post.raw;
				}
				catch (...)
				{
					// 出错可以返回 500
				}

				post.needsRebuild = true;

				// 4) 重新渲染 HTML（懒渲染缓存失效）
				//post.html.clear();
				const std::string& html = ensurePostHtml(post);

				// 5) 返回 JSON
				Json::Value jp(Json::objectValue);
				jp["title"] = post.title;
				jp["description"] = post.description;
				jp["_content"] = post.body;
				jp["raw"] = post.raw;
				jp["_id"] = post.id;
				jp["abbrlink"] = post.id;
				jp["path"] = "posts/" + post.id + "/";
				jp["content"] = html;

				// tags / categories
				{
					Json::Value tags(Json::arrayValue);
					for (auto& t : post.tags) tags.append(t);
					jp["tags"] = tags;

					Json::Value cats(Json::arrayValue);
					for (auto& c : post.categories) cats.append(c);
					jp["categories"] = cats;
				}

				// tagsCategoriesAndMetadata
				Json::Value tcm(Json::objectValue);
				tcm["categories"] = Json::Value(Json::objectValue);
				tcm["tags"] = Json::Value(Json::objectValue);
				collect_tags_categories(tcm["categories"], tcm["tags"]);
				tcm["metadata"] = Json::Value(Json::arrayValue);
				tcm["metadata"].append("description");

				Json::Value root(Json::objectValue);
				root["post"] = jp;
				root["tagsCategoriesAndMetadata"] = tcm;

				Json::StreamWriterBuilder b; b["emitUTF8"] = true;
				auto body = Json::writeString(b, root);
				auto r = HttpResponse::newHttpResponse();
				r->setStatusCode(k200OK);
				r->setContentTypeString("application/json; charset=utf-8");
				r->setBody(std::move(body));
				cb(r);
			}
		},
		{ Get,Post }
	);

	//app().registerHandler(
	//	"/admin/api/posts/{1}/unpublish",
	//	[](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb)
	//	{
	//	},
	//	{ Post }
	//);

	app().registerHandler(
		"/admin/api/posts/new",
		[](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb)
		{
			// 1) 解析输入 JSON
			Json::Value in;
			try
			{
				auto jsonPtr = req->getJsonObject();
				if (jsonPtr) in = *jsonPtr;
			}
			catch (...) {}
			const std::string title = in.isMember("title") ? in["title"].asString() : "";
			const std::string author = in.isMember("author") ? in["author"].asString() : "unknown";

			if (title.empty())
			{
				auto r = HttpResponse::newHttpResponse();
				r->setStatusCode(k400BadRequest);
				r->setContentTypeCode(CT_TEXT_PLAIN);
				r->setBody("title is required");
				cb(r);
				return;
			}

			// 2) 生成 slug / 时间 / id
			const std::string slug = slugify(title);
			const std::string nowIso = now_iso_z();
			std::string _id = "cm" + crc16_abbrlink(slug + now_iso_z(), "hex");

			//const std::string _id = gen_short_id();

			// 3) 路径与文件
			fs::create_directories(kDraftsDir);
			const fs::path mdPath = kDraftsDir / (slug + ".md");
			const fs::path assetDir = kDraftsDir / slug;

			// front-matter 原样与示例对齐
			std::ostringstream fm;
			fm << "---\n"
				<< "title: " << title << "\n"
				<< "author: " << author << "\n"
				<< "tags:\n"
				<< "---\n";

			// 4) 写入草稿文件（覆盖或新建）
			{
				std::ofstream ofs(mdPath, std::ios::binary | std::ios::trunc);
				ofs << fm.str();
			}
			std::error_code ec;
			fs::create_directories(assetDir, ec);

			// 5) 同步写入内存（草稿期用 _id 做 key；relPath 放在 _drafts 下）
			BlogPost post;
			post.id = _id;                             // ⚠️ 草稿期主键 = _id；发布时改成 abbrlink
			post.relPath = slug + ".md";
			post.title = title;
			post.description.clear();
			post.categories.clear();
			post.tags.clear();
			post.raw = fm.str();                        // 仅 FM；正文为空
			post.body = "";                              // 草稿初始无正文
			post.html = "";                              // 懒渲染
			post.date_iso = nowIso;
			try { post.size = (size_t)fs::file_size(mdPath); }
			catch (...) { post.size = 0; }

			{
#ifdef USE_STORE_LOCK
				std::unique_lock lk(g_storeMutex);
#endif
				g_postsById.emplace(post.id, post);
				g_idByStem.emplace(slug, post.id);
			}

			// 6) 组织返回 JSON（与你样例一致）
			Json::Value out(Json::objectValue);
			out["title"] = title;
			out["author"] = author;
			out["_content"] = "";                                  // 正文为空
			out["source"] = (fs::path("_drafts") / (slug + ".md")).generic_string();
			out["raw"] = fm.str();
			out["slug"] = slug;
			out["published"] = post.publish;
			out["date"] = nowIso;
			out["updated"] = nowIso;
			out["comments"] = true;
			out["layout"] = "post";
			out["photos"] = Json::Value(Json::arrayValue);
			out["_id"] = _id;
			// 还未有 abbrlink，因此按你的样例返回 undefined
			out["path"] = "posts/undefined/";
			out["permalink"] = kSiteURL + out["path"].asString();
			out["full_source"] = (kSourceRoot / out["source"].asString()).generic_string();
			out["asset_dir"] = (kDraftsDir / slug).generic_string() + "/"; // 末尾 /
			out["tags"] = Json::Value(Json::arrayValue);
			out["categories"] = Json::Value(Json::arrayValue);
			out["isDraft"] = post.draft;
			out["isDiscarded"] = false;


			// 7) 返回
			Json::StreamWriterBuilder b;
			b["emitUTF8"] = true;
			auto body = Json::writeString(b, out);
			auto r = HttpResponse::newHttpResponse();
			r->setStatusCode(k200OK);
			r->setContentTypeString("application/json; charset=utf-8");
			r->setBody(std::move(body));
			cb(r);
		},
		{ Post }
	);

	app().registerHandler(
		"/admin/api/posts/{1}/publish",
		[](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb,const std::string& id)
		{
			handlePublish(req, std::move(cb),id);
		},
		{ Post }
	);

	// POST /admin/api/posts/{id}/unpublish
	app().registerHandler(
		"/admin/api/posts/{1}/unpublish",
		[](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb,const std::string& id)
		{
			handleUnpublish(req, std::move(cb),id);
		},
		{ Post }
	);

	// 模板文件路径
#ifndef POST_TEMPLATE_PATH
#define POST_TEMPLATE_PATH "templates/post.html" // 兜底
#endif

	static const std::string kPostTpl = POST_TEMPLATE_PATH;

	app().registerHandler(
		"/posts/{1}/",
		[](const HttpRequestPtr& req,
			std::function<void(const HttpResponsePtr&)>&& cb,
			const std::string& id)
		{
			// 找内存里的文章
			BlogPost* postPtr = nullptr;
			{
#ifdef USE_STORE_LOCK
				std::shared_lock lk(g_storeMutex);
#endif
				auto it = g_postsById.find(id);
				if (it != g_postsById.end())
				{
					postPtr = &const_cast<BlogPost&>(it->second);
				}
			}
			if (!postPtr)
			{
				auto r = HttpResponse::newHttpResponse();
				r->setStatusCode(k404NotFound);
				cb(r); return;
			}

			// 首次访问生成静态页
			std::string htmlFile;
			if (!ensure_static_page(*postPtr, g_siteCfg, kPostTpl, &htmlFile))
			{
				auto r = HttpResponse::newHttpResponse();
				r->setStatusCode(k500InternalServerError);
				r->setContentTypeCode(CT_TEXT_PLAIN);
				r->setBody("failed to materialize static page");
				cb(r); return;
			}

			// 直接把生成的 index.html 返回（你也可以交给 nginx/静态托管）
			std::string body;
			if (!read_text_file(htmlFile, body))
			{
				auto r = HttpResponse::newHttpResponse();
				r->setStatusCode(k500InternalServerError);
				cb(r); return;
			}
			auto r = HttpResponse::newHttpResponse();
			r->setStatusCode(k200OK);
			r->setContentTypeString("text/html; charset=utf-8");
			r->setBody(std::move(body));
			cb(r);
		},
		{ Get }
	);
	// 4) 监听
	app().addListener("0.0.0.0", 13400)
		.setThreadNum(1)
		.run();
	return 0;
}

 

