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

struct BlogPost  {
	std::string id;
	std::string relPath;   // _posts 下的相对路径
	std::string title;
	std::string description;
	std::vector<std::string> categories;
	std::vector<std::string> tags;
	std::string raw;       // 完整 markdown（含 front-matter）
	std::string html;      // 预渲染后的 HTML（你要的）
	std::time_t mtime{ 0 };
	size_t      size{ 0 };
};

// —— 全局内存库 ——
inline std::unordered_map<std::string, BlogPost> g_postsById;          // id -> Post（按值存）
inline std::unordered_map<std::string, std::string> g_idByStem;    // stem -> id（可选）

// 全局 tags / categories 聚合（id->name），以及（可选）反向索引
inline std::unordered_map<std::string, std::string> g_allTags;        // tagId -> name
inline std::unordered_map<std::string, std::string> g_allCategories;  // catId -> name

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

static inline std::string normalize_date_iso_or_keep(const std::string& s, std::time_t fallback)
{
	// 已经是 ISO（含 'T' 和 'Z'）就直接返回
	if (s.find('T') != std::string::npos) return s;

	std::tm tm{};
	if (strptime(s.c_str(), "%Y-%m-%d %H:%M:%S", &tm))
	{
		// 当作本地时间转成 UTC ISO
		std::time_t tt = timegm(&tm); // 如果没有 timegm，可用 timegm 替代，或先当作 local 再校正
		if (tt == (std::time_t)-1) tt = fallback;
		return toIso(tt);
	}
	// 识别失败就保底用 fallback
	return toIso(fallback);
}
// 轻量解析 front-matter: 形如
// ---\nkey: value\n...\n---\n<body...>
struct ParsedMd {
	std::unordered_map<std::string, std::string> fm;
	std::string body;
	std::string raw;     // 原文（front-matter + body）
};

static inline std::string trim(const std::string& s)
{
	size_t a = s.find_first_not_of(" \t\r\n");
	if (a == std::string::npos) return "";
	size_t b = s.find_last_not_of(" \t\r\n");
	return s.substr(a, b - a + 1);
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

// 读取 block scalar（| 或 >）
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
static ParsedMd parse_front_matter(const std::string& text)
{
	ParsedMd out; out.raw = text;

	// 预处理：去 BOM、统一换行
	std::string src = text;
	strip_utf8_bom(src);
	src = to_lf(src);

	// === 关键：跳过前导空行/空白，检测第一条“非空行”是否为 '---' ===
	size_t pos = 0;
	while (pos < src.size())
	{
		size_t le = line_end(src, pos);
		std::string ln = trim_view(src, pos, le);     // 仅去左右空格/Tab
		if (!ln.empty())
		{
			// === 跳过前导空行/空白，定位首个“非空行” ===
			size_t pos = 0;
			while (pos < src.size())
			{
				size_t le = line_end(src, pos);
				std::string ln = trim_view(src, pos, le);
				if (!ln.empty()) break;
				pos = (le < src.size() ? le + 1 : le);
			}

			// === A/B 两种 front-matter 入口 ===
			size_t le = line_end(src, pos);
			std::string ln = (pos < src.size()) ? trim_view(src, pos, le) : "";

			std::string fmBuf;
			size_t bodyStart = 0;

			if (ln == "---")
			{
				// 样式 A：--- ... --- 再正文
				size_t fmStart = (le < src.size() ? le + 1 : le);
				size_t endStart = find_block_end_dashes(src, fmStart);
				if (endStart == std::string::npos) { out.body = src; return out; }
				fmBuf = src.substr(fmStart, endStart - fmStart);
				size_t endLineEnd = line_end(src, endStart);
				bodyStart = (endLineEnd < src.size() ? endLineEnd + 1 : endLineEnd);
			}
			else
			{
				// ✅ 样式 B：无起始 ---，从首非空行起，直到第一个独立 --- 为止是 FM
				size_t endStart = find_block_end_dashes(src, pos);
				if (endStart == std::string::npos) { out.body = src; return out; }
				fmBuf = src.substr(pos, endStart - pos);
				size_t endLineEnd = line_end(src, endStart);
				bodyStart = (endLineEnd < src.size() ? endLineEnd + 1 : endLineEnd);
			}

			// 解析 fmBuf -> out.fm （用你现有的逐行解析逻辑）
			std::istringstream fmss(fmBuf);
			std::string line;
			while (std::getline(fmss, line))
			{
				if (trim(line).empty()) continue;
				auto posc = find_colon_outside_quotes(line);
				if (posc == std::string::npos) continue;

				std::string key = trim(line.substr(0, posc));
				std::string val = trim(line.substr(posc + 1));

				if (val == "|" || val == "|-" || val == ">" || val == ">-")
				{
					bool folded = (val[0] == '>');
					int baseIndent = leading_indent(line);
					out.fm[key] = read_block_scalar(fmss, baseIndent, folded);
					continue;
				}

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
						std::string joined;
						for (size_t i = 0; i < items.size(); ++i) { if (i) joined += ','; joined += items[i]; }
						out.fm[key] = joined;
						continue;
					}
					else
					{
						fmss.clear(); fmss.seekg(back);
					}
				}

				val = unquote_if_pair(val);
				out.fm[key] = val;
			}

			out.body = (bodyStart < src.size() ? src.substr(bodyStart) : std::string());
			return out;
		}
		pos = (le < src.size() ? le + 1 : le);
	}

	// 全是空行
	out.body = src;
	return out;
}

