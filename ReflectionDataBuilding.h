/// Copyright 2017-2019 Ghassan.pl
/// Usage of the works is permitted provided that this instrument is retained with
/// the works, so that any entity that uses the works is notified of this instrument.
/// DISCLAIMER: THE WORKS ARE WITHOUT WARRANTY.

#pragma once

#include "Common.h"
#include <fstream>

#define TIMESTAMP_TEXT "/// TIMESTAMP: "

uint64_t FileNeedsUpdating(const path& target_path, const path& source_path, const Options& opts);

void BuildMirrorFile(FileMirror const& file, size_t& modified_files, const Options& opts);

void CreateTypeListArtifact(path const& cwd, Options const& options);
void CreateIncludeListArtifact(path const& cwd, Options const& options);
void CreateJSONDBArtifact(path const& cwd, Options const& options);
void CreateReflectorHeaderArtifact(path const& cwd, const Options& opts);

struct FileWriter
{
	std::ofstream mOutFile;
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

	FileWriter(path path) : mOutFile{ path }, mPath(path) {}

	template <typename... ARGS>
	void WriteLine(std::string_view str, ARGS&& ... args)
	{
		mOutFile << std::string(CurrentIndent, '\t');
		//((mOutFile << std::forward<ARGS>(args)), ...);
		fmt::vprint(mOutFile, str, fmt::make_format_args(std::forward<ARGS>(args)...));
		if (InDefine)
			mOutFile << " \\";
		mOutFile << '\n';
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

	void WriteLine();

	void Close();

	~FileWriter();
};
