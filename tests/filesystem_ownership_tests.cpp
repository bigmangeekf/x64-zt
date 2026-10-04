#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <utility>
#include "../src/zonetool/zonetool/utils/io/filesystem.hpp"

// The production file class only needs these directory helpers from the wider
// common library. Keep the harness independent of its game/Windows-module loader.
namespace utils::io
{
	bool directory_exists(const std::string& directory) { return std::filesystem::is_directory(directory); }
	std::vector<std::string> list_files(const std::string& directory)
	{
		std::vector<std::string> result;
		for (const auto& entry : std::filesystem::directory_iterator(directory)) result.push_back(entry.path().string());
		return result;
	}
}

void require(bool condition, const char* message)
{
	if (!condition) throw std::runtime_error(message);
}

bool exclusive_open(const std::filesystem::path& path)
{
	const auto handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (handle == INVALID_HANDLE_VALUE) return false;
	require(CloseHandle(handle) != 0, "Cannot close exclusive test handle");
	return true;
}

int main()
{
	const auto directory = std::filesystem::temp_directory_path() /
		("zt-file-tests-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
	try
	{
		require(std::filesystem::create_directory(directory), "Test directory already exists");
		const auto first = directory / "first.bin", second = directory / "second.bin";
		{ std::ofstream(first, std::ios::binary) << "first"; std::ofstream(second, std::ios::binary) << "second"; }
		using zonetool::filesystem::file;
		{
			file value(first.string());
			require(value.close() == 0 && !value.get_fp(), "Initial close is not an idempotent no-op");
			require(value.open("rb", false) == 0 && value.get_fp(), "Cannot open first file");
			require(!exclusive_open(first), "Open stream did not own its file");
			require(value.close() == 0 && !value.get_fp(), "Close left a stale stream pointer");
			require(value.close() == 0 && exclusive_open(first), "Second close or stream release failed");
			require(value.exists(false) && !value.get_fp(), "exists() retained stream ownership");
		}
		require(exclusive_open(first), "Destruction after explicit close damaged ownership");
		{
			file source(first.string());
			require(source.open("rb", false) == 0, "Cannot open move source");
			FILE* const owned = source.get_fp();
			file moved(std::move(source));
			require(!source.get_fp() && moved.get_fp() == owned && source.close() == 0, "Move construction duplicated ownership");
			file destination(second.string());
			require(destination.open("rb", false) == 0, "Cannot open move destination");
			destination = std::move(moved);
			require(!moved.get_fp() && destination.get_fp() == owned && exclusive_open(second), "Move assignment leaked prior stream");
			file copied(destination);
			require(!copied.get_fp() && destination.get_fp() == owned, "Copy construction copied stream ownership");
			copied = destination;
			require(!copied.get_fp() && copied.open("rb", false) == 0, "Copied path cannot open independently");
			copied = destination;
			require(!copied.get_fp() && destination.get_fp() == owned, "Copy assignment retained destination stream");
		}
		require(exclusive_open(first) && exclusive_open(second), "Move/copy destruction leaked a stream");
		{
			file value(first.string());
			require(value.open("rb", false) == 0, "Cannot open reopen source");
			value.initialize(second);
			require(value.open("rb", false) == 0 && exclusive_open(first), "Reopen leaked the previous stream");
			value.initialize(directory / "missing.bin");
			require(value.open("rb", false) != 0 && !value.get_fp() && exclusive_open(second), "Failed reopen retained ownership");
			require(value.open("", false) == EINVAL && !value.get_fp(), "Empty mode was not rejected safely");
			require(!value.exists(false) && value.close() == 0, "Missing-file probe retained ownership");
		}
		std::filesystem::remove(first); std::filesystem::remove(second); std::filesystem::remove(directory);
		std::cout << "Production filesystem stream ownership tests passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << "\nTest files retained: " << directory << '\n';
		return 1;
	}
}
