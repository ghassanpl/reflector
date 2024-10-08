#include "FileWriter.h"
#include "Options.h"

#include <fstream>
#include <sstream>
#include <ghassanpl/mmap.h>
#include <ghassanpl/mmap_impl.h>
#include <ghassanpl/hashes.h>

void FileWriter::WriteLine()
{
	mOutput << "\n";
}

void FileWriter::EnsurePCH()
{
	if (!mOptions.PrecompiledHeader.empty())
		WriteLine("#include \"{}\"", mOptions.PrecompiledHeader);
}

bool FileWriter::FilesAreDifferent(path const& f1, path const& f2)
{
	namespace fs = std::filesystem;

	if (fs::exists(f1) != fs::exists(f2)) return true;
	const auto f1filesize = fs::file_size(f1);
	if (f1filesize != fs::file_size(f2)) return true;
	if (f1filesize == 0) return false; /// This guard is here because we cannot map 0-sized files

	const auto f1map = ghassanpl::make_mmap_source<char>(f1);
	const auto f2map = ghassanpl::make_mmap_source<char>(f2);

	const auto h1 = ghassanpl::fnv64(f1map);
	const auto h2 = ghassanpl::fnv64(f2map);
	return h1 != h2;
}

void Artifactory::QueueCopyArtifact(path target_path, path source_path)
{
	std::unique_lock lock{mListMutex};
	++mArtifactsToFinish;

	mFutures.push_back(std::async(std::launch::async, [source_path = std::move(source_path), target_path= std::move(target_path), this]() {
		try
		{
			if (FileWriter::FilesAreDifferent(source_path, target_path))
			{
				std::filesystem::copy_file(source_path, target_path, std::filesystem::copy_options::overwrite_existing);
				++this->mModifiedFiles;

				if (!options.Quiet)
					PrintLine("Copied file '{}' to '{}'", source_path.string(), target_path.string());
			}
		}
		catch (std::exception const& e)
		{
			ReportError(target_path, 0, std::format("Exception when building artifact: {}\n", e.what()));
		}
		--this->mArtifactsToFinish;
	}));
}

void Artifactory::QueueLinkOrCopyArtifact(path target_path, path source_path)
{
	std::unique_lock lock{ mListMutex };
	++mArtifactsToFinish;

	mFutures.push_back(std::async(std::launch::async, [source_path = std::move(source_path), target_path = std::move(target_path), this]() {
		try
		{
			const bool target_exists = std::filesystem::exists(target_path);
			if (!target_exists || (!std::filesystem::equivalent(source_path, target_path) && FileWriter::FilesAreDifferent(source_path, target_path)))
			{
				std::error_code ec;
				if (!target_exists || (std::filesystem::remove(target_path, ec) && ec != std::errc{}))
					std::filesystem::create_hard_link(source_path, target_path, ec);
				if (ec != std::errc{})
				{
					std::filesystem::copy_file(source_path, target_path, std::filesystem::copy_options::overwrite_existing);
					if (!options.Quiet)
						PrintLine("Copied file '{}' as '{}'", source_path.string(), target_path.string());
				}
				else
				{
					if (!options.Quiet)
						PrintLine("Linked file '{}' as '{}'", source_path.string(), target_path.string());
				}
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

bool Artifactory::Write(path const& target_path, std::string contents) const
{
	bool write_to_file = false;
	if (options.Force)
		write_to_file = true;
	else
	{
		write_to_file = [&]
		{
			namespace fs = std::filesystem;

			if (!fs::exists(target_path))
				return true;

			const auto target_file_size = fs::file_size(target_path);
			if (target_file_size != contents.size())
				return true;

			if (target_file_size == 0)
				return false;

			const auto target_file_map = ghassanpl::make_mmap_source<char>(target_path);
			/*
			auto mismatch = std::ranges::mismatch(contents, target_file_map);
			if (mismatch.in1 == contents.end() && mismatch.in2 == target_file_map.end())
				return false;

			std::cout << std::format("[WRITING BECAUSE] Mismatch at {} pos {}: expected {}, got {}\n", target_path.string(), std::distance(contents.begin(), mismatch.in1), *mismatch.in1, *mismatch.in2);

			return true;
			/*/
			const auto h1 = ghassanpl::fnv64(contents);
			const auto h2 = ghassanpl::fnv64(target_file_map);
			return h1 != h2;
			//*/
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
		PrintLine("Target file '{}' same as source, not moved.", target_path.string());
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