// 简易 hash 生成 _id
static inline std::string make_id(const std::string& s)
{
	std::hash<std::string> H;
	size_t h = H(s);
	std::ostringstream oss;
	oss << "cm" << std::hex << h;
	return oss.str();
}

// 站点 URL（可换成你的配置）
static inline std::string site_url()
{
	// 如果你有 kSiteUrl，从配置读取；这里示例回落
	return "http://example.com/";
}

// 把源 md 路径转换为 admin 期望的 html path
static inline std::string md_to_html_path(const std::string& rel)
{
	// about/index.md -> about/index.html
	fs::path p(rel);
	if (p.extension() == ".md")
	{
		p.replace_extension(".html");
	}
	return p.generic_string();
}

#include <unordered_map>

// 粗糙地去掉标签（取纯文本），用于 title 与 id
static std::string strip_tags(const std::string& s)
{
	std::string out; out.reserve(s.size());
	bool inTag = false;
	for (char c : s)
	{
		if (c == '<') inTag = true;
		else if (c == '>') inTag = false;
		else if (!inTag) out.push_back(c);
	}
	return out;
}

// 生成 id：保留中英文/数字，空白转'-'，去掉危险字符；重复 id 自动加后缀 -2, -3 ...
static std::string slugify_keep_unicode(const std::string& s,
	std::unordered_map<std::string, int>& seen)
{
	std::string id; id.reserve(s.size());
	bool prevDash = false;
	for (unsigned char c : s)
	{
		if (c <= 0x20)
		{ // 空白
			if (!prevDash && !id.empty()) { id.push_back('-'); prevDash = true; }
			continue;
		}
		// 过滤不适合放进 id 的符号
		if (c == '"' || c == '\'' || c == '<' || c == '>' || c == '&' || c == '#' || c == '?')
			continue;
		id.push_back((char)c);
		prevDash = false;
	}
	if (!id.empty() && id.back() == '-') id.pop_back();
	if (id.empty()) id = "heading";

	// 去重
	auto it = seen.find(id);
	if (it == seen.end())
	{
		seen[id] = 1;
		return id;
	}
	else
	{
		int n = ++it->second;
		return id + "-" + std::to_string(n);
	}
}

static inline bool ieq(char a, char b) { return (char)((a | 32)) == (char)((b | 32)); }

static bool starts_with_ci(const std::string& s, size_t pos, const char* lit)
{
	for (size_t j = 0; lit[j]; ++j)
	{
		if (pos + j >= s.size()) return false;
		if (!ieq(s[pos + j], lit[j])) return false;
	}
	return true;
}

static size_t skip_tag(const std::string& s, size_t pos_lt)
{
	size_t j = pos_lt;
	while (j < s.size() && s[j] != '>') ++j;
	return (j < s.size()) ? j + 1 : j;
}

static void append_escaped_attr(std::string& out, const std::string& text)
{
	for (char c : text)
	{
		if (c == '"') out += "&quot;";
		else if (c == '&') out += "&amp;";
		else out.push_back(c);
	}
}

// 统计 code 文本行数
static size_t count_lines(const std::string& s)
{
	size_t lines = 1;
	for (char c : s) if (c == '\n') ++lines;
	return lines;
}

