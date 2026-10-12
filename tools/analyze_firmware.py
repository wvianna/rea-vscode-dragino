#!/usr/bin/env python3
"""Analisador de firmware Intel HEX para STM32 (ARM Cortex-M).

Uso:
    python3 tools/analyze_firmware.py <arquivo.hex> [--strings] [--map]
                                     [--vectors] [--disasm ADDR[,LEN]] [--find TEXT]

Sem argumentos de modo imprime um resumo completo (mapa de memoria, vetores e
todas as strings ASCII com endereco).

Evidencias geradas: todo endereco e impresso como 0x08xxxxxx (flash) ou
0x2000xxxx (RAM), direto do Intel HEX — nenhum byte e inventado.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB, CS_MODE_MCLASS
from intelhex import IntelHex

# ---------------------------------------------------------------------------
# Mapa de vetores Cortex-M0+ (STM32L0xx) — RM0377 / PM0223
# ---------------------------------------------------------------------------
CORE_EXC = [
    "Initial_SP", "Reset", "NMI", "HardFault", "Reserved4", "Reserved5",
    "Reserved6", "Reserved7", "Reserved8", "Reserved9", "Reserved10",
    "SVCall", "Reserved12", "Reserved13", "PendSV", "SysTick",
]

# Nomes dos vetores de IRQ do STM32L072 (posicoes 0..31 apos os 16 de sistema)
IRQ = [
    "WWDG", "PVD", "RTC", "FLASH", "RCC", "EXTI0_1", "EXTI2_3", "EXTI4_15",
    "TSC", "DMA1_Ch1", "DMA1_Ch2_3", "DMA1_Ch4_7", "ADC_COMP", "LPTIM1",
    "USART4_USART5", "TIM2", "TIM3", "TIM6", "TIM7", "Reserved19",
    "TIM21", "I2C3", "TIM22", "I2C1", "I2C2", "SPI1", "SPI2", "USART1",
    "USART2", "LPUART1_RNG", "LCD", "USB",
]


def load(path: Path) -> IntelHex:
    ih = IntelHex(str(path))
    return ih


def segments(ih: IntelHex) -> list[tuple[int, int]]:
    return sorted(ih.segments())


def dump_map(ih: IntelHex) -> None:
    total = 0
    print("## Mapa de memoria")
    for start, end in segments(ih):
        size = end - start
        total += size
        print(f"  {start:#010x} - {end:#010x}  ({size:6d} B)")
    print(f"  total: {total} B ({total/1024:.1f} KiB)")


def read_word(ih: IntelHex, addr: int) -> int:
    return ih[addr] | (ih[addr + 1] << 8) | (ih[addr + 2] << 16) | (ih[addr + 3] << 24)


def dump_vectors(ih: IntelHex, flash_base: int) -> None:
    print("\n## Tabela de vetores")
    words = [read_word(ih, flash_base + 4 * i) for i in range(16 + len(IRQ))]
    sp = words[0]
    valid_sp = 0x20000000 <= sp <= 0x20014000  # STM32L072: 20 KB RAM

    def fmt(word: int) -> str:
        if word & 1:
            return f"0x{word & ~1:08x} (thumb)"
        return f"0x{word:08x} (ARM/nao-thumb)"

    print(f"  [00] {CORE_EXC[0]:<12} = 0x{sp:08x}"
          f"{'' if valid_sp else '  <-- NAO aponta para RAM!'}")
    handlers = {}
    for i in range(1, len(words)):
        name = CORE_EXC[i] if i < 16 else f"IRQ{IRQ[i - 16]}({i - 16})"
        w = words[i]
        if w:
            print(f"  [{i:02d}] {name:<20} = {fmt(w)}")
            handlers.setdefault(w & ~1, []).append(name)
    print(f"  vetores nao nulos: {sum(1 for w in words if w)} de {len(words)}")


def dump_strings(ih: IntelHex, min_len: int = 4) -> list[tuple[int, str]]:
    out: list[tuple[int, str]] = []
    pat = re.compile(rb"[\x20-\x7e]{" + str(min_len).encode() + rb",}")
    for start, end in segments(ih):
        blob = ih.tobinstr(start=start, end=end - 1)
        for m in pat.finditer(blob):
            out.append((start + m.start(), m.group().decode("ascii")))
    return out


def print_strings(ih: IntelHex, min_len: int = 4) -> None:
    print("\n## Strings ASCII (endereco: texto)")
    for addr, s in dump_strings(ih, min_len):
        print(f"  0x{addr:08x}: {s}")


def cstr(ih: IntelHex, addr: int, maxlen: int = 96) -> str:
    """Le uma string C terminada em NUL a partir de addr."""
    out = bytearray()
    for i in range(maxlen):
        try:
            c = ih[addr + i]
        except Exception:
            break
        if c == 0:
            break
        out.append(c)
    return out.decode("ascii", "replace")


def at_table(ih: IntelHex) -> None:
    """Reconstroi a tabela de descritores de comandos AT.

    Layout observado (stride 0x18 = 24 B por comando):
      +0x00  char*  nome curto      ("+XXX", ou "Z" para o ATZ)
      +0x04  uint32 id/flags
      +0x08  uint32 handler A       (get: "AT+XXX=?")
      +0x0c  uint32 handler B       (set/run: "AT+XXX=.." / "AT+XXX")
      +0x10  uint32 handler C       (help: "AT+XXX?")
      +0x14  char*  texto de ajuda  ("AT+XXX: ...")
    """
    segs = segments(ih)
    lo = min(s for s, _ in segs)
    hi = max(e for _, e in segs)
    hits: list[tuple[int, str, str, int, list[int]]] = []
    for a in range(lo, hi - 0x18, 4):
        try:
            p_name = read_word(ih, a)
            flags = read_word(ih, a + 4)
            fns = [read_word(ih, a + 8 + 4 * k) for k in range(3)]
            p_help = read_word(ih, a + 0x14)
        except Exception:
            continue
        if not (lo <= p_name < hi and lo <= p_help < hi):
            continue
        name = cstr(ih, p_name, 12)
        help_txt = cstr(ih, p_help, 80)
        if not (name.startswith("+") or name in ("Z", "?")):
            continue
        if not help_txt.startswith("AT"):
            continue
        hits.append((a, help_txt, name, flags, fns))

    if not hits:
        print("(nenhuma entrada encontrada)")
        return
    print(f"## Tabela de comandos AT: {len(hits)} descritores, stride 0x18")
    print(f"   base: 0x{hits[0][0]:08x} .. 0x{hits[-1][0]:08x}")
    print()
    print(f"   {'endereco':<12} {'nome':<12} {'id':>3}  {'handlerA':<12} {'handlerB':<12}"
          f" {'handlerC':<12} ajuda")
    for a, help_txt, name, flags, fns in hits:
        fn_txt = " ".join(f"0x{f:08x}" for f in fns)
        print(f"  0x{a:08x}  {name:<14} {flags:>3}  {fn_txt}  {help_txt}")


def constants(ih: IntelHex) -> None:
    """Conta palavras de 32 bits que apontam para perifericos/RAM/EEPROM."""
    regions = [
        ("RAM 0x2000_0000", 0x20000000, 0x20008000),
        ("Perifericos APB/AHB 0x4000_0000", 0x40000000, 0x40080000),
        ("IO/GPIO 0x5000_0000", 0x50000000, 0x50002000),
        ("Data EEPROM 0x0808_0000", 0x08080000, 0x08082000),
        ("SYSCFG/Other 0x4001_0000", 0x40010000, 0x40030000),
    ]
    hits: dict[str, set[int]] = {name: set() for name, _, _ in regions}
    for start, end in segments(ih):
        for a in range(start, end - 4, 4):
            w = read_word(ih, a)
            for name, lo, hi in regions:
                if lo <= w < hi:
                    hits[name].add(w)
    print("## Constantes de endereco encontradas no codigo")
    for name, _, _ in regions:
        vals = sorted(hits[name])
        print(f"\n  {name}: {len(vals)} valores distintos")
        for v in vals[:40]:
            print(f"    {v:#010x}")
        if len(vals) > 40:
            print(f"    ... (+{len(vals) - 40})")


def dump_words(ih: IntelHex, addr: int, length: int) -> None:
    """Despeja palavras de 32 bits anotando o que o ponteiro aponta."""
    print(f"\n## Palavras 0x{addr:08x} ({length} B)")
    for i in range(0, length, 4):
        a = addr + i
        try:
            w = read_word(ih, a)
        except Exception:
            continue
        note = ""
        if 0x08000000 <= w < 0x08080000 or 0x20000000 <= w < 0x20014000:
            try:
                s = ih.tobinstr(start=w, end=w + 31)
                txt = "".join(chr(c) if 32 <= c < 127 else "." for c in s)
                note = f"-> {txt!r}"
            except Exception:
                note = "-> (fora da imagem)"
        print(f"  0x{a:08x}: {w:08x}  {note}")


def disasm(ih: IntelHex, addr: int, length: int) -> None:
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
    md.detail = False
    if addr & 1:  # convencao Thumb: bit 0 = 1, codigo comeca no endereco par
        print(f"   (endereco Thumb 0x{addr:08x} realinhado para 0x{addr & ~1:08x})")
        addr &= ~1
    blob = ih.tobinstr(start=addr, end=addr + length - 1)
    print(f"\n## Desmontagem 0x{addr:08x} ({length} B)")
    for ins in md.disasm(blob, addr):
        print(f"  0x{ins.address:08x}: {ins.mnemonic:<8} {ins.op_str}")


def find_text(ih: IntelHex, needle: str, before: int = 0, after: int = 0) -> list[int]:
    """Endereco do texto e dos ponteiros little-endian que o apontam."""
    hits = []
    data = needle.encode()
    for start, end in segments(ih):
        blob = ih.tobinstr(start=start, end=end - 1)
        idx = blob.find(data)
        while idx != -1:
            hits.append((start + idx, needle))
            idx = blob.find(data, idx + 1)
    return hits


def words_in_flash(ih: IntelHex) -> dict[int, list[int]]:
    """{endereco_alvo: [enderecos_que_guardam_esse_valor]} para ponteiros para flash."""
    starts = [s for s, _ in segments(ih)]
    lo, hi = min(starts), max(s for _, s in segments(ih))
    ptr: dict[int, list[int]] = {}
    for addr in range(0, hi, 4):
        try:
            w = read_word(ih, addr)
        except Exception:
            continue
        if 0x08000000 < w < 0x08080000:
            ptr.setdefault(w & ~1, []).append(addr)
    return ptr


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("hexfile", type=Path)
    ap.add_argument("--vectors", action="store_true")
    ap.add_argument("--map", action="store_true")
    ap.add_argument("--strings", action="store_true")
    ap.add_argument("--min-len", type=int, default=4)
    ap.add_argument("--disasm")
    ap.add_argument("--words", help="ADDR,LEN: despeja palavras anotadas")
    ap.add_argument("--at-table", action="store_true",
                    help="reconstroi a tabela de descritores de comandos AT")
    ap.add_argument("--consts", action="store_true",
                    help="lista constantes de endereco (RAM/perifericos/EEPROM)")
    ap.add_argument("--find", action="append", default=[])
    ap.add_argument("--refs", action="append", default=[],
                    help="texto: mostra tambem os enderecos de flash que o referenciam")
    args = ap.parse_args()

    ih = load(args.hexfile)
    flash_base = min(s for s, _ in segments(ih))
    explicit = (args.vectors or args.map or args.strings or args.disasm
                or args.words or args.at_table or args.consts or args.find or args.refs)

    if not explicit or args.map:
        dump_map(ih)
    if not explicit or args.vectors:
        dump_vectors(ih, flash_base)
    if args.strings:
        print_strings(ih, args.min_len)
    for needle in args.find:
        for addr, _ in find_text(ih, needle):
            print(f"  [find] 0x{addr:08x}: {needle}")
    if args.refs:
        ptr = words_in_flash(ih)
        for needle in args.refs:
            for addr, _ in find_text(ih, needle):
                holders = ptr.get(addr, [])
                hs = ", ".join(f"0x{h:08x}" for h in holders[:8]) or "(nenhum)"
                print(f"  [refs] \"{needle}\" em 0x{addr:08x} <- {hs}")
    if args.disasm:
        parts = args.disasm.split(",")
        addr = int(parts[0], 0)
        length = int(parts[1], 0) if len(parts) > 1 else 64
        disasm(ih, addr, length)
    if args.words:
        parts = args.words.split(",")
        dump_words(ih, int(parts[0], 0), int(parts[1], 0) if len(parts) > 1 else 64)
    if args.at_table:
        at_table(ih)
    if args.consts:
        constants(ih)
    return 0


if __name__ == "__main__":
    sys.exit(main())
