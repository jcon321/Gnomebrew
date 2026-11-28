// parser_tests: the file and data parsers against hand-made inputs (no EQ install needed), and,
// when MYCLIENT_EQDIR names one, against the real files too. Run by ctest (CMakeLists.txt) or on
// its own; a failed check prints where and the run ends non-zero.

#include "bitmap.h"
#include "block_compress.h"
#include "dds.h"
#include "des.h"
#include "eqstr.h"
#include "eqtext.h"
#include "font_text.h"
#include "inflate.h"
#include "load_stats.h"
#include "ini_file.h"
#include "packet_log.h"
#include "pfs.h"
#include "spell_file.h"
#include "textures.h"
#include "tga.h"
#include "wld.h"
#include "xml.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace
{
	int g_failures = 0;
	int g_checks = 0;

#define CHECK(condition)                                                                                         \
	do {                                                                                                         \
		++g_checks;                                                                                              \
		if (!(condition)) {                                                                                      \
			++g_failures;                                                                                        \
			printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);                                     \
		}                                                                                                        \
	} while (0)

	struct Test
	{
		const char *name;
		std::function<void()> run;
	};

	std::vector<Test> &Tests()
	{
		static std::vector<Test> tests;
		return tests;
	}

	struct Register
	{
		Register(const char *name, std::function<void()> run) { Tests().push_back({name, std::move(run)}); }
	};

#define TEST(name)                                                                                               \
	void name();                                                                                                 \
	Register register_##name(#name, name);                                                                       \
	void name()

	// A scratch folder for the files a test writes, gone when the run ends.
	std::filesystem::path TempDir()
	{
		static std::filesystem::path dir = [] {
			auto d = std::filesystem::temp_directory_path() / ("my-client-tests-" + std::to_string(std::rand()));
			std::filesystem::create_directories(d);
			return d;
		}();
		return dir;
	}

	std::string WriteTemp(const std::string &name, const std::string &bytes)
	{
		std::string path = (TempDir() / name).string();
		std::ofstream(path, std::ios::binary) << bytes;
		return path;
	}

	std::string Bytes(const std::vector<uint8_t> &v) { return std::string(v.begin(), v.end()); }

	void PutU16(std::vector<uint8_t> &v, uint16_t x)
	{
		v.push_back(static_cast<uint8_t>(x));
		v.push_back(static_cast<uint8_t>(x >> 8));
	}

	void PutU32(std::vector<uint8_t> &v, uint32_t x)
	{
		for (int i = 0; i < 4; ++i)
			v.push_back(static_cast<uint8_t>(x >> (8 * i)));
	}

	uint32_t Adler32(const std::vector<uint8_t> &data)
	{
		uint32_t a = 1, b = 0;
		for (uint8_t c : data) {
			a = (a + c) % 65521;
			b = (b + a) % 65521;
		}
		return (b << 16) | a;
	}

	// A zlib stream of stored (uncompressed) deflate blocks, at most `block` bytes each.
	std::vector<uint8_t> ZlibStored(const std::vector<uint8_t> &data, size_t block = 65535)
	{
		std::vector<uint8_t> out{0x78, 0x01};
		size_t pos = 0;
		do {
			size_t n = std::min(block, data.size() - pos);
			out.push_back(pos + n == data.size() ? 1 : 0);
			PutU16(out, static_cast<uint16_t>(n));
			PutU16(out, static_cast<uint16_t>(~n));
			out.insert(out.end(), data.begin() + static_cast<long>(pos), data.begin() + static_cast<long>(pos + n));
			pos += n;
		} while (pos < data.size());
		uint32_t adler = Adler32(data);
		for (int i = 3; i >= 0; --i)
			out.push_back(static_cast<uint8_t>(adler >> (8 * i)));
		return out;
	}

	// A PFS archive (s3d) of `files`, each file's data in blocks of stored deflate.
	std::vector<uint8_t> MakePfs(const std::vector<std::pair<std::string, std::vector<uint8_t>>> &files)
	{
		std::vector<uint8_t> out(12, 0);
		std::memcpy(out.data() + 4, "PFS ", 4);
		out[8] = 0x00;
		out[9] = 0x00;
		out[10] = 0x02;
		out[11] = 0x00;
		auto add_entry = [&out](const std::vector<uint8_t> &data, size_t block) {
			uint32_t offset = static_cast<uint32_t>(out.size());
			size_t pos = 0;
			do {
				size_t n = std::min(block, data.size() - pos);
				std::vector<uint8_t> piece(data.begin() + static_cast<long>(pos), data.begin() + static_cast<long>(pos + n));
				std::vector<uint8_t> packed = ZlibStored(piece);
				PutU32(out, static_cast<uint32_t>(packed.size()));
				PutU32(out, static_cast<uint32_t>(n));
				out.insert(out.end(), packed.begin(), packed.end());
				pos += n;
			} while (pos < data.size());
			return offset;
		};
		struct Dir
		{
			uint32_t crc, offset, size;
		};
		std::vector<Dir> dir;
		uint32_t crc = 0x1000;
		for (const auto &[name, data] : files)
			dir.push_back({crc++, add_entry(data, 7), static_cast<uint32_t>(data.size())}); // small blocks: several each
		std::vector<uint8_t> names;
		PutU32(names, static_cast<uint32_t>(files.size()));
		for (const auto &[name, data] : files) {
			PutU32(names, static_cast<uint32_t>(name.size() + 1));
			names.insert(names.end(), name.begin(), name.end());
			names.push_back(0);
		}
		dir.push_back({0x61580AC9, add_entry(names, 4096), static_cast<uint32_t>(names.size())});
		uint32_t dir_offset = static_cast<uint32_t>(out.size());
		PutU32(out, static_cast<uint32_t>(dir.size()));
		for (const Dir &d : dir) {
			PutU32(out, d.crc);
			PutU32(out, d.offset);
			PutU32(out, d.size);
		}
		for (int i = 0; i < 4; ++i)
			out[i] = static_cast<uint8_t>(dir_offset >> (8 * i));
		return out;
	}

	std::vector<uint8_t> Text(const char *s) { return std::vector<uint8_t>(s, s + std::strlen(s)); }
}

