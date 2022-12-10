module;

#ifdef _WIN32

#pragma warning(disable:4005)
#pragma warning(disable:5106)
#define WINDOWS_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

#include <immintrin.h>

module util;

import core;

namespace util
{


////////////////////////////////////////////////////////////////////////////////////////////////
MemoryMappedFile::MemoryMappedFile()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////
MemoryMappedFile::MemoryMappedFile(const std::filesystem::path &path)
{
	Open(path);
}

////////////////////////////////////////////////////////////////////////////////////////////////
MemoryMappedFile::MemoryMappedFile(MemoryMappedFile&& other)
{
	*this = std::move(other);
}

////////////////////////////////////////////////////////////////////////////////////////////////
MemoryMappedFile::~MemoryMappedFile()
{
	Close();
}

////////////////////////////////////////////////////////////////////////////////////////////////
MemoryMappedFile& MemoryMappedFile::operator=(MemoryMappedFile&& other)
{
	Close();
#ifdef _WIN32
	m_hFile = std::exchange(other.m_hFile, INVALID_HANDLE_VALUE);
	m_hMap = std::exchange(other.m_hMap, INVALID_HANDLE_VALUE);
#endif
	m_size = std::exchange(other.m_size, 0);
	m_pData = std::exchange(other.m_pData, nullptr);
	return *this;
}

////////////////////////////////////////////////////////////////////////////////////////////////
bool MemoryMappedFile::Open(const std::filesystem::path &path)
{
#ifdef _WIN32
	m_hFile = CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
	if (m_hFile == INVALID_HANDLE_VALUE)
		return false;
	GetFileSizeEx(m_hFile, (LARGE_INTEGER*)&m_size);

	m_hMap = CreateFileMapping(m_hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
	if (m_hMap == INVALID_HANDLE_VALUE)
		return false;

	m_pData = MapViewOfFile(m_hMap, FILE_MAP_READ, 0, 0, 0);
#else
	std::error_code ec;
	size_t size = std::filesystem::file_size(path, ec);
	if (size == -1) return false;
	int fd = open(path.c_str(), O_RDONLY);
	if (fd == -1) return false;
	void *data = mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
	close(fd);
	if (data == MAP_FAILED) return false;
	if (m_pData != nullptr) munmap((void*) m_pData, m_size);
	m_pData = data;
	m_size = size;
#endif
	return (bool)m_pData;
}

////////////////////////////////////////////////////////////////////////////////////////////////
void MemoryMappedFile::Close()
{
	if (!IsOpen())
		return;

#ifdef _WIN32
	UnmapViewOfFile(m_pData);
	CloseHandle(m_hMap);
	CloseHandle(m_hFile);
	m_hFile = INVALID_HANDLE_VALUE;
	m_hMap = INVALID_HANDLE_VALUE;
#else
	munmap((void*) m_pData, m_size);
#endif
	m_size = 0;
	m_pData = nullptr;
}

}
