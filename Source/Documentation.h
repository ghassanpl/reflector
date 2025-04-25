#pragma once

#include "Common.h"

size_t GenerateDocumentation(Artifactory& factory, Options const& options);

inline std::string Escaped(std::string_view str)
{
	return replaced(std::string{str}, "<", "&lt;");
}