TEST(IniFileReadsAndKeepsComments)
{
	std::string path = WriteTemp("test.ini", "; a comment\r\n[Display]\r\nWidth=1920\r\n; height next\r\nHeight = 1080\r\n"
	                                         "Flag=TRUE\r\n\r\n[Sound]\r\nSound=0\r\n");
	IniFile ini;
	CHECK(ini.Load(path));
	CHECK(ini.GetInt("Display", "Width", 0) == 1920);
	CHECK(ini.GetInt("display", "height", 0) == 1080); // sections and keys ignore case, values are trimmed
	CHECK(ini.GetBool("Display", "Flag", false));
	CHECK(!ini.GetBool("Sound", "Sound", true));
	CHECK(ini.Get("Display", "Missing", "x") == "x");
	CHECK(!ini.Has("Nowhere", "Width"));
	ini.SetInt("Display", "Width", 800);
	ini.Set("New", "Key", "value");
	CHECK(ini.Save(path));
	IniFile again;
	CHECK(again.Load(path));
	CHECK(again.GetInt("Display", "Width", 0) == 800);
	CHECK(again.Get("New", "Key") == "value");
	std::ifstream file(path, std::ios::binary);
	std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	CHECK(text.find("; height next") != std::string::npos);
	CHECK(text.find("; a comment") != std::string::npos);
}

