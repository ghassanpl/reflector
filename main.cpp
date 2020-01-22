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

using file_time_stamp = std::filesystem::file_time_type::duration;

std::map<path, struct DependendObject, std::less<>> all_files;

struct DependendObject
{
	path Path;

	file_time_stamp StoredModificationTime = {};
	file_time_stamp CurrentModificationTime = {};

	FileMirror Mirror;

	template <typename... DEPS>
	DependendObject(path const& path, DEPS&&... deps)
		: Path(std::filesystem::weakly_canonical(path))
	{
		if (std::filesystem::exists(Path))
			CurrentModificationTime = std::filesystem::last_write_time(Path).time_since_epoch();

		((AddDependency(std::forward<DEPS>(deps))), ...);
	}

	enum class BuildStatus
	{
		NeedsRebuild,
		Building,
		Failed,
		Built
	};

	BuildStatus Status() const { return mStatus; }

	bool CheckRebuildNeed(Options const& options)
	{
		if (options.Force || CurrentModificationTime == file_time_stamp{} || CurrentModificationTime != StoredModificationTime)
			mStatus = BuildStatus::NeedsRebuild;
		for (auto& dependency : mDependsOn)
		{
			if (all_files.at(dependency).CheckRebuildNeed(options))
				mStatus = BuildStatus::NeedsRebuild;
		}
		return mStatus == BuildStatus::NeedsRebuild;
	}

	BuildStatus Rebuild(Options const& options)
	{
		if (mStatus != BuildStatus::NeedsRebuild) 
			return mStatus;

		if (options.Verbose)
		{
			PrintLine("File {} was touched and requires rebuilding (file mod time: {}, stored mod time: {})\n", Path.string(), CurrentModificationTime.count(), StoredModificationTime.count());
		}

		mStatus = BuildStatus::Building;

		for (auto& dep : mDependsOn)
		{
			if (all_files.at(dep).Rebuild(options) == BuildStatus::Failed)
			{
				PrintLine("  Dependency failed to build: {}\n", dep);
				return mStatus = BuildStatus::Failed;
			}
		}

		if (mBuildFunc)
		{
			if (mBuildFunc(*this, options) == false)
			{
				PrintLine("  Function failed to build\n");
				return mStatus = BuildStatus::Failed;
			}
		}

		return mStatus = BuildStatus::Built;
	}

	DependendObject& AddDependency(path const& path) { mDependsOn.push_back(std::filesystem::weakly_canonical(path)); return *this; }
	DependendObject& AddDependency(DependendObject const& obj) { mDependsOn.push_back(obj.Path); return *this; }
	DependendObject& AddDependency(std::vector<path> const& obj) { for (auto& dep : obj) AddDependency(dep); return *this; }
	DependendObject& AddDependency(std::vector<DependendObject*> const& obj) { for (auto& dep : obj) AddDependency(*dep); return *this; }

	DependendObject& BuildFunction(std::function<bool(DependendObject&, Options const&)> func) { mBuildFunc = std::move(func); return *this; }
	DependendObject& BuildFunction(std::function<bool(path, Options const&)> func) { mBuildFunc = [func = std::move(func)](DependendObject& obj, Options const& options) { return func(obj.Path, options); }; return *this; }

	std::vector<path> const& Dependencies() const { return mDependsOn; }

	json ToJSON() const
	{
		auto j = json::object_t{
			{ "Path", Path.string() },
			{ "CurrentModTime", CurrentModificationTime.count() },
			{ "StoredModTime", StoredModificationTime.count() },
			{ "Status", mStatus }
		};
		auto& deps = j["DependsOn"] = json::array_t{};
		for (auto& dep : mDependsOn)
			deps.push_back(dep.string());
		return j;
	}

private:

	std::vector<path> mDependsOn;

	BuildStatus mStatus = BuildStatus::Built;

	std::function<bool(DependendObject&, Options const&)> mBuildFunc;
};

