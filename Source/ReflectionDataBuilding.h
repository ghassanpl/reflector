/// Copyright 2017-2025 Ghassan.pl
/// Usage of the works is permitted provided that this instrument is retained with
/// the works, so that any entity that uses the works is notified of this instrument.
/// DISCLAIMER: THE WORKS ARE WITHOUT WARRANTY.

#pragma once

#include "Common.h"
#include "Options.h"
#include "FileWriter.h"

constexpr std::string_view TIMESTAMP_TEXT = "/// TIMESTAMP: ";

uint64_t ArtifactNeedsRegenerating(const path& target_path, const path& source_path, const Options& opts);

bool BuildMirrorHookupFile(ArtifactArgs args, FileMirror const& mirror);
bool BuildMirrorFile(ArtifactArgs args, FileMirror const& mirror, uint64_t file_change_time);

bool CreateTypeListArtifact(ArtifactArgs args);
bool CreateIncludeListArtifact(ArtifactArgs args);
bool CreateJSONDBArtifact(ArtifactArgs args);
bool CreateReflectorHeaderArtifact(ArtifactArgs args);
bool CreateReflectorDatabaseArtifact(ArtifactArgs args);
