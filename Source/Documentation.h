/// Copyright 2017-2025 Ghassan.pl
/// Usage of the works is permitted provided that this instrument is retained with
/// the works, so that any entity that uses the works is notified of this instrument.
/// DISCLAIMER: THE WORKS ARE WITHOUT WARRANTY.

#pragma once

#include "Common.h"

size_t GenerateDocumentation(Artifactory& factory, Options const& options);

inline std::string Escaped(std::string_view str)
{
	return replaced(std::string{str}, "<", "&lt;");
}