TEST(XmlParsesSidl)
{
	std::string error;
	auto root = ParseXml("<?xml version=\"1.0\"?>\n<!-- c -->\n<XML ID=\"EQInterfaceDefinitionLanguage\">"
	                     "<Button item=\"OK\"><Text>A &amp; B &lt;C&gt;</Text><X>12</X><On>true</On></Button>"
	                     "<Button item=\"No\"/></XML>",
		error);
	CHECK(root != nullptr);
	if (!root)
		return;
	CHECK(root->name == "XML");
	CHECK(root->Attribute("ID") == "EQInterfaceDefinitionLanguage");
	auto buttons = root->Children("Button");
	CHECK(buttons.size() == 2);
	if (buttons.size() == 2) {
		CHECK(buttons[0]->Attribute("item") == "OK");
		CHECK(buttons[0]->ChildText("Text") == "A & B <C>");
		CHECK(buttons[0]->ChildInt("X", 0) == 12);
		CHECK(buttons[0]->ChildBool("On", false));
		CHECK(buttons[1]->children.empty());
	}
	std::string bad_error;
	CHECK(ParseXml("<a><b></a>", bad_error) == nullptr);
	CHECK(!bad_error.empty());
}

TEST(InflateStoredFixedAndDynamic)
{
	std::vector<uint8_t> data = Text("stored blocks, split in pieces");
	std::vector<uint8_t> out;
	std::vector<uint8_t> stored = ZlibStored(data, 5);
	CHECK(Inflate::Zlib(stored.data(), stored.size(), out) && out == data);

	// zlib.compress(b"Hello, Norrath! " * 3 (less the last space), 9): fixed Huffman codes.
	const uint8_t fixed[] = {0x78, 0xda, 0xf3, 0x48, 0xcd, 0xc9, 0xc9, 0xd7, 0x51, 0xf0, 0xcb, 0x2f, 0x2a, 0x4a, 0x2c,
		0xc9, 0x50, 0x54, 0xf0, 0xc0, 0xcf, 0x07, 0x00, 0x80, 0x53, 0x0f, 0xfe};
	out.clear();
	CHECK(Inflate::Zlib(fixed, sizeof(fixed), out));
	CHECK(Bytes(out) == "Hello, Norrath! Hello, Norrath! Hello, Norrath!");

	// zlib.compress of 120 race names (below), level 9: one dynamic Huffman block.
	const uint8_t dynamic[] = {0x78, 0xda, 0xed, 0x8d, 0xd1, 0x09, 0x80, 0x30, 0x0c, 0x05, 0x57, 0x79, 0xab, 0x45, 0x9a,
		0xa6, 0xc1, 0xd8, 0x40, 0x5a, 0xed, 0xfa, 0x8a, 0x14, 0x74, 0x88, 0xc2, 0xfd, 0x1d, 0xdc, 0x49, 0xf5, 0x83, 0x71,
		0x51, 0x41, 0x2b, 0x1a, 0x28, 0x64, 0xd9, 0xb4, 0x0a, 0x7a, 0xb8, 0x19, 0x86, 0x7b, 0x02, 0x5b, 0x86, 0xee, 0x8d,
		0x7e, 0x36, 0x0d, 0x8a, 0x8c, 0x8d, 0xe2, 0x41, 0xa9, 0x4e, 0xed, 0x12, 0x0c, 0x79, 0x83, 0x9f, 0xe2, 0x38, 0x93,
		0x76, 0x9e, 0x41, 0x59, 0xbb, 0xb5, 0x5b, 0x3b, 0xbb, 0x01, 0xd0, 0x94, 0x49, 0xf0};
	const char *words[] = {"gnome", "dwarf", "troll", "ogre", "halfling", "erudite", "iksar", "vah shir", "barbarian",
		"wood elf"};
	std::string expected;
	for (int i = 0; i < 120; ++i)
		expected += (i ? " " : "") + std::string(words[(i * 7 + i / 3) % 10]);
	out.clear();
	CHECK(Inflate::Zlib(dynamic, sizeof(dynamic), out));
	CHECK(Bytes(out) == expected);

	// Cut short, or a reserved block type: refused, not read past the end.
	out.clear();
	CHECK(!Inflate::Zlib(dynamic, 20, out));
	std::vector<uint8_t> reserved{0x78, 0x01, 0x07, 0x00};
	out.clear();
	CHECK(!Inflate::Zlib(reserved.data(), reserved.size(), out));
}

