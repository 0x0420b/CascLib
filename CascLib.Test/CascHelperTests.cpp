#include "stdafx.h"
#include "CppUnitTest.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#include "../CascLib/MemoryInfo.hpp"
#include "../CascLib/Endian.hpp"
#include "../CascLib/Hash.hpp"

using namespace Casc;
using namespace Casc::Endian;

namespace CascLibTest
{
	TEST_CLASS(CascHelperTests)
	{
	public:
		
		TEST_METHOD(ReadMemoryInfo)
		{
			MemoryInfo ref(5, 3221225406, 65);

			Assert::AreEqual(22, ref.file());
			Assert::AreEqual((size_t)1073741758u, ref.offset());
			Assert::AreEqual((size_t)65u, ref.size());
		}

		TEST_METHOD(SwapBytes)
		{
			uint32_t le = 3221225406u;

			Assert::AreEqual(3204448191u, readBE(le));
		}

		TEST_METHOD(HashFilename)
		{
			auto hash = Hash::lookup3("SPELLS\\BONE_CYCLONE_STATE.M2", 0);
			Assert::AreEqual(0x21F9EFA8u, hash);
		}

	};
}