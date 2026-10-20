#include "netignite/CommandLine.h"

namespace netignite {

bool splitCommandLine(std::string_view line, std::vector<std::string>& out) {
	out.clear();
	std::string current;
	bool inArgument = false;
	bool quoted = false;

	for (size_t i = 0; i < line.size(); i++) {
		const char c = line[i];

		if (c == '\\' && i + 1 < line.size()) {
			current += line[++i];
			inArgument = true;
			continue;
		}
		if (c == '"') {
			quoted = !quoted;
			inArgument = true;
			continue;
		}
		if (!quoted && (c == ' ' || c == '\t')) {
			if (inArgument) {
				out.push_back(current);
				current.clear();
				inArgument = false;
			}
			continue;
		}
		current += c;
		inArgument = true;
	}

	if (quoted) return false;
	if (inArgument) out.push_back(current);
	return true;
}

}  // namespace netignite
