import core;
import util;
using namespace util;
#include <immintrin.h>

#define CV_ENABLE		0	// whether the concurrency visualizer code is enabled

#if CV_ENABLE
#include <cvmarkersobj.h>
using namespace Concurrency::diagnostic;

marker_series g_markerSeries(L"Problem1");

#define CV_MARKER(x) g_markerSeries.write_flag(x)
#define CV_SCOPED_SPAN(x) span myspan ## __COUNTER__(g_markerSeries, x)

union delayed_span
{
	delayed_span(const wchar_t* x) : s(g_markerSeries, x) { }
	delayed_span(const delayed_span&) = delete;
	~delayed_span() { }
	span s;
};

#define CV_SPAN_START(name, x) delayed_span cvspan_ ## name(x)
#define CV_SPAN_STOP(name) cvspan_ ## name.s.~span()

#else

#define CV_MARKER(x) do;while(0)
#define CV_SCOPED_SPAN(x) do;while(0)
#define CV_SPAN_START(name, x) do;while(0)
#define CV_SPAN_STOP(name) do;while(0)

#endif


static constexpr size_t RegSize = sizeof(__m256i);

alignas(16) static const unsigned char s_mask[48] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
alignas(16) static const uint s_mul[32] = { 1'000'000'000, 100'000'000, 10'000'000, 1'000'000, 100'000, 10'000, 1'000, 100, 10, 1 };

alignas(16) static const unsigned char s_masks[8][16] = {
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
};

uint conv(const char* str, int size)
{
	__m128i v8 = _mm_loadu_epi8(str);
	v8 = _mm_sub_epi8(v8, _mm_set1_epi8('0'));
	v8 = _mm_and_si128(v8, _mm_loadu_epi8(s_mask + 32 - size));	// 16x 8 bit digits

	__m256i vlo = _mm256_cvtepi8_epi32(v8); // lower 8x 32 bit digits
	__m256i vhi = _mm256_cvtepi8_epi32(_mm_shuffle_epi32(v8, _MM_SHUFFLE(1, 0, 3, 2))); // upper 8x 32 bit digits
	vlo = _mm256_mullo_epi32(vlo, _mm256_loadu_si256((__m256i*)(s_mul + 10 - size)));
	vhi = _mm256_mullo_epi32(vhi, _mm256_loadu_si256((__m256i*)(s_mul + 18 - size)));

	__m256i v = _mm256_add_epi32(vlo, vhi);	// 8 combined numbers
	v = _mm256_hadd_epi32(v, v); // 4 combined numbers
	v = _mm256_hadd_epi32(v, v); // 2 combined numbers
	uint r = _mm256_cvtsi256_si32(v) + _mm256_extract_epi32(v, 4);
	return r;
}

__m256i conv_1(const char* str, int size)
{
	__assume(size >= 0 && size <= 8);
	int sizeBits = size * 8;

	ullong l = *(const ullong*)str;
	l ^= 0x30303030'30303030;
	l <<= 64 - sizeBits;

	__m128i v8 = _mm_cvtsi64_si128(l);
	return _mm256_cvtepu8_epi32(v8); // 8x 32 bit digits
}

uint conv_2(__m256i v)
{
	v = _mm256_mullo_epi32(v, _mm256_loadu_si256((__m256i*)(s_mul + 2)));
	v = _mm256_hadd_epi32(v, v); // 4 combined numbers
	v = _mm256_hadd_epi32(v, v); // 2 combined numbers
	uint r = _mm256_cvtsi256_si32(v) + _mm256_extract_epi32(v, 4);
	return r;
}

int main(int argc, char *argv[])
{
	if (argc < 2) return (std::cerr << "Missing filename argument.\n", 1);
	std::filesystem::path file = argv[1];

	auto start = std::chrono::high_resolution_clock::now();

	CV_SPAN_START(init, L"init");

	MemoryMappedFile mmap;
	if (!mmap.Open(file)) return (std::cerr << "Unable to open " << argv[1] << '\n', 1);
	const size_t fileSize = mmap.size_bytes();
	const char* const basePtr = mmap.data<char>();

	CV_SPAN_STOP(init);
	CV_SPAN_START(work, "Work");

	constexpr uint MaxThreads = 32;
	static std::array<uint, 3> threadTotals[MaxThreads] = { };
	int numThreads = std::max(1u, std::min(std::thread::hardware_concurrency(), MaxThreads) - 2);
	const ullong numRegs = (fileSize + RegSize - 1) / RegSize;

	auto ThreadFunc = [&](const int threadIdx)
	{
		std::array<uint, 3> totals = { };
		auto insert = [](uint l, std::array<uint, 3>& totals)
		{
			if (l < totals[2])
				return;
			if (l < totals[1])
			{
				totals[2] = l;
				return;
			}

			if (l < totals[0])
			{
				totals[2] = totals[1];
				totals[1] = l;
				return;
			}

			totals[2] = totals[1];
			totals[1] = totals[0];
			totals[0] = l;
		};

		ullong startReg = numRegs * threadIdx / numThreads;
		ullong endOffset = (numRegs * (threadIdx + 1) / numThreads - startReg) * RegSize;
		const char* ptr = basePtr + (startReg * RegSize);
		std::span<const __m256i> data((const __m256i*)ptr, (const __m256i*)basePtr + numRegs);

		ullong offset = 0 - RegSize, lastpos = 0;
		__m256i subtotalv = _mm256_setzero_si256();

		__m256i allNl = _mm256_set1_epi8('\n');
		bool skipFirst = threadIdx > 0;

		for (auto c : data)
		{
			offset += RegSize;
			__m256i nls = _mm256_cmpeq_epi8(c, allNl);
			uint mvmask = _mm256_movemask_epi8(nls);
			while (mvmask)
			{
				ullong npos = std::countr_zero(mvmask) + offset;
				if (npos == lastpos)
				{	// double newline
					if (skipFirst)
						skipFirst = false;
					else
					{
						uint subtotal = conv_2(subtotalv);
						insert(subtotal, totals);
						subtotalv = _mm256_setzero_si256();
					}

					if (offset >= endOffset)
					{
						threadTotals[threadIdx] = totals;
						return;
					}
				}
				else if (!skipFirst)
				{
					auto v = conv_1(ptr + lastpos, int(npos - lastpos));
					subtotalv = _mm256_add_epi32(subtotalv, v);
				}
				lastpos = npos + 1;
				mvmask &= mvmask - 1;
			}
		}

		uint subtotal = conv_2(subtotalv);
		insert(subtotal, totals);
		threadTotals[threadIdx] = totals;
	};


	std::vector<std::thread> threads;
	std::array<uint, 3> grandTotals = { };
	threads.reserve(numThreads);
	for (int n = 0; n < numThreads; n++)
		threads.emplace_back(ThreadFunc, n);
	int threadIdx = 0;
	for (auto& t : threads)
	{
		t.join();

		auto old = grandTotals;
		int l = 0, r = 0;
		for (int i = 0; i < 3; i++)
		{
			if (old[l] < threadTotals[threadIdx][r])
				grandTotals[i] = threadTotals[threadIdx][r++];
			else
				grandTotals[i] = old[l++];
		}

		threadIdx++;
	}

	auto d = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start).count();
	CV_SPAN_STOP(work);

	std::cout << "Time: " << d << " us\n";

	std::cout << "Max: " << grandTotals[0] << "\n";
	std::cout << "Top 3: " << grandTotals[0] + grandTotals[1] + grandTotals[2] << " (" << grandTotals[0] << ", " << grandTotals[1] << ", " << grandTotals[2] << ")\n";
}
