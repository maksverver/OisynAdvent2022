import core;
import util;
using namespace util;
#include <immintrin.h>

#define CV_ENABLE		0	// whether the concurrency visualizer code is enabled

#if CV_ENABLE
#include <cvmarkersobj.h>
using namespace Concurrency::diagnostic;

marker_series g_markerSeries(L"Problem1");

#define _CONCAT2(a,b) a##b
#define _CONCAT(a,b) _CONCAT2(a,b)

#define CV_MARKER(x) g_markerSeries.write_flag(x)
#define CV_SCOPED_SPAN(x) span _CONCAT(myspan, __COUNTER__)(g_markerSeries, x)

union delayed_span
{
	delayed_span(const wchar_t* x) : s(g_markerSeries, x) { }
	delayed_span(const delayed_span&) = delete;
	~delayed_span() { }
	span s;
};

#define CV_SPAN_START(name, x) delayed_span cvspan_ ## name(x)
#define CV_SPAN_STOP(name) cvspan_ ## name.s.~span()

#undef _CONCAT2
#undef _CONCAT

#else

#define CV_MARKER(x) do;while(0)
#define CV_SCOPED_SPAN(x) do;while(0)
#define CV_SPAN_START(name, x) do;while(0)
#define CV_SPAN_STOP(name) do;while(0)

#endif

static constexpr size_t RegSize = sizeof(__m256i);

int main(int argc, char *argv[])
{
	CV_SPAN_START(init, L"Init");
	auto start = std::chrono::high_resolution_clock::now();

	std::filesystem::path filename = argc > 1 ? argv[1] : "aoc_2022_day05_large_input-3.txt";
	MemoryMappedFile mmap;
	if (!mmap.Open(filename))
		return (std::cerr << "Can't open file `" << filename << "`\n"), 1;
	const char* basePtr = mmap.data<char>();
	const char* endPtr = basePtr + mmap.size<char>();

	std::vector<std::span<const char>> stateLines;
	stateLines.reserve(10000);
	int numStacks = 0;

	CV_SPAN_STOP(init);
	CV_SPAN_START(state, L"Parse state");

	const char* lineStart = basePtr;
	for(;;)
	{
		auto line = lineStart;
		auto nl = std::find(line, endPtr, '\n');
		lineStart = nl + 1;

		int len = int(nl - line);
		if (len < 2 || line[1] == '0' || line[1] == '1')
			break;

		stateLines.push_back(std::span{ line, nl });
		int stacks = (len + 1) / 4;
		numStacks = std::max(numStacks, stacks);
	}

	CV_SPAN_STOP(state);

	std::vector<int> stackStart(numStacks);
	std::thread findStackStarts([&, numStacks]
	{
		CV_SCOPED_SPAN(L"find stack starts");
		for (int i = 0; i < numStacks; i++)
		{
			int p = i * 4 + 1;
			for (int j = 0; j < stateLines.size(); j++)
			{
				if (stateLines[j].size() > p && stateLines[j][p] != ' ')
				{
					stackStart[i] = j;
					break;
				}
			}
		}
	});

	CV_SPAN_START(moves, L"Parse moves");
	constexpr uint MaxThreads = 32;
	int numThreads = std::max(1u, std::min(std::thread::hardware_concurrency() - 2, MaxThreads));
	//numThreads = 2;

	struct Move { int n, from, to; };
	std::vector<Move> threadMoves[MaxThreads];
	std::vector<std::thread> threads;
	threads.reserve(numThreads);

	for (int threadIdx = numThreads - 1; threadIdx >= 0; threadIdx--)
	{
		threads.emplace_back([&](int idx)
		{
			int total = int(endPtr - lineStart);
			const char* start = lineStart + (total * idx / numThreads);
			const char* end = lineStart + (total * (idx + 1) / numThreads);

			if (idx > 0)
				start = std::find(start, end, '\n') + 1;
			end = std::find(end, endPtr, '\n');

			std::vector<Move> mymoves;
			mymoves.reserve(1000);

			const __m256i allSpaces = _mm256_set1_epi8(' ');
			auto nextSpace = [=, mvmask = 0u](int skip = 0) mutable -> const char*
			{
				for(;; skip--)
				{
					while (!mvmask && start < end)
					{
						auto reg = _mm256_loadu_epi8(start);
						reg = _mm256_cmpeq_epi8(reg, allSpaces);
						mvmask = _mm256_movemask_epi8(reg);
						start += RegSize;
					}

					if (!mvmask)
						return end;

					uint o = std::countr_zero(mvmask);
					mvmask &= mvmask - 1;
					if (!skip)
						return std::min(start - RegSize + o, end);
				}
			};

			start = nextSpace();
			while (start < end)
			{
				// "move a from b to c\n"

				auto a = start + 1;
				auto a_end = nextSpace();
				auto b = a_end + 6;
				auto b_end = nextSpace(1);
				auto c = b_end + 4;
				start = nextSpace(1);
				auto c_end = (start < end) ? start - 5 : end;
				if (c_end[-1] == '\n')
					c_end--;

				mymoves.push_back(Move{(int) conv(a, a_end), (int) conv(b, b_end) - 1, (int) conv(c, c_end) - 1});
			}

			threadMoves[idx] = std::move(mymoves);
		}, threadIdx);
	}

	CV_SPAN_STOP(moves);

	CV_SPAN_START(process, L"Process");
	struct Pile { int junk, column; };
	std::vector<std::vector<Pile>> stacks(numStacks);
	for (int i = 0; i < numStacks; i++)
	{
		stacks[i].reserve(1000);
		stacks[i].push_back({0, i});
	}

	for (int threadIdx = numThreads - 1; threadIdx >= 0; threadIdx--)
	{
		threads[threadIdx].join();
		auto& moves = threadMoves[threadIdx];
		for (auto it = moves.rbegin(); it != moves.rend(); ++it)
		{
			auto [n, to, from] = *it;
			auto& src = stacks[from];
			auto& dest = stacks[to];

			while (n)
			{
				if (dest.empty())
				{
					if (src.empty())
						break;

					auto& srcPile = src.back();
					int amount = std::min(srcPile.junk, n);
					srcPile.junk -= amount;
					n -= amount;
					if (n)
					{
						n--;
						dest.emplace_back(Pile{0, srcPile.column});
						src.pop_back();
					}
				}
				else
				{
					if (src.empty())
					{
						dest.back().junk += n;
						break;
					}

					auto& srcPile = src.back();
					auto& destPile = dest.back();
					int amount = std::min(srcPile.junk, n);
					srcPile.junk -= amount;
					destPile.junk += amount;
					n -= amount;
					if (n)
					{
						n--;
						dest.emplace_back(Pile{0, srcPile.column});
						src.pop_back();
					}
				}
			}
		}
	}
	CV_SPAN_STOP(process);
	CV_SPAN_START(result, L"Get result");

	findStackStarts.join();
	std::vector<char> result(numStacks + 1);
	for (int i = 0; i < numStacks; i++)
	{
		auto& s = stacks[i];
		int top = stackStart[i];
		for (auto it = s.rbegin(); it != s.rend(); ++i)
		{
			auto &p = *it;
			top += p.junk;
			result[p.column] = stateLines[top][i * 4 + 1];
			top++;
		}
	}
	CV_SPAN_STOP(result);

	auto d = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start).count();
	std::cout << "Time: " << d << " us\n";
	std::cout << result.data() << std::endl;
}
