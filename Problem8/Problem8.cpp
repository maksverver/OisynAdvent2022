import core;
import util;
using namespace util;
#include <immintrin.h>

int findchar(const char* ptr, char c)
{
#ifdef __AVX512F__
	auto allC = _mm256_set1_epi8(c);
	constexpr uint RegSize = sizeof(allC);

	for (uint offset = 0; ; offset += RegSize)
	{
		auto chars = _mm256_cmpeq_epi8(_mm256_loadu_epi8(ptr + offset), allC);
		if (uint mvmask = _mm256_movemask_epi8(chars))
			return std::countr_zero(mvmask) + offset;
	}
#else
	return strchr(ptr, c) - ptr;
#endif
}

std::pair<long long, bool> GetScoreAndVisibility(const char* data, const char* tdata, int x, int y, int width, int height, int stride, int tstride)
{
	int offset = y * stride + x;
	int h = data[offset];

	long long l = 1;
	bool visible = false;
	int dx, dy, o;

	dx = x + 1;
	o = offset + 1;
	while (dx < width && data[o] < h)
		dx++, o++;
	if (dx >= width)
		dx--, visible = true;
	l *= dx - x;

	dx = x - 1;
	o = offset - 1;
	while (dx >= 0 && data[o] < h)
		dx--, o--;
	if (dx < 0)
		dx = 0, visible = true;
	l *= x - dx;

	dy = y + 1;
	o = offset + stride;
	while (dy < height  && data[o] < h)
		dy++, o += stride;
	if (dy >= height)
		dy--, visible = true;
	l *= dy - y;

	dy = y - 1;
	o = offset - stride;
	while (dy >= 0 && data[o] < h)
		dy--, o -= stride;
	if (dy < 0)
		dy = 0, visible = true;
	l *= y - dy;

	return { l, visible };
}

std::pair<long long, bool> GetScoreAndVisibility2(const char* data, const char* tdata, int x, int y, int width, int height, int stride, int tstride)
{
	int offset = y * stride + x;
	int h = data[offset];
	auto cmp = _mm256_set1_epi8(h);

	long long l = 1;
	bool visible = false;
	int dx, dy, o, base, diff;

	dx = x + 1;
	o = offset + 1;
	base = o & ~31;
	diff = o - base;
	while (dx < width)
	{
		auto v = _mm256_loadu_epi8(data + base);
		uint mask = ~_mm256_movemask_epi8(_mm256_cmpgt_epi8(cmp, v));
		mask >>= diff;
		if (mask)
		{
			dx += std::countr_zero(mask);
			break;
		}
		dx += 32 - diff;
		base += 32;
		diff = 0;
	}
	if (dx >= width)
		dx = width - 1, visible = true;
	l *= dx - x;

	dx = x - 1;
	o = offset - 1;

	base = o & ~31;
	diff = 31 - (o - base);
	while (dx >= 0)
	{
		auto v = _mm256_loadu_epi8(data + base);
		uint mask = ~_mm256_movemask_epi8(_mm256_cmpgt_epi8(cmp, v));
		mask <<= diff;
		if (mask)
		{
			dx -= std::countl_zero(mask);
			break;
		}
		dx -= 32 - diff;
		base -= 32;
		diff = 0;
	}
	if (dx < 0)
		dx = 0, visible = true;
	l *= x - dx;

	int toffset = x * tstride + y;
	dy = y + 1;
	o = toffset + 1;
	base = o & ~31;
	diff = o - base;
	while (dy < height)
	{
		auto v = _mm256_loadu_epi8(tdata + base);
		uint mask = ~_mm256_movemask_epi8(_mm256_cmpgt_epi8(cmp, v));
		mask >>= diff;
		if (mask)
		{
			dy += std::countr_zero(mask);
			break;
		}
		dy += 32 - diff;
		base += 32;
		diff = 0;
	}

	if (dy >= height)
		dy = height - 1, visible = true;
	l *= dy - y;

	dy = y - 1;
	o = toffset - 1;
	base = o & ~31;
	diff = 31 - (o - base);
	while (dy >= 0)
	{
		auto v = _mm256_loadu_epi8(tdata + base);
		uint mask = ~_mm256_movemask_epi8(_mm256_cmpgt_epi8(cmp, v));
		mask <<= diff;
		if (mask)
		{
			dy -= std::countl_zero(mask);
			break;
		}
		dy -= 32 - diff;
		base -= 32;
		diff = 0;
	}
	if (dy < 0)
		dy = 0, visible = true;
	l *= y - dy;

	return { l, visible };
}


void CheckTranspose(const char* out, const char* in, int width, int height)
{
	int istride = width + 1;
	int ostride = (height + 31) & ~31;

	for (int y = 0, io = 0, oo = 0; y < height; y++, io++, oo++)
	{
		for (int x = 0, oo2 = oo; x < width; x++, io++, oo2 += ostride)
			if (out[oo2] != in[io])
				__debugbreak();
	}
}

void Transpose1(char* out, const char* in, int width, int height)
{
	int istride = width + 1;
	int ostride = (height + 31) & ~31;

	for (int y = 0, io = 0, oo = 0; y < height; y++, io++, oo++)
	{
		for (int x = 0, oo2 = oo; x < width; x++, io++, oo2 += ostride)
			out[oo2] = in[io];
	}
}

