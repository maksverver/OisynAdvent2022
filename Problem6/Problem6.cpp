import core;
import util;

using namespace util;

template<uint WindowSize>
uint GetStart(std::span<const char> data)
{
	uint noLongerInWindow[26] = { };
	uint* noLongerInWindowO = noLongerInWindow - (int)'a';
	uint nextOk = WindowSize;
	uint size = uint(data.size());
	for (uint i = 0; i < size; i++)
	{
		if (i == nextOk)
			return i;
		char c = data[i];
		nextOk = std::max(nextOk, std::exchange(noLongerInWindowO[c], i + WindowSize + 1));
	}

	return ~0u; // bad input!
}

int main(int argc, char *argv[])
{
	Timer timer(AutoStart);

	const char *filename = argc > 1 ? argv[1] : "input.txt";
	MemoryMappedFile mmap(filename);
	if (!mmap)
		return (std::cerr << "Can't open " << filename << "\n", 1);

	auto fileSize = mmap.GetSize();
	auto data = mmap.GetSpan<const char>();
	const char* basePtr = data.data();

	auto futures = DoParallel([=](int threadIdx, int numThreads)
	{
		auto offset = uint(fileSize * threadIdx / numThreads);
		auto end = uint(fileSize * (threadIdx + 1) / numThreads);
		if (threadIdx)
			offset -= 20;
		std::span data{ basePtr + offset, basePtr + end };
		auto r = GetStart<14>(data);
		if (r != ~0u)
			r += offset;
		return r;
	});

	uint result = ~0u;
	for (auto& f : futures)
	{
		uint r = f.get();
		if (r != ~0u)
		{
			result = r;
			break;
		}
	}

	timer.Stop();
	std::cout << "Time: " << timer.GetTime() << " us\n";
	std::cout << result << std::endl;
	std::cout << std::string_view{ basePtr + result - 14, basePtr + result } << "\n";
}
