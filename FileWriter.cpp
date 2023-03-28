#include "FileWriter.h"

#include <fstream>
#include <sstream>

FileWriter::FileWriter(path path) : mOutFile{ new std::ofstream{path} }, mPath(path) {
	mOutFile->exceptions(std::ofstream::failbit | std::ofstream::badbit);
	if (!static_cast<std::ofstream*>(mOutFile.get())->is_open())
		throw std::runtime_error(std::format("could not open file '{}' for writing", path.string()));
}

FileWriter::FileWriter() : mOutFile{ new std::stringstream{} }, mPath() {}

void FileWriter::WriteLine() const
{
	*mOutFile << '\n';
}

void FileWriter::Close()
{
	mOutFile->flush();
	mOutFile = nullptr;
}

std::string FileWriter::Finish()
{
	mOutFile->flush();
	auto contents = std::move(*static_cast<std::stringstream*>(mOutFile.get())).str();
	Close();
	return contents;
}

FileWriter::~FileWriter()
{
	if (mOutFile)
	{
		ReportError({}, 0, "file '{}' not closed due to an error, deleting", mPath.string());
		mOutFile = nullptr;
		std::filesystem::remove(mPath);
	}
}
