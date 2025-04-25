/// Copyright 2017-2019 Ghassan.pl
/// Usage of the works is permitted provided that this instrument is retained with
/// the works, so that any entity that uses the works is notified of this instrument.
/// DISCLAIMER: THE WORKS ARE WITHOUT WARRANTY.

#include "Common.h"
#include "Parse.h"
#include "ReflectionDataBuilding.h"
#include "Documentation.h"
#include <future>

Options const* global_options = nullptr;

int main(int argc, const char* argv[])
{
	/// If executable changed, it's newer than the files it created in the past, so they need to be rebuild
	ExecutableChangeTime = std::filesystem::last_write_time(argv[0]).time_since_epoch().count();
	InvocationTime = std::chrono::system_clock::now().time_since_epoch().count();
	CaseInsensitiveFileSystem = (std::filesystem::path("A") <=> std::filesystem::path("a")) == std::strong_ordering::equivalent;

	try
	{
		if constexpr (BootstrapBuild)
		{
			auto exe = std::filesystem::absolute(path{ argv[0] });
			
			if (argc > 1 || exe.parent_path() != std::filesystem::current_path())
			{
				std::cerr << format("Error: {} has been build in bootstrap mode. Run it from its directory ({}), and rebuild it in non-boostrap mode to create the final executable. See https://github.com/ghassanpl/reflector/wiki/Building for more information on what this means.\n", exe.filename().string(), exe.parent_path().string());
				return 1;
			}
		}
		else
		{
			if (argc != 2)
			{
				std::cerr << "Syntax: " << std::filesystem::path{ argv[0] }.filename() << " <options file>\n";
				return 1;
			}
		}

		Options options{ argv[0], (BootstrapBuild ? "options_reflection.json" : argv[1])};
		global_options = &options;

		std::vector<std::filesystem::path> final_files;
		for (auto& path : options.GetPathsToScan())
		{
			std::cout << std::format("Looking in '{}'...\n", std::filesystem::absolute(path).string());
			if (std::filesystem::is_directory(path))
			{
				auto add_files = [&](const std::filesystem::path& file) {
					if (file.string().ends_with(options.MirrorExtension)) return;

					auto ext = file.extension().string();
					if (CaseInsensitiveFileSystem)
						std::ranges::transform(ext, ext.begin(), ::tolower);

					if (!std::filesystem::is_directory(file) && options.ExtensionsToScan.contains(ext))
						final_files.push_back(file);
				};
				if (options.Recursive)
				{
					for (auto it = std::filesystem::recursive_directory_iterator{ std::filesystem::canonical(path) }; it != std::filesystem::recursive_directory_iterator{}; ++it)
					{
						add_files(*it);
					}
				}
				else
				{
					for (auto it = std::filesystem::directory_iterator{ std::filesystem::canonical(path) }; it != std::filesystem::directory_iterator{}; ++it)
					{
						add_files(*it);
					}
				}
			}
			else
				final_files.push_back(path);
		}

		PrintLine("{} reflectable files found", final_files.size());

		std::vector<std::future<bool>> parsers;
		/// Parse all marked declarations in files
		for (const auto& file : final_files)
		{
			parsers.push_back(std::async(ParseClassFile, std::filesystem::path(file), options));
		}
		if (!std::ranges::all_of(parsers, [](auto& future) { return future.get(); }))
			return -1;

		RemoveEmptyMirrors();
		
		/// Create artificial methods, knowing all the reflected classes
		CreateArtificialMethodsAndDocument(options);

		SortMirrors();

		Artifactory factory{ options };

		size_t files_changed = 0;

		for (const auto& file : GetMirrors())
		{
			auto mirror_file_path = file->SourceFilePath;
			mirror_file_path.concat(options.MirrorExtension);

			auto file_change_time = ArtifactNeedsRegenerating(mirror_file_path, file->SourceFilePath, options);
			if (file_change_time == 0) continue;

			factory.QueueArtifact(mirror_file_path, BuildMirrorFile, std::ref(*file), file_change_time);

			if (options.ScriptBinding.SplitTypeListIntoHookupFiles)
			{
				auto hookup_file_path = mirror_file_path;
				hookup_file_path.replace_extension(options.ScriptBinding.HookupFileExtension);
				factory.QueueArtifact(hookup_file_path, BuildMirrorHookupFile, std::ref(*file));
			}
		}
		files_changed += factory.Wait();

		std::filesystem::create_directories(options.ArtifactPath);
		factory.QueueArtifact(options.ArtifactPath / "Reflector.h", CreateReflectorHeaderArtifact);
		factory.QueueArtifact(options.ArtifactPath / "Database.reflect.cpp", CreateReflectorDatabaseArtifact);
		
		factory.QueueArtifact(options.ArtifactPath / "Includes.reflect.h", CreateIncludeListArtifact);
		factory.QueueArtifact(options.ArtifactPath / "Classes.reflect.h", CreateTypeListArtifact);
		if (!BootstrapBuild)
		{
			/// TODO: Make linking these files optional; some people might want to change these files for some reason, as part of their
			/// build system, and we don't want to trample over our own files.
			factory.QueueLinkOrCopyArtifact(options.ArtifactPath / "Reflector.cpp", options.GetExePath().parent_path() / "Include" / "Reflector.cpp");
			factory.QueueLinkOrCopyArtifact(options.ArtifactPath / "ReflectorClasses.h", options.GetExePath().parent_path() / "Include" / "ReflectorClasses.h");
			factory.QueueLinkOrCopyArtifact(options.ArtifactPath / "ReflectorUtils.h", options.GetExePath().parent_path() / "Include" / "ReflectorUtils.h");
			if (options.AddGCFunctionality)
				factory.QueueLinkOrCopyArtifact(options.ArtifactPath / "ReflectorGC.h", options.GetExePath().parent_path() / "Include" / "ReflectorGC.h");
		}

		if (options.CreateDatabase)
			factory.QueueArtifact(options.ArtifactPath / "ReflectDatabase.json", CreateJSONDBArtifact);

		if (options.Documentation.Generate)
		{
			files_changed += GenerateDocumentation(factory, options);
		}

		files_changed += factory.Wait();

		if (!options.Quiet)
		{
			if (files_changed)
				PrintLine("{} files changed", files_changed);
			else
				PrintLine("No files changed");
		}

	}
	catch (json::parse_error const& e)
	{
		std::cerr << "Invalid options file:\n" << e.what() << "\n";
		return 3;
	}
	catch (std::exception& e)
	{
		std::cerr << e.what() << "\n";
		return 1;
	}

	return 0;
}
