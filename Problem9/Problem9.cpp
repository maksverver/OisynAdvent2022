import core;
import util;
using namespace util;

enum MoveDir : uchar
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

constexpr std::pair<char, char> DeltasFromDir(int dir)
{
	__assume(dir >= UpLeft && dir <= DownRight);
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
	uint nextState : 16;
	uint moveDir : 8;
	int dx : 4;
	int dy : 4;
};

constexpr int Sign(int i)
{
	return i < 0 ? -1 : i > 0 ? 1 : 0;
}

template<int N>
using StateArray = std::array<std::array<Move, 9>, N>;

constexpr auto GenerateStates()
{
	StateArray<9> s{};

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
			s[old][n] = { uint(newStateIdx), uint(DirFromDeltas(dx, dy)), dx, dy };
		}
	}

	return s;
}
constexpr StateArray<9> States = GenerateStates();

template<int A, int B>
constexpr auto CombineStates(StateArray<A> a, StateArray<B> b)
{
	StateArray<A * B> result;

	for (int i = 0; i < A; i++)
	{
		for (int j = 0; j < B; j++)
		{
			for (int d = 0; d < 9; d++)
			{
				auto move = a[i][d];
				auto newa = move.nextState;
				auto newd = move.moveDir;

				move = b[j][newd];				
				auto newb = move.nextState;

				move.nextState = newa * B + newb;
				result[i * B + j][d] = move;
			}
		}
	}

	return result;
}
constexpr auto DoubleStates = CombineStates(States, States);
constexpr auto TripleStates = CombineStates(DoubleStates, States);
//constexpr auto QuadStates = CombineStates(DoubleStates, DoubleStates);
//constexpr auto PentaStates = CombineStates(QuadStates, States);



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


bool Run(const wchar_t* file)
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
	constexpr int NumKnotStates = 3;
	int knotStates[NumKnotStates] = { };
	std::ranges::fill(knotStates, (int)TripleStates.size() / 2);	// middle state is always fully overlapping for all knots
	int x = 0, y = 0;

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
		//int minnum = std::min(num, 20);
		Move lastMove;
		for (int i = 0; i < num; i++)
		{
			lastMove = TripleStates[knotStates[0]][dir];
			knotStates[0] = lastMove.nextState;
			auto d = lastMove.moveDir;
			if (d == None)
				continue;

			lastMove = TripleStates[knotStates[1]][d];
			knotStates[1] = lastMove.nextState;
			d = lastMove.moveDir;
			if (d == None)
				continue;

			lastMove = TripleStates[knotStates[2]][d];
			knotStates[2] = lastMove.nextState;
			d = lastMove.moveDir;
			if (d == None)
				continue;

			x += lastMove.dx;
			y += lastMove.dy;
			numCells += grid.Set(x, y);

		}
		//for (int i = minnum; i < num; i++)
		//{
		//	x += lastMove.dx;
		//	y += lastMove.dy;
		//	numCells += grid.Set(x, y);
		//}
	}
	tsimulate.Stop();

	total.Stop();
	std::cout << std::format(std::locale(""), "Time: {:L}us (load:{}us, sim:{:L}us)\n{}\n", total.GetTime(), tload.GetTime(), tsimulate.GetTime(), numCells);
	//std::cout << std::format("({}, {}) - ({}, {})\n", minx, miny, maxx, maxy);

	return true;
}


int main()
{
	const wchar_t* inputs[] =
	{
		//L"example.txt",
		//L"example-2.txt",
		//L"input.txt",
		//L"aoc_2022_day09_large-1.txt",
		L"aoc_2022_day09_large-2.txt",
	};

	constexpr int NumRuns = 10;
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
