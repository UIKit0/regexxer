/*
 * Copyright (c) 2002-2007  Daniel Elstner  <daniel.kitta@gmail.com>
 *
 * This file is part of regexxer.
 *
 * regexxer is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * regexxer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with regexxer; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "stringutils.h"

#include <glib.h>
#include <glib-object.h>
#include <glibmm.h>
#include <gdkmm/color.h>

#include <algorithm>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{

typedef std::pair<int, char> ModPos;

class ScopedTypeClass
{
private:
  void* class_;

  ScopedTypeClass(const ScopedTypeClass&);
  ScopedTypeClass& operator=(const ScopedTypeClass&);

public:
  explicit ScopedTypeClass(GType type)
    : class_ (g_type_class_ref(type)) {}

  ~ScopedTypeClass() { g_type_class_unref(class_); }

  void* get() const { return class_; }
};

static inline
bool is_significant_encoding_char(char c)
{
  switch (c)
  {
    case ' ': case '-': case '_': case '.': case ':':
      return false;
  }

  return true;
}

static inline
unsigned int scale_to_8bit(unsigned int value)
{
  return (value & 0xFF00) >> 8;
}

static inline
bool ascii_isodigit(char c)
{
  return (c >= '0' && c <= '7');
}

static
std::string apply_modifiers(const std::string& subject, const std::vector<ModPos>& modifiers)
{
  std::string result;
  result.reserve(subject.size());

  int idx = 0;

  const std::vector<ModPos>::const_iterator pend = modifiers.end();
  std::vector<ModPos>::const_iterator       p    = modifiers.begin();

  while (p != pend)
  {
    const int start = p->first;
    result.append(subject, idx, start - idx);
    idx = start;

    const char mod = p->second;
    ++p;

    switch (mod)
    {
      case 'L': case 'U':
      {
        while (p != pend && (p->second == 'l' || p->second == 'u'))
          ++p;

        const int stop = (p == pend) ? subject.size() : p->first;
        const Glib::ustring slice (subject.begin() + start, subject.begin() + stop);
        const Glib::ustring str = (mod == 'L') ? slice.lowercase() : slice.uppercase();

        result.append(str.raw());
        idx = stop;
        break;
      }
      case 'l': case 'u': // TODO: Simplify.  This code is way too complicated.
      {
        if (unsigned(start) < subject.size())
        {
          while (p != pend && p->first == start && p->second != 'L' && p->second != 'U')
            ++p;

          if (p != pend && p->first == start)
          {
            const char submod = p->second;

            do
              ++p;
            while (p != pend && (p->second == 'l' || p->second == 'u'));

            const int stop = (p == pend) ? subject.size() : p->first;
            const Glib::ustring slice (subject.begin() + start, subject.begin() + stop);
            const Glib::ustring str = (submod == 'L') ? slice.lowercase() : slice.uppercase();

            if (!str.empty())
            {
              Glib::ustring::const_iterator cpos = str.begin();
              gunichar uc = *cpos++;
              uc = (mod == 'l') ? Glib::Unicode::tolower(uc) : Glib::Unicode::totitle(uc);

              if (Glib::Unicode::validate(uc))
                result.append(Glib::ustring(1, uc).raw());

              result.append(cpos.base(), str.end().base());
            }
            idx = stop;
          }
          else
          {
            Glib::ustring::const_iterator cpos (subject.begin() + start);
            gunichar uc = *cpos++;
            uc = (mod == 'l') ? Glib::Unicode::tolower(uc) : Glib::Unicode::totitle(uc);

            if (Glib::Unicode::validate(uc))
              result.append(Glib::ustring(1, uc).raw());

            idx = cpos.base() - subject.begin();
          }
        }
        break;
      }
      case 'E':
      {
        break;
      }
      default:
      {
        g_assert_not_reached();
        break;
      }
    }
  }

  result.append(subject, idx, std::string::npos);

  return result;
}

static
void parse_control_char(std::string::const_iterator& p, std::string::const_iterator pend,
                        std::string& dest)
{
  const std::string::const_iterator pnext = p + 1;

  if (pnext != pend && (static_cast<unsigned char>(*pnext) & 0x80U) == 0)
  {
    p = pnext;

    // Flip bit 6 of the upcased character.
    const char c = static_cast<unsigned char>(Glib::Ascii::toupper(*pnext)) ^ 0x40U;

    // TextBuffer can't handle NUL; interpret it as empty string instead.
    if (c != '\0')
      dest += c;
  }
  else
    dest += 'c';
}

static
void parse_hex_unichar(std::string::const_iterator& p, std::string::const_iterator pend,
                       std::string& dest)
{
  using namespace Glib;

  std::string::const_iterator pstart = p + 1;

  if (pstart != pend)
  {
    if (*pstart == '{')
    {
      const std::string::const_iterator pstop = std::find(++pstart, pend, '}');

      if (pstop != pend)
      {
        p = pstop;
        gunichar uc = 0;

        for (; pstart != pstop; ++pstart)
        {
          if (!Ascii::isxdigit(*pstart))
            return;

          uc *= 0x10;
          uc += Ascii::xdigit_value(*pstart);
        }

        if (uc != 0 && Unicode::validate(uc))
          dest += ustring(1, uc).raw();

        return;
      }
    }
    else if (pstart + 1 != pend && Ascii::isxdigit(pstart[0]) && Ascii::isxdigit(pstart[1]))
    {
      p = pstart + 1;
      const gunichar uc = 0x10 * Ascii::xdigit_value(pstart[0]) + Ascii::xdigit_value(pstart[1]);

      if (uc != 0 && Unicode::validate(uc))
        dest += ustring(1, uc).raw();

      return;
    }
  }

  dest += 'x';
}

static
void parse_oct_unichar(std::string::const_iterator& p, std::string::const_iterator pend,
                       std::string& dest)
{
  gunichar uc = 0;
  std::string::const_iterator pnum = p;

  for (; pnum != pend && (pnum - p) < 3; ++pnum)
  {
    if (!ascii_isodigit(*pnum))
      break;

    uc *= 010;
    uc += Glib::Ascii::digit_value(*pnum);
  }

  if (pnum > p)
  {
    p = pnum - 1;

    if (uc != 0 && Glib::Unicode::validate(uc))
      dest += Glib::ustring(1, uc).raw();
  }
  else
    dest += *p;
}

/*
 * On entry, p _must_ point to either a digit or a starting bracket '{'.  Also,
 * if p points to '{' the closing bracket '}' is assumed to follow before pend.
 */
