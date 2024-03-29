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

	if constexpr (!BootstrapBuild)
	{
		std::vector<std::string> unsupported_options;
		auto& options_class = Options::StaticGetReflectionData();
		for (auto& [name, value] : mOptionsFile.items())
		{
			if (options_class.FindField(name) == nullptr && !name.starts_with('#'))
				unsupported_options.push_back(name);
		}

		if (!unsupported_options.empty())
			throw std::runtime_error{ std::format("The following options are not supported: '{}'; if you want to comment out options, prefix their name with #", string_ops::join(unsupported_options, ", ")) };
	}

#else
	/// Bootstrap mode getters
	Files = mOptionsFile["Files"];
#define OPTION(name, default_value) name = mOptionsFile.value(#name, default_value)
	OPTION(ArtifactPath, "./Reflection/");
	OPTION(MacroPrefix, "REFL");
	OPTION(Files, ".");
	OPTION(Quiet, true);
	OPTION(Force, false);
	OPTION(JSON.Use, true);
	OPTION(CreateDatabase, true);
	Documentation.InlineDocNotes = {"Required"};
	Documentation.TargetDirectory = "../docs/";;
	Documentation.ClearTargetDirectory = true;
#endif

	auto EnsureAbsoluteToOptions = [options_file_path = mOptionsFilePath] (std::filesystem::path p) {
		if (p.empty() || p.is_relative())
			return std::filesystem::absolute(options_file_path.parent_path() / path{ std::move(p) });
		return p;
	};

	ArtifactPath = EnsureAbsoluteToOptions(ArtifactPath);

	if (Files.is_null())
		throw std::exception{ "Options file missing `Files' entry" };

	if (Files.is_array())
	{
		for (auto const& file : Files)
		{
			if (!file.is_string())
				throw std::exception{ "`Files' array must contain only strings" };
			mPathsToScan.emplace_back(EnsureAbsoluteToOptions(file).string());
		}
	}
	else if (Files.is_string())
		mPathsToScan.emplace_back(EnsureAbsoluteToOptions(Files).string());
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
}
