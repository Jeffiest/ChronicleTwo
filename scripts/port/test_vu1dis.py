import unittest

from vu1dis import (
    DEFAULT_ELF, DEFAULT_SYMBOLS, _Image, _gif_items, _segments_from_phdrs, _segments_from_sections,
    build_listings, decode_dmatag, decode_lower, decode_pair, decode_upper, decode_vifcode, load_image,
    load_symbols,
)


def upper(op, *, fd=0, fs=0, ft=0, dest=0, e=0, i=0):
    return (i << 31) | (e << 30) | (dest << 21) | (ft << 16) | (fs << 11) | (fd << 6) | op


def upper_special(idx, **fields):
    return upper(0x3C | (idx & 3), fd=idx >> 2, **fields)


def lower_op(op, *, fd=0, fs=0, ft=0, dest=0, raw=0):
    return (0x40 << 25) | (dest << 21) | (ft << 16) | (fs << 11) | (fd << 6) | op | raw


def lower_special(idx, **fields):
    return lower_op(0x3C | (idx & 3), fd=idx >> 2, **fields)


def lower_top(top, *, dest=0, ft=0, fs=0, imm=0):
    return (top << 25) | (dest << 21) | (ft << 16) | (fs << 11) | (imm & 0x7FF)


class UpperTests(unittest.TestCase):
    def test_nop(self):
        self.assertEqual(decode_upper(0x000002FF), "nop")

    def test_nop_end_flag(self):
        self.assertEqual(decode_upper(0x400002FF), "nop[E]")

    def test_addx_broadcast(self):
        self.assertEqual(decode_upper(upper(0x00, fd=1, fs=2, ft=3, dest=0xF)),
                         "addx.xyzw vf01, vf02, vf03x")

    def test_mulq(self):
        self.assertEqual(decode_upper(upper(0x1C, fd=1, fs=2, dest=0xE)), "mulq.xyz vf01, vf02, Q")

    def test_madda(self):
        self.assertEqual(decode_upper(upper_special(0x29, fs=2, ft=3, dest=0xF)),
                         "madda.xyzw ACC, vf02, vf03")

    def test_ftoi4(self):
        self.assertEqual(decode_upper(upper_special(0x15, fs=2, ft=3, dest=0xF)), "ftoi4.xyzw vf03, vf02")

    def test_clipw(self):
        self.assertEqual(decode_upper(upper_special(0x1F, fs=2, ft=3)), "clipw.xyz vf02, vf03w")

    def test_opmula(self):
        self.assertEqual(decode_upper(upper_special(0x2E, fs=2, ft=3, dest=0xE)),
                         "opmula.xyz ACC, vf02, vf03")

    def test_unknown_special(self):
        self.assertTrue(decode_upper(upper_special(0x2B)).startswith("???"))


class LowerTests(unittest.TestCase):
    def test_nop(self):
        self.assertEqual(decode_lower(0x8000033C, 0, {}), "nop")

    def test_lq(self):
        word = lower_top(0x00, dest=0xF, ft=1, fs=3, imm=2)
        self.assertEqual(decode_lower(word, 0, {}), "lq.xyzw vf01, 2(vi03)")

    def test_sq(self):
        word = lower_top(0x01, dest=0xF, fs=1, ft=3, imm=2)
        self.assertEqual(decode_lower(word, 0, {}), "sq.xyzw vf01, 2(vi03)")

    def test_iaddiu(self):
        self.assertEqual(decode_lower(lower_top(0x08, ft=2, fs=1, imm=5), 0, {}), "iaddiu vi02, vi01, 5")

    def test_ibne_negative_offset(self):
        word = lower_top(0x29, ft=1, fs=2, imm=-4)
        self.assertEqual(decode_lower(word, 0x10, {0x0D: "loop"}), "ibne vi01, vi02, loop (0x000d)")

    def test_b(self):
        self.assertEqual(decode_lower(lower_top(0x20, imm=2), 5, {}), "b L_0008 (0x0008)")

    def test_xgkick(self):
        self.assertEqual(decode_lower(lower_special(0x6C, fs=12), 0, {}), "xgkick vi12")

    def test_div(self):
        word = lower_special(0x38, fs=1, ft=2, raw=1 << 23)
        self.assertEqual(decode_lower(word, 0, {}), "div Q, vf01x, vf02y")

    def test_fcand(self):
        self.assertEqual(decode_lower((0x12 << 25) | 0x3F, 0, {}), "fcand vi01, 0x3f")

    def test_mtir(self):
        self.assertEqual(decode_lower(lower_special(0x3C, ft=5, fs=2), 0, {}), "mtir vi05, vf02x")

    def test_esadd(self):
        self.assertEqual(decode_lower(lower_special(0x70, fs=3), 0, {}), "esadd P, vf03")

    def test_i_bit_loads_float_immediate(self):
        self.assertEqual(decode_pair(0x800002FF, 0x3F800000, 0, {}), ("nop[I]", "loi 1.0 (0x3f800000)"))


