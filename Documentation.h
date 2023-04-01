#pragma once

#include "Common.h"

size_t GenerateDocumentation(Artifactory& factory, Options const& options);

std::string MakeLink(Declaration const* decl);

inline std::string Escaped(std::string_view str)
{
	return string_ops::replaced(std::string{str}, "<", "&lt;");
}