static
int parse_capture_index(std::string::const_iterator& p, std::string::const_iterator pend)
{
  std::string::const_iterator pnum = p;

  if (*pnum == '{' && *++pnum == '}')
  {
    p = pnum;
    return -1;
  }

  int result = 0;

  while (pnum != pend && Glib::Ascii::isdigit(*pnum))
  {
    result *= 10;
    result += Glib::Ascii::digit_value(*pnum++);
  }

  if (*p != '{') // case "$digits": set position to last digit
  {
    p = pnum - 1;
  }
  else if (*pnum == '}') // case "${digits}": set position to '}'
  {
    p = pnum;
  }
  else // case "${invalid}": return -1 but still skip until '}'
  {
    p = std::find(pnum, pend, '}');
    return -1;
  }

  return result;
}

} // anonymous namespace

/*
 * Convert the content of an std::wstring to UTF-8.  Using wide strings is
 * necessary when dealing with localized stream formatting, for the reasons
 * outlined here:  http://bugzilla.gnome.org/show_bug.cgi?id=399216
 *
 * Direct use of wide strings in regexxer is a temporary measure.  Thus,
 * this function should be removed once Glib::compose() and Glib::format()
 * are available in glibmm.
 */
Glib::ustring Util::wstring_to_utf8(const std::wstring& str)
{
  class ScopedCharArray
  {
  private:
    char* ptr_;

    ScopedCharArray(const ScopedCharArray&);
    ScopedCharArray& operator=(const ScopedCharArray&);

  public:
    explicit ScopedCharArray(char* ptr) : ptr_ (ptr) {}
    ~ScopedCharArray() { g_free(ptr_); }

    char* get() const { return ptr_; }
  };

  GError* error = 0;

#ifdef __STDC_ISO_10646__
  // Avoid going through iconv if wchar_t always contains UCS-4.
  glong n_bytes = 0;
  const ScopedCharArray buf (g_ucs4_to_utf8(reinterpret_cast<const gunichar*>(str.data()),
                                            str.size(), 0, &n_bytes, &error));
#else
  gsize n_bytes = 0;
  const ScopedCharArray buf (g_convert(reinterpret_cast<const char*>(str.data()),
                                       str.size() * sizeof(std::wstring::value_type),
                                       "UTF-8", "WCHAR_T", 0, &n_bytes, &error));
#endif /* !__STDC_ISO_10646__ */

  if (G_UNLIKELY(error))
  {
    g_warning("%s", error->message);
    g_error_free(error);
    return Glib::ustring();
  }

  return Glib::ustring(buf.get(), buf.get() + n_bytes);
}

