"""
配置管理器 — 适配干扰生成模块 (jamming.* 配置节)

功能:
  - 支持 // 和 # 行注释
  - 支持尾随逗号
  - dot-notation 参数读写 (如 "system.fc", "jamming.case1_rdj.Rj")
  - 派生参数自动计算 (nrn, tnrn, fr 等)
"""

import json
import math
import re

import numpy as np


class ConfigManager:
    """JSON 配置文件管理器, 适配干扰生成模块"""

    def __init__(self):
        self._data: dict = {}
        self._derived: dict = {}

    def load(self, path: str) -> bool:
        try:
            with open(path, "r", encoding="utf-8") as f:
                text = f.read()
            text = self._strip_comments(text)
            text = self._remove_trailing_commas(text)
            self._data = json.loads(text)
            self._update_derived()
            return True
        except Exception:
            return False

    def get(self, key: str, default=None):
        parts = key.split(".")
        obj = self._data
        for part in parts:
            if isinstance(obj, dict) and part in obj:
                obj = obj[part]
            else:
                return default
        return obj

    def set_param(self, key: str, value):
        parts = key.split(".")
        obj = self._data
        for part in parts[:-1]:
            if part not in obj or not isinstance(obj[part], dict):
                obj[part] = {}
            obj = obj[part]
        obj[parts[-1]] = value
        self._update_derived()

    def get_all_params(self) -> dict:
        return dict(self._data)

    def get_derived_params(self) -> dict:
        return dict(self._derived)

    def get_system_cfg(self) -> dict:
        return dict(self._data.get("system", {}))

    def get_jamming_cfg(self) -> dict:
        """返回 jamming 段的展平配置 (传给 pybind11)"""
        result: dict = {}
        jamming = self._data.get("jamming", {})
        self._flatten(jamming, "jamming", result)
        return result

    def _flatten(self, d: dict, prefix: str, result: dict):
        for k, v in d.items():
            key = f"{prefix}.{k}" if prefix else k
            if isinstance(v, dict):
                self._flatten(v, key, result)
            else:
                result[key] = v

    def _update_derived(self):
        fc = self.get("system.fc", 16e9)
        Tp = self.get("system.Tp", 12e-6)
        B = self.get("system.B", 40e6)
        prf = self.get("system.prf", 10e3)
        Vr = self.get("system.Vr", 50.0)
        Rs = self.get("system.Rs", 10000.0)
        wr_val = self.get("system.wr", 608.0)
        nan1 = self.get("system.nan1", 64)
        A_RJ = self.get("system.A_RJ", 10.0)
        z_R0 = self.get("system.z_R0", 2000.0)

        c = 3e8
        fs = 3.0 * B
        gama = B / Tp
        nrn = int(math.floor((Tp * fs + wr_val) / 2.0)) * 2
        Tnrn = 1.0 / fs
        Tstart = 2.0 * Rs / c - nrn / 2.0 / fs
        Tend = 2.0 * Rs / c + (nrn / 2.0 - 1.0) / fs
        tnrn = np.linspace(Tstart, Tend, nrn)
        fr = np.linspace(-fs / 2, fs / 2, nrn)
        lam = c / fc

        self._derived = {
            "fc": fc, "Tp": Tp, "B": B, "fs": fs, "gama": gama,
            "prf": prf, "Vr": Vr, "Rs": Rs, "wr": wr_val, "nan1": nan1,
            "nrn": nrn, "Tnrn": Tnrn, "lambda": lam,
            "tnrn": tnrn, "fr": fr,
            "prt": 1.0 / prf, "amp_j": 10 ** (A_RJ / 20.0),
            "z_R0": z_R0,
        }

    @staticmethod
    def _strip_comments(text: str) -> str:
        lines = text.split("\n")
        result = []
        for line in lines:
            in_string = False
            quote_char = None
            pos = len(line)
            i = 0
            while i < len(line):
                ch = line[i]
                if in_string:
                    if ch == quote_char and (i == 0 or line[i - 1] != "\\"):
                        in_string = False
                else:
                    if ch in ('"', "'"):
                        in_string = True
                        quote_char = ch
                    elif ch == "/" and i + 1 < len(line) and line[i + 1] == "/":
                        pos = i
                        break
                    elif ch == "#":
                        pos = i
                        break
                i += 1
            result.append(line[:pos])
        return "\n".join(result)

    @staticmethod
    def _remove_trailing_commas(text: str) -> str:
        # 逐行处理: 仅删除紧接 } 或 ] 之前的尾随逗号
        lines = text.split('\n')
        cleaned = []
        for i, line in enumerate(lines):
            stripped = line.rstrip()
            if stripped.endswith(','):
                # 检查下一非空行是否以 } 或 ] 开头
                for j in range(i + 1, len(lines)):
                    next_stripped = lines[j].strip()
                    if not next_stripped:
                        continue
                    if next_stripped.startswith('}') or next_stripped.startswith(']'):
                        stripped = stripped[:-1]
                    break
            cleaned.append(stripped)
        return '\n'.join(cleaned)
