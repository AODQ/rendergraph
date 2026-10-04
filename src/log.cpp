#include "log.hpp"

#include "imgui.h"

#include <vector>

static std::vector<std::string> sLogMessages;
static char sLogBuffer[8192];

void rg_assert(bool const condition, char const * const message, ...) {
	if (condition) { return; }
	va_list args;
	va_start(args, message);
	vsnprintf(sLogBuffer, sizeof(sLogBuffer), message, args);
	va_end(args);
	sLogMessages.emplace_back(sLogBuffer);
	printf("ASSERT: '%s'\n", sLogBuffer);
}

void rg_error(char const * const message, ...) {
	va_list args;
	va_start(args, message);
	vsnprintf(sLogBuffer, sizeof(sLogBuffer), message, args);
	va_end(args);
	sLogMessages.emplace_back(sLogBuffer);
	printf("ERROR: '%s'\n", sLogBuffer);
}

void rg_imgui_draw() {
	ImGui::Begin("Log");
	for (std::string const & message : sLogMessages) {
		ImGui::TextUnformatted(message.c_str());
	}
	ImGui::End();
}
