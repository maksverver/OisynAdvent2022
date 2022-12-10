import core;
import util;
using namespace util;
#include <immintrin.h>

enum MoveDir : char
{
	UpLeft,
	Up,
	UpRight,
	Left,
	None,
	Right,
	DownLeft,
	Down,
	DownRight,
};

constexpr MoveDir DirFromDeltas(int dx, int dy)
{
	return (MoveDir)(dy * 3 + dx + 4);
}

constexpr std::pair<int, int> DeltasFromDir(int dir)
{
	return { dir % 3 - 1, dir / 3 - 1 };
}

constexpr auto GenerateDirLookup()
{
	std::array<MoveDir, 256> l = {};
	l['L'] = Left;
	l['R'] = Right;
	l['U'] = Up;
	l['D'] = Down;
	return l;
}
constexpr std::array<MoveDir, 256> DirLookup = GenerateDirLookup();

struct Move
{
	char nextState;
	char dx;
	char dy;
	char moveDir;
};

constexpr int Sign(int i)
{
	return i < 0 ? -1 : i > 0 ? 1 : 0;
}

constexpr auto GenerateStates()
{
	std::array<std::array<Move, 9>, 9> s{};

	for (int old = 0; old < 9; old++)
	{
		auto [x, y] = DeltasFromDir(old);

		for (int n = 0; n < 9; n++)
		{
			auto [dx, dy] = DeltasFromDir(n);

			int nx = x - dx;	// tails moves relatively in opposite direction
			int ny = y - dy;

			if (nx < -1 || nx > 1 || ny < -1 || ny > 1)
			{
				dx = -Sign(nx);
				dy = -Sign(ny);
				nx += dx;
				ny += dy;
			}
			else
			{
				dx = 0;
				dy = 0;
			}

			int newStateIdx = DirFromDeltas(nx, ny);
			s[old][n] = { char(newStateIdx), char(dx), char(dy), DirFromDeltas(dx, dy) };
		}
	}

	return s;
}
constexpr std::array<std::array<Move, 9>, 9> States = GenerateStates();




constexpr uint BitsPerLevel = 16;
constexpr uint MaxLevel = (64 + BitsPerLevel - 1) / BitsPerLevel - 1;

template<uint Level>
struct BitPage;

template<>
struct BitPage<0>
{
	static constexpr ullong WordSize = 64;
	static constexpr ullong WordShift = 6;
	static constexpr ullong NumWords = (1ll << BitsPerLevel) / WordSize;
	static constexpr ullong LevelMask = NumWords - 1;

	auto Set(ullong b)
	{
		ullong idx = (b >> WordShift) & LevelMask;
		ullong m = 1ull << (b & WordSize - 1);

		auto old = words[idx];
		words[idx] |= m;
		return !(old & m);
	}

	auto Set(ullong b, BitPage<0>*& pageCache)
	{
		pageCache = this;
		return Set(b);
	}

	alignas(64) std::array<ullong, NumWords> words{};
};

template<uint Level>
struct BitPage
{
	using PageType = BitPage<Level - 1>;
	using PagePtr = std::unique_ptr<PageType>;
	static constexpr ullong ElemsPerPage = 1ull << BitsPerLevel;
	static constexpr ullong LevelShift = Level * BitsPerLevel;
	static constexpr ullong LevelMask = ElemsPerPage - 1;

	auto Set(ullong b, BitPage<0>*& pageCache)
	{
		ullong idx = (b >> LevelShift) & LevelMask;
		if (!pages[idx])
			pages[idx] = std::make_unique<PageType>();
		return pages[idx]->Set(b, pageCache);
	}

	std::array<PagePtr, ElemsPerPage> pages;
};

class BitGrid
{
public:
	static constexpr ullong CacheBits = 4;
	static constexpr ullong CacheMask = ~((1 << BitsPerLevel) - 1);

	BitGrid()
	{
		std::ranges::fill(m_pageCache, std::pair{ 1ull, nullptr });
	}

