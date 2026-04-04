"""
配置管理器 — 适配干扰识别与抑制模块 (detection_suppression.* 配置节)

功能:
  - 支持 // 和 # 行注释
  - 支持尾随逗号
  - dot-notation 参数读写 (如 "detection_suppression.fc")
  - 派生参数自动计算 (Kr, lambda, prt)
"""

import json
import math

import numpy as np


class ConfigManager:
    """JSON 配置文件管理器, 适配干扰识别与抑制模块"""

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

    def get_detection_cfg(self) -> dict:
        """返回 detection_suppression 段的展平配置 (传给 pybind11)"""
        result: dict = {}
        ds = self._data.get("detection_suppression", {})
        self._flatten(ds, "detection_suppression", result)
        return result

    def _flatten(self, d: dict, prefix: str, result: dict):
        for k, v in d.items():
            key = f"{prefix}.{k}" if prefix else k
            if isinstance(v, dict):
                self._flatten(v, key, result)
            else:
                result[key] = v

    def _update_derived(self):
        fc  = self.get("detection_suppression.fc", 35e9)
        Tp  = self.get("detection_suppression.Tp", 12e-6)
        B   = self.get("detection_suppression.B", 80e6)
        prf = self.get("detection_suppression.prf", 5e3)
        fs  = self.get("detection_suppression.fs", 120e6)

        c = 3e8
        Kr  = B / Tp
        lam = c / fc
        prt = 1.0 / prf

        self._derived = {
            "fc": fc, "Tp": Tp, "B": B, "fs": fs,
            "prf": prf, "Kr": Kr, "lambda": lam, "prt": prt,
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
        lines = text.split('\n')
        cleaned = []
        for i, line in enumerate(lines):
            stripped = line.rstrip()
            if stripped.endswith(','):
                for j in range(i + 1, len(lines)):
                    next_stripped = lines[j].strip()
                    if not next_stripped:
                        continue
                    if next_stripped.startswith('}') or next_stripped.startswith(']'):
                        stripped = stripped[:-1]
                    break
            cleaned.append(stripped)
        return '\n'.join(cleaned)
