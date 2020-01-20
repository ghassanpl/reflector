/// Copyright 2017-2019 Ghassan.pl
/// Usage of the works is permitted provided that this instrument is retained with
/// the works, so that any entity that uses the works is notified of this instrument.
/// DISCLAIMER: THE WORKS ARE WITHOUT WARRANTY.

#pragma once

#include "Common.h"
#include <fstream>

#define TIMESTAMP_TEXT "/// TIMESTAMP: "

uint64_t FileNeedsUpdating(const std::filesystem::path& target_path, const std::filesystem::path& source_path, const Options& opts);

void BuildMirrorFile(FileMirror const& file, size_t& modified_files, const Options& opts);

void CreateTypeListArtifact(std::filesystem::path const& cwd, Options const& options);
void CreateIncludeListArtifact(std::filesystem::path const& cwd, Options const& options);
void CreateJSONDBArtifact(std::filesystem::path const& cwd, Options const& options);
void CreateReflectorHeaderArtifact(std::filesystem::path const& cwd, const Options& opts);

struct FileWriter
{
	std::ofstream mOutFile;
	std::filesystem::path mPath;
	size_t CurrentIndent = 0;
	bool InDefine = false;

	struct Indenter
	{
		FileWriter& out;
		Indenter(FileWriter& out_) : out(out_) { out.CurrentIndent++; }
		~Indenter() { out.CurrentIndent--; }
	};

	Indenter Indent() { return Indenter{ *this }; }

	FileWriter(std::filesystem::path path) : mOutFile{ path }, mPath(path) {}

	template <typename... ARGS>
	void WriteLine(ARGS&& ... args)
	{
		mOutFile << std::string(CurrentIndent, '\t');
		//((mOutFile << std::forward<ARGS>(args)), ...);
		fmt::print(mOutFile, std::forward<ARGS>(args)...);
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
