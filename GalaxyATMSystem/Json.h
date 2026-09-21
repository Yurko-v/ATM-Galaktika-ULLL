#pragma once

#include <string>
#include <map>
#include <vector>

namespace Json
{
    struct Value
    {
        enum class Kind { Null, String, Literal, Object, Array } kind = Kind::Null;
        std::wstring str;
        std::map<std::wstring, Value> obj;
        std::vector<Value> arr;

        const Value* Find(const wchar_t* key) const
        {
            auto it = obj.find(key);
            return it == obj.end() ? nullptr : &it->second;
        }

        std::wstring AsString(const std::wstring& fallback = L"") const
        {
            return kind == Kind::String ? str : fallback;
        }

        std::wstring AsText(const std::wstring& fallback = L"") const
        {
            return (kind == Kind::String || kind == Kind::Literal) ? str : fallback;
        }

        double AsNumber(double fallback = 0.0) const
        {
            if (!HasText())
                return fallback;
            wchar_t* end = nullptr;
            double v = wcstod(str.c_str(), &end);
            return (end != NULL && *end == L'\0') ? v : fallback;
        }

        long long AsInt(long long fallback = 0) const
        {
            if (!HasText())
                return fallback;
            wchar_t* end = nullptr;
            long long v = wcstoll(str.c_str(), &end, 10);
            return (end != NULL && *end == L'\0') ? v : fallback;
        }

        bool AsBool(bool fallback) const
        {
            if (!HasText())
                return fallback;
            if (str == L"true")  return true;
            if (str == L"false") return false;
            return fallback;
        }

    private:
        bool HasText() const
        {
            return (kind == Kind::String || kind == Kind::Literal) && !str.empty();
        }
    };

    namespace Detail
    {
        class Parser
        {
        public:
            explicit Parser(const std::wstring& text) : m_s(text) {}

            bool Parse(Value& out)
            {
                SkipWs();
                return ParseValue(out, 0);
            }

        private:
            const std::wstring& m_s;
            size_t m_pos = 0;

            static const int kMaxDepth = 40;

            void SkipWs()
            {
                while (m_pos < m_s.size() && iswspace(m_s[m_pos]))
                    m_pos++;
            }

            bool ParseValue(Value& out, int depth)
            {
                if (depth > kMaxDepth)
                    return false;
                SkipWs();
                if (m_pos >= m_s.size())
                    return false;
                if (m_s[m_pos] == L'{')
                    return ParseObject(out, depth);
                if (m_s[m_pos] == L'[')
                    return ParseArray(out, depth);
                if (m_s[m_pos] == L'"')
                {
                    out.kind = Value::Kind::String;
                    return ParseString(out.str);
                }
                size_t start = m_pos;
                while (m_pos < m_s.size() && m_s[m_pos] != L',' && m_s[m_pos] != L'}' &&
                    m_s[m_pos] != L']' && !iswspace(m_s[m_pos]))
                    m_pos++;
                if (m_pos == start)
                    return false;
                out.kind = Value::Kind::Literal;
                out.str = m_s.substr(start, m_pos - start);
                return true;
            }

            bool ParseObject(Value& out, int depth)
            {
                out.kind = Value::Kind::Object;
                m_pos++;
                SkipWs();
                if (m_pos < m_s.size() && m_s[m_pos] == L'}')
                {
                    m_pos++;
                    return true;
                }
                for (;;)
                {
                    SkipWs();
                    if (m_pos >= m_s.size() || m_s[m_pos] != L'"')
                        return false;
                    std::wstring key;
                    if (!ParseString(key))
                        return false;
                    SkipWs();
                    if (m_pos >= m_s.size() || m_s[m_pos] != L':')
                        return false;
                    m_pos++;
                    Value val;
                    if (!ParseValue(val, depth + 1))
                        return false;
                    out.obj[key] = val;
                    SkipWs();
                    if (m_pos < m_s.size() && m_s[m_pos] == L',')
                    {
                        m_pos++;
                        continue;
                    }
                    if (m_pos < m_s.size() && m_s[m_pos] == L'}')
                    {
                        m_pos++;
                        break;
                    }
                    return false;
                }
                return true;
            }

            bool ParseArray(Value& out, int depth)
            {
                out.kind = Value::Kind::Array;
                m_pos++;
                SkipWs();
                if (m_pos < m_s.size() && m_s[m_pos] == L']')
                {
                    m_pos++;
                    return true;
                }
                for (;;)
                {
                    Value val;
                    if (!ParseValue(val, depth + 1))
                        return false;
                    out.arr.push_back(std::move(val));
                    SkipWs();
                    if (m_pos < m_s.size() && m_s[m_pos] == L',')
                    {
                        m_pos++;
                        continue;
                    }
                    if (m_pos < m_s.size() && m_s[m_pos] == L']')
                    {
                        m_pos++;
                        break;
                    }
                    return false;
                }
                return true;
            }

            bool ParseString(std::wstring& out)
            {
                m_pos++;
                out.clear();
                while (m_pos < m_s.size() && m_s[m_pos] != L'"')
                {
                    wchar_t c = m_s[m_pos];
                    if (c == L'\\' && m_pos + 1 < m_s.size())
                    {
                        m_pos++;
                        wchar_t e = m_s[m_pos];
                        switch (e)
                        {
                        case L'"':  out += L'"'; break;
                        case L'\\': out += L'\\'; break;
                        case L'/':  out += L'/'; break;
                        case L'n':  out += L'\n'; break;
                        case L't':  out += L'\t'; break;
                        case L'r':  out += L'\r'; break;
                        case L'b':  out += L'\b'; break;
                        case L'f':  out += L'\f'; break;
                        case L'u':
                            if (m_pos + 4 < m_s.size())
                            {
                                out += (wchar_t)wcstol(m_s.substr(m_pos + 1, 4).c_str(), nullptr, 16);
                                m_pos += 4;
                            }
                            break;
                        default:    out += e; break;
                        }
                        m_pos++;
                    }
                    else
                    {
                        out += c;
                        m_pos++;
                    }
                }
                if (m_pos >= m_s.size())
                    return false;
                m_pos++;
                return true;
            }
        };
    }

    inline std::wstring Utf8ToWide(const std::string& s)
    {
        if (s.empty())
            return std::wstring();
        int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), NULL, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
        return w;
    }

    inline std::string WideToUtf8(const std::wstring& s)
    {
        if (s.empty())
            return std::string();
        int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), NULL, 0, NULL, NULL);
        std::string out(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n, NULL, NULL);
        return out;
    }

    inline bool Parse(const std::wstring& text, Value& out)
    {
        return Detail::Parser(text).Parse(out);
    }

    inline bool ParseUtf8(const std::string& raw, Value& out)
    {
        std::string body = raw;
        if (body.size() >= 3 && (unsigned char)body[0] == 0xEF &&
            (unsigned char)body[1] == 0xBB && (unsigned char)body[2] == 0xBF)
            body.erase(0, 3);
        return Parse(Utf8ToWide(body), out);
    }
}
