//
// RegexString.cc for pekwm
// Copyright ©  2003-2008 Claes Nästén <me{@}pekdon{.}net>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif // HAVE_CONFIG_H

#include <iostream>
#include <cstdlib>

#include "RegexString.hh"
#include "Util.hh"

using std::cerr;
using std::endl;
using std::list;
using std::string;
using std::wstring;

//! @brief RegexString constructor.
RegexString::RegexString (void)
    : _reg_ok(false), _ref_max(1)
{
}

//! @brief RegexString constructor with default search
RegexString::RegexString (const std::wstring &str, bool full)
  : _reg_ok(false), _ref_max(1)
{
  parse_match(str, full);
}

//! @brief RegexString destructor.
RegexString::~RegexString (void)
{
    free_regex();
}

//! @brief Simple ed s command lookalike.
bool
RegexString::ed_s (std::wstring &str)
{
    if (! _reg_ok) {
        return false;
    }

    string mb_str(Util::to_mb_str(str));

    const char *c_str = mb_str.c_str();
    regmatch_t *matches = new regmatch_t[_ref_max];

    // Match
    if (regexec(&_regex, mb_str.c_str(), _ref_max, matches, 0)) {
        delete [] matches;
        return false;
    }

    string result;
    uint ref, size;

    list<RegexString::Part>::iterator it(_ref_list.begin ());
    for (; it != _ref_list.end(); ++it) {
        if (it->get_reference() >= 0) {
            ref = it->get_reference();

            if (matches[ref].rm_so != -1) {
                size = matches[ref].rm_eo - matches[ref].rm_so;
                result.append(string(c_str + matches[ref].rm_so, size));
            }
        } else {
            result.append(Util::to_mb_str(it->get_string()));
        }
    }

    // Replace area regexp matched.
    size = matches[0].rm_eo - matches[0].rm_so;
    mb_str.replace(matches[0].rm_so, size, result);

    str = Util::to_wide_str(mb_str);

    return true;
}

//! @brief Parses match part of regular expression.
//! @param or_match Expression.
//! @param full Full expression if true (including flags). Defaults to false.
bool
RegexString::parse_match(const std::wstring &or_match, bool full)
{
    // Free resources
    if (_reg_ok) {
        free_regex();
    }

    if (or_match.size()) {
        int flags = REG_EXTENDED;
        string expression;
        wstring expression_str;

        if (full) {
            // Full regular expression syntax, parse out flags etc
            char sep = or_match[0];

            string::size_type pos = or_match.find_last_of(sep);
            if ((pos != 0) && (pos != string::npos)) {
                // Main expression
                expression_str = or_match.substr(1, pos - 1);

                // Expression flags
                for (string::size_type i = pos + 1; i < or_match.size(); ++i) {
                    switch (or_match[i]) {
                    case 'i':
                        flags |= REG_ICASE;
                        break;
                    default:
                        cerr << "Invalid flag for regular expression." << endl;
                        break;
                    }
                }
            } else {
                cerr << "Invalid format of regular expression." << endl;
            }

            expression = Util::to_mb_str(expression_str);

        } else {
            expression = Util::to_mb_str(or_match);
        }

        _reg_ok = ! regcomp(&_regex, expression.c_str(), flags);
    } else {
        _reg_ok = false;
    }

    return _reg_ok;
}

//! @brief Parses replace part of ed_s command.
//! Expects input in the style of /replace/me/. / can be any character
//! except \. References to sub expressions are made with \num. \0 Represents
//! the part of the string that matched.
bool
RegexString::parse_replace(const std::wstring &or_replace)
{
    _ref_max = 0;

    wstring part;
    wstring::size_type begin = 0, end = 0, last = 0;

    // Go through the string and split at \num points
    while ((end = or_replace.find_first_of('\\', begin)) != string::npos) {
        // Store string between references.
        if (end > last) {
            part = or_replace.substr(last, end - last);
            _ref_list.push_back(RegexString::Part(part));
        }

        // Get reference number.
        for (begin = ++end; isdigit(or_replace[end]); end++)
            ;

        if (end > begin) {
            // Convert number and add item.
            part = or_replace.substr(begin, end - last);
            int ref = strtol(Util::to_mb_str(part).c_str(), 0, 10);
            if (ref >= 0) {
                _ref_list.push_back(RegexString::Part(L"", ref));
                if (ref > _ref_max) {
                    _ref_max = ref;
                }
            }
        }

        last = end;
        begin = last + 1;
    }

    if (begin < or_replace.size()) {
        part = or_replace.substr(begin, or_replace.size() - begin);
        _ref_list.push_back(RegexString::Part(part));
    }

    _ref_max++;

    return true;
}

//! @brief Parses ed s style command. /from/to/
bool
RegexString::parse_ed_s(const std::wstring &or_ed_s)
{
    if (or_ed_s.size() < 3) {
        return false;
    }

    wchar_t c_delimeter = or_ed_s[0];
    string::size_type middle, end;

    // Middle.
    for (middle = 1; middle < or_ed_s.size(); middle++) {
        if ((or_ed_s[middle] == c_delimeter)
            && (or_ed_s[middle - 1] != '\\')) {
            break;
        }
    }

    // End.
    for (end = middle + 1; end < or_ed_s.size(); end++) {
        if ((or_ed_s[end] == c_delimeter) && (or_ed_s[end - 1] != '\\')) {
            break;
        }
    }

    wstring match, replace;
    match = or_ed_s.substr(1, middle - 1);
    replace = or_ed_s.substr(middle + 1, end - middle - 1);

    parse_match(match);
    parse_replace(replace);

    return true;
}

//! @brief Matches RegexString against or_rhs, needs successfull parse_match.
bool
RegexString::operator==(const std::wstring &or_rhs)
{
    if (! _reg_ok) {
        return false;
    }

    string rhs(Util::to_mb_str(or_rhs));

    return ! regexec(&_regex, rhs.c_str(), 0, 0, 0);
}

//! @brief Free resources used by RegexString.
void
RegexString::free_regex(void)
{
    if (_reg_ok) {
        regfree(&_regex);
        _reg_ok = false;
    }
}