bool Util::validate_encoding(const std::string& encoding)
{
  // GLib just ignores some characters that aren't used in encoding names,
  // so we have to parse the string for invalid characters ourselves.

  if (encoding.empty() || !Glib::Ascii::isalnum(*encoding.begin())
                       || !Glib::Ascii::isalnum(*encoding.rbegin()))
    return false;

  for (std::string::const_iterator p = encoding.begin() + 1; p != encoding.end(); ++p)
  {
    if (!Glib::Ascii::isalnum(*p) && is_significant_encoding_char(*p))
      return false;
  }

  // Better don't try to call Glib::convert() with identical input and output
  // encodings.  I heard the iconv on Solaris doesn't like that idea at all.

  if (!Util::encodings_equal(encoding, "UTF-8"))
    try
    {
      Glib::convert(std::string(), "UTF-8", encoding);
    }
    catch (const Glib::ConvertError& error)
    {
      if (error.code() == Glib::ConvertError::NO_CONVERSION)
        return false;
      throw;
    }

  return true;
}

/*
 * Test lhs and rhs for equality while ignoring case
 * and several separation characters used in encoding names.
 */
bool Util::encodings_equal(const std::string& lhs, const std::string& rhs)
{
  typedef std::string::const_iterator Iterator;

  Iterator       lhs_pos = lhs.begin();
  Iterator       rhs_pos = rhs.begin();
  const Iterator lhs_end = lhs.end();
  const Iterator rhs_end = rhs.end();

  for (;;)
  {
    while (lhs_pos != lhs_end && !is_significant_encoding_char(*lhs_pos))
      ++lhs_pos;
    while (rhs_pos != rhs_end && !is_significant_encoding_char(*rhs_pos))
      ++rhs_pos;

    if (lhs_pos == lhs_end || rhs_pos == rhs_end)
      break;

    if (Glib::Ascii::toupper(*lhs_pos) != Glib::Ascii::toupper(*rhs_pos))
      return false;

    ++lhs_pos;
    ++rhs_pos;
  }

  return (lhs_pos == lhs_end && rhs_pos == rhs_end);
}

Glib::ustring Util::shell_pattern_to_regex(const Glib::ustring& pattern)
{
  // Don't use Glib::ustring to accumulate the result since we might append
  // partial UTF-8 characters during processing.  Although this would work with
  // the current Glib::ustring implementation, it's definitely not a good idea.
  std::string result;
  result.reserve(std::max<std::string::size_type>(32, 2 * pattern.raw().size()));

  result.append("\\A", 2);

  int brace_level = 0;

  const std::string::const_iterator pend = pattern.raw().end();
  std::string::const_iterator       p    = pattern.raw().begin();
  std::string::const_iterator       pcc  = pend; // start of character class

  for (; p != pend; ++p)
  {
    if (*p == '\\')
    {
      // Always escape a single trailing '\' to avoid mangling the "\z"
      // terminator.  Never escape multi-byte or alpha-numeric characters.

      if (p + 1 == pend || Glib::Ascii::ispunct(*++p))
        result += '\\';

      result += *p;
    }
    else if (pcc == pend)
    {
      switch (*p)
      {
        case '*':
          result.append(".*", 2);
          break;

        case '?':
          result += '.';
          break;

        case '[':
          result += '[';
          pcc = p + 1;
          break;

        case '{':
          result.append("(?:", 3);
          ++brace_level;
          break;

        case '}':
          result += ')';
          --brace_level;
          break;

        case ',':
          result += (brace_level > 0) ? '|' : ',';
          break;

        case '^': case '$': case '.': case '+': case '(': case ')': case '|':
          result += '\\';
          // fallthrough

        default:
          result += *p;
          break;
      }
    }
    else // pcc != pend
    {
      switch (*p)
      {
        case ']':
          result += ']';
          if (p != pcc && !(p == pcc + 1 && (*pcc == '!' || *pcc == '^')))
            pcc = pend;
          break;

        case '!':
          result += (p == pcc) ? '^' : '!';
          break;

        default:
          result += *p;
          break;
      }
    }
  }

  result.append("\\z", 2);

  return result;
}