void Transpose2(char* out, const char* in, int width, int height)
{
	int istride = width + 1;
	int ostride = (height + 31) & ~31;
	const auto offsets = _mm256_mullo_epi32(_mm256_set1_epi32(istride), _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7));
	const auto shuffle1 = _mm256_setr_epi8(
		0x00, 0x01, 0x04, 0x05, 0x08, 0x09, 0x0c, 0x0d, 0x02, 0x03, 0x06, 0x07, 0x0a, 0x0b, 0x0e, 0x0f,
		0x00, 0x01, 0x04, 0x05, 0x08, 0x09, 0x0c, 0x0d, 0x02, 0x03, 0x06, 0x07, 0x0a, 0x0b, 0x0e, 0x0f);
	const auto shuffle3 = _mm256_setr_epi8(
		0x00, 0x02, 0x04, 0x06, 0x08, 0x0a, 0x0c, 0x0e,
		0x01, 0x03, 0x05, 0x07, 0x09, 0x0b, 0x0d, 0x0f,
		0x00, 0x02, 0x04, 0x06, 0x08, 0x0a, 0x0c, 0x0e,
		0x01, 0x03, 0x05, 0x07, 0x09, 0x0b, 0x0d, 0x0f);
	static uint mask[16] = { 0xffff'ffff, 0xffff'ffff, 0xffff'ffff, 0xffff'ffff, 0xffff'ffff, 0xffff'ffff, 0xffff'ffff, 0xffff'ffff, 0, 0, 0, 0, 0, 0, 0, 0 };

	std::atomic<int> nexty = 0;
	auto futures = DoParallel([=, &nexty](int, int)
	{
		for (;;)
		{
			int y = nexty.fetch_add(8);
			if (y >= height)
				return;

			auto loadmask = _mm256_loadu_epi32(mask + 8 - std::min(height - y, 8));

			for (int x = 0; x < width; x += 4)
			{
				auto inptr = in + (y * istride + x);
				//auto data = _mm256_i32gather_epi32((const int*)inptr, offsets, 1);
				auto data = _mm256_mask_i32gather_epi32(_mm256_undefined_si256(), (const int*)inptr, offsets, loadmask, 1);
				auto s = _mm256_shuffle_epi8(data, shuffle1);
				s = _mm256_permute4x64_epi64(s, _MM_PERM_DBCA);
				s = _mm256_shuffle_epi8(s, shuffle3);

				auto outptr = out + (x * ostride + y);
				*(long long*)(outptr) = _mm256_extract_epi64(s, 0);
				*(long long*)(outptr + ostride) = _mm256_extract_epi64(s, 1);
				auto shi = _mm256_extracti128_si256(s, 1);	// let's do this manually, as _mm256_extract_epi64() generates this instruction on every call
				*(long long*)(outptr + ostride * 2) = _mm_extract_epi64(shi, 0);
				*(long long*)(outptr + ostride * 3) = _mm_extract_epi64(shi, 1);
			}
		}
	});
	for (auto& f : futures)
		f.wait();
}

bool Run(const wchar_t* file)
{
	Timer total(AutoStart);
	Timer tload(AutoStart);
	MemoryMappedFile mmap(file);
	if (!mmap)
		return false;

	auto data = mmap.GetSpan<const char>();
	int width = findchar(data.data(), '\n');
	int stride = width + 1;
	int height = int(mmap.GetSize() + 1) / stride;

	int awidth = (width + 3) & ~3;
	int aheight = (height + 31) & ~31;
	tload.Stop();

	Timer ttrans(AutoStart);
	std::vector<char> transposed(awidth * aheight);
	Transpose2(transposed.data(), data.data(), width, height);
	ttrans.Stop();

	Timer talgo(AutoStart);
	std::atomic<int> nextOffset = 0;
	auto futures = DoParallel([=, &nextOffset, &transposed](int threadIdx, int numThreads) -> std::pair<long long, int>
	{
		long long myMax = 0;
		int numVisible = 0;
		constexpr int WorkloadSize = 512;
		const int maxOffset = (width - 2) * (height - 2);

		for (;;)
		{
			int myoffsets = nextOffset.fetch_add(WorkloadSize);
			for (int i = 0; i < WorkloadSize; i++)
			{
				int o = myoffsets + i;
				if (o >= maxOffset)
					return { myMax, numVisible };

				int y = o / (width - 2) + 1;
				int x = o % (width - 2) + 1;
				auto [s, v] = GetScoreAndVisibility2(data.data(), transposed.data(), x, y, width, height, stride, aheight);
				myMax = std::max(myMax, s);
				numVisible += v;
			}
		}
	});

	long long maxScore = 0;
	int numTrees = 2 * (width + height) - 4;
	for (auto& f : futures)
	{
		auto [s, n] = f.get();
		maxScore = std::max(maxScore, s);
		numTrees += n;
	}

	// SINGLE THREADED
	//long long maxScore = 0;
	//int numTrees = 2 * (width + height) - 4;
	//for (int y = 1; y < height - 1; y++)
	//{
	//	for (int x = 1; x < width - 1; x++)
	//	{
	//		auto [s, t] = GetScoreAndVisibility2(data.data(), transposed.data(), x, y, width, height, stride, aheight);
	//		maxScore = std::max(maxScore, s);
	//		numTrees += t;
	//	}
	//}

	talgo.Stop();
	total.Stop();
	std::cout << std::format("Time: {}us (load:{}us, transpose:{}us, algo:{}us)\n{}\n{}\n", total.GetTime(), tload.GetTime(), ttrans.GetTime(), talgo.GetTime(), numTrees, maxScore);

	return true;
}


int main()
{
	const wchar_t* inputs[] =
	{
		//L"example.txt",
		L"input.txt",
		L"aoc_2022_day08_rect.txt",
		L"aoc_2022_day08_sparse.txt",
		L"input-mrhaas.txt",
	};

	constexpr int NumRuns = 1;
	for (auto f : inputs)
	{
		std::wcout << std::format(L"\n===[ {} ]==========\n", f);
		for (int i = 0; i < NumRuns; i++)
		{
			if (!Run(f))
				std::wcerr << std::format(L"Can't open `{}`\n", f);
		}
	}
}