class EncodingTests(unittest.TestCase):
    def test_dmatag_ret(self):
        tag = decode_dmatag(0x6000006C)
        self.assertEqual((tag["name"], tag["qwc"]), ("ret", 0x6C))

    def test_dmatag_cnt(self):
        tag = decode_dmatag(0x10000001)
        self.assertEqual((tag["name"], tag["qwc"]), ("cnt", 1))

    def test_vif_mpg(self):
        info = decode_vifcode(0x4AD60064)
        self.assertEqual((info["name"], info["num"], info["imm"], info["pairs"]), ("MPG", 0xD6, 0x64, 214))

    def test_vif_mscal(self):
        info = decode_vifcode(0x14000000)
        self.assertEqual((info["name"], info["imm"]), ("MSCAL", 0))

    def test_vif_stcycl(self):
        info = decode_vifcode(0x01000404)
        self.assertEqual((info["name"], info["cl"], info["wl"]), ("STCYCL", 4, 4))

    def test_vif_unpack(self):
        info = decode_vifcode(0x6C048000)
        self.assertEqual((info["name"], info["fmt"], info["num"], info["addr"], info["flg"]),
                         ("UNPACK", "V4-32", 4, 0, 1))
        self.assertEqual(info["payload_words"], 16)

    def test_vif_direct_zero_count_is_65536(self):
        self.assertEqual(decode_vifcode(0x50000000)["qwords"], 65536)


class GifTests(unittest.TestCase):
    def test_aplusd_register_is_named(self):
        tag_lo = 1 | (1 << 60)
        items = _gif_items([(0x100, tag_lo, 0xE), (0x110, 1, 0x46)])
        self.assertIn("nloop=1", items[0][2])
        self.assertEqual(items[1][2], "GIF A+D COLCLAMP (0x46) = 0x0000000000000001")


@unittest.skipUnless(DEFAULT_ELF.is_file() and DEFAULT_SYMBOLS.is_file(), "retail ELF or symbols missing")
class RetailTests(unittest.TestCase):
    def test_all_programs_decode_clean(self):
        image, _ = load_image(DEFAULT_ELF)
        listings = build_listings(image, load_symbols(DEFAULT_SYMBOLS))
        self.assertEqual(len(listings), 6)
        for listing in listings:
            with self.subTest(program=listing.name):
                self.assertEqual(listing.unknown, 0)
                self.assertEqual(listing.branch_outside, [])

    def test_section_and_program_header_mappings_agree(self):
        raw = DEFAULT_ELF.read_bytes()
        by_section = _Image(raw, _segments_from_sections(raw))
        by_phdr = _Image(raw, _segments_from_phdrs(raw))
        for ea in (0x00325C80, 0x00325C8C, 0x00363480, 0x003634D0):
            with self.subTest(ea=hex(ea)):
                self.assertEqual(by_section.u64(ea), by_phdr.u64(ea))


if __name__ == "__main__":
    unittest.main()
