#pragma once

#include "Common.h"

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

	FileWriter(path path);
	FileWriter();

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

	template <typename... ARGS>
	void StartBlock(ARGS&& ... args)
	{
		WriteLine(std::forward<ARGS>(args)...);
		CurrentIndent++;
	}

	template <typename... ARGS>
	void EndBlock(ARGS&& ... args)
	{
		CurrentIndent--;
		WriteLine(std::forward<ARGS>(args)...);
	}

	void WriteLine() const;

	void Close();

	std::string Finish();

	~FileWriter();
};
