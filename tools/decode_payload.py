#!/usr/bin/env python3
"""Decodificador de payload do no' (MOD1..MOD6) — espelho de core/payload.c.

Uso:
    python3 tools/decode_payload.py --mode 1 451F05021F0200EB03
    python3 tools/decode_payload.py --mode 2 --tf-luna 451F001F0226070000
    python3 tools/decode_payload.py --selftest

Escalas conforme o manual LSN50 v1.7.4 §2.4: bateria e ADC em mV,
temperatura em decimos de grau com sinal, digital in no bit 1 (PA12).
As mesmas amostras de referencia estao nos testes de host do firmware
(firmware/tests/test_core.c), o que permite comparar as duas implementacoes.
"""

from __future__ import annotations

import argparse
import struct
import sys

DIN_PA12 = 0x01
DIN_INT1 = 0x02
DIN_INT2 = 0x04
DIN_INT3 = 0x08
DIN_COUNT = 0x10


def _s16(v: int) -> int:
    return v - 0x10000 if v & 0x8000 else v


def decode(mode: int, data: bytes, tf_luna: bool = False) -> dict:
    need = 12 if mode == 3 else 11
    if len(data) < need:
        raise ValueError(f"payload curto para MOD{mode}: {len(data)} < {need}")

    out: dict = {"mode": mode, "bytes": len(data)}
    p = 0

    def u16() -> int:
        nonlocal p
        v = struct.unpack_from("<H", data, p)[0]
        p += 2
        return v

    def u8() -> int:
        nonlocal p
        v = data[p]
        p += 1
        return v

    def u32() -> int:
        nonlocal p
        v = struct.unpack_from("<I", data, p)[0]
        p += 4
        return v

    def din() -> None:
        nonlocal p
        byte = u8()
        out["digital_in_pa12"] = bool(byte & DIN_PA12)
        out["interrupt_int1_pb14"] = bool(byte & DIN_INT1)
        out["interrupt_int2_pb15"] = bool(byte & DIN_INT2)
        out["interrupt_int3_pa4"] = bool(byte & DIN_INT3)
        out["count_flag"] = bool(byte & DIN_COUNT)

    def temp(key: str) -> None:
        out[key] = _s16(u16()) / 10.0

    if mode == 1:
        out["battery_mv"] = u16()
        temp("temp_ds18b20_C")
        out["adc_pa0_mv"] = u16()
        din()
        temp("sht_temp_C")
        out["sht_hum_%"] = u16() / 10.0
    elif mode == 2:
        out["battery_mv"] = u16()
        temp("temp_ds18b20_C")
        if tf_luna:
            din()
            out["adc_pa0_mv"] = u16()
            out["distance_cm"] = u16()
            out["distance_strength"] = u16()
        else:
            out["adc_pa0_mv"] = u16()
            din()
            out["distance_cm"] = u16()
            out["reserved"] = u16()
    elif mode == 3:
        out["adc_pa0_mv"] = u16()
        out["adc_pa1_mv"] = u16()
        out["adc_pa4_mv"] = u16()
        din()
        temp("sht_temp_C")
        out["sht_hum_%"] = u16() / 10.0
        out["battery_mv"] = u8() * 100  # 100 mV/LSB (ver firmware/README.md)
    elif mode == 4:
        out["battery_mv"] = u16()
        temp("temp1_pb3_C")
        out["adc_pa0_mv"] = u16()
        din()
        temp("temp2_pa9_C")
        temp("temp3_pa10_C")
    elif mode == 5:
        out["battery_mv"] = u16()
        temp("temp_ds18b20_C")
        out["adc_pa0_mv"] = u16()
        din()
        out["weight_g"] = u16()
        out["reserved"] = u16()
    elif mode == 6:
        out["battery_mv"] = u16()
        temp("temp_ds18b20_C")
        out["adc_pa0_mv"] = u16()
        din()
        out["count"] = u32()
    else:
        raise ValueError("modo fora de 1..6")

    if p != need:
        raise ValueError(f"consumidos {p} bytes, esperados {need}")
    return out


def _selftest() -> int:
    falhas = 0

    def checar(nome: str, obtido, esperado) -> None:
        nonlocal falhas
        if obtido != esperado:
            falhas += 1
            print(f"FALHA {nome}: obtido {obtido!r}, esperado {esperado!r}")

    # amostra de referencia do manual: BAT 2885 mV, 26,1 C, ADC 543 mV, INT1
    ref = struct.pack("<HhHBhH", 2885, 261, 543, DIN_PA12 | DIN_INT1, 235, 541)
    m1 = decode(1, ref)
    checar("MOD1 bateria", m1["battery_mv"], 2885)
    checar("MOD1 temperatura", m1["temp_ds18b20_C"], 26.1)
    checar("MOD1 adc", m1["adc_pa0_mv"], 543)
    checar("MOD1 PA12", m1["digital_in_pa12"], True)
    checar("MOD1 umidade", m1["sht_hum_%"], 54.1)

    # MOD4: -19,3 C (0xFF3F do manual)
    ref4 = struct.pack("<HhHBhh", 2885, 261, 543, 0, -193, 500)
    m4 = decode(4, ref4)
    checar("MOD4 temp2", m4["temp2_pa9_C"], -19.3)
    checar("MOD4 temp3", m4["temp3_pa10_C"], 50.0)

    # MOD6: contador de 32 bits
    ref6 = struct.pack("<HhHBI", 2885, 261, 543, 0, 0x12345678)
    checar("MOD6 contador", decode(6, ref6)["count"], 0x12345678)

    # MOD3: bateria em 1 byte (100 mV/LSB)
    ref3 = struct.pack("<HHHBhHB", 543, 1000, 2000, 0, 235, 541, 28)
    checar("MOD3 bateria", decode(3, ref3)["battery_mv"], 2800)

    print(f"\n{6 + 6} verificacoes, {falhas} falhas")
    return 0 if falhas == 0 else 1


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("payload", nargs="?", help="hex do payload")
    ap.add_argument("--mode", type=int, default=1)
    ap.add_argument("--tf-luna", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    if args.selftest:
        return _selftest()
    if not args.payload:
        ap.error("informe o payload em hex ou use --selftest")

    data = bytes.fromhex(args.payload.replace(" ", "").replace(":", ""))
    for chave, valor in decode(args.mode, data, args.tf_luna).items():
        print(f"{chave}: {valor}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
