/// Copyright 2017-2019 Ghassan.pl
/// Usage of the works is permitted provided that this instrument is retained with
/// the works, so that any entity that uses the works is notified of this instrument.
/// DISCLAIMER: THE WORKS ARE WITHOUT WARRANTY.

#include "Common.h"
#include "Parse.h"
#include "ReflectionDataBuilding.h"
//#include <args.hxx>
#include <sstream>
#include <vector>
#include <thread>
#include <future>
#include <sqlite_orm/sqlite_orm.h>

int main(int argc, const char* argv[])
{
	/// If executable changed, it's newer than the files it created in the past, so they need to be rebuild
	ChangeTime = std::filesystem::last_write_time(argv[0]).time_since_epoch().count();
	
	/*
	args::PositionalList<std::filesystem::path> paths_list{ parser, "files", "Files or directories to scan", args::Options::Required };
	*/
	if (argc != 2)
	{
		std::cerr << "Syntax: " << std::filesystem::path{ argv[0] }.filename() << " <options file>\n";
		return 1;
	}

	std::ifstream ifs{ argv[1] };
	if (!ifs.is_open())
	{
		std::cerr << "Could not open `"<<argv[1]<<"'\n";
		return 2;
	}
	
	try
	{
		Options options { json::parse(ifs) };

		std::vector<std::filesystem::path> final_files;
		for (auto& path : options.PathsToScan)
		{
			if (std::filesystem::is_directory(path))
			{
				auto add_files = [&](const std::filesystem::path& file) {
					auto u8file = file.string();
					auto full = string_view{ u8file };
					if (full.ends_with(options.MirrorExtension)) return;

					auto ext = file.extension().string();
					std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
					if (!std::filesystem::is_directory(file) && std::find(options.ExtensionsToScan.begin(), options.ExtensionsToScan.end(), ext) != options.ExtensionsToScan.end())
					{
						final_files.push_back(file);
					}
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
				final_files.push_back(std::move(path));
		}

		PrintLine("{} reflectable files found", final_files.size());

		std::vector<std::future<bool>> parsers;
		/// Parse all types
		for (auto& file : final_files)
		{
			parsers.push_back(std::async(ParseClassFile, std::filesystem::path(file), options));
		}

		auto success = std::all_of(parsers.begin(), parsers.end(), [](auto& future) { return future.get(); });
		if (!success)
			return -1;

		/// Create artificial methods, knowing all the reflected classes
		CreateArtificialMethods();

		/// Output artifacts
		std::atomic<size_t> modified_files = 0;

		std::vector<std::future<void>> futures;
		for (auto& file : GetMirrors())
		{
			futures.push_back(std::async([&]() {
				size_t mod = 0;
				BuildMirrorFile(file, mod, options);
				modified_files += mod;
			}));
		}
		for (auto& future : futures)
			future.get(); /// to propagate exceptions
		futures.clear();

		/// Check if 

		const auto cwd = std::filesystem::absolute(options.ArtifactPath.empty() ? std::filesystem::current_path() : std::filesystem::path{ options.ArtifactPath });
		std::filesystem::create_directories(cwd);

		const bool type_list_missing = !std::filesystem::exists(cwd / "Classes.reflect.h") || options.Force;
		const bool include_list_missing = !std::filesystem::exists(cwd / "Includes.reflect.h") || options.Force;
		const bool json_db_missing = options.CreateDatabase && (!std::filesystem::exists(cwd / "ReflectDatabase.json") || options.Force);
		if (options.CreateArtifacts && (modified_files || type_list_missing || include_list_missing || json_db_missing))
		{
			futures.push_back(std::async(CreateTypeListArtifact, cwd, options));
			futures.push_back(std::async(CreateIncludeListArtifact, cwd, options));
			if (options.CreateDatabase)
				futures.push_back(std::async(CreateJSONDBArtifact, cwd, options));
		}

		const bool create_reflector = !std::filesystem::exists(cwd / "Reflector.h") || options.Force;

		if (create_reflector)
			futures.push_back(std::async(CreateReflectorHeaderArtifact, cwd, options));

		for (auto& future : futures)
			future.get(); /// to propagate exceptions
		futures.clear();

		if (options.Verbose)
		{
			if (!create_reflector)
				PrintLine("{} exists, skipping", (cwd / "Reflector.h").string());
		}

		if (!options.Quiet)
		{
			if (modified_files)
				PrintLine("{} mirror files changed", modified_files);
			else
				PrintLine("No mirror files changed");
		}
	}
	catch (json::parse_error e)
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
