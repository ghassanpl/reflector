/// Copyright 2017-2019 Ghassan.pl
/// Usage of the works is permitted provided that this instrument is retained with
/// the works, so that any entity that uses the works is notified of this instrument.
/// DISCLAIMER: THE WORKS ARE WITHOUT WARRANTY.

#pragma once

#include "Common.h"
#include "Options.h"
#include <fstream>

#define TIMESTAMP_TEXT "/// TIMESTAMP: "

uint64_t FileNeedsUpdating(const path& target_path, const path& source_path, const Options& opts);

bool BuildMirrorFile(path const& file_path, path const& final_path, const Options& options, FileMirror const& file, uint64_t file_change_time);

bool CreateTypeListArtifact(path const& cwd, path const& final_path, Options const& options);
bool CreateIncludeListArtifact(path const& cwd, path const& final_path, Options const& options);
bool CreateJSONDBArtifact(path const& cwd, path const& final_path, Options const& options);
bool CreateReflectorHeaderArtifact(path const& target_path, path const& final_path, const Options& options);
bool CreateReflectorDatabaseArtifact(path const& target_path, path const& final_path, const Options& opts);

struct FileWriter
{
	std::unique_ptr<std::ostream> mOutFile;
	path mPath;
	size_t CurrentIndent = 0;
	bool InDefine = false;

	struct Indenter
	{
		FileWriter& out;
		Indenter(FileWriter& out_) : out(out_) { out.CurrentIndent++; }
		~Indenter() { out.CurrentIndent--; }
	};

	Indenter Indent() { return Indenter{ *this }; }

	FileWriter(path path) : mOutFile{ new std::ofstream{path} }, mPath(path) {
		mOutFile->exceptions(std::ofstream::failbit | std::ofstream::badbit);
		if (!static_cast<std::ofstream*>(mOutFile.get())->is_open())
			throw std::runtime_error(std::format("could not open file '{}' for writing", path.string()));
	}
	FileWriter() : mOutFile{ new std::stringstream{} }, mPath() {}

	template <typename... ARGS>
	void WriteLine(std::string_view str, ARGS&& ... args)
	{
		*mOutFile << std::string(CurrentIndent, '\t');
		//((mOutFile << std::forward<ARGS>(args)), ...);
		*mOutFile << std::vformat(str, std::make_format_args(std::forward<ARGS>(args)...));
		if (InDefine)
			*mOutFile << " \\";
		*mOutFile << '\n';
	}

	//void WriteJSON(json const& value);

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

	void WriteLine() const;

	void Close();

	~FileWriter();
};
