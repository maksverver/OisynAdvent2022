module;

#ifdef _WIN32
#pragma warning(disable:4005)
#pragma warning(disable:5106)
#define WINDOWS_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

export module util;

#include <immintrin.h>

import core;

export
{

using uchar = unsigned char;
using ushort = unsigned short;
using uint = unsigned;
using ullong = unsigned long long;

#ifndef _MSC_VER
__attribute__((always_inline))
inline void __assume(bool x) {
	return __builtin_assume(x);
}
#endif

}

export namespace util
{

class MemoryMappedFile
{
public:
	MemoryMappedFile();
	MemoryMappedFile(const std::filesystem::path &path);
	MemoryMappedFile(MemoryMappedFile&& other);
	~MemoryMappedFile();

	MemoryMappedFile& operator=(MemoryMappedFile&& other);

	bool Open(const std::filesystem::path &path);
	void Close();
	bool IsOpen() const { return m_pData != nullptr; }
	ullong GetSize() const { return m_size; }
	explicit operator bool() const { return IsOpen(); }

	template<class T>
	std::span<T> GetSpan() const { return std::span<T>(data<T>(), data<T>() + size<T>()); }

	size_t size_bytes() const { return m_size; }
	template<class T> const T *data() const { return (const T*)m_pData; }
	template<class T> size_t size() const { return (m_size + sizeof(T) - 1) / sizeof(T); }

private:
#ifdef _WIN32
	HANDLE m_hFile = INVALID_HANDLE_VALUE;
	HANDLE m_hMap = INVALID_HANDLE_VALUE;
#endif
	ullong m_size = 0;
	const void* m_pData = nullptr;
};

inline constexpr int MaxThreads = 32;

template<class F> auto DoParallel(F&& func)
{
	int numThreads = std::clamp((int)std::thread::hardware_concurrency() - 2, 1, MaxThreads);

	using result_t = std::invoke_result_t<std::decay_t<F>, int, int>;
	std::vector<std::future<result_t>> results;
	results.reserve(numThreads);
	for (int i = 0; i < numThreads; i++)
		results.push_back(std::async(std::launch::async, std::forward<F>(func), i, numThreads));

	return results;
}

template<class F> auto DontParallel(F&& func)
{
	using result_t = std::invoke_result_t<std::decay_t<F>, int, int>;
	std::vector<std::future<result_t>> results;
	results.push_back(std::async(std::launch::deferred, std::forward<F>(func), 0, 1));
	return results;
}


inline constexpr struct AutoStart_t { } AutoStart;

class Timer
{
	using duration = std::chrono::high_resolution_clock::duration;

public:
	Timer() = default;
	Timer(AutoStart_t) { Start(); }
	void Start() { m_t -= std::chrono::high_resolution_clock::now().time_since_epoch(); }
	void Stop() { m_t += std::chrono::high_resolution_clock::now().time_since_epoch(); }
	void Reset() { m_t = { }; }
	void Restart() { m_t = -std::chrono::high_resolution_clock::now().time_since_epoch(); }