static std::string extract_language_from_code_tag_ci(const std::string& codeOpenTag)
{
	// 从 <code ...> 里解析 class="... language-xxx ..."（大小写无关）
	std::string lang = "plaintext";
	// 简易、健壮：把一份副本转小写进行搜素，定位后再从小写副本里取值
	std::string lower = codeOpenTag;
	for (char& c : lower) c = (char)std::tolower((unsigned char)c);
	size_t cls = lower.find("class=");
	if (cls != std::string::npos)
	{
		size_t q1 = lower.find_first_of("'\"", cls + 6);
		if (q1 != std::string::npos)
		{
			char q = lower[q1];
			size_t q2 = lower.find(q, q1 + 1);
			if (q2 != std::string::npos && q2 > q1 + 1)
			{
				std::string val = lower.substr(q1 + 1, q2 - (q1 + 1)); // class 值（小写）
				size_t lp = val.find("language-");
				if (lp != std::string::npos)
				{
					size_t st = lp + 9;
					size_t ed = st;
					auto isok = [](char ch)
						{
							return std::isalnum((unsigned char)ch) || ch == '_' || ch == '-';
						};
					while (ed < val.size() && isok(val[ed])) ++ed;
					if (ed > st) lang = val.substr(st, ed - st);
				}
			}
		}
	}
	return lang;
}

