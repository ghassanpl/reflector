/// Copyright 2017-2019 Ghassan.pl
/// Usage of the works is permitted provided that this instrument is retained with
/// the works, so that any entity that uses the works is notified of this instrument.
/// DISCLAIMER: THE WORKS ARE WITHOUT WARRANTY.

#include "Common.h"
#include "Parse.h"
#include "ReflectionDataBuilding.h"
#include <args.hxx>
#include <sstream>
#include <vector>
#include <thread>
#include <future>

int main(int argc, const char* argv[])
{
	/// If executable changed, it's newer than the files it created in the past, so they need to be rebuild
	ChangeTime = std::filesystem::last_write_time(argv[0]).time_since_epoch().count();
	
	args::ArgumentParser parser{ "Reflector Tool" };
	parser.helpParams.addChoices = true;
	parser.helpParams.addDefault = true;
	parser.helpParams.width = 160;
	parser.helpParams.helpindent = 80;
	args::Group command_group{ parser, "Commands", args::Group::Validators::DontCare, args::Options::Required };

	args::HelpFlag h(parser, "help", "Show help", { 'h', "help" });
	args::Flag recursive{ parser, "recursive", "Recursively search the provided directories for files", {'r', "recursive"}, args::Options::Single };
	args::Flag quiet{ parser, "quiet", "Don't print out created file names",{ 'q', "quiet" }, args::Options::Single };
	args::Flag force{ parser, "force", "Ignore timestamps, regenerate all files", { 'f', "force" }, args::Options::Single };
	args::Flag verbose{ parser, "verbose", "Print additional information",{ 'v', "verbose" }, args::Options::Single };
	args::Flag use_json{ parser, "json", "Output code that uses nlohmann::json to store class attributes", { 'j', "json" }, args::Options::Single };
	args::Flag create_db{ parser, "db", "Create a JSON database with reflection data", { 'd', "database" }, args::Options::Single };
	args::ValueFlag<std::string> annotation_prefix { parser, "annotation prefix", "The prefix for all annotation macros", { "aprefix" }, "R", args::Options::Single | args::Options::HiddenFromUsage };
	args::ValueFlag<std::string> reflection_prefix{ parser, "reflection prefix", "The prefix for all autogenerated macros this tool will generate", { "rprefix" }, "REFLECT", args::Options::Single | args::Options::HiddenFromUsage };
	args::PositionalList<std::filesystem::path> paths_list{ parser, "files", "Files or directories to scan", args::Options::Required };

	try
	{
		parser.ParseCLI(argc, argv);

		Options options { recursive, quiet, force, verbose, use_json, annotation_prefix.Get(), reflection_prefix.Get() };

		std::vector<std::filesystem::path> final_files;
		for (auto& path : paths_list)
		{
			if (std::filesystem::is_directory(path))
			{
				auto add_files = [&](const std::filesystem::path& file) {
					auto u8file = file.string();
					auto full = string_view{ u8file };
					if (full.ends_with(".mirror.h")) return;

					auto ext = file.extension();
					if (!std::filesystem::is_directory(file) && (ext == ".cpp" || ext == ".hpp" || ext == ".h"))
					{
						final_files.push_back(file);
					}
				};
				if (recursive)
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

		PrintLine(final_files.size(), " reflectable files found");

		std::vector<std::future<bool>> parsers;
		/// Parse all types
		for (auto& file : final_files)
		{
			parsers.push_back(std::async(ParseClassFile, std::filesystem::path(file), options));
		}

		for (auto& done : parsers)
		{
			if (!done.get())
				return -1;
		}

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
		futures.clear();

		auto cwd = std::filesystem::current_path();
		if (modified_files)
		{
			futures.push_back(std::async(CreateTypeListArtifact, cwd));
			futures.push_back(std::async(CreateIncludeListArtifact, cwd));
			if (create_db)
				futures.push_back(std::async(CreateJSONDBArtifact, cwd));
		}

		bool create_reflector = !std::filesystem::exists(cwd / "Reflector.h") || options.Force;

		if (create_reflector)
			futures.push_back(std::async(CreateReflectorHeaderArtifact, cwd, options));

		futures.clear();

		if (options.Verbose)
		{
			if (create_reflector)
				PrintLine("Created ", cwd / "Reflector.h");
			else
				PrintLine(cwd / "Reflector.h", " exists, skipping");
		}

		if (!options.Quiet)
		{
			if (modified_files)
				PrintLine(modified_files, " mirror files changed");
			else
				PrintLine("No mirror files changed");
		}
	}
	catch (args::Help)
	{
		std::cout << parser.Help();
	}
	catch (args::Error& e)
	{
		std::cerr << e.what() << std::endl << parser;
		return 1;
	}

	return 0;
}