	template<class D = std::chrono::microseconds>
	long long GetTime() const { return std::chrono::duration_cast<D>(m_t).count(); }

private:
	duration m_t = { };
};

class ScopedTime
{
public:
	ScopedTime(Timer& t) : m_timer(t) { m_timer.Start(); }
	~ScopedTime() { m_timer.Stop(); }
private:
	Timer& m_timer;
};


uint conv(const char* str, int size)
{
	__assume(size >= 0 && size <= 8);
	int sizeBits = size * 8;

	ullong l = *(const ullong*)str;
	l ^= 0x30303030'30303030;
	l <<= 64 - sizeBits;

	__m128i v8 = _mm_cvtsi64_si128(l);
	__m256i v = _mm256_cvtepu8_epi32(v8); // 8x 32 bit digits
	v = _mm256_mullo_epi32(v, _mm256_setr_epi32(10'000'000, 1'000'000, 100'000, 10'000, 1'000, 100, 10, 1));
	v = _mm256_hadd_epi32(v, v); // 4 combined numbers
	v = _mm256_hadd_epi32(v, v); // 2 combined numbers
	uint r = _mm_cvtsi128_si32(_mm_add_epi32(_mm256_extracti128_si256(v, 0), _mm256_extracti128_si256(v, 1)));
	return r;
}

uint conv(std::span<const char> s)
{
	return conv(s.data(), int(s.size()));
}

uint conv(const char* str, const char* end)
{
	return conv(str, int(end - str));
}


class Splitter
{
public:
	Splitter() { }
	Splitter(std::span<const char> data, char separator) : m_data(data), m_separator(separator) { m_buffer.reserve(BufferSize + 258); }

	std::optional<std::span<const char>> Next()
	{
		if (m_bufferPos >= m_buffer.size())
			if (!Generate())
				return std::nullopt;

		return m_buffer[m_bufferPos++];
	}

	struct end_iterator { };
	class iterator
	{
	public:
		friend class Splitter;
		using difference_type = std::ptrdiff_t;
		using value_type = std::span<const char>;

		iterator() = default;

		iterator& operator++() { m_cur = m_pSplitter->Next(); return *this; }
		iterator operator++(int) { iterator r(*this); operator++(); return r; }
		value_type operator*() const { return m_cur.value(); }

		bool operator==(end_iterator) const { return !m_cur.has_value(); }
		friend bool operator==(end_iterator, const iterator& i) { return i == end_iterator{}; }

	private:
		iterator(Splitter& splitter) : m_pSplitter(&splitter) { operator++(); }
		Splitter* m_pSplitter = nullptr;
		std::optional<std::span<const char>> m_cur;
	};

	iterator begin() { return iterator(*this); }
	end_iterator end() { return end_iterator{}; }

private:
	bool Generate()
	{
		if (m_dataPos >= m_data.size())
			return false;

		m_bufferPos = 0;
		m_buffer.resize(m_buffer.capacity());
		auto* result = m_buffer.data();
		auto* resultEnd = m_buffer.data() + BufferSize;

		__m256i allS = _mm256_set1_epi8(m_separator);
		constexpr int RegSize = sizeof(__m256i);
		ullong last = m_lastPos;
		const char* ptr = m_data.data();
		auto end = m_data.size();

		ullong offset;
		for (offset = m_dataPos; offset < end && result < resultEnd; offset += RegSize)
		{
			__m256i c = _mm256_loadu_epi8(ptr + offset);
			__m256i separators = _mm256_cmpeq_epi8(c, allS);
			uint mvmask = _mm256_movemask_epi8(separators);
			while (mvmask)
			{
				ullong spos = std::countr_zero(mvmask) + offset;
				*result++ = { ptr + last, spos - last };
				last = spos + 1;
				mvmask &= mvmask - 1;
			}
		}

		if (offset >= end && last < end)
			*result++ = { ptr + last, end - last };
		m_buffer.resize(result - m_buffer.data());

		m_dataPos = offset;
		m_lastPos = last;
		return true;
	}

	static constexpr int BufferSize = 1024;
	std::span<const char> m_data;
	char m_separator;
	ullong m_dataPos = 0, m_lastPos = 0;
	std::vector<std::span<const char>> m_buffer;
	int m_bufferPos = 0;
};

#ifdef __AVX512F__
std::vector<std::span<const char>> split(std::span<const char> data, char separator, size_t expected = 10000)
{
	std::vector<std::span<const char>> r;
	r.reserve(expected);

	__m256i allS = _mm256_set1_epi8(separator);
	constexpr int RegSize = sizeof(__m256i);
	ullong last = 0;
	const char* ptr = data.data();

	for (ullong offset = 0; offset < data.size(); offset += RegSize)
	{
		__m256i c = _mm256_loadu_epi8(ptr + offset);
		__m256i separators = _mm256_cmpeq_epi8(c, allS);
		uint mvmask = _mm256_movemask_epi8(separators);
		while (mvmask)
		{
			ullong spos = std::countr_zero(mvmask) + offset;
			r.emplace_back(data.data() + last, data.data() + spos);
			last = spos + 1;
			mvmask &= mvmask - 1;
		}
	}

	return r;
}
#endif

}
