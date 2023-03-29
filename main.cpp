/// Copyright 2017-2019 Ghassan.pl
/// Usage of the works is permitted provided that this instrument is retained with
/// the works, so that any entity that uses the works is notified of this instrument.
/// DISCLAIMER: THE WORKS ARE WITHOUT WARRANTY.

#include "Common.h"
#include "Parse.h"
#include "ReflectionDataBuilding.h"
#include "Documentation.h"
//#include <args.hxx>
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

	bool FilesAreDifferent(path const& f1, path const& f2) const
	{
		namespace fs = std::filesystem;

		if (fs::exists(f1) != fs::exists(f2)) return true;
		auto f1filesize = fs::file_size(f1);
		if (f1filesize != fs::file_size(f2)) return true;
		if (f1filesize == 0) return false; /// This guard is here because we cannot map 0-sized files

		const auto f1map = ghassanpl::make_mmap_source<char>(f1);
		const auto f2map = ghassanpl::make_mmap_source<char>(f2);

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
			std::make_tuple(path{ artifact.TargetPath }),
			std::make_tuple(std::ref(options)),
			std::make_tuple(std::forward<ARGS>(args)...)
		);
		artifact.Builder = [functor = std::forward<FUNCTOR>(functor), args = std::move(functor_args), this](ArtifactToBuild const& artifact) mutable {
			if (options.Verbose)
				PrintLine("Creating file {} to be moved to {}...", artifact.TargetTempPath.string(), artifact.TargetPath.string());
			if (std::apply(functor, std::move(args)))
			{
				if (options.Verbose)
					PrintLine("Created.");
				if (this->options.Force || FilesAreDifferent(artifact.TargetTempPath, artifact.TargetPath))
				{
					if (options.Verbose)
						PrintLine("Moved.");
					std::filesystem::create_directories(artifact.TargetPath.parent_path());
					std::filesystem::copy_file(artifact.TargetTempPath, artifact.TargetPath, std::filesystem::copy_options::overwrite_existing);
					std::filesystem::remove(artifact.TargetTempPath);
					++this->mModifiedFiles;

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
		for (size_t i = 0; i < mFutures.size(); ++i)
		{
			try
			{
				mFutures[i].get(); /// to propagate exceptions
			}
			catch (std::exception const& e)
			{
				std::cerr << std::format("Exception when building artifact `{}`: {}\n", mArtifactsToBuild[i].TargetPath.string(), e.what());
				throw;
			}
		}

		mFutures.clear();
		mArtifactsToBuild.clear();

		return mModifiedFiles.load();
	}
};

int main(int argc, const char* argv[])
{
	/// If executable changed, it's newer than the files it created in the past, so they need to be rebuild
	ChangeTime = std::filesystem::last_write_time(argv[0]).time_since_epoch().count();

	try
	{
		if (BootstrapBuild)
		{
			auto exe = std::filesystem::absolute(path{ argv[0] });
			
			if (argc > 1 || exe.parent_path() != std::filesystem::current_path())
			{
				std::cerr << format("Error: {} has been build in bootstrap mode. Run it from its directory ({}), and rebuild it to create the final executable. See https://github.com/ghassanpl/reflector/wiki/Building for more information on what this means.\n", exe.filename().string(), exe.parent_path().string());
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

		std::vector<std::filesystem::path> final_files;
		for (auto& path : options.GetPathsToScan())
		{
			std::cout << std::format("Looking in '{}'...\n", std::filesystem::absolute(path).string());
			if (std::filesystem::is_directory(path))
			{
				auto add_files = [&](const std::filesystem::path& file) {
					if (file.string().ends_with(options.MirrorExtension)) return;

					auto ext = file.extension().string();
					std::ranges::transform(ext, ext.begin(), ::tolower);
					if (!std::filesystem::is_directory(file) && std::ranges::find(options.ExtensionsToScan, ext) != options.ExtensionsToScan.end())
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
		for (const auto& file : final_files)
		{
			parsers.push_back(std::async(ParseClassFile, std::filesystem::path(file), options));
		}

		const auto success = std::ranges::all_of(parsers, [](auto& future) { return future.get(); });
		if (!success)
			return -1;

		/// Create artificial methods, knowing all the reflected classes
		CreateArtificialMethods(options);

		Artifactory factory{ options };

		size_t files_changed = 0;

		for (auto& file : GetMirrors())
		{
			auto file_path = file.SourceFilePath;
			file_path.concat(options.MirrorExtension);

			auto file_change_time = FileNeedsUpdating(file_path, file.SourceFilePath, options);
			if (file_change_time == 0) continue;

			factory.QueueArtifact(file_path, BuildMirrorFile, std::ref(file), file_change_time);
		}
		files_changed += factory.Run();

		static auto make_copy_file_artifact_func = [](path from) {
			return [from = std::move(from)](path const& target_path, path const& final_path, const Options& options) {
				std::filesystem::copy_file(from, target_path, std::filesystem::copy_options::overwrite_existing);
				return true;
			};
		};

		std::filesystem::create_directories(options.ArtifactPath);
		factory.QueueArtifact(options.ArtifactPath / "Reflector.h", CreateReflectorHeaderArtifact);
		factory.QueueArtifact(options.ArtifactPath / "Database.reflect.cpp", CreateReflectorDatabaseArtifact);
		if (options.CreateArtifacts)
		{
			factory.QueueArtifact(options.ArtifactPath / "Includes.reflect.h", CreateIncludeListArtifact);
			factory.QueueArtifact(options.ArtifactPath / "Classes.reflect.h", CreateTypeListArtifact);
			factory.QueueArtifact(options.ArtifactPath / "ReflectorClasses.h", make_copy_file_artifact_func(options.GetExePath().parent_path() / "Include" / "ReflectorClasses.h"));
			factory.QueueArtifact(options.ArtifactPath / "ReflectorUtils.h", make_copy_file_artifact_func(options.GetExePath().parent_path() / "Include" / "ReflectorUtils.h"));
		}
		if (options.CreateDatabase)
			factory.QueueArtifact(options.ArtifactPath / "ReflectDatabase.json", CreateJSONDBArtifact);

		if (options.Documentation.Generate)
		{
			auto doc_files = GenerateDocumentation(options);
			for (auto& doc_file : doc_files)
			{
				factory.QueueArtifact(doc_file.TargetPath, CreateDocFileArtifact, std::move(doc_file));
			}
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
