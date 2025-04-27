/// Copyright 2017-2025 Ghassan.pl
/// Usage of the works is permitted provided that this instrument is retained with
/// the works, so that any entity that uses the works is notified of this instrument.
/// DISCLAIMER: THE WORKS ARE WITHOUT WARRANTY.

#include "FileWriter.h"
#include "Options.h"

#include <fstream>
#include <sstream>
#include <ghassanpl/mmap.h>
#include <ghassanpl/mmap_impl.h>
#include <ghassanpl/hashes.h>

void FileWriter::WriteLine()
{
	mOutput << EndLineCharacters;
}

void FileWriter::EnsurePCH()
{
	if (!mOptions.PrecompiledHeader.empty())
		WriteLine("#include \"{}\"", mOptions.PrecompiledHeader);
}

bool FileWriter::FilesAreDifferent(path const& f1, path const& f2)
{
	if (exists(f1) != exists(f2)) return true;
	if (equivalent(f1, f2)) return false;
	const auto f1filesize = file_size(f1);
	if (f1filesize != file_size(f2)) return true;
	if (f1filesize == 0) return false; /// This guard is here because we cannot map 0-sized files

	const auto f1map = ghassanpl::make_mmap_source<char>(f1);
	const auto f2map = ghassanpl::make_mmap_source<char>(f2);

	const auto h1 = fnv64(f1map);
	const auto h2 = fnv64(f2map);
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
				copy_file(source_path, target_path, std::filesystem::copy_options::overwrite_existing);
				++this->mModifiedFiles;

				if (!options.Quiet)
					PrintLine("Copied file '{}' to '{}'", source_path.string(), target_path.string());
			}
		}
		catch (std::exception const& e)
		{
			ReportError(target_path, 0, std::format("Exception when building this artifact: {}\n", e.what()));
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
			if (const bool target_exists = exists(target_path);
				!target_exists || FileWriter::FilesAreDifferent(source_path, target_path))
			{
				std::error_code ec;
				if (!target_exists || (std::filesystem::remove(target_path, ec) && ec != std::errc{}))
					create_hard_link(source_path, target_path, ec);
				if (ec != std::errc{})
				{
					copy_file(source_path, target_path, std::filesystem::copy_options::overwrite_existing);
					if (!options.Quiet)
						PrintLine("Copied file '{}' as '{}'", source_path.string(), target_path.string());
				}
				else
				{
					if (!options.Quiet)
						PrintLine("Linked file '{}' as '{}'", source_path.string(), target_path.string());
				}

				/// Since we're trying to link, it means that we're basically trying to copy an "immutable shared" file.
				/// So let's make it read-only so that the user doesn't accidentally override the shared contents.
				permissions(target_path,
					std::filesystem::perms::owner_write
					| std::filesystem::perms::group_write
					| std::filesystem::perms::others_write,
					std::filesystem::perm_options::remove);

				++this->mModifiedFiles;
			}
		}
		catch (std::exception const& e)
		{
			ReportError(target_path, 0, std::format("Exception when building this artifact: {}\n", e.what()));
		}
		--this->mArtifactsToFinish;
	}));
}

bool Artifactory::Write(path const& target_path, std::string contents) const
{
	const bool write_to_file = options.Force || [&] {
		if (!exists(target_path))
			return true;

		const auto target_file_size = file_size(target_path);
		if (target_file_size != contents.size())
			return true;

		if (target_file_size == 0)
			return false;

		const auto target_file_map = ghassanpl::make_mmap_source<char>(target_path);
		
		const auto h1 = fnv64(contents);
		const auto h2 = fnv64(target_file_map);
		return h1 != h2;
	}();

	if (write_to_file)
	{
		create_directories(target_path.parent_path());

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

		for (auto& future : mFutures)
			future.get(); /// to propagate exceptions
		mFutures.clear();

		return mModifiedFiles.exchange(0);
	}
	return 0;
}