TEST(PfsArchiveReadsFiles)
{
	std::vector<uint8_t> big;
	for (int i = 0; i < 100; ++i)
		big.push_back(static_cast<uint8_t>(i * 3));
	std::vector<uint8_t> archive = MakePfs({{"Zone.WLD", Text("world data")}, {"texture.bmp", big}});
	std::string path = WriteTemp("test.s3d", Bytes(archive));
	for (bool lazy : {false, true}) {
		PfsArchive pfs;
		CHECK(pfs.Open(path, lazy));
		CHECK(pfs.Contains("zone.wld") && pfs.Contains("TEXTURE.BMP"));
		CHECK(!pfs.Contains("missing.bmp"));
		CHECK(pfs.Names().size() == 2);
		std::vector<uint8_t> out;
		CHECK(pfs.Read("ZONE.wld", out) && Bytes(out) == "world data");
		CHECK(pfs.Read("texture.bmp", out) && out == big);
	}
	// Cut off inside the directory: no archive.
	archive.resize(archive.size() - 6);
	PfsArchive cut;
	CHECK(!cut.Open(WriteTemp("cut.s3d", Bytes(archive))));
	PfsArchive none;
	CHECK(!none.Open((TempDir() / "nothing.s3d").string()));
}

TEST(BmpDecodes)
{
	// 2x2, 8-bit with a palette of 2, bottom-up: index 0 the top-left, index 1 the rest.
	std::vector<uint8_t> b{'B', 'M'};
	PutU32(b, 0); // size (not read)
	PutU32(b, 0);
	PutU32(b, 14 + 40 + 2 * 4); // pixels' offset
	PutU32(b, 40);
	PutU32(b, 2);
	PutU32(b, 2);
	PutU16(b, 1);
	PutU16(b, 8);
	PutU32(b, 0);
	PutU32(b, 0);
	PutU32(b, 0);
	PutU32(b, 0);
	PutU32(b, 2); // colors used
	PutU32(b, 0);
	PutU32(b, 0x00ff00ff); // index 0: magenta (B, G, R, 0)
	PutU32(b, 0x00336699); // index 1
	for (uint8_t row : {1, 0}) { // the bottom row first
		b.push_back(row == 0 ? 0 : 1);
		b.push_back(1);
		b.push_back(0);
		b.push_back(0); // rows padded to 4 bytes
	}
	Image image;
	CHECK(DecodeBmp(b, image));
	CHECK(image.width == 2 && image.height == 2);
	if (image.pixels.size() == 4) {
		CHECK((image.At(0, 0) & 0xffffff) == 0xff00ff);
		CHECK((image.At(1, 0) & 0xffffff) == 0x336699);
		CHECK((image.At(0, 1) & 0xffffff) == 0x336699);
		CHECK(!image.has_alpha);
	}
	Image keyed;
	CHECK(DecodeBmp(b, keyed, true));
	if (keyed.pixels.size() == 4) {
		CHECK((keyed.At(0, 0) >> 24) == 0); // palette index 0 masked
		CHECK((keyed.At(1, 0) >> 24) == 0xff);
		CHECK(keyed.has_alpha);
	}
	std::vector<uint8_t> truncated(b.begin(), b.begin() + 60);
	CHECK(!DecodeBmp(truncated, image));
}

