import math
import re
from PySide6.QtWidgets import QDoubleSpinBox, QLineEdit
from PySide6.QtGui import QValidator, QDoubleValidator
from PySide6.QtCore import Qt


class ScientificSpinBox(QDoubleSpinBox):

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setRange(-1e30, 1e30)
        self.setDecimals(12)
        self.setSingleStep(1.0)

    def validate(self, text, pos):
        valid_text = text.strip()
        if not valid_text or valid_text in ("-", "+", ".", "-.", "+."):
            return QValidator.State.Intermediate, text, pos
        try:
            val = float(valid_text)
            if math.isfinite(val):
                return QValidator.State.Acceptable, text, pos
        except ValueError:
            pass
        return QValidator.State.Invalid, text, pos

    def valueFromText(self, text):
        try:
            return float(text.strip())
        except ValueError:
            return self.value()

    def textFromValue(self, value):
        if value == 0.0:
            return "0"
        av = abs(value)
        if av >= 1e6 or (0 < av < 1e-3):
            return f"{value:.4e}"
        if av >= 100:
            return f"{value:.2f}"
        if av >= 1:
            return f"{value:.4f}"
        return f"{value:.6e}"

    def stepBy(self, steps):
        val = self.value()
        if val == 0.0:
            new_val = steps * 1.0
        else:
            magnitude = 10 ** math.floor(math.log10(abs(val)))
            new_val = val + steps * magnitude
        new_val = max(min(new_val, self.maximum()), self.minimum())
        self.setValue(new_val)
