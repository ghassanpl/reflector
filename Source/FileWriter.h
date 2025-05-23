/// Copyright 2017-2025 Ghassan.pl
/// Usage of the works is permitted provided that this instrument is retained with
/// the works, so that any entity that uses the works is notified of this instrument.
/// DISCLAIMER: THE WORKS ARE WITHOUT WARRANTY.

#pragma once

#include "Common.h"
#include <future>

struct Artifactory;

struct ArtifactArgs
{
	std::stringstream* Output;
	path TargetPath{};
	::Options const& Options;
	Artifactory& Factory;
};

struct FileWriter
{
	static constexpr std::string_view EndLineCharacters = "\n"; /// TODO: Make this be determined from OS, and configurable

	std::stringstream& mOutput;
	size_t CurrentIndent = 0;
	bool InDefine = false;
	Options const& mOptions;

	struct Indenter
	{
		FileWriter& out;
		Indenter(FileWriter& out_) : out(out_) { out.CurrentIndent++; }
		~Indenter() { out.CurrentIndent--; }
	};

	Indenter Indent() { return Indenter{ *this }; }

	explicit FileWriter(ArtifactArgs const& args) : mOutput(*args.Output), mOptions(args.Options) {}

	template <typename... ARGS>
	void WriteLine(std::string_view str, ARGS&& ... args)
	{
		mOutput << std::string(CurrentIndent, '\t');
		mOutput << std::vformat(str, std::make_format_args(args...));
		if (InDefine)
			mOutput << " \\";
		mOutput << EndLineCharacters;
	}

	template <typename... ARGS>
	void StartDefine(ARGS&& ... args)
	{
		InDefine = true;
		WriteLine(std::forward<ARGS>(args)...);
		CurrentIndent++;
	}

	template <typename... ARGS>
	void EndDefine(ARGS&& ... args)
	{
		InDefine = false;
		WriteLine(std::forward<ARGS>(args)...);
		CurrentIndent--;
	}

	template <typename... ARGS>
	void StartBlock(ARGS&& ... args)
	{
		if (sizeof...(ARGS))
			WriteLine(std::forward<ARGS>(args)...);
		CurrentIndent++;
	}

	template <typename... ARGS>
	void EndBlock(ARGS&& ... args)
	{
		CurrentIndent--;
		if (sizeof...(ARGS))
			WriteLine(std::forward<ARGS>(args)...);
	}

	void WriteLine();

	void EnsurePCH();

	static bool FilesAreDifferent(path const& f1, path const& f2);
};

struct Artifactory
{
	explicit Artifactory(Options& opt) : options(opt) {}
	
	Options& options;

	template <typename FUNCTOR, typename... ARGS>
	void QueueArtifact(path const& target_path, FUNCTOR&& functor, ARGS&&... args)
	{
		std::unique_lock lock{mListMutex};
		++mArtifactsToFinish;

		auto functor_args = std::make_tuple(ArtifactArgs{ .TargetPath = target_path, .Options = options, .Factory = *this }, std::forward<ARGS>(args)...);
		mFutures.push_back(std::async(std::launch::async, [functor = std::forward<FUNCTOR>(functor), args = std::move(functor_args), target_path, this]() mutable {
			try
			{
				std::stringstream out_data;
				std::get<0>(args).Output = &out_data;
				if (std::apply(functor, std::move(args)))
				{
					out_data.flush();
					if (Write(target_path, std::move(out_data).str()))
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

	void QueueCopyArtifact(path target_path, path source_path);
	void QueueLinkOrCopyArtifact(path target_path, path source_path);

	bool Write(path const& target_path, std::string contents) const;

	size_t Wait();

private:
	
	std::atomic<size_t> mArtifactsToFinish = 0;
	std::atomic<size_t> mModifiedFiles = 0;
	std::vector<std::future<void>> mFutures;
	std::mutex mListMutex;
};