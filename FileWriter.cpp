#include "FileWriter.h"
#include "Options.h"

#include <fstream>
#include <sstream>
#include <xxhash.h>
#include <ghassanpl/mmap.h>

//FileWriter::FileWriter() : mOutFile{ new std::stringstream{} }, mPath() {}

void FileWriter::WriteLine()
{
	mOutput << "\n";
}

bool FileWriter::FilesAreDifferent(path const& f1, path const& f2)
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

void Artifactory::QueueCopyArtifact(path target_path, path source_path)
{
	std::unique_lock lock{mListMutex};
	mArtifactsToFinish++;

	mFutures.push_back(std::async(std::launch::async, [source_path, target_path, this]() mutable {
		try
		{
			if (FileWriter::FilesAreDifferent(source_path, target_path))
			{
				std::filesystem::copy_file(source_path, target_path, std::filesystem::copy_options::overwrite_existing);
				++this->mModifiedFiles;
			}
		}
		catch (std::exception const& e)
		{
			ReportError(target_path, 0, std::format("Exception when building artifact: {}\n", e.what()));
		}
		--this->mArtifactsToFinish;
	}));
}

bool Artifactory::Write(path const& target_path, std::string contents)
{
	bool write_to_file = false;
	if (options.Force)
		write_to_file = true;
	else
	{
		write_to_file = [&]
		{
			namespace fs = std::filesystem;

			if (!fs::exists(target_path)) return true;
			const auto target_file_size = fs::file_size(target_path);
			if (target_file_size != contents.size()) return true;
			if (target_file_size == 0) return false;

			const auto target_file_map = ghassanpl::make_mmap_source<char>(target_path);

			const auto h1 = XXH64(contents.data(), contents.size(), 0);
			const auto h2 = XXH64(target_file_map.data(), target_file_map.size(), 0);
			return h1 != h2;
		}();
	}

	if (write_to_file)
	{
		std::filesystem::create_directories(target_path.parent_path());

		std::ofstream out{target_path, std::ofstream::binary};
		out.write(contents.data(), contents.size());
		out.close();

		if (!options.Quiet)
			PrintLine("Written file {}", target_path.string());
		return true;
	}

	if (options.Verbose)
		PrintLine("Files same, not moved.");
	return false;
}

size_t Artifactory::Wait()
{
	if (mArtifactsToFinish > 0)
	{
		mArtifactsToFinish.wait(0);

		for (size_t i = 0; i < mFutures.size(); ++i)
			mFutures[i].get(); /// to propagate exceptions
		mFutures.clear();

		return mModifiedFiles.exchange(0);
	}
	return 0;
}