TEST(BlockCompressRoundTrips)
{
	// A 8x4 level: a flat block, and a gradient of four evenly spaced colors (what a block's
	// palette holds) with a masked (0/255) alpha. BC1 keeps the flat color to its 565 precision
	// and the gradient close; BC3 keeps a binary alpha exactly.
	std::vector<uint32_t> texels(8 * 4);
	for (int y = 0; y < 4; ++y)
		for (int x = 0; x < 8; ++x) {
			uint32_t t = static_cast<uint32_t>(x - 4) * 45;
			texels[static_cast<size_t>(y) * 8 + x] =
				x < 4 ? 0xff336699u : ((x + y) % 3 ? 0xff000000u : 0u) | (40 + t) << 16 | (200 - t) << 8 | 0x80u;
		}
	auto close = [](uint32_t a, uint32_t b, int tolerance) {
		for (int s = 0; s < 24; s += 8)
			if (std::abs(static_cast<int>((a >> s) & 0xff) - static_cast<int>((b >> s) & 0xff)) > tolerance)
				return false;
		return true;
	};
	for (block_compress::Format format : {block_compress::Format::BC1, block_compress::Format::BC3}) {
		std::vector<uint8_t> blocks = block_compress::Compress(texels.data(), 8, 4, format);
		int bytes = block_compress::BlockBytes(format);
		CHECK(blocks.size() == static_cast<size_t>(2 * bytes));
		uint32_t out[2][16];
		for (int b = 0; b < 2; ++b)
			DecodeDxtBlock(format == block_compress::Format::BC1 ? 1 : 5, blocks.data() + b * bytes, out[b]);
		bool flat = true, gradient = true, alpha = true;
		for (int i = 0; i < 16; ++i) {
			uint32_t want = texels[static_cast<size_t>(i / 4) * 8 + 4 + i % 4];
			flat = flat && close(out[0][i], 0xff336699u, 4) && (out[0][i] >> 24) == 0xff;
			gradient = gradient && close(out[1][i], want, 12);
			if (format == block_compress::Format::BC3)
				alpha = alpha && (out[1][i] >> 24) == (want >> 24);
		}
		CHECK(flat);
		CHECK(gradient);
		CHECK(alpha);
	}
	// A level smaller than a block (a mip's 2x1): one block, its edge texels repeated.
	uint32_t tiny[2] = {0xffff0000u, 0xff0000ffu};
	std::vector<uint8_t> one = block_compress::Compress(tiny, 2, 1, block_compress::Format::BC1);
	uint32_t out[16];
	CHECK(one.size() == 8);
	DecodeDxtBlock(1, one.data(), out);
	CHECK(close(out[0], tiny[0], 8) && close(out[1], tiny[1], 8) && close(out[3], tiny[1], 8));
}

TEST(TgaDecodesRawAndRle)
{
	auto header = [](uint8_t type, int bpp, bool top_down) {
		std::vector<uint8_t> h(18, 0);
		h[2] = type;
		h[12] = 2;
		h[14] = 2;
		h[16] = static_cast<uint8_t>(bpp);
		h[17] = top_down ? 0x20 : 0;
		return h;
	};
	// Raw, 32-bit, top-down: BGRA.
	std::vector<uint8_t> raw = header(2, 32, true);
	for (uint32_t p : {0x80112233u, 0xff445566u, 0xff778899u, 0x00aabbccu}) {
		raw.push_back(p & 0xff);
		raw.push_back((p >> 8) & 0xff);
		raw.push_back((p >> 16) & 0xff);
		raw.push_back(p >> 24);
	}
	Image image;
	CHECK(DecodeTga(raw, image));
	CHECK(image.width == 2 && image.height == 2 && image.pixels.size() == 4);
	if (image.pixels.size() == 4) {
		CHECK(image.At(0, 0) == 0x80112233u);
		CHECK(image.At(1, 1) == 0x00aabbccu);
		CHECK(image.has_alpha);
	}
	// RLE, 24-bit, bottom-up: a run of 3 then one raw pixel.
	std::vector<uint8_t> rle = header(10, 24, false);
	rle.insert(rle.end(), {0x82, 0x01, 0x02, 0x03, 0x00, 0x0a, 0x0b, 0x0c});
	CHECK(DecodeTga(rle, image));
	if (image.pixels.size() == 4) {
		CHECK(image.At(0, 1) == 0xff030201u); // the file's first row is the bottom one
		CHECK(image.At(1, 0) == 0xff0c0b0au);
		CHECK(!image.has_alpha);
	}
	std::vector<uint8_t> overrun = header(10, 24, false);
	overrun.insert(overrun.end(), {0x85, 0x01, 0x02, 0x03}); // 6 pixels into 4
	CHECK(!DecodeTga(overrun, image));
	CHECK(!DecodeTga(header(1, 8, false), image)); // color-mapped: not ours
}

