#include "Options.h"

#include <fstream>

Options::Options(path exe_path, path const& options_file_path)
	: mExePath(std::move(exe_path))
	, mOptionsFilePath(std::filesystem::canonical(options_file_path))
	, mOptionsFile(json::parse(std::fstream{ options_file_path }))
{
	if (!mOptionsFile.is_object())
		throw std::exception{ "Options file must contain a JSON object" };

#if __has_include("Options.h.mirror")
	nlohmann::adl_serializer<Options>::from_json(mOptionsFile, *this);
#else
	/// Bootstrap mode getters
	Files = mOptionsFile["Files"];
#define OPTION(name, default_value) name = mOptionsFile.value(#name, default_value);
	OPTION(ArtifactPath, "./Reflection/");
	OPTION(CreateArtifacts, false);
	OPTION(MacroPrefix, "REFL");
	OPTION(Files, ".");
	OPTION(Quiet, true);
	OPTION(Force, false);
	OPTION(JSON.Use, true);
	OPTION(CreateDatabase, true);
#endif

	/// TODO: Spit out warning for unrecognized option

	ArtifactPath = std::filesystem::absolute(ArtifactPath.empty() ? std::filesystem::current_path() : path{ ArtifactPath });

	if (Files.is_null())
		throw std::exception{ "Options file missing `Files' entry" };

	if (Files.is_array())
	{
		for (auto const& file : Files)
		{
			if (!file.is_string())
				throw std::exception{ "`Files' array must contain only strings" };
			mPathsToScan.emplace_back((std::string)file);
		}
	}
	else if (Files.is_string())
		mPathsToScan.emplace_back((std::string)Files);
	else
		throw std::exception{ "`Files' entry must be an array of strings or a string" };

#define X(e) if (e##AnnotationName.empty()) e##AnnotationName = AnnotationPrefix + #e

	X(Enum);
	X(Enumerator);
	X(Class);
	X(Body);
	X(Field);
	X(Method);
	X(Namespace);

	if (mOptionsFile.size() > 0 && Verbose)
	{
		for (auto& opt : mOptionsFile.items())
		{
			std::cerr << format("Warning: Unrecognized option: {}\n", opt.key());
		}
	}
}
