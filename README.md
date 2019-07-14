# reflector

## Dependencies

* C++17 (C++20 even)
* [nlohmann/json](https://github.com/nlohmann/json/)
* [fmtlib](https://github.com/fmtlib/fmt)
* [args](https://github.com/Taywee/args)
* [magic_enum](https://github.com/Neargye/magic_enum)
* my personal [baselib](https://github.com/ghassanpl/baselib)

## Usage

		Reflector.exe files... {OPTIONS}
		OPTIONS:
				Commands
				-h, --help           Show help
				-r, --recursive      Recursively search the provided directories for files
				-q, --quiet          Don't print out created file names
				-f, --force          Ignore timestamps, regenerate all files
				-v, --verbose        Print additional information
				-j, --json           Output code that uses nlohmann::json to store class properties
				-d, --database       Create a JSON database with reflection data
				files...             Files or directories to scan
				"--" can be used to terminate flag options and force all following arguments to be treated as positional options