void LoadStoredModificationTimes(Options const& options)
{
	try 
	{
		auto db = json::parse(std::ifstream { path{options.OptionsFilePath}.concat("mtd") });
		for (auto&& [path, file] : all_files)
			file.StoredModificationTime = file_time_stamp{ db.value(path.string(), 0LL) };
	}
	catch (std::exception e)
	{
		if (!options.Quiet)
			fmt::print(std::cerr, "Modification time database missing or invalid, rebuilding\n");
		if (options.Verbose)
			fmt::print(std::cerr, "  Encountered error: {}\n", e.what());
		return;
	}
}

void SaveStoredModificationTimes(Options const& options)
{
	if (options.Verbose)
		PrintLine("Saving file modification times to {}", path{ options.OptionsFilePath }.concat("mtd").string());

	json value = json::object_t{};
	for (auto&& [path, file] : all_files)
	{
		value[path.string()] = std::filesystem::exists(path) ? std::filesystem::last_write_time(path).time_since_epoch().count() : 0;
	}
	std::ofstream out{ path{options.OptionsFilePath}.concat("mtd") };
	out << value.dump(2);
}

template <typename... DEPS>
DependendObject& AddFile(path path, DEPS&&... deps)
{
	path = std::filesystem::weakly_canonical(path);
	auto&& [it, inserted] = all_files.try_emplace(path, path, std::forward<DEPS>(deps)...);
	if (!inserted)
	{
		throw std::runtime_error(fmt::format("internal error: file {} already exists in dependency list!", path));
	}
	return it->second;
}

