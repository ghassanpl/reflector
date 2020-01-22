/// Copyright 2017-2019 Ghassan.pl
/// Usage of the works is permitted provided that this instrument is retained with
/// the works, so that any entity that uses the works is notified of this instrument.
/// DISCLAIMER: THE WORKS ARE WITHOUT WARRANTY.

#pragma once

#include "Common.h"

bool ParseClassFile(path path, Options const& options, FileMirror& mirror);

std::vector<string_view> SplitArgs(string_view argstring);

std::string ParseIdentifier(string_view& str);
std::string ParseType(string_view& str);
bool SwallowOptional(string_view& str, string_view swallow);
string_view Expect(string_view str, string_view value);

