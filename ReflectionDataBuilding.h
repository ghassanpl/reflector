/// Copyright 2017-2019 Ghassan.pl
/// Usage of the works is permitted provided that this instrument is retained with
/// the works, so that any entity that uses the works is notified of this instrument.
/// DISCLAIMER: THE WORKS ARE WITHOUT WARRANTY.

#pragma once

#include "Common.h"
#include "Options.h"
#include "FileWriter.h"

#define TIMESTAMP_TEXT "/// TIMESTAMP: "

uint64_t FileNeedsUpdating(const path& target_path, const path& source_path, const Options& opts);

bool BuildMirrorFile(path const& file_path, path const& final_path, const Options& options, FileMirror const& file, uint64_t file_change_time);

bool CreateTypeListArtifact(path const& cwd, path const& final_path, Options const& options);
bool CreateIncludeListArtifact(path const& cwd, path const& final_path, Options const& options);
bool CreateJSONDBArtifact(path const& cwd, path const& final_path, Options const& options);
bool CreateReflectorHeaderArtifact(path const& target_path, path const& final_path, const Options& options);
bool CreateReflectorDatabaseArtifact(path const& target_path, path const& final_path, const Options& opts);