TEST(DesKnownAnswer)
{
	CHECK(Des::SelfTest());
	// The classic example: key 133457799BBCDFF1, 0123456789ABCDEF -> 85E813540F0AB405 (one CBC block
	// with a zero IV is the plain block cipher).
	const uint8_t key[8] = {0x13, 0x34, 0x57, 0x79, 0x9b, 0xbc, 0xdf, 0xf1};
	const uint8_t iv[8] = {};
	const uint8_t plain[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};
	const uint8_t cipher[8] = {0x85, 0xe8, 0x13, 0x54, 0x0f, 0x0a, 0xb4, 0x05};
	uint8_t out[8], back[8];
	Des::CbcEncrypt(key, iv, plain, out, 8);
	CHECK(std::memcmp(out, cipher, 8) == 0);
	Des::CbcDecrypt(key, iv, out, back, 8);
	CHECK(std::memcmp(back, plain, 8) == 0);
}

TEST(EqStrFormatsArguments)
{
	std::string path = WriteTemp("eqstr_en.txt", "EQST0002\r\n0 4\r\n100 %1 hits %2 for %3 points.\r\n"
	                                             "101 You have %T1.\r\n102 lost\r\n103 100%% sure, see %#102\r\n");
	EqStr eqstr;
	CHECK(eqstr.Load(path));
	CHECK(eqstr.Has(100) && !eqstr.Has(99));
	CHECK(eqstr.Format(100, {"A gnoll", "you", "5"}) == "A gnoll hits you for 5 points.");
	CHECK(eqstr.Format(101, {"102"}) == "You have lost.");
	CHECK(eqstr.Format(103, {}) == "100% sure, see lost");
	CHECK(eqstr.Format(99, {"x"}) == "[string 99] x");
}

TEST(EqTextReadsQuotedStrings)
{
	std::string path = WriteTemp("eqtext.txt", "LOGIN_TITLE \"Log In\"\r\nWITH_ESCAPES \"Tab\\there\\n\\\"quoted\\\"\"\r\n");
	EqText text;
	CHECK(text.Load(path));
	CHECK(text.Count() == 2);
	CHECK(text.Get("LOGIN_TITLE") == "Log In");
	CHECK(text.Get(1) == "Tab\there\n\"quoted\"");
}

TEST(PacketLogRoundTrips)
{
	std::string error;
	std::string path;
	{
		PacketLogWriter writer;
		CHECK(writer.Open(TempDir().string(), "Tester", "127.0.0.1:7000", error));
		const uint8_t payload[] = {1, 2, 3, 4, 5};
		writer.Write(false, 0x4007, payload, sizeof(payload));
		writer.Write(true, 0x4110, nullptr, 0);
		path = writer.Path();
	}
	PacketLog log;
	CHECK(log.Load(path, error));
	CHECK(log.character == "Tester");
	CHECK(log.packets.size() == 2);
	if (log.packets.size() == 2) {
		CHECK(!log.packets[0].outgoing && log.packets[0].opcode == 0x4007);
		CHECK(log.packets[0].data == std::vector<uint8_t>({1, 2, 3, 4, 5}));
		CHECK(log.packets[1].outgoing && log.packets[1].opcode == 0x4110 && log.packets[1].data.empty());
	}
	PacketLog bad;
	CHECK(!bad.Load(WriteTemp("bad.eqr", "not a packet log"), error));
}