	bool Set(int x, int y)
	{
		// interleave bits of x and y
		ullong b = _pdep_u64(uint(x), 0x5555'5555'5555'5555ull) | _pdep_u64(uint(y), 0xaaaa'aaaa'aaaa'aaaaull);

		int cacheIdx = (b >> BitsPerLevel) & ((1 << CacheBits) - 1);
		if (m_pageCache[cacheIdx].first == (b & CacheMask))
			return m_pageCache[cacheIdx].second->Set(b);

		m_pageCache[cacheIdx].first = b & CacheMask;
		return m_topPage->Set(b, m_pageCache[cacheIdx].second);
	}

	void Clear()
	{
		m_topPage.reset();
	}

private:
	std::unique_ptr<BitPage<MaxLevel>> m_topPage = std::make_unique<BitPage<MaxLevel>>();
	std::pair<ullong, BitPage<0>*> m_pageCache[1 << CacheBits];
};


bool Run(const std::filesystem::path &file)
{
	Timer total(AutoStart);
	Timer tload(AutoStart);
	MemoryMappedFile mmap(file);
	if (!mmap)
		return false;
	int size = (int)mmap.GetSize();
	auto data = mmap.GetSpan<const char>();
	const char* ptr = (const char*)data.data();
	const char* end = ptr + size;
	tload.Stop();

	GenerateStates();

	Timer tsimulate(AutoStart);
	std::vector<ullong> coords;
	constexpr int NumKnots = 9;
	int x = 0, y = 0;
	int knotStates[NumKnots] = { };
	std::fill(std::begin(knotStates), std::end(knotStates), 4);
	//int minx = 0, miny = 0, maxx = 0, maxy = 0;

	BitGrid grid;
	int numCells = 1;
	grid.Set(0, 0);

	while (ptr < end)
	{
		char d = *ptr;
		ptr += 2;
		int n = int(std::find(ptr, end, '\n') - ptr);
		int num = conv(ptr, n);
		ptr += n + 1;

		MoveDir dir = DirLookup[d & 0xff];
		for (int i = 0; i < num; i++)
		{
			int d = dir;
			Move lastMove;
			for (int k = 0; k < NumKnots /*&& d != None*/; k++)
			{
				auto& state = knotStates[k];
				lastMove = States[state][d];
				state = lastMove.nextState;
				d = lastMove.moveDir;
			}

			if (d != None)
			{
				x += lastMove.dx;
				y += lastMove.dy;
				numCells += grid.Set(x, y);
			}

			//minx = std::min(minx, x);
			//miny = std::min(miny, y);
			//maxx = std::max(maxx, x);
			//maxy = std::max(maxy, y);
			//std::cout << std::format("({}, {})\n", x, y);
		}
	}
	tsimulate.Stop();

	total.Stop();
	std::cout << "Time: " << total.GetTime() << "us (load:" << tload.GetTime() << "us, sim:" << tsimulate.GetTime() << "us)\n" << numCells << "\n";
	//std::cout << std::format("({}, {}) - ({}, {})\n", minx, miny, maxx, maxy);

	return true;
}


int main(int argc, char* argv[])
{
	const char* inputs[] =
	{
		//"example.txt",
		//"example-2.txt",
		"input.txt",
		"aoc_2022_day09_large-1.txt",
		"aoc_2022_day09_large-2.txt",
	};
	const char **const_argv = const_cast<const char**>(argv);
	const char **inputs_begin = argc > 1 ? &const_argv[1] : std::begin(inputs);
	const char **inputs_end = argc > 1 ? &const_argv[argc] : std::end(inputs);

	constexpr int NumRuns = 1;
	for (const char **input = inputs_begin; input != inputs_end; ++input)
	{
		std::filesystem::path f(*input);
		std::cout << "\n===[ " << f << " ]==========\n";
		for (int i = 0; i < NumRuns; i++)
		{
			if (!Run(f))
				std::cerr << "Can't open `" << f << "`\n";
		}
	}
}