int main(int argc, const char* argv[])
{
	if (argc != 2)
	{
		std::cerr << "Syntax: " << path{ argv[0] }.filename() << " <options file>\n";
		return 1;
	}

	try
	{
		Options options{ argv[1] };

		const auto artifact_path = std::filesystem::absolute(options.ArtifactPath.empty() ? std::filesystem::current_path() : path{ options.ArtifactPath });
		const auto reflector_h_path = artifact_path / "Reflector.h";
		const auto classes_h_path = artifact_path / "Classes.reflect.h";
		const auto includes_h_path = artifact_path / "Includes.reflect.h";

		auto& exe_dep = AddFile(argv[0]);
		auto& options_dep = AddFile(argv[1]);

		std::vector<path> files_to_scan;
		std::vector<DependendObject*> mirror_files;
		for (auto& path_to_scan : options.PathsToScan)
		{
			if (std::filesystem::is_directory(path_to_scan))
			{
				auto add_files = [&](const path& file) {
					auto u8file = file.string();
					auto full = string_view{ u8file };
					if (full.ends_with(options.MirrorExtension)) return;
					if (file == reflector_h_path || file == classes_h_path || file == includes_h_path) return;

					auto ext = file.extension().string();
					std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
					if (!std::filesystem::is_directory(file) && std::find(options.ExtensionsToScan.begin(), options.ExtensionsToScan.end(), ext) != options.ExtensionsToScan.end())
					{
						files_to_scan.push_back(file);
					}
				};
				if (options.Recursive)
				{
					for (auto it = std::filesystem::recursive_directory_iterator{ std::filesystem::weakly_canonical(path_to_scan) }; it != std::filesystem::recursive_directory_iterator{}; ++it)
						add_files(*it);
				}
				else
				{
					for (auto it = std::filesystem::directory_iterator{ std::filesystem::weakly_canonical(path_to_scan) }; it != std::filesystem::directory_iterator{}; ++it)
						add_files(*it);
				}
			}
			else
				files_to_scan.push_back(std::move(path_to_scan));

		}

		auto& artificial_methods_dep = AddFile(artifact_path / "artificial_methods_dep");

		for (auto& final_file : files_to_scan)
		{
			auto& file_dep = AddFile(final_file, exe_dep, options_dep).BuildFunction([&](DependendObject& obj, Options const& opt) { return ParseClassFile(obj.Path, options, obj.Mirror); });
			artificial_methods_dep.AddDependency(file_dep);
		}

		artificial_methods_dep.BuildFunction([](path, Options const&) { 
			CreateArtificialMethods(); return true; 
		});

		for (auto& final_file : files_to_scan)
		{
			auto& mirror_dep = AddFile(path{ final_file } += ".mirror", final_file, artificial_methods_dep).BuildFunction([final_file = final_file, &options](DependendObject& obj, Options const& opt) { 
				auto& mirror = all_files.at(final_file).Mirror;
				if (mirror.Classes.size() > 0 || mirror.Enums.size() > 0)
					return BuildMirrorFile(mirror, options);
				return true;
			});
			mirror_files.push_back(&mirror_dep);
		}

		PrintLine("{} reflectable files found", files_to_scan.size());

		std::filesystem::create_directories(artifact_path);

		auto& reflector_dep = AddFile(reflector_h_path).BuildFunction(&CreateReflectorHeaderArtifact);

		auto& all_artifacts = AddFile(artifact_path / "all_artifacts_dep", reflector_dep, mirror_files);
		if (options.CreateArtifacts)
		{
			all_artifacts.AddDependency(AddFile(classes_h_path, mirror_files).BuildFunction(&CreateTypeListArtifact));
			all_artifacts.AddDependency(AddFile(includes_h_path, mirror_files).BuildFunction(&CreateIncludeListArtifact));
			if (options.CreateDatabase)
			{
				all_artifacts.AddDependency(AddFile(artifact_path / "ReflectDatabase.json", mirror_files).BuildFunction(&CreateJSONDBArtifact));
			}
		}

		LoadStoredModificationTimes(options);
		all_artifacts.CheckRebuildNeed(options);

		/*/
		std::ofstream alldeps_file{ "alldeps.json" };
		json alldeps = json::object_t{};
		for (auto&& [k, v] : all_files)
			alldeps[k.string()] = v.ToJSON();
		fmt::print(alldeps_file, "{}", alldeps.dump(2));
		alldeps_file.close();
		*/

		/// //////////////////////////////// ///

		const auto modified_files = std::count_if(all_files.begin(), all_files.end(), [](auto& it) { return it.second.Status() == DependendObject::BuildStatus::NeedsRebuild; });

		/*
		if (!options.Quiet)
		{
			if (modified_files)
				PrintLine("{} files changed", modified_files);
			else
				PrintLine("No files changed");
		}
		*/

		const auto status = all_artifacts.Rebuild(options);

		if (status != DependendObject::BuildStatus::Built)
		{
			PrintLine("Errors while building");
			return 5;
		}

		/// //////////////////////////////// ///

		SaveStoredModificationTimes(options);

	}
	catch (json::parse_error e)
	{
		std::cerr << "Error: Invalid options file:\n" << e.what() << "\n";
		return 3;
	}
	catch (std::exception & e)
	{
		std::cerr << "Error: " << e.what() << "\n";
		return 1;
	}
}
/*
int main(int argc, const char* argv[])
{
	/// If executable changed, it's newer than the files it created in the past, so they need to be rebuild
	ChangeTime = std::filesystem::last_write_time(argv[0]).time_since_epoch().count();
	
	if (argc != 2)
	{
		std::cerr << "Syntax: " << path{ argv[0] }.filename() << " <options file>\n";
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

		std::vector<path> final_files;
		for (auto& path : options.PathsToScan)
		{
			if (std::filesystem::is_directory(path))
			{
				auto add_files = [&](const path& file) {
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
					for (auto it = std::filesystem::recursive_directory_iterator{ std::filesystem::weakly_canonical(path) }; it != std::filesystem::recursive_directory_iterator{}; ++it)
						add_files(*it);
				}
				else
				{
					for (auto it = std::filesystem::directory_iterator{ std::filesystem::weakly_canonical(path) }; it != std::filesystem::directory_iterator{}; ++it)
						add_files(*it);
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
			parsers.push_back(std::async(ParseClassFile, path(file), options));
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

		const auto cwd = std::filesystem::absolute(options.ArtifactPath.empty() ? std::filesystem::current_path() : path{ options.ArtifactPath });
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
*/