TEST(Utf8DecodesAsWindowsDoes)
{
	CHECK(FontTextRenderer::Decode("A\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80") == U"Aé€\U0001F600");
	CHECK(FontTextRenderer::Decode("\xff") == U"�");            // never a lead byte
	CHECK(FontTextRenderer::Decode("\xc3") == U"�");            // cut short
	CHECK(FontTextRenderer::Decode("\xc0\xaf") == U"��");  // overlong
	CHECK(FontTextRenderer::Decode("\xed\xa0\x80").size() == 3);     // a surrogate: each byte replaced
	CHECK(FontTextRenderer::Decode("") .empty());
}

// The real files, when MYCLIENT_EQDIR names an EQ install.
TEST(RealFilesWhenThere)
{
	const char *eq_dir = std::getenv("MYCLIENT_EQDIR");
	if (!eq_dir) {
		printf("  (MYCLIENT_EQDIR not set: skipped)\n");
		return;
	}
	std::string dir = eq_dir;
	{
		// A Luclin zone's DXT texture through the cache as a GPU renderer has them: block compressed
		// with its mips (no decoded levels), counted, and gone once Release says so.
		PfsArchive zone;
		CHECK(zone.Open(dir + "/dawnshroud.s3d", true));
		std::string dds;
		for (const std::string &name : zone.Names())
			if (name.size() > 4 && name.compare(name.size() - 4, 4, ".dds") == 0) {
				dds = name;
				break;
			}
		CHECK(!dds.empty());
		render::SetGpuTextures(true);
		int64_t before = load_stats::texel_bytes.load();
		{
			render::TextureCache cache;
			cache.AddArchive(&zone);
			const render::Texture *texture = cache.Get(dds, false);
			render::SetGpuTextures(false);
			CHECK(texture && texture->levels.empty() && (texture->block_format == 1 || texture->block_format == 3));
			CHECK(texture && texture->blocks.size() == static_cast<size_t>(std::max(texture->width_shift, texture->height_shift) + 1));
			CHECK(texture && texture->blocks.back().size() == static_cast<size_t>(texture->block_format == 1 ? 8 : 16));
			CHECK(load_stats::texel_bytes.load() > before);
			if (texture)
				texture->Release();
			CHECK(load_stats::texel_bytes.load() == before);
		}
		render::SetGpuTextures(false);
	}
	PfsArchive global;
	CHECK(global.Open(dir + "/global_chr.s3d", true));
	std::vector<uint8_t> data;
	CHECK(global.Read("global_chr.wld", data));
	WldFile wld;
	CHECK(wld.Load(std::move(data)));
	int meshes = 0, read = 0;
	WldMesh mesh;
	for (int i = 0; i < static_cast<int>(wld.Fragments().size()); ++i)
		if (wld.Fragments()[i].type == 0x36) {
			++meshes;
			read += wld.ReadMesh(i, mesh) ? 1 : 0;
		}
	CHECK(meshes > 0 && read == meshes);
	EqStr eqstr;
	CHECK(eqstr.Load(dir + "/eqstr_en.txt") && eqstr.Has(6682));
	EqText text;
	CHECK(text.Load(dir + "/eqtext.txt") && text.Count() > 100);
	SpellFile spells;
	CHECK(spells.Load(dir + "/spells_en.txt"));
	std::ifstream xml_file(dir + "/uifiles/default/EQUI.xml", std::ios::binary);
	std::string xml((std::istreambuf_iterator<char>(xml_file)), std::istreambuf_iterator<char>());
	std::string error;
	CHECK(!xml.empty() && ParseXml(xml, error) != nullptr);
}

int main()
{
	std::srand(static_cast<unsigned>(std::time(nullptr)));
	for (const Test &test : Tests()) {
		int before = g_failures;
		printf("%s\n", test.name);
		test.run();
		if (g_failures != before)
			printf("  -> %d failed\n", g_failures - before);
	}
	std::error_code ec;
	std::filesystem::remove_all(TempDir(), ec);
	printf("%d checks, %d failed, %zu tests\n", g_checks, g_failures, Tests().size());
	return g_failures == 0 ? 0 : 1;
}
