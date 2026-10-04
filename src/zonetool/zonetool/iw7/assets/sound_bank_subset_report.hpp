#pragma once

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace zonetool::iw7::sound_bank_subset
{
	// Keep repeat builds valid, but never acknowledge a partial or stale report.
	// The sibling is exclusively created, flushed and read back before an atomic
	// same-directory replacement. Every failure propagates to the build caller.
	inline void publish_report(const std::filesystem::path& directory, const std::string& name, const std::string& bytes)
	{
		if (name.empty() || name.find_first_of("/\\:") != std::string::npos || name == "." || name == "..")
			throw std::runtime_error("Invalid soundbank report name");
		if (!CreateDirectoryW(directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
			throw std::runtime_error("Could not create soundbank report directory");
		const auto directory_handle = CreateFileW(directory.c_str(), FILE_READ_ATTRIBUTES,
			FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
		if (directory_handle == INVALID_HANDLE_VALUE) throw std::runtime_error("Could not lock soundbank report directory");
		HANDLE handle = INVALID_HANDLE_VALUE;
		std::filesystem::path temporary;
		bool owns_temporary = false;
		try
		{
			BY_HANDLE_FILE_INFORMATION information{};
			if (!GetFileInformationByHandle(directory_handle, &information) ||
				!(information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
				throw std::runtime_error("Soundbank report directory must not be a file or reparse point");
			const auto destination = directory / (name + ".json");
			const auto attributes = GetFileAttributesW(destination.c_str());
			if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
				throw std::runtime_error("Soundbank report destination must be an ordinary file");
			if (attributes == INVALID_FILE_ATTRIBUTES && GetLastError() != ERROR_FILE_NOT_FOUND)
				throw std::runtime_error("Could not inspect soundbank report destination");
			static std::atomic<unsigned long long> sequence{0};
			for (unsigned int attempt = 0; attempt < 64; ++attempt)
			{
				temporary = directory / (name + ".tmp-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(sequence.fetch_add(1)));
				handle = CreateFileW(temporary.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (handle != INVALID_HANDLE_VALUE) { owns_temporary = true; break; }
				const auto error = GetLastError();
				if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS)
					throw std::runtime_error("Could not create exclusive soundbank report sibling");
			}
			if (!owns_temporary) throw std::runtime_error("Soundbank report temporary names exhausted");
			for (size_t offset = 0; offset < bytes.size();)
			{
				const auto count = static_cast<DWORD>((std::min<size_t>)(bytes.size() - offset, MAXDWORD));
				DWORD written{};
				if (!WriteFile(handle, bytes.data() + offset, count, &written, nullptr) || written != count)
					throw std::runtime_error("Incomplete soundbank report write");
				offset += written;
			}
			if (!FlushFileBuffers(handle)) throw std::runtime_error("Could not flush soundbank report");
			LARGE_INTEGER beginning{}, size{};
			if (!GetFileSizeEx(handle, &size) || size.QuadPart < 0 || static_cast<unsigned long long>(size.QuadPart) != bytes.size() ||
				!SetFilePointerEx(handle, beginning, nullptr, FILE_BEGIN))
				throw std::runtime_error("Soundbank report size/seek verification failed");
			std::string readback(bytes.size(), '\0');
			for (size_t offset = 0; offset < readback.size();)
			{
				const auto count = static_cast<DWORD>((std::min<size_t>)(readback.size() - offset, MAXDWORD));
				DWORD read{};
				if (!ReadFile(handle, readback.data() + offset, count, &read, nullptr) || !read)
					throw std::runtime_error("Incomplete soundbank report readback");
				offset += read;
			}
			if (readback != bytes) throw std::runtime_error("Soundbank report readback mismatch");
			if (!CloseHandle(handle)) throw std::runtime_error("Could not close soundbank report sibling");
			handle = INVALID_HANDLE_VALUE;
			if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
				throw std::runtime_error("Could not publish verified soundbank report");
			owns_temporary = false;
		}
		catch (...)
		{
			if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
			if (owns_temporary) DeleteFileW(temporary.c_str());
			CloseHandle(directory_handle);
			throw;
		}
		if (!CloseHandle(directory_handle)) throw std::runtime_error("Could not release soundbank report directory");
	}
}