std::string transform_html_hexo_onepass(const std::string& htmlIn)
{
	const char* S = htmlIn.c_str();
	const size_t N = htmlIn.size();
	size_t i = 0;

	std::string out;
	out.reserve(N + N / 10);

	bool inP = false;
	int  depthCode = 0;     // <pre>/<code> 嵌套深度（仅用于换行转 <br> 的抑制）
	int  depthFigure = 0;   // <figure> 嵌套深度

	// </hN> 之后，如果紧跟的是 <p> 则要丢弃中间的空白；否则保留
	bool drop_ws_mode = false;
	std::string ws_buf;

	std::unordered_map<std::string, int> seen_ids; // 标题 id 去重

	auto flush_ws_if_needed = [&](bool next_is_p)
		{
			if (drop_ws_mode)
			{
				if (!next_is_p) out += ws_buf; // 不是 <p>，把空白补回
				ws_buf.clear();
				drop_ws_mode = false;
			}
		};

	while (i < N)
	{
		if (S[i] == '<')
		{
			// 先处理两种“整体消费”的结构：<h1..6>…</h..> 与 <pre><code …>…</code></pre>
			// 1) 标题：<hN ...>inner</hN> → 注入 id/anchor
			if (i + 3 < N && (S[i + 1] == 'h' || S[i + 1] == 'H') && (S[i + 2] >= '1' && S[i + 2] <= '6'))
			{
				char level = S[i + 2];
				size_t tagOpenEnd = htmlIn.find('>', i);
				if (tagOpenEnd == std::string::npos) { out.push_back(S[i++]); continue; }

				size_t contentStart = tagOpenEnd + 1;
				std::string endTag = "</h"; endTag.push_back(level); endTag += ">";
				size_t closePos = htmlIn.find(endTag, contentStart);
				if (closePos == std::string::npos) { out.append(S + i, tagOpenEnd - i + 1); i = tagOpenEnd + 1; continue; }

				// inner
				std::string inner = htmlIn.substr(contentStart, closePos - contentStart);
				std::string titleText = strip_tags(inner);
				std::string id = slugify_keep_unicode(titleText, seen_ids);

				// 输出替换段
				// 注意：在进入标题前，若有处于“等待决定是否丢空白”的状态，遇到非 <p> 的标签应先把空白冲刷出来
				flush_ws_if_needed(/*next_is_p=*/false);

				out += "<h"; out.push_back(level); out += " id=\"";
				out += id;
				out += "\"><a href=\"#"; out += id; out += "\" class=\"headerlink\" title=\"";
				append_escaped_attr(out, titleText);
				out += "\"></a>";
				out += inner;
				out += "</h"; out.push_back(level); out += ">";

				// 启动“标题后空白判定”模式
				drop_ws_mode = true;
				ws_buf.clear();

				i = closePos + endTag.size();
				continue;
			}

			// 2) 代码块：<pre> [空白] <code ...>code</code> </pre> → Hexo figure+table 行号结构
			if (starts_with_ci(htmlIn, i, "<pre"))
			{
				size_t preOpenEnd = skip_tag(htmlIn, i);
				size_t j = preOpenEnd;
				while (j < N && (S[j] == ' ' || S[j] == '\t' || S[j] == '\r' || S[j] == '\n')) ++j;

				if (j < N && starts_with_ci(htmlIn, j, "<code"))
				{
					size_t codeOpenEnd = skip_tag(htmlIn, j);
					// 解析语言
					std::string codeOpenTag = htmlIn.substr(j, codeOpenEnd - j);
					std::string lang = extract_language_from_code_tag_ci(codeOpenTag);

					// 找 </code>
					size_t codeClose = htmlIn.find("</code>", codeOpenEnd);
					if (codeClose == std::string::npos)
					{
						// 保守：按普通标签输出
						flush_ws_if_needed(false);
						out.append(S + i, preOpenEnd - i);
						i = preOpenEnd;
						depthCode++; // 进入 <pre>（保守）
						continue;
					}
					// 找 </pre>（必须在 </code> 之后）
					size_t preClose = htmlIn.find("</pre>", codeClose + 7);
					if (preClose == std::string::npos)
					{
						flush_ws_if_needed(false);
						out.append(S + i, preOpenEnd - i);
						i = preOpenEnd;
						depthCode++;
						continue;
					}

					// 提取 code 文本
					std::string code = htmlIn.substr(codeOpenEnd, codeClose - codeOpenEnd);

					// 生成带行号结构
					size_t lines = count_lines(code);

					std::string gutter;
					gutter.reserve(lines * 20);
					gutter += "<pre>";
					for (size_t ln = 1; ln <= lines; ++ln)
					{
						gutter += "<span class=\"line\">" + std::to_string(ln) + "</span><br>";
					}
					gutter += "</pre>";

					std::string codeCol;
					codeCol.reserve(code.size() + lines * 16);
					codeCol += "<pre>";
					{
						size_t st = 0;
						while (st <= code.size())
						{
							size_t nl = code.find('\n', st);
							if (nl == std::string::npos)
							{
								codeCol += "<span class=\"line\">";
								codeCol.append(code, st, std::string::npos);
								codeCol += "</span>";
								break;
							}
							else
							{
								codeCol += "<span class=\"line\">";
								codeCol.append(code, st, nl - st);
								codeCol += "</span><br>";
								st = nl + 1;
							}
						}
					}
					codeCol += "</pre>";

					flush_ws_if_needed(false);
					out += "<figure class=\"highlight " + lang + "\">"
						"<table><tr>"
						"<td class=\"gutter\">" + gutter + "</td>"
						"<td class=\"code\">" + codeCol + "</td>"
						"</tr></table>"
						"</figure>";

					i = preClose + 6 + 1; // len("</pre>")=6, 再加1到 '>' 后
					continue;
				}
				// 不是 <pre><code> 组合：按普通标签处理并维护状态
			}

			// —— 走到这里：普通标签路径（复制 + 状态机维护 + 标题后空白决策）——
			// 在真正输出这个“下一个标签”前，若处于 drop_ws_mode：
			// 如果它是 <p>，就丢弃 ws；否则把 ws 写回。
			if (drop_ws_mode)
			{
				bool next_is_p = starts_with_ci(htmlIn, i, "<p");
				flush_ws_if_needed(next_is_p);
			}

			// 状态维护（避免换行转 <br> 的区域 & 段落状态）
			if (starts_with_ci(htmlIn, i, "<pre")) { depthCode++; }
			else if (starts_with_ci(htmlIn, i, "</pre")) { if (depthCode) --depthCode; }
			else if (starts_with_ci(htmlIn, i, "<code")) { depthCode++; }
			else if (starts_with_ci(htmlIn, i, "</code")) { if (depthCode) --depthCode; }
			else if (starts_with_ci(htmlIn, i, "<figure")) { depthFigure++; }
			else if (starts_with_ci(htmlIn, i, "</figure")) { if (depthFigure) --depthFigure; }
			else if (starts_with_ci(htmlIn, i, "<p")) { inP = true; }
			else if (starts_with_ci(htmlIn, i, "</p")) { inP = false; }

			size_t j = skip_tag(htmlIn, i);
			out.append(S + i, j - i);
			i = j;
			continue;
		}

		// —— 文本字符 ——（可能是 </hN> 之后到下个标签前的空白，或正常文本）
		if (drop_ws_mode)
		{
			// 标题后：先把空白缓存起来，等待看到下一个是否 <p>
			if (S[i] == ' ' || S[i] == '\t' || S[i] == '\r' || S[i] == '\n')
			{
				ws_buf.push_back(S[i++]);
				continue;
			}
			else
			{
				// 碰到非空白文本，按照“不是 <p>”的规则保留空白
				out += ws_buf;
				ws_buf.clear();
				drop_ws_mode = false;
			}
		}

		// 段落正文内（且不在代码/figure）把 '\n' → <br>，忽略 '\r'
		if (inP && depthCode == 0 && depthFigure == 0)
		{
			if (S[i] == '\r') { ++i; continue; }
			if (S[i] == '\n') { out += "<br>"; ++i; continue; }
		}

		out.push_back(S[i++]);
	}

	return out;
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

static Json::Value listMarkdown(const fs::path& root)
{
	Json::Value arr(Json::arrayValue);
	if (!fs::exists(root)) return arr;
	for (auto& e : fs::recursive_directory_iterator(root))
	{
		if (!e.is_regular_file()) continue;
		auto ext = e.path().extension().string();
		if (ext == ".md" || ext == ".markdown" || ext == ".mdx")
		{
			Json::Value item;
			item["path"] = fs::relative(e.path(), root).generic_string();
			item["size"] = (Json::UInt64)fs::file_size(e.path());
			// 简单从 YAML front-matter 中抓 title（可选）
			std::string content = readAll(e.path());
			std::smatch m;
			if (std::regex_search(content, m, std::regex("^---[\\s\\S]*?title:\\s*(.+?)\\s*\\n[\\s\\S]*?---", std::regex::icase)))
			{
				item["title"] = m[1].str();
			}
			else
			{
				item["title"] = e.path().stem().string();
			}
			arr.append(item);
		}
	}
	return arr;
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


#include <md4c-html.h>

// Markdown -> HTML（兼容不同 md4c 版本的 flag）
static std::string mdToHtml(const std::string& md)
{
#ifdef USE_MD4C
	std::string out;

	unsigned pf = 0;  // parser flags
	// 这些宏在不同版本可能不存在，逐个 #ifdef 检查
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
	pf |= MD_FLAG_PERMISSIVEWWWAUTOLINKS;   // 注意这个名字是没有下划线的 WWWA
#endif
#ifdef MD_FLAG_LATEXMATHSPANS
	pf |= MD_FLAG_LATEXMATHSPANS;
#endif
#ifdef MD_FLAG_WIKILINKS
	pf |= MD_FLAG_WIKILINKS;
#endif

	unsigned rf = 0;  // renderer flags
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

	int rc = md_html(
		md.c_str(),
		(MD_SIZE)md.size(),
		md4c_out,       // ✅ void 回调
		&out,
		pf,
		rf
	);
	if (rc == 0) return out;
	// 渲染失败则降级
#endif
	return std::string("<pre>") + escapeHtml(md, /*keepNewline=*/true) + "</pre>";
}



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

//// 工具：简单 CORS（同域也无妨）
//static void addCORS(const HttpRequestPtr& req, const HttpResponsePtr& r)
//{
//	auto origin = req->getHeader("Origin");
//	if (origin.empty()) origin = "*";
//	r->addHeader("Access-Control-Allow-Origin", origin);
//	r->addHeader("Vary", "Origin");
//	r->addHeader("Access-Control-Allow-Headers", "Content-Type, X-Admin-Token, Authorization");
//	r->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
//}

	// 小工具：把 filesystem 的 mtime 转毫秒时间戳
static Json::Int64 mtimeMs(const fs::path& p)
{
	try
	{
		auto ftime = fs::last_write_time(p);
		auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
			ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
		auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(sctp.time_since_epoch()).count();
		return static_cast<Json::Int64>(ms);
	}
	catch (...)
	{
		return 0;
	}
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

// ====== front-matter 解析（够用的轻量实现）======
struct FrontMatter {
	std::string id;
	std::string title;
	std::string description;
	std::vector<std::string> categories;
	std::vector<std::string> tags;
};
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
static FrontMatter parse_front_matter(const std::string& md, std::string* bodyOut = nullptr)
{
	FrontMatter fm;
	if (md.rfind("---", 0) != 0) { if (bodyOut) *bodyOut = md; return fm; }
	size_t p = md.find("\n", 3);
	if (p == std::string::npos) { if (bodyOut) *bodyOut = md; return fm; }
	size_t end = md.find("\n---", p);
	if (end == std::string::npos) { if (bodyOut) *bodyOut = md; return fm; }
	size_t fmStart = p + 1;
	size_t fmLen = end - (p + 1);
	std::string y = md.substr(fmStart, fmLen);
	if (bodyOut) *bodyOut = md.substr(end + 4 /*\n---*/ + 1 /*\n*/);

	std::istringstream iss(y);
	std::string line;
	std::vector<std::string>* currentList = nullptr;
	while (std::getline(iss, line))
	{
		if (line.size() >= 2 && line[0] == '-' && line[1] == ' ')
		{
			if (currentList) currentList->push_back(unquote(trim(line.substr(2))));
			continue;
		}
		currentList = nullptr;
		auto pos = line.find(':');
		if (pos == std::string::npos) continue;
		std::string key = trim(line.substr(0, pos));
		std::string val = trim(line.substr(pos + 1));

		if (key == "title") fm.title = unquote(val);
		else if (key == "id") fm.id = unquote(val);
		else if (key == "description") fm.description = unquote(val);
		else if (key == "categories") { fm.categories.clear(); if (!val.empty()) parse_yaml_list_inline(val, fm.categories); else currentList = &fm.categories; }
		else if (key == "tags") { fm.tags.clear();       if (!val.empty()) parse_yaml_list_inline(val, fm.tags);       else currentList = &fm.tags; }
	}
	return fm;
}
// ====== 把单篇放入全局索引 ======
static void upsert_global_taxonomy(const std::vector<std::string>& names,
	std::unordered_map<std::string, std::string>& kv,
	const char* prefix)
{
	for (const auto& name : names)
	{
		if (name.empty()) continue;
		// 同名应有同 id：用名称作为种子
		std::string id = stableShortId(name, prefix); // prefix: "tag" / "cat" 都可以，或都用 "cm"
		kv.emplace(id, name);
	}
}

// ====== 主函数：初始化全站到内存 ======
bool initPosts()
{
	// 扫描目录 & 组装新容器（构建完成后再一次性交换，避免中途读访问看到半成品）
	std::unordered_map<std::string, BlogPost > postsById;
	std::unordered_map<std::string, std::string> idByStem;
	postsById.reserve(256);
	idByStem.reserve(256);

	std::unordered_map<std::string, std::string> allTags;
	std::unordered_map<std::string, std::string> allCategories;
	allTags.reserve(256); allCategories.reserve(256);

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

			// front-matter
			std::string body;
			FrontMatter fm = parse_front_matter(md, &body);

			// 计算 id（优先 front-matter；否则用 stem 或 title）
			const std::string stem = e.path().stem().string();
			// —— 改成用 abbrlink ——
			// 1) 先取 fm.abbrlink；为空就生成
			std::string abbr = fm.abbrlink;
			if (abbr.empty())
			{
				// 生成策略：用 crc16(title 或 由路径/stem 组成的种子)
				const std::string stem = e.path().stem().string();
				const std::string seed = !fm.title.empty() ? fm.title : stem;
				abbr = crc16_abbrlink(seed, /*format=*/"dec");   // 或 "hex"
			}

			// 2) 确保全局唯一（极少碰撞时重试）
			std::string chosen = abbr;
			int salt = 0;
			while (postsById.find(chosen) != postsById.end())
			{
				// 发生碰撞：加盐重算，或切换到 crc32
				++salt;
				chosen = crc16_abbrlink(abbr + "#" + std::to_string(salt), "dec");
			}

			// 3) 把 chosen 作为 Post.id，同时把 abbrlink 存起来（可和 id 相同）
			std::string id = chosen;


			// HTML 预渲染（你可以换成延迟渲染）
			std::string html = "";// renderMarkdownToHtml(md);

			// mtime / size
			std::time_t mtime = 0;
			try
			{
				auto ft = fs::last_write_time(e.path());
				auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
					ft - fs::file_time_type::clock::now()
					+ std::chrono::system_clock::now());
				mtime = std::chrono::system_clock::to_time_t(sctp);
			}
			catch (...) {}

			size_t fsize = 0;
			try { fsize = (size_t)fs::file_size(e.path()); }
			catch (...) {}

			// 填 Post
			Post p;
			p.id = id;
			p.relPath = fs::relative(e.path(), kPostsDir).generic_string();
			p.title = fm.title;
			p.description = fm.description;
			p.categories = fm.categories;
			p.tags = fm.tags;
			p.raw = std::move(md);
			p.html = std::move(html);
			p.mtime = mtime;
			p.size = fsize;

			// 放入容器
			postsById.emplace(id, std::move(p));
			idByStem.emplace(stem, id);

			// 更新全局 tags/categories
			upsert_global_taxonomy(fm.tags, allTags, "cm");        // 也可用 "tag"
			upsert_global_taxonomy(fm.categories, allCategories, "cm"); // 也可用 "cat"
		}
	}

	// 一次性替换到全局（写锁保护）
	{
		std::unique_lock lk(g_storeMutex);
		g_postsById.swap(postsById);
		g_idByStem.swap(idByStem);
		g_allTags.swap(allTags);
		g_allCategories.swap(allCategories);
	}
	return true;
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

	// 2) 托管 /admin/ 前端（把 hexo-admin 当静态文件）
	//    Drogon 只有一个 documentRoot，这里用一个“兜底路由”来读文件：
	app().registerHandler("/admin", [](const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&& cb)
		{
			auto p = fs::path(kAdminRoot) / "login/index.html";   // 你原来的入口
			auto r = HttpResponse::newFileResponse(p.string());
			r->setContentTypeCode(CT_TEXT_HTML);
			cb(r);
		}, { Get });

	// 登录：POST /admin   (表单 or JSON)
	// 通过后发一个 cookie（或你想用的 token），再 302 跳回 /admin/
	app().registerHandler(
		"/admin",
		[](const HttpRequestPtr& req,std::function<void(const HttpResponsePtr&)>&& cb)
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
			{ // 视作表单：application/x-www-form-urlencoded
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
		},
		{ Post }
	);

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

			Json::Value posts(Json::arrayValue);
			auto files = listMarkdown(kContentRoot + "/pages");
			for (auto& f : files)
			{
				Json::Value p;
				p["title"] = f["title"];
				p["source"] = f["path"];       // 原始文件名
				p["slug"] = f["path"];         // URL slug
				p["raw"] = f["path"];          // raw path
				p["path"] = f["path"];         // 相对路径
				p["published"] = true;
				p["date"] = (Json::Int64)time(nullptr) * 1000;
				p["updated"] = (Json::Int64)time(nullptr) * 1000;
				p["categories"] = Json::arrayValue;
				p["tags"] = Json::arrayValue;
				posts.append(p);
			}

			Json::Value out;
			out["posts"] = posts;   // 🔑 前端需要的是 posts，不是 items

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

			{
				const fs::path postsRoot = fs::path(kContentRoot) / "_posts";
				Json::Value postFiles = listMarkdown(postsRoot);
				for (const auto& it : postFiles)
				{
					const std::string rel = it["path"].asString();   // e.g. "blender-2.md"
					const fs::path abs = postsRoot / rel;

					std::time_t mt = 0;
					try
					{
						auto ftime = fs::last_write_time(abs);
						mt = filetime_to_time_t(ftime);
					}
					catch (...) { mt = std::time(nullptr); }

					const std::string fileText = read_all_text(abs);
					ParsedMd parsed = parse_front_matter(fileText);

					const std::string title = parsed.fm.count("title") ? parsed.fm.at("title") : fs::path(rel).stem().string();
					const std::string author = parsed.fm.count("author") ? parsed.fm.at("author") : "";
					const std::string abbrlink = parsed.fm.count("abbrlink") ? parsed.fm.at("abbrlink") : fs::path(rel).stem().string();
					const std::string desc = parsed.fm.count("description") ? parsed.fm.at("description") : "";
					const std::string layout = parsed.fm.count("layout") ? parsed.fm.at("layout") : "post";
					const std::string commentsS = parsed.fm.count("comments") ? parsed.fm.at("comments") : "false";
					const bool        commentsB = (commentsS == "true" || commentsS == "True" || commentsS == "1");
					const std::string slug = fs::path(rel).stem().string();
					const std::string& body = parsed.body;
					std::string renderedHtml = mdToHtml(body);
					renderedHtml = transform_html_hexo_onepass(renderedHtml);

					// 日期：优先 front-matter；否则用文件 mtime
					std::string date_iso = toIso(mt);
					if (parsed.fm.count("date"))
					{
						date_iso = normalize_date_iso_or_keep(parsed.fm.at("date"), mt);
					}

					 Json::Value tags(Json::arrayValue);
					 if (parsed.fm.count("tags"))
					 {
						 tags = split_to_json_array(parsed.fm.at("tags"));
					 }
					 Json::Value categories(Json::arrayValue);
					 if (parsed.fm.count("categories"))
					 {
						 categories = split_to_json_array(parsed.fm.at("categories"));
					 }

					Json::Value p(Json::objectValue);
					p["title"] = title;
					p["author"] = author;
					p["abbrlink"] = abbrlink;
					p["description"] = desc;
					p["date"] = date_iso;							// 形如 "2023-03-31T02:51:00.000Z"
					p["_content"] = body;							// markdown 正文
					p["source"] = std::string("_posts/") + rel;		// 与示例一致地含 _posts
					p["raw"] = fileText;							// front-matter + markdown 原文
					p["slug"] = slug;
					p["published"] = true;
					p["updated"] = toIso(mt);
					p["comments"] = commentsB;
					p["layout"] = layout;
					p["photos"] = Json::Value(Json::arrayValue);
					p["_id"] = make_id(p["source"].asString());		// 用路径做种子
					p["content"] = renderedHtml;							// 如需 HTML，后面再接 md4c
					p["excerpt"] = "";
					p["more"] = renderedHtml;
					p["path"] = std::string("posts/") + abbrlink + "/";   // Hexo abbrlink 规则
					p["permalink"] = site_url() + p["path"].asString();
					p["full_source"] = abs.generic_string();
					p["asset_dir"] = abs.parent_path().generic_string() + "/";
					p["tags"] = tags;
					p["categories"] = categories;
					p["isDraft"] = false;
					p["isDiscarded"] = false;

					arr.append(std::move(p));
				}
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

	// 3.6 预览渲染（POST /admin/api/preview） body: { "markdown": "..." }
	//app().registerHandler("/admin/api/preview", [](const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> &&cb)
	//	{
	//		if (!authOk(req)) { auto r = HttpResponse::newHttpResponse(); r->setStatusCode(k401Unauthorized); cb(r); return; }
	//		Json::Value body; Json::CharReaderBuilder rb; std::string errs; auto s = req->getBody();
	//		std::unique_ptr<Json::CharReader> rd(rb.newCharReader());
	//		if (!rd->parse(s.data(), s.data() + s.size(), &body, &errs) || !body.isMember("markdown"))
	//		{
	//			auto r = HttpResponse::newHttpResponse(); r->setStatusCode(k400BadRequest); r->setBody("bad json"); cb(r); return;
	//		}
	//		std::string html = mdToHtml(body["markdown"].asString());
	//		auto r = HttpResponse::newHttpResponse();
	//		addCORS(r);
	//		r->setContentTypeCode(CT_TEXT_HTML);
	//		r->setBody(html);
	//		cb(r);
	//	}, { Post });

	//app().registerHandler("/admin/api/settings/list", [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb)
	//	{
	//		Json::Value root;
	//		root["options"]["overwriteImages"] = true;
	//		root["options"]["spellcheck"] = true;
	//		root["options"]["lineNumbers"] = true;
	//		root["options"]["askImageFilename"] = true;
	//		root["options"]["imagePath"] = "/images";

	//		root["editor"]["inputStyle"] = "contenteditable";
	//		root["editor"]["spellcheck"] = true;
	//		root["editor"]["lineNumbers"] = true;

	//		auto resp = HttpResponse::newHttpJsonResponse(root);
	//		resp->setStatusCode(k200OK);
	//		cb(resp);
	//	},{ Get });

	//app().registerHandler("/admin/api/tags-categories-and-metadata",[](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb)
	//	{
	//		Json::Value root;
	//		root["categories"] = Json::Value(Json::objectValue);
	//		root["tags"] = Json::Value(Json::objectValue);
	//		// 收集
	//		collect_tags_categories(root["categories"], root["tags"]);
	//		// 你给的格式里有 metadata 数组，目前就返回 ["description"]
	//		root["metadata"] = Json::Value(Json::arrayValue);
	//		root["metadata"].append("description");

	//		auto resp = drogon::HttpResponse::newHttpJsonResponse(root);
	//		resp->setStatusCode(k200OK);
	//		cb(resp);
	//	},{ Get });

	//app().registerHandler(
	//	"/admin/api/posts/{1}",
	//	[](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& cb, const std::string& id)
	//	{
	//		fs::path p; FrontMatter fm; std::string raw;
	//		if (!find_post_by_id(id, p, &fm, &raw))
	//		{
	//			auto r = drogon::HttpResponse::newHttpResponse();
	//			r->setStatusCode(k404NotFound);
	//			cb(r);
	//			return;
	//		}

	//		// 尝试从正文首行提取标题（若 front-matter 没有）
	//		if (fm.title.empty())
	//		{
	//			std::istringstream iss(raw);
	//			std::string line;
	//			while (std::getline(iss, line))
	//			{
	//				line = trim(line);
	//				if (line.rfind("#", 0) == 0)
	//				{
	//					// 去掉开头的#和空格
	//					size_t p = line.find_first_not_of("# ");
	//					fm.title = (p == std::string::npos) ? "" : trim(line.substr(p));
	//					break;
	//				}
	//				// 碰到非空行但不是标题就退出
	//				if (!line.empty() && line != "---") break;
	//			}
	//		}

	//		Json::Value post(Json::objectValue);
	//		post["id"] = id;
	//		post["title"] = fm.title;
	//		post["path"] = p.lexically_relative(kPostsDir).generic_string();
	//		post["raw"] = raw;

	//		// categories / tags
	//		post["categories"] = Json::Value(Json::arrayValue);
	//		for (auto& c : fm.categories) post["categories"].append(c);
	//		post["tags"] = Json::Value(Json::arrayValue);
	//		for (auto& t : fm.tags) post["tags"].append(t);

	//		if (!fm.description.empty()) post["description"] = fm.description;

	//		// 如果你希望严格“和 list 里的一个一样、只多一个 id”，
	//		// 可以只返回上述这几个字段（或按你 list 的结构裁剪）。

	//		auto resp = drogon::HttpResponse::newHttpJsonResponse(post);
	//		resp->setStatusCode(k200OK);
	//		cb(resp);
	//	},
	//	{ Get });



	// 4) 监听
	app().addListener("0.0.0.0", 13400)
		.setThreadNum(2)
		.run();
	return 0;
}
