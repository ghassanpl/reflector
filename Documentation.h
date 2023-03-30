#pragma once

#include "Common.h"

struct DocFile
{
	path TargetPath{};
	std::string Contents{};
};

std::vector<DocFile> GenerateDocumentation(Options const& options);

bool CreateDocFileArtifact(path const& target_path, path const& final_path, Options const& options, DocFile const& doc_file);

std::string MakeLink(Declaration const* decl);

inline std::string Escaped(std::string_view str)
{
	return string_ops::replaced(std::string{str}, "<", "&lt;");
}