Glib::ustring Util::substitute_references(const Glib::ustring& substitution,
                                          const Glib::ustring& subject,
                                          const CaptureVector& captures)
{
  std::string result;
  result.reserve(2 * std::max(substitution.raw().size(), subject.raw().size()));

  std::vector<ModPos> modifiers;

  const std::string::const_iterator pend = substitution.raw().end();
  std::string::const_iterator       p    = substitution.raw().begin();

  for (; p != pend; ++p)
  {
    if (*p == '\\' && p + 1 != pend)
    {
      switch (*++p)
      {
        case 'L': case 'U': case 'l': case 'u': case 'E':
          modifiers.push_back(ModPos(result.size(), *p));
          break;

        case 'a':
          result += '\a';
          break;

        case 'e':
          result += '\033';
          break;

        case 'f':
          result += '\f';
          break;

        case 'n':
          result += '\n';
          break;

        case 'r':
          result += '\r';
          break;

        case 't':
          result += '\t';
          break;

        case 'c':
          parse_control_char(p, pend, result);
          break;

        case 'x':
          parse_hex_unichar(p, pend, result);
          break;

        case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7':
          parse_oct_unichar(p, pend, result);
          break;

        default:
          result += *p;
          break;
      }
    }
    else if (*p == '$' && p + 1 != pend)
    {
      std::pair<int, int> bounds;

      if (Glib::Ascii::isdigit(*++p) || (*p == '{' && std::find(p + 1, pend, '}') != pend))
      {
        const int index = parse_capture_index(p, pend);

        if (index >= 0 && unsigned(index) < captures.size())
          bounds = captures[index];
        else
          continue;
      }
      else switch (*p)
      {
        case '+':
          if (captures.size() > 1)
            bounds = captures.back();
          break;

        case '&':
          bounds = captures.front();
          break;

        case '`':
          bounds.first  = 0;
          bounds.second = captures.front().first;
          break;

        case '\'':
          bounds.first  = captures.front().second;
          bounds.second = subject.raw().size();
          break;

        default:
          result += '$';
          result += *p;
          continue;
      }

      if (bounds.first >= 0 && bounds.second > bounds.first)
        result.append(subject.raw(), bounds.first, bounds.second - bounds.first);
    }
    else // (*p != '\\' && *p != '$') || (p + 1 == pend)
    {
      result += *p;
    }
  }

  if (!modifiers.empty())
    result = apply_modifiers(result, modifiers);

  return result;
}

Glib::ustring Util::int_to_string(int number)
{
  std::wostringstream output;

  try // don't abort if the user-specified locale doesn't exist
  {
    output.imbue(std::locale(""));
  }
  catch (const std::runtime_error& error)
  {
    g_warning("%s", error.what());
  }

  output << number;

  return Util::wstring_to_utf8(output.str());
}

Glib::ustring Util::filename_short_display_name(const std::string& filename)
{
  const std::string homedir = Glib::get_home_dir();
  const std::string::size_type len = homedir.length();

  if (filename.length() >= len
      && (filename.length() == len || G_IS_DIR_SEPARATOR(filename[len]))
      && filename.compare(0, len, homedir) == 0)
  {
    std::string short_name (1, '~');
    short_name.append(filename, len, std::string::npos);

    return Glib::filename_display_name(short_name);
  }

  return Glib::filename_display_name(filename);
}

Glib::ustring Util::color_to_string(const Gdk::Color& color)
{
  std::ostringstream output;

  output.imbue(std::locale::classic());
  output.setf(std::ios::hex, std::ios::basefield);
  output.setf(std::ios::uppercase);
  output.fill('0');

  output << '#' << std::setw(2) << scale_to_8bit(color.get_red())
                << std::setw(2) << scale_to_8bit(color.get_green())
                << std::setw(2) << scale_to_8bit(color.get_blue());

  return output.str();
}

int Util::enum_from_nick_impl(GType type, const Glib::ustring& nick)
{
  const ScopedTypeClass type_class (type);

  GEnumClass *const enum_class = G_ENUM_CLASS(type_class.get());
  GEnumValue *const enum_value = g_enum_get_value_by_nick(enum_class, nick.c_str());

  g_return_val_if_fail(enum_value != 0, enum_class->minimum);

  return enum_value->value;
}

Glib::ustring Util::enum_to_nick_impl(GType type, int value)
{
  const ScopedTypeClass type_class (type);

  GEnumClass *const enum_class = G_ENUM_CLASS(type_class.get());
  GEnumValue *const enum_value = g_enum_get_value(enum_class, value);

  g_return_val_if_fail(enum_value != 0, "");

  return enum_value->value_nick;
}
