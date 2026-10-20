#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace netignite {

/**
 * Splits a console line into arguments. Arguments are separated by spaces; double quotes group
 * words (`wifi "My Network" secret`) and a backslash escapes the next character. Returns false
 * for an unterminated quote.
 */
bool splitCommandLine(std::string_view line, std::vector<std::string>& out);

}  // namespace netignite
