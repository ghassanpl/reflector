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
#include <ghassanpl/mmap.h>
#include <xxhash.h>

struct Artifactory
{
	Options& options;

	struct ArtifactToBuild
	{
		path TargetPath;
		path TargetTempPath;
		//std::function<void(ArtifactToBuild const&)> Builder;
		std::move_only_function<void(ArtifactToBuild const&)> Builder;

		ArtifactToBuild() noexcept = default;
		ArtifactToBuild(ArtifactToBuild const&) noexcept = delete;
		ArtifactToBuild(ArtifactToBuild&&) noexcept = default;
		ArtifactToBuild& operator=(ArtifactToBuild const&) noexcept = delete;
		ArtifactToBuild& operator=(ArtifactToBuild&&) noexcept = default;
	};

	bool FilesAreDifferent(path const& f1, path const& f2)
	{
		namespace fs = std::filesystem;
		std::error_code ec{};
		
		if (fs::exists(f1) != fs::exists(f2)) return true;
		if (fs::file_size(f1) != fs::file_size(f2)) return true;

		auto f1map = ghassanpl::make_mmap_source<char>(f1);
		auto f2map = ghassanpl::make_mmap_source<char>(f2);

		const auto h1 = XXH64(f1map.data(), f1map.size(), 0);
		const auto h2 = XXH64(f2map.data(), f2map.size(), 0);
		return h1 != h2;
	}

	template <typename FUNCTOR, typename... ARGS>
	void QueueArtifact(path target_path, FUNCTOR&& functor, ARGS&&... args)
	{
		ArtifactToBuild& artifact = mArtifactsToBuild.emplace_back();
		artifact.TargetPath = std::move(target_path);
		artifact.TargetTempPath = std::filesystem::temp_directory_path() / std::format("{}", std::hash<std::filesystem::path>{}(artifact.TargetPath));
		auto functor_args = std::tuple_cat(
			std::make_tuple(path{ artifact.TargetTempPath }),
			std::make_tuple(std::ref(options)),
			std::make_tuple(args...)
		);
		artifact.Builder = [functor = std::forward<FUNCTOR>(functor), args = std::move(functor_args), this](ArtifactToBuild const& artifact) mutable {
			if (options.Verbose)
				PrintLine("Creating file {} to be moved to {}...", artifact.TargetTempPath.string(), artifact.TargetPath.string());
			if (std::apply(functor, args))
			{
				if (options.Verbose)
					PrintLine("Created.");
				if (this->options.Force || FilesAreDifferent(artifact.TargetTempPath, artifact.TargetPath))
				{
					if (options.Verbose)
						PrintLine("Moved.");
					std::filesystem::copy_file(artifact.TargetTempPath, artifact.TargetPath, std::filesystem::copy_options::overwrite_existing);
					std::filesystem::remove(artifact.TargetTempPath);
					this->mModifiedFiles++;

					if (!options.Quiet)
						PrintLine("Written file {}", artifact.TargetPath.string());
				}
				else if (options.Verbose)
					PrintLine("Files same, not moved.");
			}
			else if (options.Verbose)
				PrintLine("Not created.");
		};
	}

	std::vector<ArtifactToBuild> mArtifactsToBuild;
	std::vector<std::future<void>> mFutures;
	std::atomic<size_t> mModifiedFiles = 0;

	size_t Run()
	{
		mModifiedFiles = 0;

		for (auto& artifact : mArtifactsToBuild)
			mFutures.push_back(std::async(std::launch::async, std::move(artifact.Builder), std::ref(artifact)));
		for (auto& future : mFutures)
			future.get(); /// to propagate exceptions

		mFutures.clear();
		mArtifactsToBuild.clear();

		return mModifiedFiles.load();
	}
};

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

	try
	{
		Options options{ argv[0], argv[1] };

		const auto artifact_path = options.ArtifactPath = std::filesystem::absolute(options.ArtifactPath.empty() ? std::filesystem::current_path() : path{ options.ArtifactPath });
	
		std::vector<std::filesystem::path> final_files;
		for (auto& path : options.PathsToScan)
		{
			std::cout << std::format("Looking in '{}'...\n", std::filesystem::absolute(path).string());
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

		Artifactory factory{ options };

		size_t files_changed = 0;

		for (auto& file : GetMirrors())
		{
			auto file_path = file.SourceFilePath;
			file_path.concat(options.MirrorExtension);

			auto file_change_time = FileNeedsUpdating(file_path, file.SourceFilePath, options);
			if (file_change_time == 0) continue;

			factory.QueueArtifact(file_path, BuildMirrorFile, file, file_change_time, file_path);
		}
		files_changed += factory.Run();

		std::filesystem::create_directories(artifact_path);
		if (options.CreateArtifacts)
		{
			factory.QueueArtifact(artifact_path / "Classes.reflect.h", CreateTypeListArtifact);
			factory.QueueArtifact(artifact_path / "Includes.reflect.h", CreateIncludeListArtifact);
			if (options.CreateDatabase)
				factory.QueueArtifact(artifact_path / "ReflectDatabase.json", CreateJSONDBArtifact);
			factory.QueueArtifact(artifact_path / "Reflector.h", CreateReflectorHeaderArtifact, artifact_path / "Reflector.h");
			factory.QueueArtifact(artifact_path / "ReflectorClasses.h", CreateReflectorClassesHeaderArtifact);
			factory.QueueArtifact(artifact_path / "Database.reflect.cpp", CreateReflectorDatabaseArtifact);
		}
		files_changed += factory.Run();


		if (!options.Quiet)
		{
			if (files_changed)
				PrintLine("{} files changed", files_changed);
			else
				PrintLine("No files changed");
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
